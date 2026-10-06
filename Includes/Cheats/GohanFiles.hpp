#pragma once

// GohanFiles — SD の gohan フォルダのリソース（利用者 2026-10-06）。
//
// sdmc:/gohan/
//   common/                       言語に依らないもの
//     hidden_item_icons.arc       没アイテムの自前のアイコン（DARC。timg/<番号>.bclim、32x32 ETC1A4。ゲームの item_icon_tex.arc と同じ形）
//   <タイトル ID 16 桁>/           その版（言語）だけのもの。ほかの版を足すときは同じ名前で置く
//     hidden_items.tsv            没アイテム表（UTF-8 タブ区切り: id, icon, kana, name。人が読んで直せる）
//     hhd_charcreate.arc          HHD式スタイル変更のレイアウト（上画面の文字がその言語）
//
// 生成元は解析 repo（tools/items/export_hidden_items.py / export_hidden_icons.py / tools/hhd/build_hhd_arc.py）。
// gohan の repo の sdmc/gohan/ が SD と同じ並び（arc はゲーム由来の素材を含むので Git に入れない）。

#include <3ds/types.h>

namespace GohanFiles {

// "/gohan/<実行中のタイトル ID>/<name>"（CTRPF の File の SD ルートからの道）。入らなければ false
bool TitlePath(char *out, u32 size, const char *name);
// "/gohan/common/<name>"
bool CommonPath(char *out, u32 size, const char *name);
// ファイル全体を new[] で読む（末尾に 0 を 1 バイト足すので文字列としても読める）。無い・0 バイト・maxBytes を超えるなら nullptr
u8 *ReadAll(const char *path, u32 maxBytes, u32 &size);

}  // namespace GohanFiles
