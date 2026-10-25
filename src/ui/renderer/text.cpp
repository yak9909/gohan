#include "ui/renderer/internal.hpp"
#include "core/utf.hpp"
#include <iterator>

namespace gohan::ui::renderer {
namespace {
struct Line {
    char16_t text[core::limits::RendererChars + 1]{};
    std::size_t length = 0;
    int width = 0;
    bool valid = true;
    bool truncated = false;
};
Line Prepare(std::string_view text, Font font) {
    Line line;
    char16_t decoded[core::limits::RendererChars + 1];
    const auto result = core::ToUtf16(text, decoded, std::size(decoded));
    line.valid = result.valid;
    line.truncated = result.truncated;
    for (std::size_t i = 0; i < result.written; ++i) {
        const auto advance = detail::Advance(decoded[i], font);
        if (!advance)
            continue;
        line.text[line.length++] = decoded[i];
        line.width += advance;
    }
    return line;
}
} // namespace

int MeasureText(std::string_view text, unsigned scale, Font font) {
    if (scale < 1 || scale > 8 || (font != Font::Main && font != Font::Numeric))
        return 0;
    char16_t decoded[core::limits::RendererChars + 1];
    const auto result = core::ToUtf16(text, decoded, std::size(decoded));
    int width = 0;
    for (std::size_t i = 0; i < result.written; ++i) width += detail::Advance(decoded[i], font);
    return width * static_cast<int>(scale);
}

Result Canvas::Text(int x, int y, std::string_view text, std::uint32_t color,
                    unsigned scale, Font font) {
    if (scale < 1 || scale > 8 || (font != Font::Main && font != Font::Numeric))
        return Result::Invalid;
    const auto line = Prepare(text, font);
    if (!line.valid)
        return Result::Invalid;
    if (!line.length || !(color >> 24))
        return Result::Empty;
    const Rect bounds{x, y, line.width * static_cast<int>(scale), 8 * static_cast<int>(scale)};
    if (!detail::Intersect(bounds, clip_).width)
        return Result::Empty;
    auto &current = Current();
    const auto result = detail::Reserve(current, true, line.length);
    if (result != Result::Added)
        return result;
    const int slot = detail::FindSlot(current, line.length);
    if (slot < 0)
        return Result::Full;
    auto &command = current.commands[current.count];
    command.kind = game::lyt::Kind::Text;
    command.bounds = bounds;
    command.clip = clip_;
    command.font = font;
    command.scale = scale;
    command.color = color;
    command.slot = static_cast<unsigned>(slot);
    command.length = static_cast<unsigned>(line.length);
    for (std::size_t i = 0; i <= line.length; ++i)
        command.text[i] = line.text[i];
    detail::Account(current, true, line.length);
    return line.truncated ? Result::Truncated : Result::Added;
}
} // namespace gohan::ui::renderer
