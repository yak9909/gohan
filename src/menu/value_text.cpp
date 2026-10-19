#include "menu/value_text.hpp"
#include "core/format.hpp"

namespace gohan::menu {
std::size_t FormatNumber(Format format, s32 value, char *buffer, std::size_t capacity) {
    core::TextWriter w(buffer, capacity);
    if (format == Format::Hex) {
        w.Append("0x").Unsigned(static_cast<u32>(value), 16);
    } else if (format == Format::Decimal1) {
        const bool negative = value < 0;
        const u32 magnitude = negative ? 0u - static_cast<u32>(value) : static_cast<u32>(value);
        if (negative) w.Append("-");
        w.Unsigned(magnitude / 10).Append(".").Unsigned(magnitude % 10);
    } else {
        w.Signed(value);
    }
    return w.Size();
}
} // namespace gohan::menu
