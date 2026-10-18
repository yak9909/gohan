#include "menu/sample_items.hpp"
#include "menu_ids.hpp"

namespace gohan::menu::sample {
namespace {
State g_state;

Result SetFlag(bool on) {
    g_state.flag = on;
    return Result::Ok;
}
bool FlagActive() { return g_state.flag; }

void HoldStep(bool holding) {
    g_state.holding = holding;
    if (holding) ++g_state.hold_frames;
}

Result Stamp() {
    ++g_state.stamps;
    return Result::Ok;
}

Result OpenEditor(unsigned index) {
    g_state.editor_open[index] = true;
    return Result::Ok;
}
Result OpenA() { return OpenEditor(0); }
Result OpenB() { return OpenEditor(1); }
bool StepEditor() {
    ++g_state.editor_steps;
    return true;
}
void CloseA() { g_state.editor_open[0] = false; }
void CloseB() { g_state.editor_open[1] = false; }

bool ReadWallet(s32 *value) {
    if (!g_state.wallet_readable) return false;
    *value = g_state.wallet;
    return true;
}
Result WriteWallet(s32 value) {
    g_state.wallet = value;
    ++g_state.wallet_writes;
    return Result::Ok;
}
} // namespace

State &Get() { return g_state; }

const Bound kBindings[kBindingCount] = {
    GOHAN_ITEM(MenuId::SampleFlag, Patch{.set = SetFlag, .is_active = FlagActive}),
    GOHAN_ITEM(MenuId::SampleHold, Hold{.step = HoldStep}),
    GOHAN_ITEM(MenuId::SampleStamp, Trigger{.run = Stamp}),
    GOHAN_ITEM(MenuId::SampleEditorA, Mode{.open = OpenA, .step = StepEditor, .close = CloseA}),
    GOHAN_ITEM(MenuId::SampleEditorB, Mode{.open = OpenB, .step = StepEditor, .close = CloseB}),
    GOHAN_ITEM(MenuId::SampleWallet, GameValue{.read = ReadWallet, .write = WriteWallet}),
};
} // namespace gohan::menu::sample
