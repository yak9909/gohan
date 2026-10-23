#include "game/input_hook.hpp"
#include "core/input_block.hpp"
#include "core/mailbox.hpp"
#include "core/signal.hpp"
#include "game/generated/api.hpp"
#include "game/input_filter.hpp"
#include <CTRPluginFramework.hpp>
#include <cstddef>

extern "C" void gohan_input_pad_callback();
extern "C" void gohan_input_controller_callback();

namespace gohan::game {
namespace {
using namespace CTRPluginFramework;
namespace block = core::input_block;
namespace filter = input_filter;
using Pad = generated::AcnlPadState;
using Controller = generated::SeadControllerBasePrefix;
#ifdef __3DS__
static_assert(sizeof(Pad) == 0x10);
static_assert(offsetof(Pad, held) == 0 && offsetof(Pad, pressed) == 4 && offsetof(Pad, released) == 8);
static_assert(sizeof(Controller) == 0x134);
static_assert(offsetof(Controller, vtable) == 0);
static_assert(offsetof(Controller, pointer_flags) == 0x10);
static_assert(offsetof(Controller, held) == 0x110);
static_assert(offsetof(Controller, axis0) == 0x11C);
static_assert(sizeof(generated::AcnlUiPoint2) == 8);
static_assert(offsetof(generated::AcnlUiPoint2, x) == 0 && offsetof(generated::AcnlUiPoint2, y) == 4);
#endif

enum class Status { Waiting, Ready, WrongTitle, Unreadable, Mismatch, Failed, Stopped };
struct Connection {
    Hook hook;
    core::Signal ready;
    core::Mailbox<Status> status;
};
Connection pad_connection, controller_connection;
core::Signal select_down, opening, resumed;
Process::OnPauseResumeCallback previous_resume = nullptr;
const Clock epoch;
filter::State state;
block::Effective demand;
std::uint32_t demand_generation = 0;

#ifdef GOHAN_DEV
struct PadStats { std::uint32_t calls = 0; std::uintptr_t thread = 0; std::uint32_t max_ticks = 0; };
struct ControllerStats {
    std::uint32_t calls = 0, excluded = 0;
    std::uintptr_t thread = 0;
    std::uint32_t max_ticks = 0;
    filter::State state{};
    block::Mask blocked = 0;
};
PadStats pad_stats;
ControllerStats controller_stats;
core::Mailbox<PadStats> pad_box;
core::Mailbox<ControllerStats> controller_box;
#endif

class PauseGuard {
  public:
    PauseGuard() : resume_(!Process::IsPaused()) { if (resume_) Process::Pause(); }
    ~PauseGuard() { if (resume_) Process::Play(); }
  private:
    bool resume_;
};

void PauseResume(bool pause) {
    // Mailbox の競合に依存しない。停止前の C は opening を消費しない。
    if (!pause && opening.Take()) resumed.Set();
    if (previous_resume) previous_resume(pause);
}

bool Install(Connection &connection, std::uintptr_t address, std::uint32_t length,
             std::uint32_t expected, std::uintptr_t callback, std::uint32_t flags) {
    if (connection.hook.IsEnabled()) return true;
    const auto fail = [&](Status reason) { connection.status.TryPublish(reason); return false; };
    if (Process::GetTitleID() != 0x0004000000086200ULL) return fail(Status::WrongTitle);
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < length; ++i) {
        std::uint8_t byte;
        if (!Process::Read8(address + i, byte)) return fail(Status::Unreadable);
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
    }
    if ((crc ^ 0xFFFFFFFFu) != expected) return fail(Status::Mismatch);
    connection.hook.Initialize(address, callback);
    connection.hook.SetFlags(flags);
    if (connection.hook.Enable() != HookResult::Success) {
        connection.hook.Disable();
        return fail(Status::Failed);
    }
    connection.ready.Set();
    connection.status.TryPublish(Status::Ready);
    return true;
}

bool WirePad() {
    if (pad_connection.hook.IsEnabled()) return true;
    PauseGuard pause;
    select_down.Set(false);
    return Install(pad_connection, generated::InputPadRead_addr, generated::InputPadRead_len,
                   generated::InputPadRead_crc32, reinterpret_cast<std::uintptr_t>(&gohan_input_pad_callback),
                   HOOK_DEFAULT_PARAMS);
}
bool WireController() {
    if (controller_connection.hook.IsEnabled()) return true;
    PauseGuard pause;
    state = {};
    demand = {};
    demand_generation = 0;
    opening.Set(false);
    resumed.Set(false);
    if (!Install(controller_connection, generated::InputControllerCalc_addr, generated::InputControllerCalc_len,
                 generated::InputControllerCalc_crc32,
                 reinterpret_cast<std::uintptr_t>(&gohan_input_controller_callback),
                 USE_LR_TO_RETURN | EXECUTE_OI_AFTER_CB)) return false;
    previous_resume = Process::OnPauseResume;
    Process::OnPauseResume = PauseResume;
    return true;
}
void StopPad() {
    PauseGuard pause;
    pad_connection.hook.Disable();
    pad_connection.ready.Set(false);
    select_down.Set(false);
    pad_connection.status.TryPublish(Status::Stopped);
}
void StopController() {
    PauseGuard pause;
    controller_connection.hook.Disable();
    controller_connection.ready.Set(false);
    if (Process::OnPauseResume == PauseResume) Process::OnPauseResume = previous_resume;
    previous_resume = nullptr;
    opening.Set(false);
    resumed.Set(false);
    state = {};
#ifdef GOHAN_DEV
    controller_stats.state = {};
    controller_stats.blocked = 0;
    controller_box.TryPublish(controller_stats);
#endif
    controller_connection.status.TryPublish(Status::Stopped);
}
const core::Feature pad_feature{"game.input.pad", WirePad, StopPad, core::ShutdownStage::GameHooks};
const core::Feature controller_feature{"game.input.controller", WireController, StopController,
                                       core::ShutdownStage::GameHooks};

const char *StatusName(Status s) {
    switch (s) {
    case Status::Waiting: return "未導入";
    case Status::Ready: return "導入済み";
    case Status::WrongTitle: return "タイトル違い";
    case Status::Unreadable: return "読めない";
    case Status::Mismatch: return "照合不一致";
    case Status::Failed: return "導入失敗";
    case Status::Stopped: return "終了済み";
    }
    return "不明";
}
#ifdef GOHAN_DEV
void MaskText(core::TextWriter &writer, block::Mask mask) {
    constexpr const char *names[] = {"ZL", "L", "R", "ZR", "上", "下", "左", "右",
                                    "A", "B", "X", "Y", "SELECT", "START", "パッド", "タッチ"};
    if (!mask) { writer.Append("-"); return; }
    bool first = true;
    if ((mask & block::group::Buttons) == block::group::Buttons) {
        writer.Append("ボタン十字START");
        mask &= ~block::group::Buttons;
        first = false;
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (!(mask & (1u << i))) continue;
        if (!first) writer.Append("+");
        writer.Append(names[i]);
        first = false;
    }
}
#endif
} // namespace

const core::Feature &InputPadFeature() { return pad_feature; }
const core::Feature &InputControllerFeature() { return controller_feature; }
bool InputControllerReady() { return controller_connection.ready.Get(); }
void InputCtrpfOpening() { if (controller_connection.ready.Get()) opening.Set(); }

void AppendInputStatus(core::TextWriter &w) {
    static Status p = Status::Waiting, c = Status::Waiting;
    static std::uint32_t pg = 0, cg = 0;
    pad_connection.status.ReadIfNew(p, pg);
    controller_connection.status.ReadIfNew(c, cg);
    w.Append("入力の遮断: P=").Append(StatusName(p)).Append(" / C=").Append(StatusName(c));
#ifdef GOHAN_DEV
    static PadStats ps;
    static ControllerStats cs;
    static std::uint32_t psg = 0, csg = 0;
    pad_box.ReadIfNew(ps, psg);
    controller_box.ReadIfNew(cs, csg);
    w.Append("\n呼出 P ").Unsigned(ps.calls).Append(" C ").Unsigned(cs.calls)
        .Append(" 除外 ").Unsigned(cs.excluded);
    static block::Effective effective;
    static std::uint32_t generation = 0;
    block::Global().Read(effective, generation);
    w.Append("\n要求 block="); MaskText(w, effective.block);
    w.Append(" defer="); MaskText(w, effective.defer);
    w.Append(" 世代 ").Unsigned(effective.generation).Append("\n所有者:");
    block::OwnerInfo owners[core::limits::InputBlockOwners];
    const auto count = block::Global().List(owners, core::limits::InputBlockOwners);
    if (!count) w.Append(" 競合（次回再読）");
    for (std::size_t i = 0; i < count; ++i) {
        w.Append(" ").Append(owners[i].name).Append("["); MaskText(w, owners[i].claim.block);
        w.Append(" / "); MaskText(w, owners[i].claim.defer); w.Append("]");
    }
    const auto activity = block::Global().ActivityCounts();
    w.Append("\n表の競合 書き ").Unsigned(activity.writes).Append(" 読み ").Unsigned(activity.reads);
    w.Append("\n遮断 "); MaskText(w, cs.blocked);
    w.Append(" 掛かり "); MaskText(w, cs.state.latch);
    w.Append(" 保留 "); MaskText(w, cs.state.pending);
    w.Append(" 窓 "); MaskText(w, cs.state.window);
    w.Append(" 通過 "); MaskText(w, cs.state.passing);
    const auto &n = cs.state.counts;
    w.Append("\n累計 離し ").Unsigned(n.hidden_releases).Append(" 押下 ").Unsigned(n.hidden_presses)
        .Append(" 掛かり ").Unsigned(n.latch_entries).Append(" SELECT ").Unsigned(n.select_protections)
        .Append(" 中断 ").Unsigned(n.suspensions).Append(" 押し切り ").Unsigned(n.deferred_presses)
        .Append(" 長押し ").Unsigned(n.long_holds);
    w.Append("\nP/C スレッド: ").Append(!ps.thread || !cs.thread ? "未確認" :
        ps.thread == cs.thread ? "同じ" : "異なる");
    w.Append(" / 最大 tick P ").Unsigned(ps.max_ticks).Append(" C ").Unsigned(cs.max_ticks);
#endif
}
#ifdef GOHAN_DEV
void ToggleInputHooks() {
    if (pad_connection.ready.Get() || controller_connection.ready.Get()) { StopController(); StopPad(); }
    else { WirePad(); WireController(); }
}
#endif

// 以下の値はフックから渡される。ゲームの構造体に触る範囲をここだけに閉じる。
void FilterPad(Pad *pad) {
#ifdef GOHAN_DEV
    const auto start = svcGetSystemTick();
    ++pad_stats.calls;
    pad_stats.thread = reinterpret_cast<std::uintptr_t>(getThreadLocalStorage());
#endif
    select_down.Set((pad->held & 4u) != 0);
    pad->held &= ~4u;
    pad->pressed &= ~4u;
    pad->released &= ~4u;
#ifdef GOHAN_DEV
    if (pad_stats.calls % 30 == 0) pad_box.TryPublish(pad_stats);
    const auto ticks = static_cast<std::uint32_t>(svcGetSystemTick() - start);
    if (ticks > pad_stats.max_ticks) pad_stats.max_ticks = ticks;
#endif
}
void FilterController(Controller *controller, std::uint32_t *old) {
#ifdef GOHAN_DEV
    const auto start = svcGetSystemTick();
    ++controller_stats.calls;
    controller_stats.thread = reinterpret_cast<std::uintptr_t>(getThreadLocalStorage());
#endif
    if (reinterpret_cast<std::uintptr_t>(controller->vtable) == generated::vtbl_sead_CtrController_addr) {
        block::Global().Read(demand, demand_generation);
        const auto valid = static_cast<std::uint32_t>(generated::AcnlPointerFlags_Valid);
        filter::Input in;
        in.held = controller->held;
        in.old_held = old[0];
        in.pointer_on = (controller->pointer_flags & valid) != 0;
        in.old_pointer_on = (old[1] & valid) != 0;
        in.slide_x = controller->axis0.x;
        in.slide_y = controller->axis0.y;
        in.demand = demand;
        in.select_down = select_down.Get();
        in.ctrpf_resumed = resumed.Take();
        in.now_ms = static_cast<std::uint32_t>(epoch.GetElapsedTime().AsMilliseconds());
        filter::Output out;
        filter::Step(state, in, out);
        controller->held = out.held;
        controller->pointer_flags = (controller->pointer_flags & ~valid) | (out.pointer_on ? valid : 0u);
        controller->axis0.x = out.slide_x;
        controller->axis0.y = out.slide_y;
        old[0] = out.old_held;
        old[1] = (old[1] & ~valid) | (out.old_pointer_on ? valid : 0u);
#ifdef GOHAN_DEV
        controller_stats.blocked = out.blocked;
        controller_stats.state = state;
#endif
    }
#ifdef GOHAN_DEV
    else ++controller_stats.excluded;
    if (controller_stats.calls % 30 == 0) controller_box.TryPublish(controller_stats);
    const auto ticks = static_cast<std::uint32_t>(svcGetSystemTick() - start);
    if (ticks > controller_stats.max_ticks) controller_stats.max_ticks = ticks;
#endif
}
} // namespace gohan::game

extern "C" void gohan_input_pad(gohan::game::generated::AcnlPadState *pad) { gohan::game::FilterPad(pad); }
extern "C" void gohan_input_controller(gohan::game::generated::SeadControllerBasePrefix *controller,
                                       std::uint32_t *old) { gohan::game::FilterController(controller, old); }
