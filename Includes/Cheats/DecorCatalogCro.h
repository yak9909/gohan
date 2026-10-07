#pragma once

// 自動生成: tools/hhd/export_decor_catalog_cro.py（解析リポジトリ project_v2）。手で直さない。
// 根拠 IDA-gpt-6.1-sol-F002 / F??? 訂正（t022_catalog_fix3）。CRO のオフセットは .text の先頭から。照合語が全部合うときだけ呼ぶ。

#include <3ds/types.h>

namespace DecorCatalogCro {

const u32 kEditorChipResource = 0x4C;   // Chip_Setup の第 2 引数 = エディター + 0x4C（chip.arc の資源）
const u32 kIndoorChipSetup = 0x43160, kIndoorChipSetupWord = 0xE92D4070;   // ModuleIndoor Chip_Setup(チップ, エディター + 0x4C, 記録へのポインタ)
const u32 kIndoorChipRestoreOwnRoot = 0x2F77C, kIndoorChipRestoreOwnRootWord = 0xE92D4070;   // ModuleIndoor Chip_RestoreOwnRoot(チップ。Neutral と同じ親へ戻す)
const u32 kIndoorCreateChips = 0x3CC08, kIndoorCreateChipsWord = 0xE92D41F0;   // ModuleIndoor Editor_CreateChips（呼ばない。Setup の呼び方の照合用）

}  // namespace DecorCatalogCro
