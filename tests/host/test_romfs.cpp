#include "game/romfs.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdlib>

namespace {
using namespace gohan::game::romfs;
using Bytes = std::vector<std::uint8_t>;
void Put(Bytes &b, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) b.at(at + i) = value >> (i * 8);
}
bool Read(void *p, std::uint64_t at, void *out, std::size_t size) {
    const auto &b = *static_cast<Bytes *>(p);
    assert(Fits(at, size, b.size()));
    std::memcpy(out, b.data() + at, size); return true;
}
Bytes Image() {
    Bytes b(248);
    Put(b, 0, 40); Put(b, 4, 40); Put(b, 8, 4); Put(b, 12, 44); Put(b, 16, 96);
    Put(b, 20, 140); Put(b, 24, 4); Put(b, 28, 144); Put(b, 32, 96); Put(b, 36, 240);
    Put(b, 40, 24); Put(b, 140, 0); // 衝突する単一バケット。
    for (auto off : {44u, 68u, 100u}) {
        Put(b, off + 4, 0xFFFFFFFF); Put(b, off + 8, 0xFFFFFFFF); Put(b, off + 12, 0xFFFFFFFF); Put(b, off + 16, 0xFFFFFFFF);
    }
    Put(b, 68 + 16, 56); Put(b, 68 + 20, 2); b[68 + 24] = 'A';
    Put(b, 100 + 20, 2); b[100 + 24] = 'B';
    Put(b, 144, 56); Put(b, 148, 0xFFFFFFFF); Put(b, 160, 4); Put(b, 168, 40); Put(b, 172, 2); b[176] = 'X';
    Put(b, 184, 24); Put(b, 188, 0xFFFFFFFF); Put(b, 192, 4); Put(b, 200, 4); Put(b, 208, 0xFFFFFFFF); Put(b, 212, 2); b[216] = 'X';
    std::memcpy(b.data() + 240, "baseNEXT", 8);
    return b;
}
void HashTest() {
    auto bytes = Image();
    Index index;
    assert(index.Open({&bytes, bytes.size(), Read}) == Failure::None);
    Region region;
    assert(index.Find("B/X", region) == Failure::None && region.offset == 240 && region.size == 4);
    assert(index.Find("A/X", region) == Failure::None && region.offset == 244 && region.size == 4);
    assert(index.Find("C/X", region) == Failure::Missing);
    assert(index.Find("B/Y", region) == Failure::Missing);
    assert(index.Find("B//X", region) == Failure::Romfs);
    Put(bytes, 168, 0); // 名前不一致のハッシュ鎖が循環。
    assert(index.Find("B/Y", region) == Failure::Romfs);
    bytes = Image(); Put(bytes, 152, 0xFFFFFFFF); Put(bytes, 156, 0xFFFFFFFF);
    assert(index.Find("B/X", region) == Failure::Romfs);
    bytes = Image(); Put(bytes, 172, 0xFFFFFFFF);
    assert(index.Find("B/X", region) == Failure::Romfs);
    bytes = Image(); Put(bytes, 40, 96);
    assert(index.Find("A/X", region) == Failure::Romfs);
    bytes = Image(); Put(bytes, 8, 3);
    assert(index.Open({&bytes, bytes.size(), Read}) == Failure::Romfs);
    bytes = Image(); Put(bytes, 12, 0xFFFFFFFF);
    assert(index.Open({&bytes, bytes.size(), Read}) == Failure::Romfs);
}
void Env(const char *key, const char *value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}
void FallbackTest() {
    namespace fs = std::filesystem;
    const auto root = fs::absolute("build/host/p3-romfs");
    fs::create_directories(root / "base/A"); fs::create_directories(root / "update/A");
    fs::remove(root / "update/A/X");
    { std::ofstream out(root / "base/A/X", std::ios::binary); out << "base"; }
    Env("GOHAN_PREVIEW_ROMFS_BASE", (root / "base").string().c_str());
    Env("GOHAN_PREVIEW_ROMFS_UPDATE", (root / "update").string().c_str());
    Session session;
    std::string text;
    auto consume = [](const Reader &r, void *p) {
        auto &out = *static_cast<std::string *>(p);
        assert(r.size == 4);
        char bytes[4];
        if (!r.Read(0, bytes, sizeof(bytes))) return Failure::Read;
        assert(!r.Read(1, bytes, sizeof(bytes)));
        out.assign(bytes, sizeof(bytes)); return Failure::None;
    };
    auto status = session.Visit("A/X", consume, &text);
    assert(status.ready && !status.fromUpdate && text == "base");
    { std::ofstream out(root / "update/A/X", std::ios::binary); out << "new!"; }
    status = session.Visit("A/X", consume, &text);
    assert(status.ready && status.fromUpdate && text == "new!" && !status.base.attempted);
    session.Close();
    auto loaded = ReadAll("A/X", 4);
    assert(loaded && loaded.size == 4 && !std::memcmp(loaded.data.get(), "new!", 4) && loaded.data[4] == 0);
    assert(!ReadAll("A/X", 3));
    assert(!session.Visit("../X", consume, &text).ready);
    session.Close();
    // テストが作った出力先だけを片付ける。
    fs::remove(root / "base/A/X"); fs::remove(root / "update/A/X");
    Env("GOHAN_PREVIEW_ROMFS_BASE", ""); Env("GOHAN_PREVIEW_ROMFS_UPDATE", "");
}
}
int main() { HashTest(); FallbackTest(); }
