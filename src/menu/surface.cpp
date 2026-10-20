#include "menu/surface.hpp"
#include "core/limits.hpp"
#include "ui_values.hpp"
#include <cstring>
#include <initializer_list>

namespace gohan::menu {
namespace {
// 1 つの文字列は最大 RendererChars 字。折り返しには省略記号の余裕を残す。
constexpr std::size_t kMaxChars = core::limits::RendererChars;
constexpr std::size_t kLineChars = kMaxChars - 3;

std::size_t CharBytes(std::string_view text, std::size_t at) {
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
    if (at + length > text.size()) length = text.size() - at;
    return length;
}

std::string_view Store(char *buffer, std::size_t capacity, std::string_view head, std::string_view tail) {
    std::size_t size = 0;
    for (std::string_view part : {head, tail}) {
        const std::size_t room = capacity - 1 - size;
        const std::size_t n = part.size() < room ? part.size() : room;
        std::memcpy(buffer + size, part.data(), n);
        size += n;
    }
    buffer[size] = '\0';
    return std::string_view(buffer, size);
}

// 幅と字数に収まる最も長い先頭部分のバイト数。
std::size_t FitPrefix(const Surface &surface, std::string_view text, int width, std::size_t max_chars, bool numeric) {
    std::size_t fit = 0, chars = 0;
    for (std::size_t at = 0; at < text.size();) {
        at += CharBytes(text, at);
        if (++chars > max_chars || surface.Measure(text.substr(0, at), numeric) > width) break;
        fit = at;
    }
    return fit;
}
} // namespace

std::string DisplayText(std::string_view text) {
    std::string result(text);
    const auto &from = core::design::ca2::Text::Replace::From;
    const auto &to = core::design::ca2::Text::Replace::To;
    for (std::size_t n = 0; n < from.size(); ++n) {
        const std::size_t len = std::strlen(from[n]);
        if (!len) continue;
        for (std::size_t at = 0; (at = result.find(from[n], at)) != std::string::npos;) {
            result.replace(at, len, to[n]); at += std::strlen(to[n]);
        }
    }
    return result;
}

std::string_view Trim(const Surface &surface, std::string_view text, int max_width, char *buffer,
                      std::size_t capacity, bool numeric) {
    std::size_t chars = 0;
    for (std::size_t at = 0; at < text.size(); at += CharBytes(text, at)) ++chars;
    if (chars <= kMaxChars && surface.Measure(text, numeric) <= max_width) return text;
    std::size_t fit = FitPrefix(surface, text, max_width - surface.Measure(core::design::ca2::Trim::Ellipsis, numeric), kLineChars, numeric);
    const std::string_view brackets = core::design::ca2::Trim::OpenBrackets;
    while (fit) {
        std::size_t at = fit - 1;
        while (at && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) --at;
        if (brackets.find(text.substr(at, fit - at)) == std::string_view::npos) break;
        fit = at;
    }
    return Store(buffer, capacity, text.substr(0, fit), core::design::ca2::Trim::Ellipsis);
}

int Wrap(const Surface &surface, std::string_view text, int max_width, char (*lines)[kLineBytes], int max_lines,
         bool *truncated) {
    if (truncated) *truncated = false;
    if (max_lines <= 0) return 0;
    int count = 0;
    std::size_t start = 0, chars = 0;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t next = at + CharBytes(text, at);
        const bool too_wide =
            chars > 0 && (chars >= kLineChars || surface.Measure(text.substr(start, next - start)) > max_width);
        if (!too_wide) {
            at = next;
            ++chars;
            continue;
        }
        Store(lines[count], kLineBytes, text.substr(start, at - start), "");
        if (++count >= max_lines) {
            if (truncated) *truncated = true;
            char* const last = lines[count - 1];
            const std::size_t fit =
                FitPrefix(surface, std::string_view(last), max_width - surface.Measure(core::design::ca2::Trim::Ellipsis), kLineChars, false);
            last[fit] = '\0';
            std::strcat(last, core::design::ca2::Trim::Ellipsis);
            return count;
        }
        start = at;
        chars = 0;
    }
    if (start < text.size()) Store(lines[count++], kLineBytes, text.substr(start), "");
    return count;
}
} // namespace gohan::menu
