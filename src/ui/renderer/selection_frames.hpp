#pragma once
#include "ui/renderer/renderer.hpp"

namespace gohan::ui::renderer::detail {
inline constexpr unsigned SelectionFirst = 0x53454C00, SelectionSecond = SelectionFirst + 1;
const std::uint8_t *ComposeSelection(unsigned frame);
void StoreSelection(unsigned slot, const std::uint8_t *pixels);
struct SelectionTiming {
    std::uint64_t totalMicros = 0;
    unsigned samples = 0, lastMicros = 0, maxMicros = 0, unavailable = 0;
};
SelectionTiming ReadSelectionTiming();
} // namespace gohan::ui::renderer::detail
