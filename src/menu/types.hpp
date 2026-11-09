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
// 定義表の外にある仮のフォルダ。保存する項目の ID には使わない。
inline constexpr u16 kFavoritesFolder = 0xFFFE;
enum class ActionKind : u8 { Fix, Unfix, AddFavorite, RemoveFavorite, Reorder, SetHotkey, RemoveHotkey };
struct ItemAction { ActionKind kind; const char *label; bool disabled; };
inline constexpr std::size_t kMaxActions = 5;

// 軸1: 見た目
enum class Control : u8 { Folder, Toggle, Select, Multiselect, Number, Slider, Action, Key };
// 軸3: 振る舞い（Folder だけ None）
enum class Behavior : u8 { None, State, Hold, Trigger, Mode, Value };
// 軸2: 値の出どころ（Value だけ）。Setting は gohan の変数、Game は開いた時にゲームから 1 回読む。
enum class Source : u8 { None, Setting, Game };
enum class Format : u8 { Decimal, Hex, Decimal1 };
// 実行の属性。Immediate はメニューで選んだ時にその場で実行、OnApply は適用で実行（予約）。
enum class MenuKind : u8 { Immediate, OnApply };
// 確認。Hotkey はホットキーで実行する時だけ、Menu はメニューで実行する時も確認ダイアログを出す。
enum class Confirm : u8 { None, Hotkey, Menu };
// 実行の入力。実行する前に値を受け取る。
enum class InputKind : u8 { None, Number, Hex };
// フォルダの中身の出どころ。Fixed は固定中の項目、Favorites はお気に入り（どちらも仮のフォルダ。エンジンが並べる）。
// 設定画面の中だけに置ける（codegen が検査する）。
enum class ListKind : u8 { None, Fixed, Favorites };
// 設定の値の適用。Immediate は適用待ちを経ず、編集した時にその場で効く。
enum class ApplyKind : u8 { Pending, Immediate };
// 表示。Buttons は複数選択の選択肢をボタンの印で見せる（選択肢の名前がボタンの名前）。
enum class DisplayKind : u8 { Default, Buttons };
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
    ListKind list;
    ApplyKind apply;
    DisplayKind display;
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
    bool hold_without_hotkey = false;
};

// 値を項目の範囲に収める（編集・持ち越しの読み込みで同じ規則）。値を持たない項目（フォルダ・実行・画面）には使わない。
inline s32 NormalizeValue(const ItemDef &d, s32 value) {
    switch (d.control) {
    case Control::Toggle:
    case Control::Action: return value ? 1 : 0;
    case Control::Select:
    case Control::Number:
    case Control::Slider: return value < d.min ? d.min : value > d.max ? d.max : value;
    case Control::Multiselect: return value & d.max;
    case Control::Key: return value & 0x3FFF;
    case Control::Folder: break;
    }
    return value;
}

// 生成した MenuId ごとの宣言（結び付けの検査に使う）。menu_ids.hpp が特殊化を出す。
template <auto Id> struct Spec;
} // namespace gohan::menu
