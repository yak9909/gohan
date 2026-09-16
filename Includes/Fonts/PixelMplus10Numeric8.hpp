#pragma once

#include <string>

namespace CTRPluginFramework
{
    class Color;
    class Screen;

    namespace PixelMplus10Numeric8
    {
        int LineHeight(void);
        int Measure(const std::string &text);
        void Draw(const Screen &screen, const std::string &text, int x, int y, const Color &color);
    }
}
