// 次の起動に持ち越す物を、ゲーム中のメニューの毎フレームの処理から試す（段 4b）。
// 起動の状態（空・壊れた欄・正しい欄）で場面が違うので、引数の場面ごとに別の起動で動かす。
// 作業（Task）と設定ファイルの gohan の欄（GohanData）は代役。実機の代わりにはならない。
#include "core/frame.hpp"
#include "core/input_block.hpp"
#include "core/mailbox.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "features/menu_screen/persist_image.hpp"
#include "features/menu_screen/persist_policy.hpp"
#include "game/lyt/renderer.hpp"
#include "menu/engine.hpp"
#include "menu/persist.hpp"
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include "ui/renderer/renderer.hpp"
#include <CTRPluginFramework.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gohan::core {
struct MailboxTestAccess {
    template <class T> static SpinMutex &Mutex(Mailbox<T> &box) { return box.mutex_; }
};
} // namespace gohan::core

namespace gohan::game::lyt {
RendererMemory ReadRendererMemory() { return {}; }
namespace {
Drawing presented;
int presents = 0;
} // namespace
InstallResult InstallRenderer(const FontResource &, BackgroundAllocation *) { return InstallResult::Ready; }
bool Present(const Drawing &drawing) {
    presented = drawing;
    ++presents;
    return true;
}
bool DetachRenderer() { return true; }
void WaitMilliseconds(unsigned) {}
Drawing &Last() { return presented; }
int Presents() { return presents; }
} // namespace gohan::game::lyt

namespace {
using CTRPluginFramework::Fake;
using CTRPluginFramework::Key;
namespace menu_screen = gohan::features::menu_screen;
namespace renderer = gohan::ui::renderer;
namespace lyt = gohan::game::lyt;
namespace core = gohan::core;
namespace persist = gohan::menu::persist;
namespace menu = gohan::menu;
using core::MailboxTestAccess;

void Frames(int count, int step_ms = 16) {
    for (int i = 0; i < count; ++i) {
        Fake::nowMs += step_ms;
        core::frame::Dispatch();
    }
}
void Press(std::uint32_t keys) {
    Fake::keys = keys;
    Frames(2);
    Fake::keys = 0;
    Frames(2);
}
// 押して、動きが終わるまで待つ（メニューの場面が落ち着いてから次の操作をする）。
void Tap(std::uint32_t keys) {
    Press(keys);
    Frames(25);
}

std::string Utf8(const char16_t *text, unsigned length) {
    std::string out;
    for (unsigned i = 0; i < length; ++i) {
        const char16_t c = text[i];
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) out += {static_cast<char>(0xC0 | (c >> 6)), static_cast<char>(0x80 | (c & 0x3F))};
        else out += {static_cast<char>(0xE0 | (c >> 12)), static_cast<char>(0x80 | ((c >> 6) & 0x3F)), static_cast<char>(0x80 | (c & 0x3F))};
    }
    return out;
}

bool TopHasText(const std::string &needle) {
    const auto &screen = lyt::Last().screens[0];
    for (std::size_t i = 0; i < screen.count; ++i)
        if (screen.commands[i].kind == lyt::Kind::Text &&
            Utf8(screen.commands[i].text, screen.commands[i].length).find(needle) != std::string::npos)
            return true;
    return false;
}

// 押し切るボタン（遮断の要求の defer）。
gohan::core::input_block::Effective Demand() {
    gohan::core::input_block::Effective value;
    std::uint32_t generation = 0;
    assert(gohan::core::input_block::Global().Read(value, generation) == gohan::core::MailRead::Updated);
    return value;
}

std::uint16_t Item(const char *key) {
    const std::size_t length = std::strlen(key);
    const auto item = persist::FindItem(menu::kMenuItems, menu::kMenuItemCount, key, length);
    assert(item != menu::kNoItem);
    return item;
}

// 次の起動の欄（正しい欄）。固定した toggle・お気に入り・押し切るボタン・トグルを全て保持・設定の値。
std::vector<std::uint8_t> ValidSection() {
    persist::Saved saved;
    saved.keep_toggles = true;
    saved.press_keys = menu::key::A | menu::key::Start;
    saved.count = 3;
    saved.records[0].item = Item("sample.flag");
    saved.records[0].fields = persist::kFixed | persist::kOn;
    saved.records[1].item = Item("sample.stamp");
    saved.records[1].fields = persist::kFavorite;
    saved.records[1].order = 0;
    saved.records[2].item = Item("sample.mode");
    saved.records[2].fields = persist::kValue;
    saved.records[2].value = 1;
    static std::uint8_t buffer[core::limits::PersistBytes];
    const std::size_t size = persist::Encode(saved, menu::kMenuItems, menu::kMenuItemCount, buffer, sizeof(buffer));
    assert(size > 0);
    return std::vector<std::uint8_t>(buffer, buffer + size);
}

// 保存された欄を、読み込みの検査と同じ規則で読み直す。
persist::Saved ReadBack() {
    persist::Saved saved;
    persist::Rules rules;
    rules.settings_root = static_cast<menu::u16>(menu::kSettingsRoot);
    rules.press_valid = 0x3FFF;
    assert(persist::Decode(Fake::gohanFile.data(), Fake::gohanFile.size(), menu::kMenuItems, menu::kMenuItemCount, rules, saved));
    return saved;
}
bool SavedFixedOn(const persist::Saved &saved, const char *key) {
    const auto item = Item(key);
    for (std::size_t n = 0; n < saved.count; ++n)
        if (saved.records[n].item == item) return (saved.records[n].fields & persist::kFixed) && (saved.records[n].fields & persist::kOn);
    return false;
}

// 設定画面は、同じ起動の中で最後の位置を覚える（D.8）。最初の一回は一番上の「固定中の項目」から
// 下へ二つ進んで（「お気に入り」を越えて）「保持設定」に入る。二回目以降は、前回の位置（保持設定）から始まる。
// 「トグルを全て保持」を切り替えて閉じる。first は、設定画面を初めて開く時だけ真。
void ToggleKeepAndClose(bool first = false) {
    Tap(Key::Select);   // 開く
    Tap(Key::Start);    // 設定画面（前回の位置）
    if (first) {        // 保持設定（固定中の項目 → お気に入り → 保持設定）
        Tap(Key::DPadDown);
        Tap(Key::DPadDown);
    }
    Tap(Key::A);        // 保持設定の中へ（前回の位置は一番上のトグル）
    Tap(Key::A);        // トグルを全て保持（ON と OFF を切り替える）
    Tap(Key::B);        // 設定画面へ戻る（カーソルは保持設定）
    Tap(Key::Start);    // 設定画面を閉じる
    Tap(Key::Select);   // メニューを閉じる（閉じ切るまで待つ）
    Frames(40);
}

// 「保存を全て消す」を、開いたまま実行する（確認の既定はキャンセルなので、左で「はい」を選ぶ）。
// 設定画面は前回の位置（保持設定）から始まる。
void EraseWhileOpen() {
    Tap(Key::Select);
    Tap(Key::Start);
    Tap(Key::A);        // 保持設定の中へ
    Tap(Key::DPadDown); // 保存を全て消す
    Tap(Key::A);        // 確認
    Tap(Key::DPadLeft); // はい
    Tap(Key::A);
}

// 読み込み（GohanData::Load）の中で、毎フレームの処理が動くかを見る。登録より前なら、フレームの境界を回しても
// 毎フレームの処理（ボタンを読む）は動かない。登録の後なら、境界で動く（試験の対照）。
int load_probe_reads = -1;
void ProbeLoad() {
    const int before = Fake::keyReads;
    core::frame::Dispatch();
    load_probe_reads = Fake::keyReads - before;
}

void Begin(const std::string &scenario) {
    auto &feature = menu_screen::Feature();
    if (scenario == "broken") {
        // 印と版は正しいが、記録の数が項目の上限を超える。
        Fake::gohanFile = {'G', 'H', 'N', '3', 1, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    } else if (scenario == "valid") {
        Fake::gohanFile = ValidSection();
    }
    Fake::onLoad = ProbeLoad;
    assert(renderer::RendererFeature().Wire() && feature.Wire());
    // 読み込みは Wire の中で 1 回。毎フレームの処理の登録より前に動いた（フレームを回しても読み込みの中では動かなかった）。
    assert(Fake::gohanLoads == 1);
    assert(load_probe_reads == 0);
    Fake::onLoad = nullptr;
    // 対照: 登録の後は、フレームの境界で毎フレームの処理が動く（ボタンを読む）。
    const int before = Fake::keyReads;
    core::frame::Dispatch();
    assert(Fake::keyReads > before);
}

// 終了の待ちは上限まで。毎フレームの保存の部分を取ったままでも、終了は戻る。
void ScenarioShutdownGate() {
    Frames(20);
    assert(menu_screen::Gate().TryEnter()); // 毎フレームの保存の部分が取ったまま（ここでは試験が取る）
    const int sleeps = Fake::sleeps;
    menu_screen::Feature().Shutdown();
    assert(Fake::sleeps - sleeps == 99); // 上限の 100 回まで、1 ms ずつ待って戻る
    assert(menu_screen::Gate().Stopped());
    assert(Fake::gohanSaves == 0);
    std::puts("menu screen persist (shutdown with the gate held): ok");
}

// 作業が Scheduled のまま動かなくても、終了は上限まで待って戻る。書き手は外さないので、作業が後で動けば書ける。
void ScenarioShutdownStuck() {
    Frames(30);
    Fake::taskHold = true; // 作業は Scheduled のまま（作業のスレッドが動くまで）
    ToggleKeepAndClose(true);
    assert(Fake::taskStarts == 1 && Fake::gohanSaves == 0);
    const int sleeps = Fake::sleeps;
    menu_screen::Feature().Shutdown();
    assert(Fake::sleeps - sleeps == 999); // 作業の待ちは 1 秒（1000 回、1 ms ずつ）。ロックの待ちは別（取れるので待たない）
    assert(Fake::gohanSaves == 0);
    Fake::taskHold = false;
    Fake::RunHeldTask(); // 作業のスレッドが後で動く。書き手はまだ登録されている
    assert(Fake::gohanSaves == 1);
    assert(ReadBack().keep_toggles);
    std::puts("menu screen persist (shutdown with a stuck task): ok");
}

void ScenarioEmpty(bool broken) {
    Frames(40);
    // 欄が無い・壊れている: 既定のまま（押し切るボタンは START）。戻したという通知は出ない。
    assert(Demand().defer == gohan::core::input_block::part::Start);
    assert(!TopHasText("前回の状態"));
    assert(!gohan::menu::sample::Get().flag);
    // 起動の直後は頼まない（保存の欄は書かれていない）。
    assert(Fake::gohanSaves == 0);
    Tap(Key::Select);
    Tap(Key::Select);
    Frames(40);
    assert(Fake::gohanSaves == 0); // 変わっていない時は閉じても頼まない
    std::puts(broken ? "menu screen persist (startup, broken): ok" : "menu screen persist (startup, empty): ok");
}

void ScenarioValid() {
    Frames(40);
    // 正しい欄: 固定の toggle（ホットキー付き）を ON にして戻す。ホットキーが付いた ON は待機で終わり、効果はホットキーを押すまで入らない。
    // 押し切るボタン（A と START）を入れ、通知を 1 つ出す。
    assert(!menu::sample::Get().flag);
    assert(Demand().defer == (gohan::core::input_block::part::A | gohan::core::input_block::part::Start));
    assert(TopHasText("前回の状態"));
    assert(Fake::gohanSaves == 0); // 戻しの後も頼まない
    // 戻した物が一覧に出る（お気に入りのフォルダ）。
    Tap(Key::Select);
    assert(TopHasText("お気に入り"));
    Tap(Key::Select);
    Frames(40);
    // 変えて閉じると、書いた欄を読み直して同じ状態になる（トグルを全て保持だけが変わる）。
    ToggleKeepAndClose(true);
    assert(Fake::gohanSaves == 1);
    const auto saved = ReadBack();
    assert(!saved.keep_toggles);
    assert(SavedFixedOn(saved, "sample.flag"));
    assert(saved.press_keys == (menu::key::A | menu::key::Start));
    bool favorite = false;
    for (std::size_t n = 0; n < saved.count; ++n)
        favorite |= saved.records[n].item == Item("sample.stamp") && (saved.records[n].fields & persist::kFavorite);
    assert(favorite);
    std::puts("menu screen persist (valid): ok");
}

void ScenarioSave() {
    // 1. 起動の直後は頼まない。
    Frames(30);
    assert(Fake::gohanSaves == 0);

    // 2. 開いている間に変えても頼まない（閉じ切るまで）。
    Tap(Key::Select);
    Tap(Key::Start);
    Tap(Key::DPadDown); // お気に入り
    Tap(Key::DPadDown); // 保持設定
    Tap(Key::A);
    Tap(Key::A); // トグルを全て保持: ON
    Frames(20);
    assert(Fake::gohanSaves == 0);
    Tap(Key::B);
    Tap(Key::Start);
    Tap(Key::Select);
    Frames(40);
    // 3. 変わって閉じ切った時に 1 回。
    assert(Fake::gohanSaves == 1);
    assert(ReadBack().keep_toggles);
    Frames(30);
    assert(Fake::gohanSaves == 1);

    // 4. 同じ版では、開いて閉じても頼まない。
    Tap(Key::Select);
    Frames(20);
    Tap(Key::Select);
    Frames(40);
    assert(Fake::gohanSaves == 1);

    // 5. Mailbox が Busy の間は頼めない。空いた次のフレームで頼み直す。
    auto &mutex = MailboxTestAccess::Mutex(menu_screen::Latest());
    mutex.Lock();
    ToggleKeepAndClose();
    assert(Fake::gohanSaves == 1);
    mutex.Unlock();
    Frames(3);
    assert(Fake::gohanSaves == 2);
    assert(!ReadBack().keep_toggles);

    // 6. 保存の途中に頼みが来たら、終わった後にもう 1 回だけ書く。作業は予約だけして、わざと動かさない。
    Fake::taskHold = true;
    ToggleKeepAndClose(); // 1 回目の頼み（予約のまま）
    assert(Fake::gohanSaves == 2);
    ToggleKeepAndClose(); // 途中の頼み（起こせないので、次のフレームでも起こそうとする）
    Frames(5);
    assert(Fake::gohanSaves == 2);
    Fake::RunHeldTask(); // 1 回目が終わる（書き手は最新の写しを写す）
    assert(Fake::gohanSaves == 3);
    Frames(3); // 途中の頼みの分を起こす（予約のまま）
    assert(Fake::gohanSaves == 3);
    Fake::RunHeldTask();
    assert(Fake::gohanSaves == 4);
    Frames(5);
    assert(Fake::gohanSaves == 4); // もう頼みは無い
    Fake::taskHold = false;
    // 2 回目の切り替え（OFF）の後に書かれた写しが残る。
    assert(!ReadBack().keep_toggles);

    // 7. 失敗したら、次に閉じた時にもう一度頼む（閉じたままでは頼まない）。
    Fake::saveFails = true;
    ToggleKeepAndClose(); // ON にする。書けない
    assert(Fake::gohanSaves == 5);
    Frames(60);
    assert(Fake::gohanSaves == 5);
    assert(!ReadBack().keep_toggles); // 既存の欄は残る
    Fake::saveFails = false;
    Tap(Key::Select);
    Frames(20);
    Tap(Key::Select);
    Frames(40);
    assert(Fake::gohanSaves == 6);
    assert(ReadBack().keep_toggles);

    // 8. 保存を全て消す時は、開いていてもすぐ頼む。
    EraseWhileOpen();
    Frames(10);
    assert(Fake::gohanSaves == 7);
    assert(!ReadBack().keep_toggles);
    Tap(Key::B);
    Tap(Key::Start);
    Tap(Key::Select);
    Frames(40);
    assert(Fake::gohanSaves == 7);

    std::puts("menu screen persist (save timing): ok");
}
} // namespace

int main(int argc, char **argv) {
    const std::string scenario = argc > 1 ? argv[1] : "";
    assert(scenario == "empty" || scenario == "broken" || scenario == "valid" || scenario == "save" ||
           scenario == "shutdown-gate" || scenario == "shutdown-task");
    Begin(scenario);
    if (scenario == "empty") ScenarioEmpty(false);
    else if (scenario == "broken") ScenarioEmpty(true);
    else if (scenario == "valid") {
        ScenarioValid();
        menu_screen::Feature().Shutdown();
    } else if (scenario == "save") ScenarioSave();
    else if (scenario == "shutdown-gate") ScenarioShutdownGate();
    else ScenarioShutdownStuck();
    renderer::RendererFeature().Shutdown();
    return 0;
}
