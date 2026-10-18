#include "core/utf.hpp"
#include <cstdint>

namespace gohan::core {
namespace {
constexpr std::uint32_t Replacement = 0xFFFD;

std::uint32_t Decode(std::string_view input, std::size_t &pos, bool &valid) {
    const auto lead = static_cast<unsigned char>(input[pos++]);
    if (lead < 0x80)
        return lead;
    unsigned count = 0;
    std::uint32_t code = 0;
    std::uint32_t minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        count = 1;
        code = lead & 0x1F;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        count = 2;
        code = lead & 0x0F;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        count = 3;
        code = lead & 0x07;
        minimum = 65536;
    } else {
        valid = false;
        return Replacement;
    }
    // 不正な先頭の次のバイトは次回も調べる（ASCII を飲み込まない）。
    if (input.size() - pos < count) {
        valid = false;
        return Replacement;
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(input[pos + i]);
        if ((byte & 0xC0) != 0x80) {
            valid = false;
            return Replacement;
        }
        code = (code << 6) | (byte & 0x3F);
    }
    if (code < minimum || code > 1114111 || (code >= 0xD800 && code <= 0xDFFF)) {
        valid = false;
        return Replacement;
    }
    pos += count;
    return code;
}
} // namespace

UtfResult ToUtf16(std::string_view input, char16_t *output, std::size_t capacity) {
    UtfResult result{0, true, false};
    if (!output)
        capacity = 0;
    std::size_t pos = 0;
    while (pos < input.size()) {
        auto code = Decode(input, pos, result.valid);
        const std::size_t count = code >= 65536 ? 2 : 1;
        if (!capacity || capacity - 1 - result.written < count) {
            result.truncated = true;
            break;
        }
        if (count == 2) {
            code -= 65536;
            output[result.written++] = static_cast<char16_t>(0xD800 + (code >> 10));
            output[result.written++] = static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        } else {
            output[result.written++] = static_cast<char16_t>(code);
        }
    }
    if (capacity)
        output[result.written] = 0;
    return result;
}

UtfResult ToUtf8(std::u16string_view input, char *output, std::size_t capacity) {
    UtfResult result{0, true, false};
    if (!output)
        capacity = 0;
    for (std::size_t pos = 0; pos < input.size(); ++pos) {
        std::uint32_t code = input[pos];
        if (code >= 0xD800 && code <= 0xDBFF && pos + 1 < input.size() &&
            input[pos + 1] >= 0xDC00 && input[pos + 1] <= 0xDFFF) {
            code = 65536 + ((code - 0xD800) << 10) + (input[++pos] - 0xDC00);
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            code = Replacement;
            result.valid = false;
        }
        const std::size_t count = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 65536 ? 3 : 4;
        if (!capacity || capacity - 1 - result.written < count) {
            result.truncated = true;
            break;
        }
        if (count == 1) {
            output[result.written++] = static_cast<char>(code);
        } else {
            const unsigned shift = static_cast<unsigned>((count - 1) * 6);
            const unsigned prefix = count == 2 ? 0xC0 : count == 3 ? 0xE0 : 0xF0;
            output[result.written++] = static_cast<char>(prefix | (code >> shift));
            for (unsigned bits = shift; bits; bits -= 6)
                output[result.written++] = static_cast<char>(0x80 | ((code >> (bits - 6)) & 0x3F));
        }
    }
    if (capacity)
        output[result.written] = 0;
    return result;
}
} // namespace gohan::core
