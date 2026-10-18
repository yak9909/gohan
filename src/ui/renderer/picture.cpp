#include "ui/renderer/internal.hpp"
#include <limits>

namespace gohan::ui::renderer {
Result Canvas::FillRect(Rect bounds, std::uint32_t color) {
    bounds = detail::Intersect(bounds, clip_);
    if (!bounds.width || !bounds.height || !(color >> 24))
        return Result::Empty;
    auto &current = Current();
    const auto result = detail::Reserve(current, false, 0);
    if (result != Result::Added)
        return result;
    auto &command = current.commands[current.count];
    command.kind = game::lyt::Kind::Rectangle;
    command.bounds = bounds;
    command.clip = clip_;
    command.color = color;
    command.length = 0;
    detail::Account(current, false, 0);
    return Result::Added;
}

Result Canvas::FrameRect(Rect bounds, std::uint32_t color) {
    if (bounds.width <= 0 || bounds.height <= 0)
        return Result::Empty;
    if (bounds.width < 2 || bounds.height < 2)
        return FillRect(bounds, color);
    const auto right = std::int64_t(bounds.x) + bounds.width - 1;
    const auto bottom = std::int64_t(bounds.y) + bounds.height - 1;
    if (right > std::numeric_limits<int>::max() || bottom > std::numeric_limits<int>::max())
        return Result::Invalid;
    auto &current = Current();
    const auto count = current.count, rects = current.rectangles, bytes = current.estimatedBytes;
    const Rect edges[] = {{bounds.x, bounds.y, bounds.width, 1},
                          {bounds.x, static_cast<int>(bottom), bounds.width, 1},
                          {bounds.x, bounds.y + 1, 1, bounds.height - 2},
                          {static_cast<int>(right), bounds.y + 1, 1, bounds.height - 2}};
    Result overall = Result::Empty;
    for (const auto &edge : edges) {
        const auto result = FillRect(edge, color);
        if (result == Result::Full || result == Result::Budget) {
            current.count = count;
            current.rectangles = rects;
            current.estimatedBytes = bytes;
            return result;
        }
        if (result == Result::Added)
            overall = Result::Added;
    }
    return overall;
}
} // namespace gohan::ui::renderer
