#pragma once
#include "core/limits.hpp"
#include "menu/types.hpp"
#include <cstddef>

// 次の起動に持ち越す物の形式（docs/design/menu-item-actions.md の 7.2）と、読み込みの検査。
// CTRPF に依存しない純粋な関数（PC の試験で確かめる）。ファイルの読み書きと保存の時機は features が持つ。
// 鍵は spec の id（ItemDef::key）。固定の並びは記録の順、お気に入りの並びは order で表す。
namespace gohan::menu::persist {
// 先頭の印（4 バイト）と版。旧版の印（GMP1）は読まない。
inline constexpr char kMagic[4] = {'G', 'H', 'N', '3'};
inline constexpr u16 kVersion = 1;
// 印 4・版 2・旗 1・予備 1・押し切るボタン 2・記録の数 2。
inline constexpr std::size_t kHeaderBytes = 12;
inline constexpr std::size_t kMaxIdBytes = 64;
// 項目の記録の欄の旗（bit）。
inline constexpr u8 kValue = 1u << 0;    // 設定の値の適用値、または固定したゲームの値（4 バイト）
inline constexpr u8 kHotkey = 1u << 1;   // 適用済みのホットキー（既定と違う時だけ。2 バイト）
inline constexpr u8 kFixed = 1u << 2;    // 固定している
inline constexpr u8 kOn = 1u << 3;       // トグルの ON（固定の時。「トグルを全て保持」の時は固定でなくても）
inline constexpr u8 kFavorite = 1u << 4; // お気に入り（並びは order。2 バイト）
inline constexpr u8 kFieldMask = 0x1F;
// ヘッダーの旗。
inline constexpr u8 kKeepToggles = 1u << 0;
// 項目の記録の欄の最大（値 4・ホットキー 2・並び 2）。大きさの上限の検査に使う。
inline constexpr std::size_t kFieldBytesMax = 8;
static_assert(kHeaderBytes + core::limits::MenuItems * (1 + kMaxIdBytes + 1 + kFieldBytesMax) <= core::limits::PersistBytes,
              "全項目の最大の大きさが上限を超えます");

// 1 項目の記録。覚える欄だけを持つ（欄の旗が立っている物だけ意味がある）。
struct Record {
    u16 item = kNoItem;
    u8 fields = 0;
    s32 value = 0;
    u16 hotkey = 0;
    u16 order = 0;
};

// 保存の中身。records の順が固定の並び（固定している記録の順）になる。
struct Saved {
    bool keep_toggles = false;
    u16 press_keys = 0; // 押し切るボタン（キーのビット）
    std::size_t count = 0;
    Record records[core::limits::MenuItems];
};

// 読み込みの検査に使う、エンジンの決まり。
struct Rules {
    u16 reserved = 0;            // ホットキーに使えないボタン（SELECT）
    u16 press_valid = 0;         // 押し切るボタンに使えるボタン（選択肢の名前のボタン）
    u16 settings_root = kNoItem; // 設定画面の根。その下の項目は持ち越さない
    bool dev_visible = false;    // 開発版の項目を出している時だけ真。隠れた項目の記録は読まず、重なりの判定にも数えない
};

// 鍵（spec の id）で項目を引く。無ければ kNoItem。
u16 FindItem(const ItemDef *defs, std::size_t count, const char *key, std::size_t length);

// 書き出す（リトルエンディアン）。入らない・鍵が不正な時は 0。
std::size_t Encode(const Saved &saved, const ItemDef *defs, std::size_t count, u8 *out, std::size_t capacity);

// 読み込む。印・版・記録の数（項目数の上限を超える時）が合わない時は false（saved は空。既定で始める）。
// 欄の誤り（未知の鍵・種類の合わない欄・予約や重なるホットキー）はその欄だけ捨てる。値は NormalizeValue で範囲に収める。
// 途中で切れた記録は、それ以降を読まずに捨てる（それより前の記録は残す）。
// 隠れている項目（開発版の項目で、rules.dev_visible が偽）の記録は読まない。
bool Decode(const u8 *data, std::size_t size, const ItemDef *defs, std::size_t count, const Rules &rules, Saved &saved);
} // namespace gohan::menu::persist
