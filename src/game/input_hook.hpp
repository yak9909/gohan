#pragma once
#include "core/feature.hpp"
#include "core/format.hpp"

namespace gohan::game {
const core::Feature &InputPadFeature();
const core::Feature &InputControllerFeature();
bool InputControllerReady();
// CTRPF が開くことを許可したときだけ呼ぶ。再開まで保持する。
void InputCtrpfOpening();
void AppendInputStatus(core::TextWriter &writer);
#ifdef GOHAN_DEV
void ToggleInputHooks();
#endif
} // namespace gohan::game
