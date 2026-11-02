// 次の起動に持ち越す物の保存の時機（CTRPF に依存しない判断）と、書き手が最新の写しを受け取る手順を確かめる。
// 試験の中の Mailbox の Busy は、試験専用の入口で作る（実機の公開の途中で止まった時を模す）。
#include "core/mailbox.hpp"
#include "features/menu_screen/persist_image.hpp"
#include "features/menu_screen/persist_policy.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

namespace gohan::core {
struct MailboxTestAccess {
    template <class T> static SpinMutex &Mutex(Mailbox<T> &box) { return box.mutex_; }
};
} // namespace gohan::core

namespace {
using namespace gohan::features::menu_screen;
using gohan::core::MailboxTestAccess;
using gohan::core::SpinMutex;
using gohan::core::Mailbox;

SaveView View(gohan::menu::u32 revision, gohan::menu::u32 erase, gohan::menu::u32 failures, bool visible, bool settled) {
    SaveView view;
    view.revision = revision;
    view.erase_count = erase;
    view.failures = failures;
    view.visible = visible;
    view.settled = settled;
    return view;
}

// 起動の直後は頼まない。閉じ切って版が変わった時に 1 回。同じ版では頼まない。開いている間・閉じる動きの途中は頼まない。
void TestTiming() {
    SavePolicy p;
    p.Begin(5, 0);
    assert(!p.WantsPublish(View(5, 0, 0, false, true))); // 起動直後
    assert(!p.WantsPublish(View(6, 0, 0, true, false))); // 開いている（版が変わった）
    assert(!p.WantsPublish(View(6, 0, 0, false, false))); // 閉じる動きの途中（確認の枠の退場などを含む）
    assert(p.WantsPublish(View(6, 0, 0, false, true)));  // 閉じ切った
    p.Published(View(6, 0, 0, false, true));
    assert(p.WantsStart());
    p.Started();
    assert(!p.WantsStart());
    assert(!p.WantsPublish(View(6, 0, 0, false, true))); // 同じ版
    assert(!p.WantsPublish(View(6, 0, 0, true, true)));  // 同じ版で開いた
    assert(p.WantsPublish(View(7, 0, 0, false, true)));  // 変わって閉じ切った
    p.Published(View(7, 0, 0, false, true));
    p.Started();
    // 閉じたままの変化（ホットキーの入力の枠の後など）も、閉じ切った時に頼む。
    assert(!p.WantsPublish(View(8, 0, 0, false, false)));
    assert(p.WantsPublish(View(8, 0, 0, false, true)));
}

// 保存を全て消した時は、開いていてもすぐ頼む。
void TestErase() {
    SavePolicy p;
    p.Begin(3, 0);
    assert(p.WantsPublish(View(4, 1, 0, true, false)));
    p.Published(View(4, 1, 0, true, false));
    p.Started();
    assert(!p.WantsPublish(View(4, 1, 0, true, false)));
}

// Mailbox が Busy で出せなかった時は、Published を呼ばないので次のフレームも頼む。
void TestBusyResend() {
    SavePolicy p;
    p.Begin(1, 0);
    assert(p.WantsPublish(View(2, 0, 0, false, true)));
    assert(p.WantsPublish(View(2, 0, 0, false, true))); // まだ出せていない
    p.Published(View(2, 0, 0, false, true));
    assert(!p.WantsPublish(View(2, 0, 0, false, true)));
}

// 作業を起こせない（動いている）時は、起こせるまで毎回求める。起こしたら止まる。
void TestStartPending() {
    SavePolicy p;
    p.Begin(1, 0);
    p.Published(View(2, 0, 0, false, true));
    assert(p.WantsStart());
    assert(p.WantsStart()); // 動いている間の -1 は、そのまま残る
    p.Started();
    assert(!p.WantsStart());
}

// 失敗の後は、次に閉じた時にもう一度頼む。開いたままや閉じたままの間は頼まない。
void TestFailureRetry() {
    SavePolicy p;
    p.Begin(1, 0);
    assert(p.WantsPublish(View(2, 0, 0, false, true)));
    p.Published(View(2, 0, 0, false, true));
    p.Started();
    // 失敗の報告が届いた（閉じ切ったまま）: 同じ版でも頼まない。
    assert(!p.WantsPublish(View(2, 0, 1, false, true)));
    assert(!p.WantsPublish(View(2, 0, 1, false, true)));
    // 開いたら、次に閉じた時に頼む。
    assert(!p.WantsPublish(View(2, 0, 1, true, false)));
    assert(p.WantsPublish(View(2, 0, 1, false, true)));
    p.Published(View(2, 0, 1, false, true));
    p.Started();
    assert(!p.WantsPublish(View(2, 0, 1, false, true)));
    // 開いている間に失敗が届いた場合も、閉じた時に試す。
    assert(!p.WantsPublish(View(2, 0, 2, true, false)));
    assert(p.WantsPublish(View(2, 0, 2, false, true)));
}

void TestStop() {
    SavePolicy p;
    p.Begin(1, 0);
    p.Published(View(2, 0, 0, false, true));
    p.Stop();
    assert(!p.WantsStart());
    assert(!p.WantsPublish(View(3, 1, 0, false, true)));
}

// 書き手: 写しが無ければ空、最新の写しを返す、同じ世代でも毎回返す、Busy の上限で必ず戻る。
int sleeps = 0;
SpinMutex *held = nullptr;
void CountSleep() { ++sleeps; }
void ReleaseAfterSecondSleep() {
    if (++sleeps == 2) held->Unlock();
}

void TestCopy() {
    Mailbox<Image> box;
    std::vector<unsigned char> out;
    assert(!CopyLatest(box, out, nullptr, 3) && out.empty()); // 写しが無い

    Image image;
    image.size = 3;
    image.bytes[0] = 1;
    image.bytes[1] = 2;
    image.bytes[2] = 3;
    assert(box.TryPublish(image));
    assert(CopyLatest(box, out, nullptr, 3) && out.size() == 3 && out[0] == 1 && out[2] == 3);
    assert(CopyLatest(box, out, nullptr, 3) && out.size() == 3); // 同じ世代でも毎回返す

    image.bytes[0] = 7;
    assert(box.TryPublish(image));
    assert(CopyLatest(box, out, nullptr, 3) && out[0] == 7); // 最新

    image.size = 0;
    assert(box.TryPublish(image));
    assert(!CopyLatest(box, out, nullptr, 3) && out.empty()); // 大きさ 0 の写しは使わない

    // Busy の上限: 待ちの回数は試行の回数より 1 少ない。上限に達したら空で戻る。
    image.size = 3;
    assert(box.TryPublish(image));
    SpinMutex &mutex = MailboxTestAccess::Mutex(box);
    mutex.Lock();
    sleeps = 0;
    assert(!CopyLatest(box, out, CountSleep, 5) && out.empty() && sleeps == 4);
    // Busy の間に解放されたら、その後の試行で受け取る。
    sleeps = 0;
    held = &mutex;
    assert(CopyLatest(box, out, ReleaseAfterSecondSleep, 5) && out.size() == 3 && sleeps == 2);
    mutex.Unlock();
}
} // namespace

int main() {
    TestTiming();
    TestErase();
    TestBusyResend();
    TestStartPending();
    TestFailureRetry();
    TestStop();
    TestCopy();
    std::puts("menu save: ok");
    return 0;
}
