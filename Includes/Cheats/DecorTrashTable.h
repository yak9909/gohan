#pragma once

// 自動生成: tools/hhd/export_decor_trash.py（解析リポジトリ project_v2）。手で直さない。
// 根拠 IDA-opus-5.5-F105 / IDA-opus-5.5-HHD-F015・F017。CRO のオフセットは .text の先頭から。

#include <3ds/types.h>

namespace DecorTrashTable {

const u32 kUnloadedSlotWord = 0x007B1428;   // 取り込み口の語（モジュールが無いとき。nullsub）
const u32 kIndoorSlot = 0x001504C8, kIndoorSlotOffset = 0x052A4;   // ModuleIndoor .text + オフセット（0xb052a4）
const u32 kFtrSlot = 0x001501B8, kFtrSlotOffset = 0x1CF44;   // ModuleFtr .text + オフセット（0xb1cf44）
const u32 kIndoorChipDragCalc = 0x422C4, kIndoorChipDragCalcWord = 0xE92D41F0;   // ModuleIndoor Chip_Drag_Calc（チップの状態 Drag の calc。チップ +12 と比べる）
const u32 kIndoorChipDropCalc = 0x42450, kIndoorChipDropCalcWord = 0xE92D40F0;   // ModuleIndoor Chip_Drop_Calc（Drag で指が離れると Drop へ。離した瞬間の判定）
const u32 kIndoorChipOutCalc = 0x4304C, kIndoorChipOutCalcWord = 0xE92D4038;   // ModuleIndoor Chip_Out_Calc（チップの状態 Out の calc）
const u32 kIndoorStateChange = 0x2ED80, kIndoorStateChangeWord = 0xE92D47F0;   // ModuleIndoor StateObj_ChangeByCalc(obj, calc, 0)
const u32 kIndoorEditorInCalc = 0x3D678, kIndoorEditorInCalcWord = 0xE92D4010;   // ModuleIndoor エディター In の calc（エディター +52）
const u32 kIndoorEditorNeutralCalc = 0x3D1E4, kIndoorEditorNeutralCalcWord = 0xE92D4FF1;   // ModuleIndoor Editor_Neutral_Calc
const u32 kIndoorEditorCmnBtnInCalc = 0x3DC48, kIndoorEditorCmnBtnInCalcWord = 0xE92D4070;   // ModuleIndoor エディター CommonButtonIn の calc（In の後に通る。decor_trash1 でここで一度消えた）
const u32 kIndoorEditorCmnBtnOutCalc = 0x3DCA0, kIndoorEditorCmnBtnOutCalcWord = 0xE92D4070;   // ModuleIndoor エディター CommonButtonOut の calc（共通ボタンで閉じ始め）
const u32 kIndoorEditorOutCalc = 0x3D708, kIndoorEditorOutCalcWord = 0xE92D4070;   // ModuleIndoor エディター Out の calc
const u32 kIndoorSlotTarget = 0x052A4, kIndoorSlotTargetWord = 0xE2801A01;   // ModuleIndoor 本体の取り込み口 0x1504C8 が指す関数
const u32 kFtrRecGet = 0x1D244, kFtrRecGetWord = 0xE1D000F0;   // ModuleFtr FtrRec_Get(&s16 番号) -> 記録（32 B）
const u32 kFtrRecIsAlive = 0x1EDEC, kFtrRecIsAliveWord = 0xE92D4010;   // ModuleFtr FtrRec_IsAlive(&s16 番号)
const u32 kFtrSlotTarget = 0x1CF44, kFtrSlotTargetWord = 0xE92D41F0;   // ModuleFtr FtrRec_RegisterAcFtr（本体の取り込み口 0x1501B8 が指す）
const u32 kIndoorEditorPtrLiteral = 0x2F384;   // ModuleIndoor .text のリテラル語 = &g_InteriorEditor（実行時に再配置される）
const u32 kIndoorFtrMgrPtrLiteral = 0x42440;   // ModuleIndoor .text のリテラル語 = &g_FtrMgr（実行時に再配置される）
// 当たり（HHD の B_Base_00、L_Trash_00 の位置）。中心座標 x [-165, -118) y [-126.5, -73.5)（y 上向き）
const float kHitLeft = -5.0f, kHitRight = 42.0f, kHitTop = 193.5f, kHitBottom = 246.5f;   // 下画面の画素（左上原点）

}  // namespace DecorTrashTable
