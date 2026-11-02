#include "features/menu_screen/persist_policy.hpp"

namespace gohan::features::menu_screen {
void SavePolicy::Begin(menu::u32 revision, menu::u32 erase_count) {
    revision_ = revision;
    erase_count_ = erase_count;
}

bool SavePolicy::WantsPublish(const SaveView &view) {
    if (stopped_) return false;
    // 失敗の報告は、次に閉じた時にもう一度頼むための印。開いている間に来た失敗は、閉じた時に試す。
    if (view.failures != failures_) {
        failures_ = view.failures;
        stale_ = true;
        hold_ = true;
    }
    if (view.visible) hold_ = false;
    // 保存を全て消した時は、開いていてもすぐ頼む。
    if (view.erase_count != erase_count_) return true;
    return view.settled && !hold_ && (stale_ || view.revision != revision_);
}

void SavePolicy::Published(const SaveView &view) {
    revision_ = view.revision;
    erase_count_ = view.erase_count;
    stale_ = false;
    start_ = true;
}

void SavePolicy::Stop() {
    stopped_ = true;
    start_ = false;
}
} // namespace gohan::features::menu_screen
