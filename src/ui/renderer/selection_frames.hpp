#pragma once
#include "ui/renderer/renderer.hpp"
#include "ui_values.hpp"

namespace gohan::ui::renderer::detail {
namespace selection = core::design::ca2::Titlebg::Selection;
inline constexpr unsigned SelectionFirst = 0x53454C00, SelectionSecond = SelectionFirst + 1;
inline constexpr unsigned SelectionRows = selection::AtlasSize[1] / selection::CellSize[1];
struct SelectionAddress { unsigned page, x, y; };
inline SelectionAddress SelectionAt(unsigned offset) {
    const unsigned row = offset / selection::AtlasSize[0];
    return {row >= SelectionRows ? 1u : 0u, offset % selection::AtlasSize[0],
            (row >= SelectionRows ? row - SelectionRows : row) * unsigned(selection::CellSize[1])};
}
} // namespace gohan::ui::renderer::detail
