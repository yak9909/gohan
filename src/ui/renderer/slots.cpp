#include "ui/renderer/internal.hpp"
#include <iterator>

namespace gohan::ui::renderer::detail {
namespace {
constexpr unsigned Capacities[] = {2, 2, 4, 4, 8, 8, 8, 8, 16, 16, 16, 16, 28, 28, 28, 28};
static_assert(std::size(Capacities) == core::limits::RendererTexts);
constexpr std::size_t Estimate(bool text, std::size_t characters) {
    return text ? 552 + 48 * characters : 204;
}
} // namespace

int FindSlot(const game::lyt::ScreenDrawing &drawing, std::size_t length) {
    for (unsigned slot = 0; slot < std::size(Capacities); ++slot) {
        if (Capacities[slot] < length)
            continue;
        bool used = false;
        for (std::size_t i = 0; i < drawing.count; ++i)
            if (drawing.commands[i].kind == game::lyt::Kind::Text &&
                drawing.commands[i].slot == slot)
                used = true;
        if (!used)
            return static_cast<int>(slot);
    }
    return -1;
}

Result Reserve(const game::lyt::ScreenDrawing &drawing, bool text, std::size_t characters) {
    if (drawing.count == core::limits::RendererCommands ||
        (text ? drawing.texts == core::limits::RendererTexts :
                drawing.rectangles == core::limits::RendererRects))
        return Result::Full;
    constexpr auto limit = core::limits::RendererRecordBytes *
                           core::limits::RendererRecordPercent / 100;
    if (drawing.estimatedBytes + Estimate(text, characters) > limit)
        return Result::Budget;
    return Result::Added;
}

void Account(game::lyt::ScreenDrawing &drawing, bool text, std::size_t characters) {
    ++drawing.count;
    if (text)
        ++drawing.texts;
    else
        ++drawing.rectangles;
    drawing.characters += characters;
    drawing.estimatedBytes += Estimate(text, characters);
}
} // namespace gohan::ui::renderer::detail
