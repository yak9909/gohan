#include "game/title_background.hpp"
#include "ui_values.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gohan::game::titlebg {
namespace {
namespace v = core::design::ca2;
static_assert(v::Titlebg::Stripes::Count == 4 && v::Titlebg::Selection::Boards == 6);
unsigned Morton(unsigned x, unsigned y, unsigned width) {
    unsigned n = (y / 8 * (width / 8) + x / 8) * 64;
    for (unsigned b = 0; b < 3; ++b)
        n += ((x >> b & 1) << (b * 2)) + ((y >> b & 1) << (b * 2 + 1));
    return n;
}
double Mix(double a, double b, double t) { return a + (b - a) * t; }
Rgb Color(std::uint32_t c) { return {(c >> 16 & 255) / 255., (c >> 8 & 255) / 255., (c & 255) / 255.}; }
Rgb Mix(Rgb a, Rgb b, double t) { return {Mix(a.r, b.r, t), Mix(a.g, b.g, t), Mix(a.b, b.b, t)}; }
double Texel(const std::array<std::uint8_t, 256> &tex, double u, double v) {
    const double x = (u - std::floor(u)) * 16 - .5, y = (v - std::floor(v)) * 16 - .5;
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const auto at = [&](int a, int b) { return tex[((b + 16) % 16) * 16 + (a + 16) % 16] / 255.; };
    return Mix(Mix(at(ix, iy), at(ix + 1, iy), x - ix),
               Mix(at(ix, iy + 1), at(ix + 1, iy + 1), x - ix), y - iy);
}
} // namespace

Assembly Assemble(std::uint64_t frame) {
    const double phase = (frame % v::Titlebg::Loop) / double(v::Titlebg::Loop);
    const double tx = Mix(v::Titlebg::Root::TranslateX[0], v::Titlebg::Root::TranslateX[1], phase);
    const double radians = v::Titlebg::Root::Rotation * std::acos(-1.) / 180.;
    const double c = std::cos(radians) * v::Titlebg::Root::Scale;
    const double s = std::sin(radians) * v::Titlebg::Root::Scale;
    Assembly result{};
    for (unsigned i = 0; i < result.size(); ++i) {
        auto &b = result[i];
        const bool base = i == 0, filter = i == 5;
        const auto &size = base ? v::Titlebg::Base::Size : filter ? v::Titlebg::Filter::Size : v::Titlebg::Stripes::Size;
        const auto &uv = base ? v::Titlebg::Base::UvScale : filter ? v::Titlebg::Filter::UvScale : v::Titlebg::Stripes::UvScale;
        const double w = size[0] * (base || filter ? 1 : v::Titlebg::Stripes::SignX[i - 1]);
        const double h = size[1] * (base || filter ? 1 : v::Titlebg::Stripes::SignY[i - 1]);
        const double shift = base || filter ? 0 : Mix(v::Titlebg::Stripes::UvTranslate[0], v::Titlebg::Stripes::UvTranslate[1], phase);
        for (unsigned j = 0; j < 4; ++j) {
            const double u = j == 1 || j == 2 ? 1 : 0, t = j >= 2 ? 1 : 0;
            const double x = (u - .5) * w + tx + (filter ? v::Titlebg::Filter::X : 0);
            const double y = (.5 - t) * h;
            b.positions[j] = {c * x - s * y, -(s * x + c * y)};
            b.uv[j] = {u * uv[0] + shift, t * uv[1] + shift};
        }
        b.texture = base ? 1 : 0;
        b.black = base ? v::Titlebg::Base::Colors[0] : filter ? v::Titlebg::Filter::Color : v::Titlebg::Stripes::Color;
        b.white = base ? v::Titlebg::Base::Colors[1] : b.black;
        b.alpha = base ? 255 : filter ? v::Titlebg::Filter::Alpha : v::Titlebg::Stripes::Alpha;
    }
    return result;
}

Rgb Sample(const Sources &sources, const Assembly &boards, double x, double y) {
    Rgb rgb{};
    for (const auto &b : boards) {
        const auto p = b.positions[0];
        const double ax = b.positions[1].x - p.x, ay = b.positions[1].y - p.y;
        const double bx = b.positions[3].x - p.x, by = b.positions[3].y - p.y;
        const double det = ax * by - ay * bx;
        const double u = ((x - p.x) * by - (y - p.y) * bx) / det;
        const double t = (ax * (y - p.y) - ay * (x - p.x)) / det;
        const double su = Mix(b.uv[0].x, b.uv[1].x, u), sv = Mix(b.uv[0].y, b.uv[3].y, t);
        // 繰り返し画像を作るため板の範囲外も周期を延長して評価する。
        if (b.texture == 1)
            rgb = Mix(Color(b.black), Color(b.white), Texel(sources.luminance, su, sv));
        else
            rgb = Mix(rgb, Color(b.white), Texel(sources.alpha, su, sv) * b.alpha / 255.);
    }
    return rgb;
}

namespace {
double Tone(Rgb rgb, Palette palette) {
    const auto &range = palette == Palette::Cream ? v::Window::Overlay::Luminance : v::Selection::Luminance;
    const double lum = .299 * rgb.r + .587 * rgb.g + .114 * rgb.b;
    return std::clamp((lum - range[0]) / (range[1] - range[0]), 0., 1.);
}
}
Rgb Recolor(Rgb rgb, Palette palette) {
    const auto &colors = palette == Palette::Cream ? v::Window::Overlay::Color : v::Selection::Color;
    return Mix(Color(colors[0]), Color(colors[1]), Tone(rgb, palette));
}

void Bake(const Sources *sources, Palette palette, unsigned width, unsigned height,
          std::uint8_t *out, std::uint64_t frame) {
    const auto boards = Assemble(frame);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const Rgb rgb = sources ? Recolor(Sample(*sources, boards, x + .5 - v::Screen[0] / 2.,
                                                   y + .5 - v::Screen[1] / 2.), palette) : Color(v::Titlebg::Fallback::Selection);
            const unsigned pos = Morton(x, y, width) * 4;
            out[pos] = palette == Palette::Cream ? (sources ? unsigned(v::Window::Overlay::Alpha * 255 + .5) : 0) : 255;
            out[pos + 1] = unsigned(rgb.b * 255 + .5);
            out[pos + 2] = unsigned(rgb.g * 255 + .5);
            out[pos + 3] = unsigned(rgb.r * 255 + .5);
        }
    }
}
void BakeWindowL4(const Sources *sources, unsigned width, unsigned height, std::uint8_t *out) {
    const auto boards = Assemble(v::Titlebg::Window::Frame);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto n = Morton(x, y, width);
            const auto value = sources ? static_cast<unsigned>(Tone(Sample(*sources, boards,
                x + .5 - v::Screen[0] / 2., y + .5 - v::Screen[1] / 2.), Palette::Cream) * 15 + .5) : 0;
            if ((n & 1) == 0) out[n / 2] = value;
            else out[n / 2] |= value << 4;
        }
    }
}
bool BakeSelectionL4(const Sources *sources, unsigned width, unsigned height, unsigned usedWidth,
                     unsigned usedHeight, std::uint8_t *out, std::uint64_t frame) {
    if (!out || !width || !height || width % 8 || height % 8 || usedWidth > width || usedHeight > height) return false;
    std::memset(out, 0, width * height / 2);
    const auto boards = Assemble(frame);
    for (unsigned y = 0; y < usedHeight; ++y) for (unsigned x = 0; x < usedWidth; ++x) {
        const auto n = Morton(x, y, width);
        const unsigned value = sources ? unsigned(Tone(Sample(*sources, boards,
            x + .5 - v::Screen[0] / 2., y + .5 - v::Screen[1] / 2.), Palette::Green) * 15 + .5) : 0;
        out[n / 2] |= value << ((n & 1) * 4);
    }
    return true;
}
const char *FailureText(Failure failure) {
    switch (failure) {
    case Failure::None: return "成功";
    case Failure::Open: return "開く";
    case Failure::Read: return "読み出し";
    case Failure::Romfs: return "RomFS の索引";
    case Failure::Missing: return "対象なし";
    case Failure::Darc: return "アーカイブ";
    case Failure::Clim: return "画像の形式";
    }
    return "不明";
}
} // namespace gohan::game::titlebg
