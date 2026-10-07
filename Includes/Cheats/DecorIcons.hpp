#pragma once

// DecorIcons — カタログの格子のアイコン（HHD の 64x64 ETC1A4）を SD の束から読み、ゲームのヒープの枠へ置く（T022 段 2）。
// 束: sdmc:/luma/plugins/gohan/common/hhd_icons.bin（解析リポジトリ tools/hhd/make_icon_bank.py）。
//   見出し 32 B = 'HHDICN1\0', 版 1, 件数, 索引の位置 32, 索引 1 件 8 B, 絵の位置（0x80 境界）, 絵 4096 B。索引 = {HHD 品番 u16, 0 u16, 絵の位置 u32}（品番の昇順）。
// 全部の HHD の品（3,723 個、15 MB）はヒープに置けないので、HHD の Catalog_LoadVisibleIcons 0x16A4E4 と同じく見えている 3 枠 × 15 = 45 個だけを置く。
// スレッド: Open / Close / Service はメニューのスレッド（SD を読む）。Want / Ready は ゲームのスレッド（ヒープの枠は ゲームのスレッドが借りて渡す）。
//   Service が絵をヒープの枠へ直接読み、D-cache を掃き出してから「準備済み」にする。ゲームのスレッドは準備済みの枠だけを Picture に貼る。

#include <3ds/types.h>

namespace DecorIcons {

const u32 kSlots = 45, kIconBytes = 4096;

bool Open(void);                                // 見出しと索引を読む（メニューのスレッド）。済んでいれば何もしない
void Close(void);                               // 索引を返す（メニューのスレッド）
bool IsOpen(void);
bool Has(u16 hhdId);                            // 束に絵があるか（索引の二分探索）

// ヒープの枠（ゲームのスレッドが借りる。kSlots × kIconBytes、0x80 境界）。0 = 枠なし（Service は何もしない）
void SetSlots(u32 base);
// 枠 slot に品 hhdId の絵を置いてほしい（ゲームのスレッド）。0 = 空にする
void Want(u32 slot, u16 hhdId);
// 枠 slot に hhdId の絵が置けていれば絵の VA（ゲームのスレッド）。無ければ 0
u32 Ready(u32 slot, u16 hhdId);
// 頼まれた絵を読む（メニューのスレッド。毎ティック）。1 回に読む数の上限 maxReads
void Service(u32 maxReads);

}  // namespace DecorIcons
