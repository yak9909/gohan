#include "menu/hotkey.hpp"
#include "core/format.hpp"

namespace gohan::menu {
namespace {
constexpr const char *kNames[] = {"ZL", "L", "R", "ZR", "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "Select", "Start"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);

bool SameName(std::string_view text, const char *name) {
    std::size_t i = 0;
    for (; i < text.size() && name[i] != '\0'; ++i) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        char n = name[i];
        if (n >= 'a' && n <= 'z') n = static_cast<char>(n - 'a' + 'A');
        if (c != n) return false;
    }
    return i == text.size() && name[i] == '\0';
}
} // namespace

bool ParseHotkey(std::string_view text, u16 &mask) {
    u16 result = 0;
    std::size_t begin = 0;
    for (;;) {
        std::size_t end = text.find('+', begin);
        std::string_view part = text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
        unsigned bit = 0;
        while (bit < kNameCount && !SameName(part, kNames[bit])) ++bit;
        if (bit == kNameCount || (result & (1u << bit)) != 0) return false;
        result = static_cast<u16>(result | (1u << bit));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    mask = result;
    return true;
}

std::size_t FormatHotkey(u16 mask, char *buffer, std::size_t capacity) {
    core::TextWriter writer(buffer, capacity);
    if (mask == 0) {
        writer.Append("なし");
        return writer.Size();
    }
    bool first = true;
    for (unsigned bit = 0; bit < kNameCount; ++bit) {
        if ((mask & (1u << bit)) == 0) continue;
        if (!first) writer.Append("+");
        writer.Append(kNames[bit]);
        first = false;
    }
    return writer.Size();
}
} // namespace gohan::menu
