#include "ui/renderer/internal.hpp"
#include <algorithm>
#include <cstdint>

namespace gohan::ui::renderer {
namespace detail {
Rect ScreenBounds(Screen screen) { return {0, 0, screen == Screen::Bottom ? 320 : 400, 240}; }

Rect Intersect(Rect a, Rect b) {
    if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0)
        return {};
    const std::int64_t left = std::max(a.x, b.x);
    const std::int64_t top = std::max(a.y, b.y);
    const auto right = std::min(std::int64_t(a.x) + a.width, std::int64_t(b.x) + b.width);
    const auto bottom = std::min(std::int64_t(a.y) + a.height, std::int64_t(b.y) + b.height);
    if (right <= left || bottom <= top)
        return {};
    return {static_cast<int>(left), static_cast<int>(top),
            static_cast<int>(right - left), static_cast<int>(bottom - top)};
}
} // namespace detail

game::lyt::ScreenDrawing &Canvas::Current() {
    return drawing_.screens[screen_ == Screen::Bottom ? 1 : 0];
}

void Canvas::Begin(Screen screen) {
    screen_ = screen;
    auto &current = Current();
    current.count = current.rectangles = current.texts = current.characters = current.estimatedBytes = 0;
    clip_ = detail::ScreenBounds(screen);
    clipped_ = false;
}

bool Canvas::BeginClip(Rect bounds) {
    if (clipped_)
        return false;
    clip_ = detail::Intersect(bounds, detail::ScreenBounds(screen_));
    clipped_ = true;
    return true;
}

void Canvas::EndClip() {
    clip_ = detail::ScreenBounds(screen_);
    clipped_ = false;
}
} // namespace gohan::ui::renderer
