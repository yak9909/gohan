#pragma once
#include "core/feature.hpp"
#include "game/lyt/drawing.hpp"
#include <string_view>

namespace gohan::ui::renderer {
using game::lyt::Drawing;
using game::lyt::Font;
using game::lyt::Rect;
using game::lyt::Screen;
enum class Result { Added, Empty, Full, Budget, Invalid, Truncated };

// 作成者が所有するフレーム。Begin で画面を選んでから描く。
// Commit 後も変更でき、共有された実体は書き換えない。
class Canvas {
  public:
    void Begin(Screen screen);
    Result FillRect(Rect bounds, std::uint32_t color);
    Result FrameRect(Rect bounds, std::uint32_t color);
    Result Text(int x, int y, std::string_view text, std::uint32_t color,
                unsigned scale = 1, Font font = Font::Main);
    // 入れ子は受け付けない。矩形・文字ともに同じ切り抜き範囲を保持する。
    bool BeginClip(Rect bounds);
    void EndClip();
    const Drawing &Data() const { return drawing_; }

  private:
    Drawing drawing_{};
    Screen screen_ = Screen::Top;
    Rect clip_{};
    bool clipped_ = false;
    game::lyt::ScreenDrawing &Current();
};

// UTF-8 の先頭から最大 RendererChars 字。非対応の字は幅にも含めない。
int MeasureText(std::string_view text, unsigned scale = 1, Font font = Font::Main);
const game::lyt::FontResource &Fonts();

enum class Status { Stopped, Waiting, Ready, WrongTitle, Unreadable, Mismatch,
                    MissingBindings, PresentFailed };
// メニューのスレッドから。競合した場合は false を返すので次回再試行する。
bool SetEnabled(bool enabled);
bool Commit(const Canvas &canvas);
Status ReadStatus();
const char *StatusText();
const core::Feature &RendererFeature();
} // namespace gohan::ui::renderer
