#pragma once
#include <cstddef>
#include <string_view>

namespace gohan::core {
struct UtfResult {
    std::size_t written;
    bool valid;
    bool truncated;
};

// 容量は終端を含む。壊れた入力は U+FFFD とし、符号の途中では切らない。
UtfResult ToUtf16(std::string_view input, char16_t *output, std::size_t capacity);
UtfResult ToUtf8(std::u16string_view input, char *output, std::size_t capacity);
} // namespace gohan::core
