#pragma once
#include "core/feature.hpp"

namespace gohan::menu { struct Bound; }

// ゲーム中に下画面へ出す gohan のメニュー。毎フレームの処理でボタンとタッチを読み、
// メニューの画面（menu::Shell）を動かして、描画の部品（ui::renderer）へ渡す。
namespace gohan::features::menu_screen {
const core::Feature &Feature();
// 構成の接続点から、Wire より前にゲームの項目を結ぶ。
bool BindItems(const menu::Bound *bindings, std::size_t count);
// CTRPF のスレッドから開閉を頼む（開発版の CTRPF の項目用）。次のフレームに反映する。競合したら false。
bool RequestToggle();
// CTRPF のメニューを開く操作（PluginMenu::OnOpening）の答え。
// 自前のメニューが使える間は、Select 単独を自前のメニューに譲る。Select+十字上は従来どおり CTRPF のメニューを開く。
// 自前のメニューが使えない（毎フレームの処理か描画が導入されていない）間は常に true。
bool ShouldOpenCtrpfMenu();
// CTRPF の終了の合図のスレッドから呼ぶ。新しい保存の頼みを止め、動いている保存の作業を上限（約 1 秒）まで待つ。
// ゲームを止めず、描画にも CTRPF の排他にも触らない。続く Shutdown は、ここで待った分を余計に待たない。
void StopSavingForExit();
} // namespace gohan::features::menu_screen
