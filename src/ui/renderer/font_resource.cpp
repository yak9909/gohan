#include "ui/renderer/internal.hpp"
#include "font_atlas.hpp"
#include "ui_atlas.hpp"
#include "ui_values.hpp"
#include <iterator>

namespace gohan::ui::renderer {
static_assert(std::size(ui_data::Atlas) == core::design::ca2::Atlas::Bytes);
static_assert(std::size(ui_data::Atlas) + std::size(ui_data::DashH) + std::size(ui_data::DashV) ==
              core::limits::RendererUiTextureBytes);
const game::lyt::FontResource &Fonts() {
    static const game::lyt::TextureResource textures[] = {
        {ui_data::Atlas, std::size(ui_data::Atlas), core::design::ca2::Atlas::Size[0], core::design::ca2::Atlas::Size[1]},
        {ui_data::DashH, std::size(ui_data::DashH), core::design::ca2::Part::Dash[0], core::design::ca2::Part::Dash[1]},
        {ui_data::DashV, std::size(ui_data::DashV), core::design::ca2::Part::Dash[1], core::design::ca2::Part::Dash[0]},
        TitleBackgroundImage(0), TitleBackgroundImage(1), TitleBackgroundImage(2),
#ifndef __3DS__
        TitleBackgroundImage(3),
#endif
    };
    static const game::lyt::FontResource resource{
        font_data::Atlas, std::size(font_data::Atlas), font_data::Widths,
        std::size(font_data::Widths) / 3,
        font_data::MisakiMap, std::size(font_data::MisakiMap) / 2,
        font_data::NumericMap, std::size(font_data::NumericMap) / 2,
        font_data::SheetWidth, font_data::SheetHeight, font_data::CellWidth, font_data::CellHeight,
        font_data::Columns, font_data::SolidIndex, textures,
#ifdef __3DS__
        TitleBackgroundSources() ? std::size(textures) : 3,
#else
        std::size(textures),
#endif
        3};
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
