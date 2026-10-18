#pragma once
#include "core/feature.hpp"

namespace gohan::game {
const core::Feature &FrameHookFeature();
// メニュー用に公開済みの状態だけを読む。
const char *FrameHookStatus();
} // namespace gohan::game
