#pragma once
#include "menu/surface.hpp"
#include "menu/hotkey.hpp"
#include "ui_values.hpp"
#include <string>

namespace gohan::menu {
namespace ca = core::design::ca2;
inline Rect Bounds(const std::array<int, 4> &r, int x = 0, int y = 0, int width = 0) {
    return {x + r[0], y + r[1], r[2] < 0 ? width + r[2] : r[2], r[3]};
}
inline void Paper(Surface &s, Rect r, float alpha = 1) {
    s.Box(r, "window", Fade(0xFFFFFFFFu, alpha));
    const int inset = ca::Titlebg::Window::Inset;
    s.Pattern({r.x + inset, r.y + inset, r.w - inset * 2, r.h - inset * 2}, "window", Fade(0xFFFFFFFFu, alpha));
}
inline void Selected(Surface &s, Rect r, bool disabled = false, float alpha = 1) {
    if (disabled) { s.Box(r, "disband", Fade(0xFFFFFFFFu, alpha)); return; }
    const auto &i = ca::Titlebg::Selection::Inset;
    const Rect inner{r.x + i[0], r.y + i[1], r.w + i[2], r.h + i[3]};
    s.Fill(inner, Fade(ca::Titlebg::Fallback::Selection, alpha));
    s.Pattern(inner, "selection", Fade(0xFFFFFFFFu, alpha));
    s.Box(r, "selrim", Fade(0xFFFFFFFFu, alpha));
}
inline void Ink(Surface &s, int x, int y, std::string_view text, bool selected, u32 color, bool numeric = false,
                u32 shadow = ca::Color::Sel::Shadow) {
    if (selected) s.Text(x + ca::Color::Sel::ShadowOffset[0], y + ca::Color::Sel::ShadowOffset[1], text,
                         shadow, numeric);
    s.Text(x, y, text, color, numeric);
}
// 影を含めても文字枠を増やさない描画先が実装する。代役では通常の字だけを記録する。
inline int KeyIcons(Surface &s, int x, int y, u16 mask, bool bar = false) {
    constexpr u16 keys[] = {key::L, key::R, key::Up, key::Down, key::Left, key::Right, key::A, key::B, key::X, key::Y};
    constexpr const char *names[] = {"l", "r", "up", "down", "left", "right", "a", "b", "x", "y"};
    const std::string prefix = bar ? "key.bar." : "key.paper.";
    const int start = x;
    auto icon = [&](const char *name, int width) {
        if (x != start) { s.Sprite(x + 1, y + 3, prefix + "plus"); x += ca::Icon::Plus[0] + 2; }
        s.Sprite(x, y, prefix + name); x += width;
    };
    if ((mask & (key::Up | key::Down)) == (key::Up | key::Down)) { icon("ud", ca::Icon::Dpad[0]); mask &= ~(key::Up | key::Down); }
    if ((mask & (key::Left | key::Right)) == (key::Left | key::Right)) { icon("lr", ca::Icon::Dpad[0]); mask &= ~(key::Left | key::Right); }
    for (unsigned n = 0; n < 10; ++n) if (mask & keys[n]) icon(names[n], n < 2 ? ca::Icon::Button::Shoulder[0] : ca::Icon::Button::Round[0]);
    if (mask & (key::ZL | key::ZR | key::Start | key::Select)) {
        char text[64]; FormatHotkey(mask & (key::ZL | key::ZR | key::Start | key::Select), text, sizeof(text));
        if (x != start) { s.Sprite(x + 1, y + 3, prefix + "plus"); x += ca::Icon::Plus[0] + 2; }
        s.Text(x, y + 2, text, bar ? ca::Color::Bar::Text : ca::Color::Text); x += s.Measure(text);
    }
    return x;
}
} // namespace gohan::menu
