#pragma once
#include <3ds/types.h>

// DecorCatalog — 家の模様替え（BsMenuInteriorEditor）に HHD の品リストを足す（T022）。
// 設計: 解析リポジトリ docs/topics/t022_decorate_port_requirements.md §7。
//   模様替え UI 中、家具 / 壁かけ家具 / 壁紙・床紙のタブと、Xで最後のリストを開く。
//   品を決めると家具は空きマスへ置き（HHD の探す順 + かざると同じ判定・生成）、壁紙・床紙はゲーム自身の貼り替えで替えて、窓を閉じる。
// 資源: SD の gohan/common/hhd_catalog.arc（tools/hhd/make_catalog_arc.py）と hhd_icons.bin（tools/hhd/make_icon_bank.py）。

namespace DecorCatalog {

void Wire(void);
// GuiMenu のチェックの項目（Cheats.cpp の DispatchTick / DispatchDisable から）。自分の項目なら真
bool Tick(int index, unsigned short held);
bool Disable(int index);
// Game thread only: loading/in/out included; camera may use raw input during a list.
bool IsListOpen(void);
bool IsEditorOpen(void);
// Shared native operations; call only on the game thread.
bool EditorContext(u32 &indoor, u32 &editor);
u32 RoomDataForInterior(void);
bool InteriorBackgroundReady(void);
bool PlacementBusy(void);
bool DuplicateReady(void);
void StepDuplicate(void);
bool DuplicateItem(unsigned short id, unsigned short flags, int rotation, int x, int z);
void RefreshChipTracking(void);

}  // namespace DecorCatalog
