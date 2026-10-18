#pragma once
#include "core/limits.hpp"
#include <cstddef>
#include <cstdint>

namespace gohan::game::lyt {
// プラグイン独自の描画指示。ゲームのオブジェクトの配置を表す型ではない。
enum class Screen : unsigned { Top, Bottom };
enum class Font : unsigned { Main, Numeric };
enum class Kind : unsigned { Rectangle, Text };
struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
};
struct Command {
    Kind kind = Kind::Rectangle;
    Rect bounds{};
    Rect clip{};
    std::uint32_t color = 0;
    Font font = Font::Main;
    unsigned scale = 0;
    unsigned slot = 0;
    unsigned length = 0;
    char16_t text[core::limits::RendererChars + 1]{};
};
struct ScreenDrawing {
    Command commands[core::limits::RendererCommands]{};
    std::size_t count = 0;
    std::size_t rectangles = 0;
    std::size_t texts = 0;
    std::size_t characters = 0;
    std::size_t estimatedBytes = 0;
};
struct Drawing {
    ScreenDrawing screens[2]{};
};
struct FontResource {
    const std::uint8_t *atlas;
    std::size_t atlasBytes;
    const std::uint8_t *widths;
    std::size_t glyphs;
    const std::uint16_t *mainMap;
    std::size_t mainCount;
    const std::uint16_t *numericMap;
    std::size_t numericCount;
};
} // namespace gohan::game::lyt
