#pragma once

// DecorCatalog — 家の模様替え（BsMenuInteriorEditor）に HHD の家具リスト・壁紙/床紙リストを足す（T022 段 2、2026-10-07。未実機）。
// 設計: 解析リポジトリ docs/topics/t022_decorate_port_requirements.md §7。
//   模様替え UI が開いている間、下画面の上に HHD の上段タブ 2 つ（家具 / 壁紙・床紙）を出し、押すとカタログの窓を開く。
//   品を決めると家具は空きマスへ置き（HHD の探す順 + かざると同じ判定・生成）、壁紙・床紙はゲーム自身の貼り替えで替えて、窓を閉じる。
// 資源: SD の gohan/common/hhd_catalog.arc（tools/hhd/make_catalog_arc.py）と hhd_icons.bin（tools/hhd/make_icon_bank.py）。

namespace DecorCatalog {

void Wire(void);
// GuiMenu のチェックの項目（Cheats.cpp の DispatchTick / DispatchDisable から）。自分の項目なら真
bool Tick(int index, unsigned short held);
bool Disable(int index);

}  // namespace DecorCatalog
