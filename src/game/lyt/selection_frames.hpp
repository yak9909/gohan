#pragma once
#include "core/limits.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace gohan::game::lyt::detail {
// 182×11の実画素だけを、GPUのMorton順の連続範囲として保存する。
// 幅が偶数なので、端のニブルを実行時に混ぜ直す必要はない。
struct SelectionSpan { std::uint16_t offset, bytes; };
inline constexpr auto SelectionUsedBytes = [] {
    std::array<bool, core::limits::RendererSelectionSurfaceBytes> used{};
    for (unsigned y = 0; y < 11; ++y) for (unsigned x = 0; x < 182; ++x) {
        unsigned n = (y / 8 * 32 + x / 8) * 64;
        for (unsigned bit = 0; bit < 3; ++bit)
            n += ((x >> bit & 1) << (bit * 2)) + ((y >> bit & 1) << (bit * 2 + 1));
        used[n / 2] = true;
    }
    return used;
}();
inline constexpr unsigned SelectionSpanCount = [] {
    unsigned count = 0;
    for (unsigned i = 0; i < SelectionUsedBytes.size(); ++i)
        if (SelectionUsedBytes[i] && (!i || !SelectionUsedBytes[i - 1])) ++count;
    return count;
}();
inline constexpr auto SelectionSpans = [] {
    std::array<SelectionSpan, SelectionSpanCount> spans{};
    unsigned count = 0;
    for (unsigned i = 0; i < SelectionUsedBytes.size(); ++i) {
        if (!SelectionUsedBytes[i]) continue;
        if (!i || !SelectionUsedBytes[i - 1]) spans[count++] = {std::uint16_t(i), 0};
        ++spans[count - 1].bytes;
    }
    return spans;
}();
static_assert(SelectionSpanCount == 96);
static_assert([] {
    unsigned bytes = 0;
    for (const auto span : SelectionSpans) bytes += span.bytes;
    return bytes == core::limits::RendererSelectionFrameBytes;
}());

// 導入時にだけ使う。フレーム時には色や座標の計算をしない。
inline void PackSelectionFrame(std::uint8_t *packed, const std::uint8_t *surface) {
    for (const auto span : SelectionSpans) {
        std::memcpy(packed, surface + span.offset, span.bytes);
        packed += span.bytes;
    }
}
// 未使用画素を0で初期化した面へ、合計1,001 Bだけをコピーする。
// 未使用画素の位置は全位相で同じ。96本の範囲はコンパイル時に確定済み。
inline void CopySelectionFrame(std::uint8_t *surface, const std::uint8_t *packed) {
    for (const auto span : SelectionSpans) {
        std::memcpy(surface + span.offset, packed, span.bytes);
        packed += span.bytes;
    }
}
} // namespace gohan::game::lyt::detail
