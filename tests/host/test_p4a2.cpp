#include "world/scene_resources.hpp"
#include "world/instanced_draw.hpp"
#include "world/fgobj_draw_hook.hpp"
#include "world/unit_cursor.hpp"
#include "world/material_tint.hpp"
#include "game/draw_slots.hpp"
#include "game/memory.hpp"
#include "core/frame.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace gohan;
namespace {
struct Resource : game::scene::Backend {
    game::scene::Identity identity{this, this, 0};
    game::heap::Room room{1000000, 1000000, true};
    int result = 0;
    unsigned loads = 0, creates = 0, destroys = 0, waits = 0;
    bool allocation = true, cgfx = true, model = true, build = true;
    std::uint32_t bytes = 0, tag = 0x40000092u;
    std::vector<std::string> trace;
    game::scene::Identity Current() override { return identity; }
    game::heap::Room Room() override { return room; }
    bool Create(const game::scene::Plan &, std::uint32_t n) override { ++creates; bytes = n; return allocation; }
    int Load(std::size_t) override { ++loads; return result; }
    bool Setup(std::size_t) override { return game::scene::IsCgfx(cgfx ? "CGFX" : "bad!", 4); }
    bool Find(const game::scene::Plan &p) override { return model && game::scene::AcceptsModel(p.kind, tag); }
    bool Build() override { return build; }
    void Destroy() override {
        ++destroys;
        game::scene::ReleaseInOrder([&] { trace.push_back("instance"); },
            [&] { trace.push_back("holder"); }, [&] { trace.push_back("heap"); });
    }
    void Wait() override { ++waits; }
};
game::scene::Plan Plan() {
    game::scene::Plan p;
    p.files = {{"a.bcres", 1000}}; p.model = "model";
    p.instance_bytes = 2000; p.reserve_bytes = 3000;
    return p;
}
void Resources() {
    using S = world::SceneResources;
    for (auto policy : {S::Shortage::Stop, S::Shortage::Retry}) {
        Resource b; S s(b); b.room.free_bytes = 10;
        assert(s.Start(Plan(), policy)); s.Step();
        assert(b.creates == 0 && s.Failure() == S::Error::Space);
        if (policy == S::Shortage::Stop) assert(s.State() == S::Stage::Failed);
        else {
            b.room.free_bytes = 1000000; b.room.contiguous_bytes = 20;
            s.Step(); assert(b.creates == 0);
            b.room.contiguous_bytes = 1000000; s.Step();
            assert(b.creates == 1 && b.bytes == 17884);
        }
    }
    // 上限回数で打ち切らず、取り消し後も現在の1本だけを待つ。
    for (unsigned reason = 0; reason < 4; ++reason) {
        Resource b; S s(b); assert(s.Start(Plan(), S::Shortage::Retry));
        s.Step();
        for (unsigned i = 0; i < 150; ++i) s.Step();
        assert(b.loads == 150 && b.destroys == 0 && s.Loading());
        if (reason == 0) s.Cancel();
        if (reason == 1) ++b.identity.room;
        if (reason == 2) b.identity.resource = nullptr;
        if (reason == 3) assert(!s.Shutdown(2));
        s.Step(); assert(b.destroys == 0);
        b.result = 1; s.Step();
        assert(s.State() == S::Stage::Off && b.destroys == 1);
        assert((b.trace == std::vector<std::string>{"instance", "holder", "heap"}));
    }
    {
        Resource b; S s(b); assert(s.Start(Plan(), S::Shortage::Retry));
        s.Step(); s.Step();
        assert(!s.Shutdown(3) && b.waits == 3 && b.destroys == 0 && s.ParentBytes() > 0);
        b.result = 1;
        assert(s.Shutdown(3) && b.destroys == 1 && s.ParentBytes() == 0);
    }
    for (unsigned failure = 0; failure < 5; ++failure) {
        Resource b; b.result = 1;
        if (failure == 0) b.allocation = false;
        if (failure == 1) b.cgfx = false;
        if (failure == 2) b.model = false;
        if (failure == 3) b.tag = 0x40000012;
        if (failure == 4) b.build = false;
        S s(b); assert(s.Start(Plan(), S::Shortage::Stop));
        for (unsigned i = 0; i < 7; ++i) s.Step();
        assert(s.State() == S::Stage::Failed && b.destroys == 1);
    }
    {
        Resource b; b.result = 1; b.tag = 0x40000012;
        auto p = Plan(); p.kind = game::scene::ModelKind::ModelOrSkeletal;
        p.files.push_back({"b.bcres", 2000});
        S s(b); assert(s.Start(p, S::Shortage::Stop));
        for (unsigned i = 0; i < 9; ++i) s.Step();
        assert(s.State() == S::Stage::Ready && b.loads == 2);
        ++b.identity.room; s.Step(); assert(b.destroys == 1);
    }
    assert(!game::scene::IsCgfx("CGFX", 3));
    assert(!game::scene::AcceptsModel(game::scene::ModelKind::ModelOrSkeletal, 123));
}
void Tint() {
    using namespace game::material;
    std::array<std::uint32_t, 181> material{};
    Tev tev{};
    for (unsigned i = 0; i < 6; ++i) {
        tev[11 + 7 * i + 1] = 0x0FFF000F; tev[11 + 7 * i + 2] = 100 + i;
    }
    material.fill(0xFFFFFFFF); material[82] = 1u << 16; material[70] = 3;
    auto old_material = material; const auto old_tev = tev;
    View v{&tev, material.data() + 9, material.data() + 178,
        material.data() + 75, material.data() + 82, material.data() + 84, material.data() + 180, true};
    world::MaterialTint tint;
    assert(tint.Push(v, {0x000000FF, 128, 99}));
    assert(tev[46] == 5 && (tev[47] & 65535) == 0xEFE && (tev[50] & 65535) == 4);
    assert(material[70] == 3 && material[84] == 99u << 24);
    assert(!tint.Push(v, {}));
    tint.Restore(); assert(material == old_material && tev == old_tev);
    // 半透明の元のブレンド式・深度・読み込みフラグは変更しない。
    material[82] = (6u << 16) | (7u << 20);
    old_material = material;
    assert(tint.Push(v, {255, 128, 77}));
    assert(tev[47] == 0x00EF0EEF && tev[50] == 0x00010008);
    assert(material[82] == old_material[82] && material[70] == 3 && material[75] == old_material[75]);
    assert(material[63] >> 24 == 77 && (material[63] & 255) == 128);
    tint.Restore(); assert(material == old_material && tev == old_tev);
    v.lut_valid = false;
    assert(!tint.Push(v, {})); assert(tev == old_tev && material == old_material);
    v.lut_valid = true;
    {
        tev[10] = 128;
        Tev copy{};
        assert(CopyTev(v, copy));
        assert(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&copy) + 40 + copy[10]) ==
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tev) + 40 + tev[10]));
        tev[10] = 0;
        assert(CopyTev(v, copy) && copy[10] == 0);
        v.lut_valid = false; assert(!CopyTev(v, copy)); v.lut_valid = true;
    }
    // 最終段が使用済みなら空き段を詰め、各位置のヘッダは保つ。
    auto compact = tev; compact[50] = 1; compact[57] = 0;
    const auto plan = Prepare(compact, std::span<const std::uint32_t, 55>(v.palette, 55));
    assert(plan == Plan::Compact);
    for (unsigned i = 0; i < 6; ++i) assert(compact[11 + 7 * i + 2] == 100 + i);
    // 全段使用の半透明材質は変更を断り、元のアルファを壊さない。
    for (unsigned i = 0; i < 6; ++i) tev[11 + 7 * i + 4] = 1;
    const auto full = tev;
    assert(!tint.Push(v, {})); assert(tev == full && material == old_material);
    auto replace = tev;
    assert(Prepare(replace, std::span<const std::uint32_t, 55>(v.palette, 55)) == Plan::Replace);
    assert((replace[50] & 65535) == 8);
}
void MatrixCache() {
    std::map<std::uintptr_t, std::uint32_t> words{{0x1008, 0x2000}, {0x201C, 99}};
    unsigned writes = 0, reads = 0;
    auto read = [&](std::uintptr_t a) { ++reads; assert(a == 0x1008); return words.at(a); };
    auto readable = [](std::uintptr_t a, std::size_t n) {
        return (a == 0x1000 && n == 12) || (a == 0x2000 && n == 32);
    };
    auto write = [&](std::uintptr_t a, std::uint32_t v) { assert(a == 0x201C && v == 0); words.at(a) = v; ++writes; };
    for (unsigned i = 0; i < 9; ++i) {
        words[0x201C] = 99;
        game::instance::ResetCache(0x1000, read, readable, write);
        assert(words[0x201C] == 0);
    }
    assert(writes == 9);
    words[0x1008] = 0x3F800000;
    game::instance::ResetCache(0x1000, read, readable, write);
    assert(writes == 9);
    const auto before = reads;
    game::instance::ResetCache(0, read, readable, write);
    assert(reads == before && writes == 9);
}
struct Drawer : game::instance::Backend {
    unsigned prepared = 0, resets = 0, meshes = 0;
    bool cleared = false;
    bool Prepare(std::uintptr_t, const game::instance::Matrix &) override { ++prepared; cleared = false; return prepared < 4; }
    void ResetMatrixCache(std::uintptr_t) override { cleared = true; ++resets; }
    void Meshes(std::uintptr_t) override { assert(cleared); cleared = false; ++meshes; }
};
void DrawInstances() {
    Drawer b; world::InstancedDraw draw(b);
    std::array<game::instance::Matrix, 9> matrices{};
    assert(draw.Draw(1, matrices) == 3 && b.meshes == 3 && b.resets == 4);
    assert(draw.Draw(1, {}) == 0 && b.resets == 5);
}
std::uintptr_t first = 10, second = 20;
int node, scene_token;
bool available = true;
std::vector<int> calls;
void OriginalFirst(std::uintptr_t, std::uintptr_t) { calls.push_back(2); }
void OriginalSecond(std::uintptr_t, std::uintptr_t) { calls.push_back(5); }
void Client(std::uintptr_t, void *data) { calls.push_back(*static_cast<int *>(data)); }
void Hook() {
    using P = game::draw::Pair;
    P p;
    game::draw::Slots slots{&first, &second, &node, &scene_token};
    first = 10; second = 99;
    assert(!p.Install(slots, 10, 20, 30, 40) && first == 10 && second == 99);
    second = 20; assert(p.Install(slots, 10, 20, 30, 40));
    first = 99; assert(!p.Restore() && first == 99 && second == 20);
    first = 30; assert(p.Restore() && first == 10 && second == 20);
    assert(p.Install(slots, 10, 20, 30, 40));
    p.Abandon(); // 場面が消えた後は古い欄を復元しない。
    assert(p.Restore() && first == 30 && second == 40);
    using H = world::FgobjDrawHook;
    first = reinterpret_cast<std::uintptr_t>(&OriginalFirst);
    second = reinterpret_cast<std::uintptr_t>(&OriginalSecond);
    assert(H::Feature().Wire());
    int a = 1, b = 3, c = 4;
    available = false; assert(!H::Add(&a, H::Where::Before, Client, &a));
    available = true;
    assert(H::Add(&a, H::Where::Before, Client, &a));
    assert(H::Add(&b, H::Where::After, Client, &b));
    assert(H::Add(&c, H::Where::BeforeLayer1, Client, &c));
    reinterpret_cast<game::draw::Callback>(first)(0, 0);
    reinterpret_cast<game::draw::Callback>(second)(0, 0);
    assert((calls == std::vector<int>{1, 2, 3, 4, 5}));
    H::Remove(&b); H::Remove(&a); H::Remove(&c);
    assert(first == reinterpret_cast<std::uintptr_t>(&OriginalFirst));
    assert(second == reinterpret_cast<std::uintptr_t>(&OriginalSecond));
    H::Feature().Shutdown();
    core::frame::Global().Reset();
}
std::map<std::uintptr_t, std::vector<unsigned char>> memory;
unsigned queries = 0;
bool Query(std::uintptr_t address, game::memory::Region &r) {
    ++queries;
    r = {address & ~std::uintptr_t(4095), 4096, address != 0x3F800000 && address < 0x10000000};
    return true;
}
bool Copy(std::uintptr_t address, void *out, std::size_t size) {
    for (const auto &[base, bytes] : memory) if (address >= base && address - base <= bytes.size() &&
        size <= bytes.size() - (address - base)) {
        std::memcpy(out, bytes.data() + (address - base), size); return true;
    }
    return false;
}
void Put(std::uintptr_t a, std::uint32_t value) {
    memory[a].resize(4); std::memcpy(memory[a].data(), &value, 4);
}
void ReadMemory() {
    game::memory::Reader reader({Query, Copy});
    assert(reader.Readable(0x8001000, 8)); const auto before = queries;
    assert(reader.Readable(0x8001004, 4) && queries == before);
    assert(reader.Readable(0x8001FFE, 4) && queries == before + 1);
    assert(!reader.Readable(0x3F800000, 4));
    reader.BeginFrame(); assert(reader.Readable(0x8001000, 4) && queries == before + 3);
    Put(0x8001000, 0x400104); Put(0x400100, 0x400200); Put(0x400204, 0x400300);
    const char name[] = "N2nw3gfx8MaterialE";
    memory[0x400300] = std::vector<unsigned char>(name, name + sizeof(name));
    assert(reader.IsClass(0x8001000, name));
    assert(!reader.IsClass(0x8001000, "other"));
    assert(!reader.IsClass(0x3F800000, name));
    for (unsigned i = 0; i < core::limits::ReadableRegions + 10; ++i) assert(reader.Readable(0x8010000 + i * 4096, 4));
    assert(reader.Readable(0x8001000, 4));
}
void Ownership() {
    world::UnitCursor::Ownership o;
    int a, b;
    assert(!o.Acquire(nullptr));
    assert(o.Acquire(&a) && o.Acquire(&a) && !o.Acquire(&b));
    assert(!o.Release(&b) && o.Is(&a));
    assert(o.Release(&a) && !o.Get() && o.Acquire(&b));
}
}
namespace gohan::game::draw {
Slots CurrentSlots() { return available ? Slots{&first, &second, &node, &scene_token} : Slots{}; }
Callback Original0() { return OriginalFirst; }
Callback Original1() { return OriginalSecond; }
bool Wire() { return true; }
void Unwire() {}
void Suspend() {}
void Resume() {}
}
int main() {
    Resources(); Tint(); MatrixCache(); DrawInstances(); Hook(); ReadMemory(); Ownership();
    std::cout << "P4a-2: OK\n";
}
