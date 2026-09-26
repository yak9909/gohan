#pragma once

// ItemNames — アイテム ID から名前を引く窓口（没アイテム表示の自前表示、今後のアイテム検索で使う）。
//
// - 通常アイテム: ゲームの ROM の Script/Str/STR_Item_name.umsbt を実行時に読む（3gx に名前を焼き込まない）。
//   TXT2 の i 番目 = ID 0x2000 + i（ゲームの ItemName_ResolvePhrase 0x56D274 と同じ引き方。IDA-opus-5.5-F049）。
//   読むのは頼まれたときに 1 回（約 190 KB を持ち続ける。ReleaseNormal で返す）。
// - 没アイテム: 3gx に内蔵した表（HiddenItemTable.h。漢字名・かな名）。
// - 自前表示: ゲームの名前の関数をフックし、没アイテムの名前を表の漢字名にする（SetCustomNames）。

#include <3ds/types.h>

#include "HiddenItemTable.h"

namespace ItemNames {

// 通常アイテム
bool LoadNormal(void);                              // 済んでいれば何もしないで true。ROM が読めなければ false
void ReleaseNormal(void);
u32 NormalCount(void);                              // 表の件数（読めていなければ 0）
const u16 *NormalName(u16 id, u32 &length);         // UTF-16（制御タグ 0x000E を含みうる）。無ければ nullptr / 空は length 0

// 没アイテム
const HiddenItemTable::Entry *FindHidden(u16 id);    // 無ければ nullptr

// どちらでも: UTF-8 で書く（没アイテムは kana が真ならかな名）。名前が無ければ false（out は空）
bool NameUtf8(u16 id, char *out, u32 cap, bool kana = false);

// 自前表示（名前）。真の間、ゲームが没アイテムの名前を引くと表の漢字名を返す。フックは最初に真にしたとき入れる
bool SetCustomNames(bool on);                       // フックを入れられなければ false
bool CustomNames(void);

}  // namespace ItemNames
