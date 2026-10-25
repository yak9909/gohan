#pragma once
#include "core/mailbox.hpp"
#include "game/lyt/drawing.hpp"
#include <algorithm>

namespace gohan::ui::renderer::detail {
// 確定前の指示を保持する。帯の再生用に書き換えた drawing とは比べない。
// 比較とコピーを同じロック内で行い、別の作成者や競合後の再送も取りこぼさない。
class DrawingMailbox {
  public:
    bool TryPublish(const game::lyt::ScreenDrawing &value) {
        if (!mutex_.TryLock()) return false;
        if (!generation_ || !Equal(value_, value)) {
            Copy(value_, value);
            if (++generation_ == 0) ++generation_;
        }
        mutex_.Unlock();
        return true;
    }
    core::MailRead ReadIfNew(game::lyt::ScreenDrawing &value, std::uint32_t &generation) {
        if (!mutex_.TryLock()) return core::MailRead::Busy;
        if (!generation_ || generation_ == generation) {
            mutex_.Unlock();
            return core::MailRead::Unchanged;
        }
        Copy(value, value_);
        generation = generation_;
        mutex_.Unlock();
        return core::MailRead::Updated;
    }

  private:
    static bool Equal(const game::lyt::Rect &a, const game::lyt::Rect &b) {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    static bool Equal(const game::lyt::ScreenDrawing &a, const game::lyt::ScreenDrawing &b) {
        if (a.count != b.count || a.rectangles != b.rectangles || a.texts != b.texts ||
            a.characters != b.characters || a.estimatedBytes != b.estimatedBytes || a.pictureLimit != b.pictureLimit)
            return false;
        for (std::size_t i = 0; i < a.count; ++i) {
            const auto &x = a.commands[i], &y = b.commands[i];
            if (x.kind != y.kind || !Equal(x.bounds, y.bounds) || !Equal(x.clip, y.clip) || x.color != y.color)
                return false;
            if (x.kind == game::lyt::Kind::Text) {
                if (x.font != y.font || x.scale != y.scale || x.slot != y.slot || x.length != y.length ||
                    !std::equal(x.text, x.text + x.length, y.text)) return false;
            } else if (x.kind == game::lyt::Kind::Image || x.kind == game::lyt::Kind::Repeat) {
                if (x.scale != y.scale || x.slot != y.slot || x.texture != y.texture || !Equal(x.source, y.source))
                    return false;
            }
        }
        return true;
    }
    static void Copy(game::lyt::ScreenDrawing &to, const game::lyt::ScreenDrawing &from) {
        std::copy_n(from.commands, from.count, to.commands);
        to.count = from.count;
        to.rectangles = from.rectangles;
        to.texts = from.texts;
        to.characters = from.characters;
        to.estimatedBytes = from.estimatedBytes;
        to.pictureLimit = from.pictureLimit;
    }
    core::SpinMutex mutex_;
    game::lyt::ScreenDrawing value_{};
    std::uint32_t generation_ = 0;
};
} // namespace gohan::ui::renderer::detail
