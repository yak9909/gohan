#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace gohan::game::titlebg {
// すべてプラグイン所有。読み込み・作成は描画フックの登録前に一度だけ行う。
struct Reader {
    void *context;
    std::uint64_t size;
    bool (*read)(void *, std::uint64_t, void *, std::size_t);
    bool Read(std::uint64_t offset, void *out, std::size_t bytes) const;
};
struct Region { std::uint64_t offset = 0, size = 0; };
enum class Failure { None, Open, Read, Romfs, Missing, Darc, Clim };
struct Attempt {
    Failure failure = Failure::Open;
    std::uint32_t result = 0;
    bool attempted = false;
};
struct Status { Attempt update{}, base{}; bool ready = false, fromUpdate = false; };
struct Sources {
    // 復号した上から順の A4 と L4（0〜255）。
    std::array<std::uint8_t, 256> alpha{}, luminance{};
};
struct Point { double x, y; };
struct Board {
    unsigned texture; // 0 = A4、1 = L4
    std::array<Point, 4> positions; // 左上、右上、右下、左下。画面中央からの差。
    std::array<Point, 4> uv;
    std::uint32_t black, white;
    unsigned alpha;
};
using Assembly = std::array<Board, 6>;
struct Rgb { double r, g, b; }; // 0〜1
enum class Palette { Cream, Green };

// 入力は無圧縮 RomFS の先頭（IVFC 付き・第3層からの両方に対応）。
Failure FindArchive(const Reader &romfs, Region &out);
Failure DecodeArchive(const Reader &archive, Sources &out);
bool DecodeClim(const std::uint8_t *data, std::size_t size, unsigned format,
                std::array<std::uint8_t, 256> &out);
Assembly Assemble(std::uint64_t frame);
Rgb Sample(const Sources &sources, const Assembly &boards, double x, double y);
Rgb Recolor(Rgb rgb, Palette palette);
// GPU の Morton 順・ABGR バイト順。寸法は 8 の倍数。
void Bake(const Sources *sources, Palette palette, unsigned width, unsigned height,
          std::uint8_t *out, std::uint64_t frame = 0);
// クリーム色の黒→白へ写す係数だけを L4 の Morton 順（下位ニブルが先）で返す。
void BakeWindowL4(const Sources *sources, unsigned width, unsigned height, std::uint8_t *out);
// 帯の実寸だけ合成し、残りを 0 で初期化する。範囲外は拒否する。
bool BakeSelectionL4(const Sources *sources, unsigned width, unsigned height, unsigned usedWidth,
                     unsigned usedHeight, std::uint8_t *out, std::uint64_t frame);
Status LoadSelf(Sources &out);
const char *FailureText(Failure failure);
} // namespace gohan::game::titlebg
