#pragma once
#include "menu/types.hpp"

namespace gohan::menu {
// 数値を項目の書式（10進・16進・小数1桁）で文字にする。小数1桁の値は 10 倍した整数。
std::size_t FormatNumber(Format format, s32 value, char *buffer, std::size_t capacity);
inline std::size_t FormatNumber(const ItemDef &def, s32 value, char *buffer, std::size_t capacity) {
    return FormatNumber(def.format, value, buffer, capacity);
}
} // namespace gohan::menu
