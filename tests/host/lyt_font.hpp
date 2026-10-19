#pragma once
#include "game/lyt/drawing.hpp"
#include <cstdint>
#include <vector>

namespace fakegame {
// 字形の表の代役: 空白〜~ が 95 字（送り 6）、「予」「約」（送り 8）、塗りつぶし、数字用 13 字（送り 5）。
struct TestFont {
    static constexpr unsigned Main = 97, Solid = 97, Total = 111;
    std::vector<std::uint8_t> atlas = std::vector<std::uint8_t>(65536, 0x11);
    std::vector<std::uint8_t> widths = std::vector<std::uint8_t>(Total * 3);
    std::vector<std::uint16_t> main, numeric;
    gohan::game::lyt::FontResource resource{};
    TestFont() {
        for (unsigned g = 0; g < Total; ++g) {
            widths[g * 3 + 1] = 5;
            widths[g * 3 + 2] = g < 95 ? 6 : g < 97 ? 8 : g == Solid ? 7 : 5;
        }
        for (unsigned c = 0x20; c < 0x7F; ++c) {
            main.push_back(static_cast<std::uint16_t>(c));
            main.push_back(static_cast<std::uint16_t>(c - 0x20));
        }
        main.insert(main.end(), {0x4E88, 95, 0x7D04, 96});
        unsigned g = 98;
        for (unsigned c : {0x2B, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39}) {
            numeric.push_back(static_cast<std::uint16_t>(c));
            numeric.push_back(static_cast<std::uint16_t>(g++));
        }
        resource.atlas = atlas.data();
        resource.atlasBytes = atlas.size();
        resource.widths = widths.data();
        resource.glyphs = Total;
        resource.mainMap = main.data();
        resource.mainCount = main.size() / 2;
        resource.numericMap = numeric.data();
        resource.numericCount = numeric.size() / 2;
        resource.sheetWidth = resource.sheetHeight = 256;
        resource.cellWidth = 7;
        resource.cellHeight = 8;
        resource.columns = 32;
        resource.solidGlyph = Solid;
    }
};
} // namespace fakegame
