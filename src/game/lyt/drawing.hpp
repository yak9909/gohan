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
// 上・下の画面は別々に確定される。revisions は確定が入れ替わるたびに増え、
// ゲーム側は変化のあった画面だけを組み直す。
struct Drawing {
    ScreenDrawing screens[2]{};
    std::uint32_t revisions[2]{};
};
// 字形のアトラス（1 枚の LA4 画像）と、その字形表。マップは (文字コード, 字形番号) の組を
// 文字コードの昇順に並べた物。widths は字形ごとの 3 バイト（左, 字形幅, 送り幅）。
struct FontResource {
    const std::uint8_t *atlas = nullptr;
    std::size_t atlasBytes = 0;
    const std::uint8_t *widths = nullptr;
    std::size_t glyphs = 0;
    const std::uint16_t *mainMap = nullptr;
    std::size_t mainCount = 0;
    const std::uint16_t *numericMap = nullptr;
    std::size_t numericCount = 0;
    // アトラスの寸法。字形は 1 画素の隙間を空けた格子に並び、塗りつぶし用のセルが 1 つある。
    unsigned sheetWidth = 0, sheetHeight = 0;
    unsigned cellWidth = 0, cellHeight = 0, columns = 0;
    unsigned solidGlyph = 0;
};
} // namespace gohan::game::lyt
