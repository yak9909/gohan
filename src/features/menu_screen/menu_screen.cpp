#include "features/menu_screen/menu_screen.hpp"
#include "core/frame.hpp"
#include "core/input_block.hpp"
#include "core/mailbox.hpp"
#include "core/request.hpp"
#include "core/signal.hpp"
#include "features/menu_screen/canvas_surface.hpp"
#include "features/menu_screen/persist_image.hpp"
#include "features/menu_screen/persist_policy.hpp"
#include "game/input_hook.hpp"
#include "menu/engine.hpp"
#include "menu/persist.hpp"
#include "menu/persist_status.hpp"
#include "menu/sample_items.hpp"
#include "menu/shell.hpp"
#include "menu_ids.hpp"
#include "ui/input/buttons.hpp"
#include "ui/renderer/renderer.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdint>
#include <optional>

namespace gohan::features::menu_screen {
using menu::u8;
using menu::u32;
namespace {
namespace renderer = ui::renderer;
namespace input = ui::input;

constexpr renderer::Screen kScreen = renderer::Screen::Top;
// 入力の順序（menu の key）と ui の button が同じであること。
static_assert(menu::key::ZL == input::button::ZL && menu::key::L == input::button::L &&
              menu::key::R == input::button::R && menu::key::ZR == input::button::ZR &&
              menu::key::Up == input::button::Up && menu::key::Down == input::button::Down &&
              menu::key::Left == input::button::Left && menu::key::Right == input::button::Right &&
              menu::key::A == input::button::A && menu::key::B == input::button::B &&
              menu::key::X == input::button::X && menu::key::Y == input::button::Y &&
              menu::key::Select == input::button::Select && menu::key::Start == input::button::Start);

// 中身はゲームの描画スレッド（毎フレームの処理）だけが触る。
menu::Engine engine(menu::kMenuItems, menu::kMenuItemCount);
menu::Shell shell(engine, {400});
renderer::Canvas canvas;
bool enable_requested = false;
bool drawn = false;
namespace block = core::input_block;
core::SpinMutex demand_mutex;
core::Signal running;
bool demanding = false;
block::Owner menu_owner, settings_owner;
// 押し切るボタン（DeferKeys）の値を、遮断の部品のビットとしてそのまま使う。
static_assert(menu::key::ZL == block::part::ZL && menu::key::L == block::part::L && menu::key::R == block::part::R &&
              menu::key::ZR == block::part::ZR && menu::key::Up == block::part::Up && menu::key::Down == block::part::Down &&
              menu::key::Left == block::part::Left && menu::key::Right == block::part::Right && menu::key::A == block::part::A &&
              menu::key::B == block::part::B && menu::key::X == block::part::X && menu::key::Y == block::part::Y &&
              menu::key::Select == block::part::Select && menu::key::Start == block::part::Start);

// 次の起動に持ち越す物（7.3・7.4）。読み込みは Wire の中で 1 回、保存はゲームのスレッドが頼み、作業のスレッドが書く。
using CTRPluginFramework::GohanData;
using CTRPluginFramework::Task;
// Busy の上限（1 ms ずつ待つ）。CTRPF のメニューの間はゲームのスレッドが止まるので、上限で必ず戻る。
constexpr unsigned kBusyAttempts = 100;
// 読み込みの検査（Wire の中）と写しの組み立て（毎フレームの処理）で使う。両方は重ならない（読み込みは登録より前）。
menu::persist::Saved saved;
Image staging;                      // 写しの組み立て（ゲームのスレッドだけ）
SavePolicy save_policy;             // ゲームのスレッド（Gate() の中だけ）
std::optional<Task> save_task;      // 作業のスレッド（Wire で作る。静的な初期化では CTRPF を使わない）
menu::persist::SaveStats save_stats; // 作業のスレッドだけが書く
menu::persist::SaveStats observed;   // ゲームのスレッドが読んだ結果（失敗の観測）
u32 observed_generation = 0;

void SleepOneMs() { CTRPluginFramework::Sleep(CTRPluginFramework::Milliseconds(1)); }

// 書き手（GohanData::Save の中。作業のスレッドか、CTRPF のメニューのスレッド）。最新の写しを写す。
void WriteSection(std::vector<u8> &out) {
    if (!CopyLatest(Latest(), out, SleepOneMs, kBusyAttempts)) return;
    // 状態表示の「最後の大きさ」は、書けた時だけ更新する。Busy の間は待って出し直す。
    for (unsigned n = 0; n < kBusyAttempts; ++n) {
        if (menu::persist::PublishWrittenSize(static_cast<u32>(out.size()))) return;
        SleepOneMs();
    }
}

// 読み手（Wire の中。GohanData::Load から同じスレッドで呼ばれる）。欄が正しい時だけ戻す。
// 偽（大きさ 0・壊れた欄）なら何もしない。そのまま Restore すると、押し切るボタンが「なし」になるため。
void ReadSection(const u8 *data, u32 size) {
    if (!menu::persist::Decode(data, size, menu::kMenuItems, menu::kMenuItemCount, engine.PersistRules(), saved)) return;
    engine.Restore(saved);
}

// 作業のスレッド: 写しを書く。結果を数えて、状態表示と失敗の後の頼み直しに渡す。
std::int32_t SaveRun(void *) {
    const bool ok = GohanData::Save();
    ++save_stats.saves;
    if (!ok) ++save_stats.failures;
    for (unsigned n = 0; n < kBusyAttempts; ++n) {
        if (menu::persist::PublishSaveStats(save_stats)) break;
        SleepOneMs();
    }
    return 0;
}

// 写しを作って Mailbox へ出す（ゲームのスレッド）。Busy なら偽（次のフレームで出し直す）。
bool PublishCapture() {
    engine.Capture(saved);
    const std::size_t size = menu::persist::Encode(saved, menu::kMenuItems, menu::kMenuItemCount, staging.bytes,
                                                   sizeof(staging.bytes));
    // 大きさは静的に検査している（PersistBytes に収まる）。入らないのは表の誤りなので、この頼みは出さずに終える。
    if (size == 0) return true;
    staging.size = static_cast<u32>(size);
    return Latest().TryPublish(staging);
}

// メニューを閉じ切り、確認・入力の枠も無い（保存を頼む条件）。Shell の公開の値だけを読む。
bool Settled(u32 now) { return !engine.Visible() && !shell.Modal() && !shell.panel().Visible(now); }

// 保存の時機（7.3）。毎フレームの処理の最後に呼ぶ。Start は頼んだ時だけ試し、失敗した・Busy の時は次のフレームで試す。
void SaveStep(u32 now) {
    if (!Gate().TryEnter()) return; // 終了の処理が取っている
    if (!Gate().Stopped()) {
        menu::persist::ReadSaveStats(observed, observed_generation);
        SaveView view;
        view.revision = engine.Revision();
        view.erase_count = engine.EraseCount();
        view.failures = observed.failures;
        view.visible = engine.Visible();
        view.settled = Settled(now);
        if (save_policy.WantsPublish(view) && PublishCapture()) save_policy.Published(view);
        if (save_policy.WantsStart() && save_task->Start() == 0) save_policy.Started();
    }
    Gate().Leave();
}

// フレームの最後だけが要求を更新する。Busy は次のフレームで再送する。
void PublishDemand() {
    if (!demand_mutex.TryLock()) return;
    if (running.Get()) {
        if (menu_owner.slot == 0xFF) block::Global().Acquire("menu", menu_owner);
        if (settings_owner.slot == 0xFF) block::Global().Acquire("settings", settings_owner);
        if (menu_owner.slot != 0xFF) block::Global().Set(menu_owner, shell.InputDemand());
        // 押し切るボタンは設定の値（既定は START だけ）。ボタンのビットは入力と同じ並びなので、そのまま渡す。
        if (settings_owner.slot != 0xFF) block::Global().Set(settings_owner, {0, static_cast<block::Mask>(engine.DeferKeys())});
    }
    demand_mutex.Unlock();
}

core::Request<bool> toggle_request;
core::Mailbox<bool> active_box;

void FrameStep() {
    if (!running.Get()) return;
    const input::Snapshot pad = input::Read();
    menu::Input in;
    in.held = pad.buttons;
    bool toggle = false;
    toggle_request.TryTake(toggle);
    const std::uint32_t now = input::NowMs();
    // CTRPF のメニューを閉じた操作（SELECT など）は、再開した時にまだ押されている。
    // それを新しい押下として扱わないよう、再開の時点で押されているボタンは離すまで使わない。
    if (game::InputTakeCtrpfResumed()) shell.IgnoreHeld(pad.buttons);
    shell.Step(now, in, toggle);
    const bool demand_active = shell.InputDemand().block != 0;
    if (demand_active && !demanding && !game::InputControllerReady()) {
        const menu::Notice notice{"入力の遮断", "遮断が無効です。\n操作がゲームにも反映されます。", true};
        shell.Notify(notice, now);
    }
    demanding = demand_active;

    // 字形・UI・窓・帯をまとめて常駐させ、閉じている間も保持する。
    // 競合した時は次のフレームで頼み直す。
    if (!enable_requested) enable_requested = renderer::SetEnabled(renderer::Client::Menu, true);
    const bool need = shell.NeedsDraw(now);
    if (need || drawn) {
        // 自分の画面だけを描いて確定する（もう一方の画面を使う別の呼び出し元に触れない）。
        canvas.Begin(kScreen);
        if (need) {
            CanvasSurface surface(canvas, kScreen);
            shell.Draw(surface, now);
        }
        // 確定に失敗（競合）したら、次のフレームでもう一度描いて確定する。
        if (renderer::Commit(canvas, kScreen)) drawn = need;
    }
    SaveStep(now);
    PublishDemand();
}

// 作業が終わるまで 1 ms ずつ、上限まで待つ。終わったら真。作業が Scheduled のまま動かない時も、ここで必ず戻る。
bool WaitTask() {
    for (unsigned n = 0; n < kBusyAttempts; ++n) {
        const u32 status = save_task->Status();
        if (status == static_cast<u32>(Task::Idle) || status == static_cast<u32>(Task::Finished)) return true;
        if (n + 1 < kBusyAttempts) SleepOneMs();
    }
    return false;
}

// 終了（7.3）: 新しい頼みは止める。毎フレームの保存の部分と動いている作業は、上限まで待つ。
// 作業が終わらなかった時は書き手を外さない（書き手を外すには CTRPF の排他を取るので、作業が中で止まっていると待つため）。
void StopSaving() {
    Gate().RequestStop();
    // 取れたら戻さない（以後の毎フレームの処理は、TryEnter で入らない）。取れなくても、停止の要求で新しい頼みは出ない。
    Gate().Acquire(SleepOneMs, kBusyAttempts);
    if (save_task && !WaitTask()) return;
    GohanData::SetHandlers(nullptr, nullptr);
}

bool Wire() {
    engine.SetReservedHotkeys(menu::key::Select);
#ifdef GOHAN_DEV
    engine.SetDevVisible(true);
#endif
    engine.BindSettings({static_cast<menu::u16>(menu::kSettingsRoot),
                         static_cast<menu::u16>(menu::MenuId::SettingsKeepErase),
                         static_cast<menu::u16>(menu::MenuId::SettingsKeepToggles),
                         static_cast<menu::u16>(menu::MenuId::SettingsPressThrough)});
    if (!engine.BindAll(menu::sample::kBindings, menu::sample::kBindingCount)) return false;
    // 次の起動に持ち越した物を戻す（7.4）。毎フレームの処理を登録する前に、結び付けの後で 1 回だけ。
    GohanData::SetHandlers(WriteSection, ReadSection);
    GohanData::Load();
    // 戻した後の版を保存済みとする。起動直後には頼まない。
    save_policy.Begin(engine.Revision(), engine.EraseCount());
    // 作業の affinity は AppCore（コア 0 の 2 本のハンドラー）。SysCore の bit を含まない（7.3）。
    save_task.emplace(SaveRun, nullptr, Task::AppCore);
    const auto change = core::frame::Global().Add(FrameStep, 50);
    if (change.result != core::frame::Result::Queued && change.result != core::frame::Result::Unchanged) {
        GohanData::SetHandlers(nullptr, nullptr);
        return false;
    }
    running.Set();
    active_box.TryPublish(true);
    return true;
}

void Shutdown() {
    running.Set(false);
    if (demand_mutex.TryLock()) {
        // 停止中のスレッドが表を保持していても待たない。返却できなければ枠を保持する。
        // 全体の終了では入力フックは既に停止済み。終了後に新たな Set はしない。
        if (menu_owner.slot != 0xFF) block::Global().Release(menu_owner);
        if (settings_owner.slot != 0xFF) block::Global().Release(settings_owner);
        demand_mutex.Unlock();
    }
    core::frame::Global().Remove(FrameStep);
    StopSaving();
    active_box.TryPublish(false);
    // 描画の部品が先に止まっていても、要求だけは取り下げておく（失敗しても部品側が止める）。
    renderer::SetEnabled(renderer::Client::Menu, false);
}

const core::Feature feature{"menu.screen", Wire, Shutdown, core::ShutdownStage::Other};
} // namespace

const core::Feature &Feature() { return feature; }

bool RequestToggle() { return toggle_request.TrySubmit(true); }

bool ShouldOpenCtrpfMenu() {
    // 読み手は CTRPF のメニューのスレッドだけ。
    static bool active = false;
    static std::uint32_t generation = 0;
    active_box.ReadIfNew(active, generation);
    if (!active || (input::Read().buttons & input::button::Up) != 0) return true;
    return renderer::ReadStatus() != renderer::Status::Ready;
}
} // namespace gohan::features::menu_screen
