#include "features/menu_screen/persist_image.hpp"
#include <memory>
#include <new>

namespace gohan::features::menu_screen {
namespace {
ImageBox latest;
} // namespace

ImageBox &Latest() { return latest; }

bool CopyLatest(ImageBox &box, std::vector<menu::u8> &out, void (*sleep_ms)(), unsigned attempts) {
    out.clear();
    // 写しは作業の領域（ヒープ）で受ける。静的領域の 3 つ目の 16 KiB を作らないため。
    // 例外の無い環境で落ちないよう、取れなければ空（書けなかった）として返す。
    std::unique_ptr<Image> copy(new (std::nothrow) Image);
    if (!copy) return false;
    for (unsigned n = 0; n < attempts; ++n) {
        // 世代は毎回 0 から始める。CTRPF のメニューを閉じた保存でも中身が要るので、同じ世代でも最新を返す。
        menu::u32 generation = 0;
        const core::MailRead result = box.ReadIfNew(*copy, generation);
        if (result == core::MailRead::Updated) {
            if (copy->size == 0 || copy->size > core::limits::PersistBytes) return false;
            out.assign(copy->bytes, copy->bytes + copy->size);
            return true;
        }
        // まだ一度も出されていない（写しが無い）。待っても増えないので、そこで終える。
        if (result == core::MailRead::Unchanged) return false;
        if (n + 1 < attempts && sleep_ms) sleep_ms();
    }
    return false;
}
} // namespace gohan::features::menu_screen
