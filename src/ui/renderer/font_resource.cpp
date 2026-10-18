#include "ui/renderer/internal.hpp"
#include "font_atlas.hpp"
#include <iterator>

namespace gohan::ui::renderer {
const game::lyt::FontResource &Fonts() {
    static constexpr game::lyt::FontResource resource{
        font_data::Atlas, std::size(font_data::Atlas), font_data::Widths,
        std::size(font_data::Widths) / 3,
        font_data::MisakiMap, std::size(font_data::MisakiMap) / 2,
        font_data::NumericMap, std::size(font_data::NumericMap) / 2};
    return resource;
}

namespace detail {
int FindGlyph(char16_t code, Font font) {
    const auto &resource = Fonts();
    const auto *map = font == Font::Numeric ? resource.numericMap : resource.mainMap;
    std::size_t low = 0;
    std::size_t high = font == Font::Numeric ? resource.numericCount : resource.mainCount;
    while (low < high) {
        const auto mid = low + (high - low) / 2;
        if (map[mid * 2] == code)
            return map[mid * 2 + 1];
        if (map[mid * 2] < code)
            low = mid + 1;
        else
            high = mid;
    }
    return -1;
}
int Advance(char16_t code, Font font) {
    const auto glyph = FindGlyph(code, font);
    return glyph < 0 ? 0 : Fonts().widths[glyph * 3 + 2];
}
} // namespace detail
} // namespace gohan::ui::renderer
