// ROM を含まない合成データで、索引・不正入力・画像の向き・合成と周期を確かめる。
#include "game/title_background.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#ifdef __3DS__
#include <3ds.h>
#endif

namespace {
using namespace gohan::game::titlebg;
using Bytes = std::vector<std::uint8_t>;
void Put(Bytes &b, unsigned at, std::uint32_t value, unsigned size = 4) {
    for (unsigned i = 0; i < size; ++i) b.at(at + i) = value >> (8 * i);
}
void Text(Bytes &b, unsigned at, const char *text) {
    for (unsigned i = 0; text[i]; ++i) Put(b, at + i * 2, text[i], 2);
}
Bytes Clim(unsigned format) {
    Bytes b(168, 0);
    // 非対称な左上の2画素と、右下タイルの末尾。
    b[0] = 0xF1;
    b[127] = 0x80;
    std::memcpy(b.data() + 128, "CLIM", 4);
    Put(b, 132, 0xFEFF, 2); Put(b, 134, 20, 2); Put(b, 140, 168); Put(b, 144, 1, 2);
    std::memcpy(b.data() + 148, "imag", 4);
    Put(b, 152, 16); Put(b, 156, 16, 2); Put(b, 158, 16, 2); Put(b, 160, format); Put(b, 164, 128);
    return b;
}
Bytes Archive() {
    Bytes b(1024, 0);
    std::memcpy(b.data(), "darc", 4);
    Put(b, 4, 0xFEFF, 2); Put(b, 6, 28, 2); Put(b, 8, 0x01000000);
    Put(b, 12, b.size()); Put(b, 16, 28); Put(b, 20, 300); Put(b, 24, 384);
    Put(b, 28, 0x01000000); Put(b, 36, 5);
    Put(b, 40, 0x01000002); Put(b, 48, 5); // '.'
    Put(b, 52, 0x01000006); Put(b, 56, 1); Put(b, 60, 5); // timg
    Put(b, 64, 16); Put(b, 68, 512); Put(b, 72, 168);
    Put(b, 76, 54); Put(b, 80, 768); Put(b, 84, 168);
    Text(b, 90, "."); Text(b, 94, "timg");
    Text(b, 104, "my_opn_bg_00.bclim"); Text(b, 142, "my_opn_bg_01.bclim");
    const auto a = Clim(13), l = Clim(12);
    std::memcpy(b.data() + 512, a.data(), a.size()); std::memcpy(b.data() + 768, l.data(), l.size());
    return b;
}
struct Input { Bytes data; unsigned calls = 0, maxRead = 0; bool fail = false; };
bool Read(void *c, std::uint64_t at, void *out, std::size_t bytes) {
    auto &in = *static_cast<Input *>(c);
    ++in.calls;
    if (bytes > in.maxRead) in.maxRead = bytes;
    if (in.fail) return false;
    assert(at + bytes <= in.data.size());
    std::memcpy(out, in.data.data() + at, bytes);
    return true;
}
Reader View(Input &in) { return {&in, in.data.size(), Read}; }
bool Near(double a, double b) { return std::abs(a - b) < 1e-8; }

void Images() {
    std::array<std::uint8_t, 256> decoded;
    auto b = Clim(13);
    assert(DecodeClim(b.data(), b.size(), 13, decoded));
    assert(decoded[0] == 17 && decoded[1] == 255 && decoded[255] == 136 && decoded[240] == 0);
    assert(!DecodeClim(b.data(), b.size() - 1, 13, decoded));
    assert(!DecodeClim(b.data(), b.size(), 12, decoded));
    b[156] = 32;
    assert(!DecodeClim(b.data(), b.size(), 13, decoded));
    Input in{Archive()};
    Sources sources;
    assert(DecodeArchive(View(in), sources) == Failure::None);
    assert(sources.alpha[0] == 17 && sources.luminance[1] == 255);
    assert(in.maxRead <= 192);
    const auto unchanged = sources;
    Put(in.data, 80, 1000); // 範囲を越すデータ
    assert(DecodeArchive(View(in), sources) == Failure::Darc);
    assert(sources.alpha == unchanged.alpha && sources.luminance == unchanged.luminance);
    in.data = Archive(); Put(in.data, 36, 0xFFFFFFFFu);
    assert(DecodeArchive(View(in), sources) == Failure::Darc);
    in.data = Archive(); Put(in.data, 160 + 768, 13);
    assert(DecodeArchive(View(in), sources) == Failure::Clim);
    in.fail = true;
    assert(DecodeArchive(View(in), sources) == Failure::Read);
    const auto calls = in.calls;
    assert(!View(in).Read(UINT64_MAX, &sources, 4));
    assert(in.calls == calls);
}

Bytes Romfs() {
    Bytes b(512, 0);
    Put(b, 0, 40); Put(b, 12, 40); Put(b, 16, 128); Put(b, 28, 168); Put(b, 32, 128); Put(b, 36, 384);
    Put(b, 44, 0xFFFFFFFFu); Put(b, 48, 24); Put(b, 52, 0xFFFFFFFFu);
    Put(b, 68, 0xFFFFFFFFu); Put(b, 72, 64); Put(b, 76, 0xFFFFFFFFu);
    Put(b, 84, 12); Text(b, 88, "Layout");
    Put(b, 104, 24); Put(b, 108, 0xFFFFFFFFu); Put(b, 112, 0xFFFFFFFFu); Put(b, 116, 0);
    Put(b, 124, 10); Text(b, 128, "Title");
    Put(b, 168, 64); Put(b, 172, 0xFFFFFFFFu); Put(b, 176, 8); Put(b, 184, 64);
    Put(b, 196, 26); Text(b, 200, "opng_titl.arc");
    // opng_titl.arc は13文字（26バイト）。
    return b;
}
void Index() {
    Input in{Romfs()};
    Region region;
    assert(FindArchive(View(in), region) == Failure::None);
    assert(region.offset == 392 && region.size == 64 && in.maxRead <= 192);
    auto raw = in.data;
    in.data = Bytes(4096 + raw.size());
    std::memcpy(in.data.data(), "IVFC", 4); Put(in.data, 0x4C, 12);
    std::memcpy(in.data.data() + 4096, raw.data(), raw.size());
    assert(FindArchive(View(in), region) == Failure::None && region.offset == 4488);
    in.data = raw; Put(in.data, 84, 10); Put(in.data, 68, 24); // 名前不一致・兄弟が循環
    assert(FindArchive(View(in), region) == Failure::Romfs);
    in.data = raw; Put(in.data, 176, 0xFFFFFFFFu); Put(in.data, 180, 0xFFFFFFFFu);
    assert(FindArchive(View(in), region) == Failure::Romfs);
    in.data = raw; Put(in.data, 52, 0xFFFFFFFFu); Put(in.data, 48, 0xFFFFFFFFu);
    assert(FindArchive(View(in), region) == Failure::Missing);
    in.fail = true;
    assert(FindArchive(View(in), region) == Failure::Read);
}

void WindowL4() {
    Sources sources;
    for (unsigned i = 0; i < 256; ++i) {
        sources.alpha[i] = (i * 71) & 255;
        sources.luminance[i] = (i * 53) & 255;
    }
    std::array<std::uint8_t, 256 * 256 / 2 + 2> packed{};
    packed.front() = 0xAB; packed.back() = 0xCD;
    BakeWindowL4(&sources, 256, 256, packed.data() + 1);
    assert(packed.front() == 0xAB && packed.back() == 0xCD);
    const auto boards = Assemble(0);
    unsigned differences = 0;
    for (unsigned y = 0; y < 256; ++y) {
        for (unsigned x = 0; x < 256; ++x) {
            unsigned n = (y / 8 * 32 + x / 8) * 64;
            for (unsigned bit = 0; bit < 3; ++bit)
                n += ((x >> bit & 1) << (2 * bit)) + ((y >> bit & 1) << (2 * bit + 1));
            const unsigned level = (packed[1 + n / 2] >> (4 * (n & 1))) & 15;
            const auto target = Recolor(Sample(sources, boards, x + .5 - 200, y + .5 - 120), Palette::Cream);
            const auto black = Recolor({0, 0, 0}, Palette::Cream), white = Recolor({1, 1, 1}, Palette::Cream);
            const auto close = [level](double a, double b, double actual) {
                assert(std::abs(a + (b - a) * level / 15. - actual) <= std::abs(b - a) / 30. + 1e-9);
            };
            close(black.r, white.r, target.r); close(black.g, white.g, target.g); close(black.b, white.b, target.b);
            if (n & 1) differences += (packed[1 + n / 2] & 15) != level;
        }
    }
    assert(differences > 0); // ニブルの順序を取り違えても一様な画像では通ってしまう。
    BakeWindowL4(nullptr, 256, 256, packed.data() + 1);
    for (std::size_t i = 1; i + 1 < packed.size(); ++i) assert(packed[i] == 0);
    assert(packed.front() == 0xAB && packed.back() == 0xCD);
}
void Composition() {
    const auto a = Assemble(0), half = Assemble(75), end = Assemble(149), loop = Assemble(150);
    const auto &p = a[0].positions;
    assert(Near(std::hypot(p[1].x - p[0].x, p[1].y - p[0].y), 528));
    assert(Near(std::hypot(p[3].x - p[0].x, p[3].y - p[0].y), 408));
    const double root = std::sqrt(.5) * 1.5;
    assert(Near(half[0].positions[0].x - p[0].x, -16 * root));
    for (unsigned i = 0; i < 6; ++i) {
        for (unsigned j = 0; j < 4; ++j) {
            assert(Near(a[i].positions[j].x, loop[i].positions[j].x));
            assert(Near(a[i].uv[j].y, loop[i].uv[j].y));
        }
    }
    for (unsigned i = 1; i < 5; ++i) {
        assert(Near(half[i].uv[0].x, .5) && Near(half[i].uv[0].y, .5));
        assert(Near(end[i].uv[0].x, 149. / 150));
        const auto &q = a[i].positions;
        assert((q[1].x > q[0].x) == (i == 1 || i == 3));
        assert((q[3].x > q[0].x) == (i == 3 || i == 4));
    }
    assert(a[0].texture == 1 && a[0].alpha == 255 && a[1].alpha == 70 && a[5].alpha == 55);
    assert(Near(a[5].positions[0].x - a[0].positions[0].x, -34.56 * root));
    assert(Near(a[5].uv[2].x, 160) && Near(a[5].uv[2].y, 120));
    Sources s;
    auto rgb = Sample(s, a, 0, 0);
    assert(Near(rgb.r, 253. / 255) && Near(rgb.g, 172. / 255));
    s.luminance.fill(255);
    rgb = Sample(s, a, 0, 0);
    assert(Near(rgb.r, 231. / 255) && Near(rgb.g, 238. / 255));
    s.alpha.fill(255);
    rgb = Sample(s, half, 3, -7);
    double red = 231. / 255;
    for (unsigned i = 0; i < 4; ++i) red *= 1 - 70. / 255;
    red = red * (1 - 55. / 255) + 55. / 255;
    assert(Near(rgb.r, red));
    const auto dark = Recolor({0, 0, 0}, Palette::Green), light = Recolor({1, 1, 1}, Palette::Cream);
    assert(Near(dark.r, 70. / 255) && Near(dark.g, 140. / 255));
    assert(Near(light.r, 1) && Near(light.g, 251. / 255));
    std::uint8_t image[256];
    Bake(nullptr, Palette::Cream, 8, 8, image);
    assert(image[0] == 0 && image[252] == 0);
    Bake(nullptr, Palette::Green, 8, 8, image);
    assert(image[0] == 255 && image[1] == 75 && image[2] == 187 && image[3] == 109);
    Bake(&s, Palette::Cream, 8, 8, image);
    assert(image[0] == 36);
}
} // namespace
#ifdef __3DS__
namespace {
Input update, base;
unsigned opens = 0, closes = 0;
bool failUpdateOpen = false, failSize = false, shortRead = false;
constexpr Result FsError = -123456;
void PrepareSelf() {
    auto bytes = Romfs();
    const auto archive = Archive();
    bytes.resize(392 + archive.size());
    Put(bytes, 184, archive.size());
    std::memcpy(bytes.data() + 392, archive.data(), archive.size());
    update = {bytes}; base = {bytes}; opens = closes = 0;
    failUpdateOpen = failSize = shortRead = false;
}
void Self() {
    Sources sources;
    PrepareSelf();
    auto status = LoadSelf(sources);
    assert(status.ready && status.fromUpdate && opens == 1 && closes == 1 && !status.base.attempted);
    PrepareSelf(); failUpdateOpen = true;
    status = LoadSelf(sources);
    assert(status.ready && !status.fromUpdate && opens == 2 && closes == 1);
    assert(status.update.result == static_cast<std::uint32_t>(FsError) && status.update.failure == Failure::Open);
    PrepareSelf(); update.data[392] = 0;
    status = LoadSelf(sources);
    assert(status.ready && !status.fromUpdate && status.update.failure == Failure::Darc && closes == 2);
    PrepareSelf(); update.fail = true; base.fail = true;
    status = LoadSelf(sources);
    assert(!status.ready && status.update.failure == Failure::Read && closes == 2);
    assert(status.base.result == static_cast<std::uint32_t>(FsError));
    PrepareSelf(); failSize = true;
    status = LoadSelf(sources);
    assert(!status.ready && status.base.failure == Failure::Read && closes == 2);
    PrepareSelf(); shortRead = true;
    status = LoadSelf(sources);
    assert(!status.ready && status.update.failure == Failure::Read && closes == 2);
}
}
Result FSUSER_OpenFileDirectly(Handle *handle, FS_ArchiveID archive, FS_Path path, FS_Path file, u32 flags, u32 attributes) {
    assert(archive == ARCHIVE_ROMFS && path.type == PATH_EMPTY && path.size == 1);
    assert(path.data && *static_cast<const char *>(path.data) == 0);
    assert(file.type == PATH_BINARY && file.size == 12 && flags == FS_OPEN_READ && !attributes);
    const auto words = static_cast<const u32 *>(file.data);
    assert((words[0] == 5 || words[0] == 0) && !words[1] && !words[2]);
    ++opens;
    if (words[0] == 5 && failUpdateOpen) return FsError;
    *handle = words[0] == 5 ? 5 : 1;
    return 0;
}
Result FSFILE_GetSize(Handle handle, u64 *size) {
    if (failSize) return FsError;
    *size = (handle == 5 ? update : base).data.size();
    return 0;
}
Result FSFILE_Read(Handle handle, u32 *got, u64 at, void *out, u32 bytes) {
    auto &file = handle == 5 ? update : base;
    if (!Read(&file, at, out, bytes)) return FsError;
    *got = shortRead ? bytes - 1 : bytes;
    return 0;
}
Result FSFILE_Close(Handle handle) { assert(handle == 5 || handle == 1); ++closes; return 0; }
#endif
int main() {
    {
        Sources source;
        for (unsigned i = 0; i < 256; ++i) { source.alpha[i] = i; source.luminance[i] = 255 - i; }
        std::array<std::uint8_t, 2050> a{}, b{}, loop{};
        a.front() = a.back() = 0xAD;
        assert(BakeSelectionL4(&source, 256, 16, 182, 11, a.data() + 1, 0));
        assert(BakeSelectionL4(&source, 256, 16, 182, 11, b.data() + 1, 74));
        assert(BakeSelectionL4(&source, 256, 16, 182, 11, loop.data() + 1, 150));
        assert(a.front() == 0xAD && a.back() == 0xAD);
        assert(std::memcmp(a.data() + 1, loop.data() + 1, 2048) == 0);
        assert(std::memcmp(a.data() + 1, b.data() + 1, 2048) != 0);
        assert(!BakeSelectionL4(&source, 256, 16, 257, 11, a.data() + 1, 0));
        assert(!BakeSelectionL4(&source, 256, 16, 182, 17, a.data() + 1, 0));
    }
    Images(); Index(); Composition(); WindowL4();
#ifdef __3DS__
    Self();
#endif
}
