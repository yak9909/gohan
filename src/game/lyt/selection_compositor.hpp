#pragma once
#include "game/title_background.hpp"
#include "core/limits.hpp"
#include "ui_values.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gohan::game::lyt::detail {
// 同じ6層source-over→輝度→clamp→L4丸め。輝度の線形性でRGBを1成分へまとめる。
// 高速経路はQ22の標本位置とQ24の輝度。丸めの境界に近い画素だけを元のdoubleの式で再計算する。
// 合成元と作業用の値はプラグイン側で持ち、ゲームのヒープから追加確保しない。
class SelectionCompositor {
  public:
    void Prepare(const titlebg::Sources *sources) {
        ready_ = sources != nullptr;
        if (sources) sources_ = *sources;
    }
    // 毎フレームの小さな合成だけは速度を優先する。fast-mathは使わない。
#if defined(__GNUC__) && !defined(__clang__)
    [[gnu::optimize("O2")]]
#endif
    void Bake(unsigned frame, std::uint8_t *out) const {
        namespace v = core::design::ca2;
        static_assert(v::Titlebg::Selection::CellSize[0] == 182 && v::Titlebg::Selection::CellSize[1] == 11);
        static_assert(v::Titlebg::Selection::ImageSize[0] == 256 && v::Titlebg::Selection::ImageSize[1] == 16);
        std::memset(out, 0, core::limits::RendererSelectionSurfaceBytes);
        preciseSamples_ = 0;
        if (!ready_) return;
        const auto assembly = titlebg::Assemble(frame);
        struct Prepared {
            std::uint32_t su, sv, ux, uy, vx, vy, black, white, opacity;
        };
        std::array<Prepared, 6> prepared{};
        for (unsigned i = 0; i < assembly.size(); ++i) {
            const auto &b = assembly[i]; auto &p = prepared[i];
            const double ax = b.positions[1].x - b.positions[0].x, ay = b.positions[1].y - b.positions[0].y;
            const double bx = b.positions[3].x - b.positions[0].x, by = b.positions[3].y - b.positions[0].y;
            const double det = ax * by - ay * bx;
            const double x = .5 - v::Screen[0] / 2. - b.positions[0].x;
            const double y = .5 - v::Screen[1] / 2. - b.positions[0].y;
            const double u = b.uv[0].x + (b.uv[1].x - b.uv[0].x) * ((x * by - y * bx) / det);
            const double t = b.uv[0].y + (b.uv[3].y - b.uv[0].y) * ((ax * y - ay * x) / det);
            p.su = Coordinate(u * 16 - .5); p.sv = Coordinate(t * 16 - .5);
            p.ux = Coordinate((b.uv[1].x - b.uv[0].x) * by / det * 16);
            p.uy = Coordinate((b.uv[1].x - b.uv[0].x) * -bx / det * 16);
            p.vx = Coordinate((b.uv[3].y - b.uv[0].y) * -ay / det * 16);
            p.vy = Coordinate((b.uv[3].y - b.uv[0].y) * ax / det * 16);
            p.black = Fixed(Luminance(b.black)); p.white = Fixed(Luminance(b.white));
            p.opacity = Fixed(b.alpha / 255.);
        }
        const auto &range = v::Selection::Luminance;
        const double scale = 15. / (range[1] - range[0]);
        for (unsigned y = 0; y < 11; ++y) for (unsigned x = 0; x < 182; ++x) {
            std::uint32_t lum = 0;
            for (unsigned i = 0; i < assembly.size(); ++i) {
                const auto &b = assembly[i]; const auto &p = prepared[i];
                // uint32の巡回は16画素周期と一致する。負のUVも剰余の分岐を要しない。
                const auto su = p.su + x * p.ux + y * p.uy, sv = p.sv + x * p.vx + y * p.vy;
                if (b.texture == 1) lum = Mix(p.black, p.white, Texel(sources_.luminance.data(), su, sv));
                else lum = Mix(lum, p.white, unsigned(std::uint64_t(Texel(sources_.alpha.data(), su, sv)) * p.opacity >> 24));
            }
            double quantized = std::clamp((lum / 16777216. - range[0]) * scale, 0., 15.);
            // UVはQ28。182×11・6層での輝度誤差を192/16777216以下に収める。
            // L4段階への写像でも1/2048未満。境界から離れた画素の丸めは変わらない。
            // 境界近傍では近似を使わず、従来と同じ座標・RGB合成・演算順序に戻す。
            static_assert(192. / 16777216. * 15. / (v::Selection::Luminance[1] - v::Selection::Luminance[0]) < 1. / 2048.);
            if (std::abs(quantized - (int(quantized) + .5)) <= 1. / 2048.) {
                const auto rgb = titlebg::Sample(sources_, assembly, x + .5 - v::Screen[0] / 2., y + .5 - v::Screen[1] / 2.);
                const double exact = .299 * rgb.r + .587 * rgb.g + .114 * rgb.b;
                quantized = std::clamp((exact - range[0]) / (range[1] - range[0]), 0., 1.) * 15;
                ++preciseSamples_;
            }
            const unsigned value = unsigned(quantized + .5);
            unsigned n = (y / 8 * 32 + x / 8) * 64;
            for (unsigned bit = 0; bit < 3; ++bit)
                n += ((x >> bit & 1) << (bit * 2)) + ((y >> bit & 1) << (bit * 2 + 1));
            out[n / 2] |= value << ((n & 1) * 4);
        }
    }
    unsigned PreciseSamples() const { return preciseSamples_; }

  private:
    static std::uint32_t Coordinate(double value) {
        return static_cast<std::uint32_t>(std::llround(value * 268435456.));
    }
    static std::uint32_t Fixed(double value) { return unsigned(value * 16777216. + .5); }
    static double Luminance(std::uint32_t c) {
        return .299 * ((c >> 16 & 255) / 255.) + .587 * ((c >> 8 & 255) / 255.) + .114 * ((c & 255) / 255.);
    }
    [[gnu::always_inline]] static inline unsigned Mix(unsigned a, unsigned b, unsigned t) {
        return unsigned(a + ((std::int64_t(int(b) - int(a)) * t) >> 24));
    }
    [[gnu::always_inline]] static inline unsigned Texel(const std::uint8_t *tex, std::uint32_t u, std::uint32_t v) {
        const unsigned x = u >> 28, y = v >> 28, fx = (u >> 6) & 4194303, fy = (v >> 6) & 4194303;
        const auto at = [&](unsigned a, unsigned b) { return tex[(b & 15) * 16 + (a & 15)]; };
        const int top = at(x, y), bottom = at(x, y + 1);
        const int a = top * 4194304 + (int(at(x + 1, y)) - top) * int(fx);
        const int b = bottom * 4194304 + (int(at(x + 1, y + 1)) - bottom) * int(fx);
        return unsigned(((std::int64_t(a) << 22) + std::int64_t(b - a) * fy) >> 20) / 255;
    }
    titlebg::Sources sources_{};
    bool ready_ = false;
    mutable unsigned preciseSamples_ = 0;
};
} // namespace gohan::game::lyt::detail
