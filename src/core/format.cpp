#include "core/format.hpp"

namespace gohan::core {
TextWriter::TextWriter(char *buffer, std::size_t capacity)
    : buffer_(buffer), capacity_(buffer ? capacity : 0) {
    if (capacity_)
        buffer_[0] = '\0';
}

void TextWriter::Put(char value) {
    if (capacity_ && size_ < capacity_ - 1) {
        buffer_[size_++] = value;
        buffer_[size_] = '\0';
    } else {
        complete_ = false;
    }
}

TextWriter &TextWriter::Append(std::string_view text) {
    for (char value : text)
        Put(value);
    return *this;
}

TextWriter &TextWriter::Unsigned(std::uint32_t value, unsigned base, unsigned width) {
    if (base < 2 || base > 16 || width > 32) {
        complete_ = false;
        return *this;
    }
    char digits[32];
    unsigned count = 0;
    do {
        digits[count++] = "0123456789ABCDEF"[value % base];
        value /= base;
    } while (value);
    for (unsigned i = count; i < width; ++i)
        Put('0');
    while (count)
        Put(digits[--count]);
    return *this;
}

TextWriter &TextWriter::Signed(std::int32_t value) {
    const auto magnitude = static_cast<std::uint32_t>(value);
    if (value < 0) {
        Put('-');
        return Unsigned(0u - magnitude);
    }
    return Unsigned(magnitude);
}
} // namespace gohan::core
