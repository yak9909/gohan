// 元の6層CPU合成と、固定値を外へ出した合成を全150位相で比較する。
#include "game/lyt/selection_compositor.hpp"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>

int main() {
    namespace bg = gohan::game::titlebg;
    using Clock = std::chrono::steady_clock;
    bg::Sources sources;
    const auto status = bg::LoadSelf(sources);
    if (!status.ready) {
        for (unsigned i = 0; i < 256; ++i) {
            sources.alpha[i] = ((i * 13 ^ (i >> 2)) & 15) * 17;
            sources.luminance[i] = ((i * 7 ^ (i >> 3)) & 15) * 17;
        }
    }
    gohan::game::lyt::detail::SelectionCompositor compositor;
    compositor.Prepare(&sources);
    std::array<std::uint8_t, 2048> expected{}, actual{};
    for (unsigned phase = 0; phase < 150; ++phase) {
        assert(bg::BakeSelectionL4(&sources, 256, 16, 182, 11, expected.data(), phase));
        compositor.Bake(phase, actual.data());
        assert(actual == expected);
    }
    // 黒白端点・細かい変化・8bitの中間値でも、近似経路と境界の再計算を照合する。
    for (unsigned pattern = 0; pattern < 6; ++pattern) {
        auto synthetic = sources;
        for (unsigned i = 0; i < 256; ++i) {
            synthetic.alpha[i] = pattern == 0 ? 0 : pattern == 1 ? 255 :
                pattern == 2 ? (i & 1) * 255 : (i * 71 + pattern * 13) & 255;
            synthetic.luminance[i] = pattern == 0 ? 0 : pattern == 1 ? 255 :
                pattern == 2 ? ((i >> 4) & 1) * 255 : (i * 43 + pattern * 29) & 255;
        }
        compositor.Prepare(&synthetic);
        for (unsigned phase = 0; phase < 150; ++phase) {
            assert(bg::BakeSelectionL4(&synthetic, 256, 16, 182, 11, expected.data(), phase));
            compositor.Bake(phase, actual.data());
            assert(actual == expected);
        }
    }
    compositor.Prepare(&sources);
    for (unsigned method = 0; method < 2; ++method) {
        double total = 0, maximum = 0;
        std::array<double, 1500> samples{};
        unsigned checksum = 0;
        for (unsigned sample = 0; sample < 1500; ++sample) {
            const auto begin = Clock::now();
            if (method) compositor.Bake(sample % 150, actual.data());
            else assert(bg::BakeSelectionL4(&sources, 256, 16, 182, 11, actual.data(), sample % 150));
            const double us = std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
            total += us;
            samples[sample] = us;
            maximum = std::max(maximum, us);
            checksum += actual[sample % actual.size()];
        }
        std::sort(samples.begin(), samples.end());
        std::printf("帯合成PC %s: %s / 1500標本 平均 %.3f us 中央 %.3f us p95 %.3f us 最大 %.3f us / 検算 %u\n",
                    status.ready ? "実入力" : "自作入力", method ? "巡回用" : "従来式", total / 1500,
                    samples[750], samples[1425], maximum, checksum);
    }
    compositor.Prepare(nullptr);
    compositor.Bake(0, actual.data());
    assert(actual == (std::array<std::uint8_t, 2048>{}));
}
