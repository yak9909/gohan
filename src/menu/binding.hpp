#pragma once
#include "menu/types.hpp"
#include <type_traits>

// 機能が振る舞いを項目に結ぶ書き方。
//
//   inline constexpr Bound kBindings[] = {
//       GOHAN_ITEM(MenuId::PlayerWall,  Patch{.set = SetWall}),
//       GOHAN_ITEM(MenuId::PlayerMove,  Hold{.step = MoveStep}),
//       GOHAN_ITEM(MenuId::ItemsPocket, Trigger{.run_with = AddToPocket}),
//   };
//
// 結び付けの種類が spec の振る舞いと合わない、必要な関数が無いなどは、コンパイルで止まる。
namespace gohan::menu {
// 失敗の理由。文言は notice.cpp が決める（機能は文言を持たない）。
enum class Result : u8 { Ok, NotReady, Unsupported, Busy, Failed };

// 状態（state）: 効果を入れる・外す。is_active があれば、効果が入っているかをそれで調べる。
struct Patch {
    Result (*set)(bool on);
    bool (*is_active)() = nullptr;
};
// 押下中（hold）: 有効の間、毎フレーム呼ぶ。holding は有効かつホットキーを押している間だけ真。
struct Hold {
    void (*step)(bool holding);
};
// 実行（trigger）: 入力が無ければ run、あれば run_with を結ぶ（どちらか一方）。
struct Trigger {
    Result (*run)() = nullptr;
    Result (*run_with)(u32 input) = nullptr;
};
// 画面（mode）: open で開き、起動中は毎フレーム step（false を返したら自分で閉じた）。
// close はエンジンが閉じる時（再実行・排他）に呼ぶ。
struct Mode {
    Result (*open)();
    bool (*step)() = nullptr;
    void (*close)() = nullptr;
};
// 値（ゲーム）: 開いた時に read で 1 回読み、適用で write する。
struct GameValue {
    bool (*read)(s32 *value);
    Result (*write)(s32 value);
};
// 値（設定）: 値の置き場はエンジンが持つ。適用された時に知りたい機能だけが applied を結ぶ。
struct SettingValue {
    void (*applied)(s32 value) = nullptr;
};

struct Bound {
    u16 item = kNoItem;
    Behavior behavior = Behavior::None;
    Source source = Source::None;
    Patch patch{};
    Hold hold{};
    Trigger trigger{};
    Mode mode{};
    GameValue game{};
    SettingValue setting{};
};

// 定数評価の中で呼ぶと、名前がそのままコンパイルの誤りに出る（定義は無い）。
namespace bind_error {
void required_function_is_null();
void run_and_run_with_must_be_exactly_one();
} // namespace bind_error

template <auto Id, class B> consteval Bound BindChecked(B binding) {
    using S = Spec<Id>;
    Bound bound;
    bound.item = static_cast<u16>(Id);
    bound.behavior = S::behavior;
    bound.source = S::source;
    if constexpr (std::is_same_v<B, Patch>) {
        static_assert(S::behavior == Behavior::State, "Patch は振る舞いが state の項目に結ぶ");
        if (binding.set == nullptr) bind_error::required_function_is_null();
        bound.patch = binding;
    } else if constexpr (std::is_same_v<B, Hold>) {
        static_assert(S::behavior == Behavior::Hold, "Hold は振る舞いが hold の項目に結ぶ");
        if (binding.step == nullptr) bind_error::required_function_is_null();
        bound.hold = binding;
    } else if constexpr (std::is_same_v<B, Trigger>) {
        static_assert(S::behavior == Behavior::Trigger, "Trigger は振る舞いが trigger の項目に結ぶ");
        // 入力のある項目は run_with、無い項目は run を結ぶ。
        if ((S::input == InputKind::None) ? (binding.run == nullptr || binding.run_with != nullptr)
                                          : (binding.run_with == nullptr || binding.run != nullptr))
            bind_error::run_and_run_with_must_be_exactly_one();
        bound.trigger = binding;
    } else if constexpr (std::is_same_v<B, Mode>) {
        static_assert(S::behavior == Behavior::Mode, "Mode は振る舞いが mode の項目に結ぶ");
        if (binding.open == nullptr) bind_error::required_function_is_null();
        bound.mode = binding;
    } else if constexpr (std::is_same_v<B, GameValue>) {
        static_assert(S::behavior == Behavior::Value && S::source == Source::Game,
                      "GameValue は source が game の値の項目に結ぶ");
        if (binding.read == nullptr || binding.write == nullptr) bind_error::required_function_is_null();
        bound.game = binding;
    } else if constexpr (std::is_same_v<B, SettingValue>) {
        static_assert(S::behavior == Behavior::Value && S::source == Source::Setting,
                      "SettingValue は source が setting の値の項目に結ぶ");
        bound.setting = binding;
    } else {
        static_assert(!sizeof(B), "結び付けの型は Patch / Hold / Trigger / Mode / GameValue / SettingValue");
    }
    return bound;
}
} // namespace gohan::menu

#define GOHAN_ITEM(id, ...) ::gohan::menu::BindChecked<id>(__VA_ARGS__)
