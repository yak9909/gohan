#pragma once
#include "menu/types.hpp"
#include <string_view>

namespace gohan::menu {
// ホットキーはボタンのビットの組み合わせ。ビットの並びは ZL L R ZR Up Down Left Right A B X Y Select Start。
namespace key {
inline constexpr u16 ZL = 1u << 0;
inline constexpr u16 L = 1u << 1;
inline constexpr u16 R = 1u << 2;
inline constexpr u16 ZR = 1u << 3;
inline constexpr u16 Up = 1u << 4;
inline constexpr u16 Down = 1u << 5;
inline constexpr u16 Left = 1u << 6;
inline constexpr u16 Right = 1u << 7;
inline constexpr u16 A = 1u << 8;
inline constexpr u16 B = 1u << 9;
inline constexpr u16 X = 1u << 10;
inline constexpr u16 Y = 1u << 11;
inline constexpr u16 Select = 1u << 12;
inline constexpr u16 Start = 1u << 13;
inline constexpr u16 Directions = Up | Down | Left | Right;
} // namespace key

// "L+Up" のような表記を読む（大文字小文字は問わない）。不正なら false。
bool ParseHotkey(std::string_view text, u16 &mask);
// mask を "L+Up" の形に書く。0 は "なし"。容量は終端を含む。書けた長さを返す。
std::size_t FormatHotkey(u16 mask, char *buffer, std::size_t capacity);
} // namespace gohan::menu
