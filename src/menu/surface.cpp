#include "menu/surface.hpp"
#include "game/frame_timing.hpp"
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

std::size_t Previous(std::string_view text, std::size_t at) {
    --at;
    while (at && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) --at;
    return at;
}

bool ForbiddenStart(std::string_view text, std::size_t at) {
    return std::string_view(core::design::ca2::Trim::LineStartForbidden).find(text.substr(at, CharBytes(text, at))) !=
           std::string_view::npos;
}

// 禁則文字を 1 字分の余白へぶら下げる。連続して収まらない時は、その前の字ごと次行へ送る。
std::size_t LineEnd(const Surface &surface, std::string_view text, int width) {
    const std::size_t fit = FitPrefix(surface, text, width, kLineChars, false);
    if (!fit) return CharBytes(text, 0);
    std::size_t end = fit, extra = 0;
    while (end < text.size() && ForbiddenStart(text, end)) {
        end += CharBytes(text, end);
        ++extra;
    }
    if (!extra) return fit;
    if (extra == 1 && surface.Measure(text.substr(0, end)) <= width + core::design::ca2::Trim::Hang) return end;
    std::size_t back = fit;
    do { back = Previous(text, back); } while (back && ForbiddenStart(text, back));
    return back ? back : fit;
}
} // namespace

std::string DisplayText(std::string_view text) {
    std::string storage;
    return std::string(DisplayText(text, storage));
}

std::string_view DisplayText(std::string_view text, std::string &storage) {
    bool replaced = false;
    const auto &from = core::design::ca2::Text::Replace::From;
    const auto &to = core::design::ca2::Text::Replace::To;
    for (std::size_t n = 0; n < from.size(); ++n) {
        const std::size_t len = std::strlen(from[n]);
        if (!len) continue;
        for (std::size_t at = 0; (at = text.find(from[n], at)) != std::string_view::npos;) {
            if (!replaced) { storage.assign(text); replaced = true; }
            storage.replace(at, len, to[n]);
            text = storage;
            at += std::strlen(to[n]);
        }
    }
    return text;
}

std::string_view Trim(const Surface &surface, std::string_view text, int max_width, char *buffer,
                      std::size_t capacity, bool numeric) {
    const game::frame_timing::Scope watch(game::frame_timing::Stage::Layout);
    std::string storage;
    const auto display = DisplayText(text, storage);
    const bool replaced = display != text;
    if (replaced) text = display;
    std::size_t chars = 0;
    for (std::size_t at = 0; at < text.size(); at += CharBytes(text, at)) ++chars;
    if (chars <= kMaxChars && surface.Measure(text, numeric) <= max_width)
        return replaced ? Store(buffer, capacity, text, "") : text;
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
    const game::frame_timing::Scope watch(game::frame_timing::Stage::Layout);
    std::string storage;
    text = DisplayText(text, storage);
    if (truncated) *truncated = false;
    if (max_lines <= 0) return 0;
    int count = 0;
    for (std::size_t start = 0; start < text.size();) {
        const std::size_t end = start + LineEnd(surface, text.substr(start), max_width);
        Store(lines[count++], kLineBytes, text.substr(start, end - start), "");
        if (end < text.size() && count >= max_lines) {
            if (truncated) *truncated = true;
            char* const last = lines[count - 1];
            const std::size_t fit =
                FitPrefix(surface, std::string_view(last), max_width - surface.Measure(core::design::ca2::Trim::Ellipsis), kLineChars, false);
            last[fit] = '\0';
            std::strcat(last, core::design::ca2::Trim::Ellipsis);
            return count;
        }
        start = end;
    }
    return count;
}
} // namespace gohan::menu
