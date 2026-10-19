#pragma once
#include "ui/renderer/renderer.hpp"

namespace gohan::ui::renderer::detail {
Rect ScreenBounds(Screen screen);
Rect Intersect(Rect first, Rect second);
int FindGlyph(char16_t code, Font font);
int Advance(char16_t code, Font font);
int FindSlot(const game::lyt::ScreenDrawing &drawing, std::size_t length);
Result Reserve(const game::lyt::ScreenDrawing &drawing, bool text, std::size_t characters);
void Account(game::lyt::ScreenDrawing &drawing, bool text, std::size_t characters);
} // namespace gohan::ui::renderer::detail
