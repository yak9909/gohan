#pragma once

#include <3ds.h>       // ★u8 / u16 / u32 の定義。GlyphInfo で使う
#include <string>

namespace CTRPluginFramework
{
    class Color;
    class Screen;

    namespace MisakiGothic2nd8
    {
        // ★GPU 経路（GuiRender）からグリフのビット列を直接読むための公開窓（TODO-134 段 D）。
        //   OSD 用の Draw() は 1 ドットずつ塗るのでここでは使わない。
        //   rows は LSB が左端。height は最大 7、cell は 8x8 に収まる。
        struct GlyphInfo
        {
            u8          width;
            u8          height;
            s8          offsetX;
            s8          offsetY;
            u8          advance;
            const u16  *rows;
        };

        // 見つからなければ false。その場合は幅 4 の空白として扱うのが既存の挙動。
        bool GetGlyph(u32 codepoint, GlyphInfo &out);

        // UTF-8 を 1 文字読み進める。index は次の位置へ進む。
        u32  NextCodepoint(const std::string &text, u32 &index);

        int LineHeight(void);
        int Measure(const std::string &text);
        void Draw(const Screen &screen, const std::string &text, int x, int y, const Color &color);
    }
}
