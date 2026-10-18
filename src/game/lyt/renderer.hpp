#pragma once
#include "game/lyt/drawing.hpp"

namespace gohan::game::lyt {
enum class InstallResult { Ready, WrongTitle, Unreadable, Mismatch, MissingBindings };
// いずれもゲームの描画スレッドから呼ぶ。所有する資源の変更もここに閉じる。
InstallResult InstallRenderer(const FontResource &font);
bool Present(const Drawing &drawing);
void DetachRenderer();
} // namespace gohan::game::lyt
