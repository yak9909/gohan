#pragma once
#include <cstddef>
#include <cstdint>

// メニューの項目の定義の型。spec/menu.toml から生成した表（build/gen/menu_table.cpp）がこれを埋める。
namespace gohan::menu {
using s32 = std::int32_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u8 = std::uint8_t;

inline constexpr u16 kNoItem = 0xFFFF;

// 軸1: 見た目
enum class Control : u8 { Folder, Toggle, Select, Multiselect, Number, Slider, Action, Key };
// 軸3: 振る舞い（Folder だけ None）
enum class Behavior : u8 { None, State, Hold, Trigger, Mode, Value };
// 軸2: 値の出どころ（Value だけ）。Setting は gohan の変数、Game は開いた時にゲームから 1 回読む。
enum class Source : u8 { None, Setting, Game };
enum class Format : u8 { Decimal, Hex, Decimal1 };
// 実行の属性。Immediate はメニューで選んだ時にその場で実行、OnApply は適用で実行（予約）。
enum class MenuKind : u8 { Immediate, OnApply };
// 確認。Hotkey はホットキーで実行する時だけ確認ダイアログを出す。
enum class Confirm : u8 { None, Hotkey };
// 実行の入力。実行する前に値を受け取る。
enum class InputKind : u8 { None, Number, Hex };
enum class CondOp : u8 { None, Eq, Ne, Lt, Le, Gt, Ge };

// 他の項目の編集値との比較で無効にする（enabled_if）。比較が成り立つ間だけ有効。
struct Cond {
    u16 item = kNoItem;
    CondOp op = CondOp::None;
    s32 rhs = 0;
};

// 項目の定義。MenuId の値が添え字で、深さ優先の順に並ぶ。数値は decimal1 なら 10 倍した整数。
struct ItemDef {
    const char *key; // spec の id
    const char *label;
    const char *desc;
    Control control;
    Behavior behavior;
    Source source;
    Format format;
    MenuKind menu;
    Confirm confirm;
    InputKind input;
    bool lockable;
    bool persist;
    bool dev;
    u8 option_count;
    u16 parent;
    u16 first_child;
    u16 next; // 次の兄弟
    u16 group; // 0 は無し
    u16 hotkey; // 既定のホットキー（ボタンのビット）
    s32 def;
    s32 min;
    s32 max;
    s32 step;
    const char *const *options;
    Cond cond;
};

// 生成した MenuId ごとの宣言（結び付けの検査に使う）。menu_ids.hpp が特殊化を出す。
template <auto Id> struct Spec;
} // namespace gohan::menu
