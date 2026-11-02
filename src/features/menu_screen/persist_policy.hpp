#pragma once
#include "menu/types.hpp"

// 保存の時機（docs/design/menu-item-actions.md 7.3）。CTRPF に依存しない判断だけを持つ。
// 毎フレームの処理が観測した値を渡し、写しを出すか（WantsPublish）と作業を起こすか（WantsStart）を聞く。
// 結果は Published・Started で返す（出せなかった時は返さず、次のフレームで聞き直す）。
namespace gohan::features::menu_screen {
struct SaveView {
    menu::u32 revision = 0;     // エンジンの版（覚える物が変わるたびに増える）
    menu::u32 erase_count = 0;  // 保存を全て消した回数
    menu::u32 failures = 0;     // 保存の失敗の回数（作業のスレッドの報告）
    bool visible = false; // メニューが開いている
    bool settled = false; // メニューを閉じ切り、確認・入力の枠も無い
};

class SavePolicy {
  public:
    // 読み込みの後に 1 回。この版は保存済みとみなすので、起動直後には頼まない。
    void Begin(menu::u32 revision, menu::u32 erase_count);
    // 写しを出すべきか。呼ぶたびに、失敗の観測と開いた時の再試行の解除を進める。
    // 閉じ切った時に版が変わっていれば真。保存を全て消した時は、開いていても真。失敗の後は、開くまで閉じた時には頼まない。
    bool WantsPublish(const SaveView &view);
    // 写しを Mailbox へ出せた。この版を保存済みとして覚え、作業を起こすことを待つ状態にする。
    void Published(const SaveView &view);
    // 作業を起こすべきか（写しを出した後、作業を起こせるまで真。作業が動いている間は起こせないので、続けて真）。
    bool WantsStart() const { return start_; }
    void Started() { start_ = false; }
    // 終了: 新しい頼みはしない。
    void Stop();

  private:
    menu::u32 revision_ = 0;
    menu::u32 erase_count_ = 0;
    menu::u32 failures_ = 0;
    bool stale_ = false; // 失敗した: 次に閉じた時にもう一度頼む
    bool hold_ = false;  // 失敗の後は、開くまで閉じた時の頼みをしない（失敗を続けて試さないため）
    bool start_ = false;
    bool stopped_ = false;
};
} // namespace gohan::features::menu_screen
