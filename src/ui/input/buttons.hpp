#pragma once
#include <cstdint>

// ボタンとタッチの読み取り。ビットの並びは ZL L R ZR Up Down Left Right A B X Y Select Start（十字キーのみ）。
namespace gohan::ui::input {
namespace button {
inline constexpr std::uint16_t ZL = 1u << 0;
inline constexpr std::uint16_t L = 1u << 1;
inline constexpr std::uint16_t R = 1u << 2;
inline constexpr std::uint16_t ZR = 1u << 3;
inline constexpr std::uint16_t Up = 1u << 4;
inline constexpr std::uint16_t Down = 1u << 5;
inline constexpr std::uint16_t Left = 1u << 6;
inline constexpr std::uint16_t Right = 1u << 7;
inline constexpr std::uint16_t A = 1u << 8;
inline constexpr std::uint16_t B = 1u << 9;
inline constexpr std::uint16_t X = 1u << 10;
inline constexpr std::uint16_t Y = 1u << 11;
inline constexpr std::uint16_t Select = 1u << 12;
inline constexpr std::uint16_t Start = 1u << 13;
} // namespace button

struct Snapshot {
    std::uint16_t buttons = 0;
    bool touching = false;
    int x = 0, y = 0; // 下画面の画素。touching が偽の時は 0
};

// 押されているボタンの水準とタッチの位置を読む。CTRPF が更新した最新の値を、更新せずに読む。
Snapshot Read();
// 起動からのミリ秒（約 24 日で周回する。差で扱うこと）。
std::uint32_t NowMs();
} // namespace gohan::ui::input
