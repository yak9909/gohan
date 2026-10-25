#include "ui/renderer/internal.hpp"
#include <iterator>

namespace gohan::ui::renderer::detail {
namespace {
constexpr const auto &Capacities = core::limits::RendererSlotChars;
static_assert(std::size(Capacities) == core::limits::RendererTexts);
constexpr std::size_t Estimate(bool text, std::size_t characters) {
    return text ? 552 + 48 * characters : 204;
}
} // namespace

int FindSlot(const game::lyt::ScreenDrawing &drawing, std::size_t length) {
    bool used[core::limits::RendererTexts]{};
    for (std::size_t i = 0; i < drawing.count; ++i)
        if (drawing.commands[i].kind == game::lyt::Kind::Text && drawing.commands[i].slot < std::size(used))
            used[drawing.commands[i].slot] = true;
    for (unsigned slot = 0; slot < std::size(Capacities); ++slot) {
        if (static_cast<std::size_t>(Capacities[slot]) < length)
            continue;
        if (!used[slot])
            return static_cast<int>(slot);
    }
    return -1;
}

Result Reserve(const game::lyt::ScreenDrawing &drawing, bool text, std::size_t characters) {
    if (drawing.count == core::limits::RendererCommands ||
        (text ? drawing.texts == core::limits::RendererTexts :
                drawing.rectangles == drawing.pictureLimit))
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
