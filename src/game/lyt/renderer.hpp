#pragma once
#include "game/lyt/drawing.hpp"

namespace gohan::game::lyt {
enum class InstallResult { Ready, WrongTitle, Unreadable, Mismatch, MissingBindings, ResourceFailed };
// いずれもゲームの描画スレッドから呼ぶ（毎フレームの処理の中）。所有する資源の変更もここに閉じる。
// 照合に通らない、または前提が揃わない場合は、ゲームの資源を何も確保しない。
InstallResult InstallRenderer(const FontResource &font);
// 導入済みなら、変化のあった画面の木を組み直し、描くものがある画面のノードをゲームのリストに登録する。
bool Present(const Drawing &drawing);
// 登録をやめ、ゲームのリストから外れて GPU が使い終えるのを待ってから資源を返す。
// 完全に返せたら true。false の間は毎フレーム呼ぶ。導入していなければ true。
bool DetachRenderer();
// 描画スレッド以外から、ミリ秒だけ待つ（終了の順序の待機用）。
void WaitMilliseconds(unsigned milliseconds);
} // namespace gohan::game::lyt
