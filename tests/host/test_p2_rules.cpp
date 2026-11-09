#include "ui/timed_repeat.hpp"
#include "game/player_motion.hpp"
#include "game/player_actions.hpp"
#include "game/keep_items.hpp"
#include "game/shizue_skip.hpp"
#include "game/memory.hpp"
#include "game/generated/api.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace {
using namespace gohan;
unsigned room = 0x63, exit_calls = 0, prepare_calls = 0, resolves = 0;
bool valid = true, prepared = false, after_valid = true;
unsigned Room() { return room; }
bool Resolve(game::shizue_skip::Exit &exit) { ++resolves; exit = {reinterpret_cast<void *>(128), 2}; return valid && (!prepared || after_valid); }
bool Prepared() { return prepared; }
void Prepare() { ++prepare_calls; prepared = true; }
void Exit(const game::shizue_skip::Exit &exit) { assert(exit.slot == 2 && exit.manager == reinterpret_cast<void *>(128)); ++exit_calls; }
alignas(4) std::array<std::uint8_t, 0x200> actor{};
std::array<std::uint8_t, 0x26> source{}, result{};
bool network = true, readable = true;
unsigned states = 0, last_state = 0;
bool Network() { return network; }
void *Actor() { return actor.data(); }
const void *Packet(void *p) { assert(p == actor.data()); return source.data(); }
bool Readable(std::uintptr_t, std::size_t) { return readable; }
void Pack(void *out, const float *xyz) { assert(xyz == reinterpret_cast<const float *>(actor.data() + 0x14)); const std::uint32_t word = 0xAABBCCDD; std::memcpy(out, &word, 4); }
void SetState(void *p, unsigned state, const void *packet, unsigned zero) {
    assert(p == actor.data() && zero == 0); ++states; last_state = state; std::memcpy(result.data(), packet, result.size());
}
std::map<std::uintptr_t, std::uint8_t> memory;
unsigned queries = 0;
bool Query(std::uintptr_t a, game::memory::Region &r) { ++queries; r = {a & ~std::uintptr_t(4095), 4096, (a & ~std::uintptr_t(4095)) != 0x9000}; return true; }
bool Copy(std::uintptr_t a, void *out, std::size_t size) { for (std::size_t i = 0; i < size; ++i) { if (!memory.contains(a + i)) return false; static_cast<std::uint8_t *>(out)[i] = memory[a + i]; } return true; }
void Put(std::uintptr_t a, std::uint32_t word) { for (unsigned i = 0; i < 4; ++i) memory[a + i] = word >> (i * 8); }
void Near(float a, float b) { assert(std::fabs(a - b) < .01f); }
}
int main() {
    using namespace gohan;
    ui::TimedRepeat repeat;
    assert(repeat.Step(1, 0) && !repeat.Step(1, 199) && repeat.Step(1, 200));
    assert(!repeat.Step(1, 259) && repeat.Step(1, 260));
    assert(repeat.Step(1, 900) && !repeat.Step(1, 900) && !repeat.Step(1, 959) && repeat.Step(1, 960));
    assert(repeat.Step(2, 961) && !repeat.Step(2, 1160) && repeat.Step(2, 1161));
    assert(!repeat.Step(0, 1170) && repeat.Step(2, 1171));
    ui::TimedRepeat grace({200, 60, 300, 20, 80});
    assert(grace.Step(1, 0) && !grace.Step(3, 10) && !grace.Step(3, 89) && grace.Step(3, 90));
    assert(grace.Step(3, 310) && !grace.Step(3, 329) && grace.Step(3, 330));
    ui::TimedRepeat wrap;
    assert(wrap.Step(1, 0xFFFFFFF0u) && !wrap.Step(1, 183) && wrap.Step(1, 184));

    namespace motion = game::player_motion;
    assert(motion::GridCenter(0) == 16 && motion::GridCenter(-1) == -16 && motion::GridCenter(10) == 336);
    for (int x = -1; x <= 1; ++x) for (int z = -1; z <= 1; ++z) {
        assert(motion::Round8(x * .5f) == x && motion::Round8(z * .5f) == z);
        assert(bool(motion::DirectionBits(x, z)) == bool(x || z));
    }
    assert(motion::Round8(.499f) == 0 && motion::Round8(-.499f) == 0);
    game::InputSnapshot input;
    input.buttons = (1u << 7) | (1u << 4);
    auto direction = motion::ReadDirection(input, 0, 1u << 4); assert(direction.x == 1 && direction.z == 0);
    input.slide_x = .1f; input.slide_y = .1f;
    direction = motion::ReadDirection(input, 1, 0); assert(direction.x == 0 && direction.z == 0);
    input.cstick_x = 146; input.cstick_y = -146;
    direction = motion::ReadDirection(input, 2, 0); assert(direction.x == 1 && direction.z == 1);
    struct Warp { unsigned room; bool opened; int x, y, w, h; float ox, oz, scale; };
    const Warp warps[] = {{0, false, 70, 32, 180, 175, 30, 5, 14.2f}, {0, true, 7, 32, 180, 175, -33, 6, 14.2f}, {0x68, false, 76, 36, 168, 168, 72, 23, 12.1f}, {1, false, 4, 43, 312, 159, -16, 55, 6.2f}, {0x69, false, 65, 34, 190, 170, 24, -7, 13.5f}, {0x7F, false, 65, 34, 190, 170, 24, -7, 13.5f}};
    for (const auto &w : warps) {
        assert(motion::TouchCoordinates(w.room, w.opened, w.x, w.y, direction));
        Near(direction.x, (w.x - w.ox) * w.scale); Near(direction.z, (w.y - w.oz) * w.scale);
        assert(motion::TouchCoordinates(w.room, w.opened, w.x + w.w - 1, w.y + w.h - 1, direction));
        Near(direction.x, (w.x + w.w - 1 - w.ox) * w.scale);
        assert(!motion::TouchCoordinates(w.room, w.opened, w.x - 1, w.y, direction));
        assert(!motion::TouchCoordinates(w.room, w.opened, w.x + w.w, w.y, direction));
        assert(!motion::TouchCoordinates(w.room, w.opened, w.x, w.y + w.h, direction));
    }
    assert(!motion::TouchCoordinates(0x80, false, 100, 100, direction));
    assert(!motion::TouchCoordinates(2, false, 100, 100, direction));
    const std::uintptr_t returns[] = {104, 204, 304, 404, 504};
    for (auto p : returns) {
        assert(game::keep_items::GrowthCaller(p, returns) && game::keep_items::GrowthCaller(p | 1, returns));
        assert(!game::keep_items::GrowthCaller(p - 4, returns) && !game::keep_items::GrowthCaller(p + 2, returns));
    }
    namespace api = game::generated;
    const std::uintptr_t expected_returns[] = {api::Field_GrowUpWiltBushSapling_addr + 0x51,
        api::Field_GrowUpWiltSapling_addr + 0x6D, api::Field_GrowUpWiltSapling_addr + 0xFB,
        api::Field_GrowUpWiltSaplingToItem3_addr + 0x51, api::Field_GrowUpWiltSaplingToItem4_addr + 0x51};
    const auto sites = game::keep_items::GrowthReturns();
    assert(sites.size() == 5);
    for (unsigned i = 0; i < 5; ++i) {
        assert(sites[i] == expected_returns[i]);
        assert(game::keep_items::GrowthCaller(sites[i] | 1, sites));
        assert(!game::keep_items::GrowthCaller(sites[i] - 4, sites));
    }
    assert(game::keep_items::NextGrowth(0xABCD0020) == 0xABCD0021 && game::keep_items::NextGrowth(0xABCDFFFF) == 0xABCD0000);

    game::shizue_skip::Sequence sequence({Room, Resolve, Prepared, Prepare, Exit});
    room = 0; for (int i = 0; i < 40; ++i) assert(!sequence.Step(true)); assert(resolves == 0);
    room = 0x63; for (int i = 0; i < 40; ++i) assert(!sequence.Step(false)); assert(resolves == 0);
    for (int i = 0; i < 29; ++i) assert(!sequence.Step(true));
    valid = false; assert(!sequence.Step(true)); valid = true;
    for (int i = 0; i < 29; ++i) assert(!sequence.Step(true));
    assert(exit_calls == 0 && sequence.Step(true) && exit_calls == 1 && prepare_calls == 1);
    for (int i = 0; i < 40; ++i) assert(!sequence.Step(true));
    assert(!sequence.Step(false) && !sequence.Step(true) && exit_calls == 1);
    room = 0; assert(!sequence.Step(true)); room = 0x63;
    for (int i = 0; i < 29; ++i) assert(!sequence.Step(true));
    after_valid = false; assert(!sequence.Step(true) && exit_calls == 1);
    after_valid = true;
    for (int i = 0; i < 29; ++i) assert(!sequence.Step(true));
    assert(sequence.Step(true) && exit_calls == 2 && prepare_calls == 1);
    // 日次処理が材料を変えてしまった場合は、処理後の再取得に失敗したまま出口を使わない。
    game::shizue_skip::Sequence refresh({Room, Resolve, Prepared, Prepare, Exit});
    prepared = false; after_valid = false;
    for (int i = 0; i < 29; ++i) assert(!refresh.Step(true));
    const auto resolve_before = resolves;
    assert(!refresh.Step(true) && exit_calls == 2 && prepare_calls == 2 && resolves == resolve_before + 2);
    after_valid = true;
    for (int i = 0; i < 29; ++i) assert(!refresh.Step(true));
    assert(refresh.Step(true) && exit_calls == 3 && prepare_calls == 2);

    const game::player_actions::Access access{Network, Actor, Packet, Readable, Pack, SetState};
    for (unsigned i = 0; i < source.size(); ++i) source[i] = i;
    actor[0x2F] = 0x5A;
    for (bool faint : {false, true}) {
        assert(game::player_actions::Run(access, faint) == game::Result::Ok);
        assert(last_state == (faint ? 0x9Du : 6u));
        auto expected = source; const std::uint32_t packed = 0xAABBCCDD; std::memcpy(expected.data() + 2, &packed, 4);
        if (faint) { expected[0xE] = 0; std::memcpy(expected.data() + 0xF, &packed, 4); expected[0x13] = 0x5A; }
        assert(result == expected);
    }
    network = false; assert(game::player_actions::Run(access, false) == game::Result::NotReady);
    network = true; readable = false; assert(game::player_actions::Run(access, true) == game::Result::NotReady && states == 2);

    game::memory::Reader reader({Query, Copy});
    assert(reader.Readable(0x8FFF, 1) && !reader.Readable(0x8FFF, 2));
    const auto old = queries; assert(!reader.Readable(0x9000, 4) && queries == old);
    reader.BeginFrame(); assert(!reader.Readable(0x9000, 4) && queries == old + 1);
    assert(!reader.Readable(1, 0) && !reader.Readable(~std::uintptr_t(0), 2));
    assert(game::memory::IsHeapPointer(0x08000000) && !game::memory::IsHeapPointer(0x08000001) && !game::memory::IsHeapPointer(0x40000000));
    Put(0x08000000, 0x100004); Put(0x100000, 0x100100); Put(0x100104, 0x100200);
    const char name[] = "8AcPlayer";
    for (unsigned i = 0; i < sizeof(name); ++i) memory[0x100200 + i] = name[i];
    assert(reader.IsClass(0x08000000, name) && !reader.IsClass(0x08000000, "7Unknown"));
}
