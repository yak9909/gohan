#pragma once
#include "core/feature.hpp"

// ゲーム中に下画面へ出す gohan のメニュー。毎フレームの処理でボタンとタッチを読み、
// メニューの画面（menu::Shell）を動かして、描画の部品（ui::renderer）へ渡す。
namespace gohan::features::menu_screen {
const core::Feature &Feature();
// CTRPF のスレッドから開閉を頼む（開発版の CTRPF の項目用）。次のフレームに反映する。競合したら false。
bool RequestToggle();
// CTRPF のメニューを開く操作（PluginMenu::OnOpening）の答え。
// 自前のメニューが使える間は、Select 単独を自前のメニューに譲る。Select+十字上は従来どおり CTRPF のメニューを開く。
// 自前のメニューが使えない（毎フレームの処理か描画が導入されていない）間は常に true。
bool ShouldOpenCtrpfMenu();
} // namespace gohan::features::menu_screen
