#include "core/input_block.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace gohan::core {
struct MailboxTestAccess {
    template <class T> static SpinMutex &Mutex(Mailbox<T> &box) { return box.mutex_; }
};
}
namespace gohan::core::input_block {
struct TableTestAccess {
    static SpinMutex &Mutex(Table &table) { return table.mutex_; }
    static SpinMutex &Publication(Table &table) { return MailboxTestAccess::Mutex(table.published_); }
};
}
using namespace gohan::core;
using namespace gohan::core::input_block;
namespace {
void Basics() {
    Table table;
    Effective value{};
    std::uint32_t generation = 0;
    assert(table.Read(value, generation) == MailRead::Updated);
    assert(value.block == part::Select && value.defer == 0 && generation == 1 && value.generation == generation);
    assert(table.Read(value, generation) == MailRead::Unchanged);
    Owner reserved{0}, invalid, a, b;
    assert(table.Set(reserved, {}) == Result::Invalid);
    assert(table.Release(reserved) == Result::Invalid && reserved.slot == 0);
    assert(table.Set(invalid, {}) == Result::Invalid);
    assert(table.Release(invalid) == Result::Invalid);
    assert(table.Acquire(nullptr, a) == Result::Invalid);
    assert(table.Acquire("", a) == Result::Invalid);
    assert(table.Acquire("a", a) == Result::Ok);
    assert(table.Acquire("duplicate", a) == Result::Invalid);
    assert(table.Acquire("b", b) == Result::Ok);
    assert(table.Set(a, {part::A, part::Start}) == Result::Ok);
    assert(table.Set(b, {part::A | part::Touch, part::B}) == Result::Ok);
    assert(table.Read(value, generation) == MailRead::Updated);
    assert(value.block == (part::Select | part::A | part::Touch));
    assert(value.defer == (part::Start | part::B));
    const auto saved = generation;
    assert(table.Set(a, {part::A, part::Start}) == Result::Unchanged);
    for (Mask bad : {part::Select, part::Touch, part::SlidePad, 1u << 31})
        assert(table.Set(a, {part::B, bad}) == Result::Invalid);
    assert(table.Read(value, generation) == MailRead::Unchanged && saved == generation);
    assert(table.Set(a, {}) == Result::Ok);
    assert(table.Read(value, generation) == MailRead::Updated);
    assert(value.block == (part::Select | part::A | part::Touch) && value.defer == part::B);
    assert(table.Release(b) == Result::Ok && b.slot == 0xFF);
    assert(table.Release(b) == Result::Invalid);
    assert(table.Set(a, {1u << 31, 0}) == Result::Ok); // block の未知の区分も表は保持する。
    assert(table.Read(value, generation) == MailRead::Updated);
    assert(value.block == (part::Select | (1u << 31)) && value.defer == 0);
    OwnerInfo listing[limits::InputBlockOwners];
    assert(table.List(listing, limits::InputBlockOwners) == 2);
    assert(listing[0].claim == (Claim{part::Select, 0}));
    assert(table.List(listing, 1) == 1 && table.List(nullptr, 1) == 0 && table.List(listing, 0) == 0);
    std::vector<Owner> owners(limits::InputBlockOwners - 2);
    for (auto &owner : owners) assert(table.Acquire("full", owner) == Result::Ok);
    assert(table.Acquire("overflow", invalid) == Result::Full && invalid.slot == 0xFF);
    assert(table.List(listing, limits::InputBlockOwners) == limits::InputBlockOwners);
    assert(table.Release(a) == Result::Ok);
    assert(table.Acquire("reused", invalid) == Result::Ok);
    assert(&Global() == &Global());
    generation = 0;
    assert(Global().Read(value, generation) == MailRead::Updated && (value.block & part::Select));
}

// もう一つのスレッドをロック内で止め、Busy が片方だけの変更を残さないか確認する。
void Busy(bool publication) {
    Table table;
    Owner owner, fresh;
    assert(table.Acquire("busy", owner) == Result::Ok);
    assert(table.Set(owner, {part::A, part::Start}) == Result::Ok);
    Effective value{};
    std::uint32_t generation = 0;
    assert(table.Read(value, generation) == MailRead::Updated);
    const auto saved_generation = generation;
    std::atomic<bool> locked{false}, finish{false};
    std::thread writer([&] {
        auto &mutex = publication ? TableTestAccess::Publication(table) : TableTestAccess::Mutex(table);
        assert(mutex.TryLock());
        locked.store(true);
        while (!finish.load()) std::this_thread::yield();
        mutex.Unlock();
    });
    while (!locked.load()) std::this_thread::yield();
    assert(table.Set(owner, {part::B, 0}) == Result::Busy);
    assert(table.Release(owner) == Result::Busy && owner.slot != 0xFF);
    assert(table.Acquire("new", fresh) == Result::Busy && fresh.slot == 0xFF);
    if (publication) {
        assert(table.Read(value, generation) == MailRead::Busy);
        assert(value.block == (part::Select | part::A) && value.defer == part::Start && generation == saved_generation);
    } else {
        OwnerInfo listing[2];
        assert(table.List(listing, 2) == 0);
    }
    finish.store(true);
    writer.join();
    assert(table.Read(value, generation) == MailRead::Unchanged && generation == saved_generation);
    OwnerInfo listing[3];
    assert(table.List(listing, 3) == 2 && listing[1].claim == (Claim{part::A, part::Start}));
    assert(table.Set(owner, {part::B, 0}) == Result::Ok);
    assert(table.Read(value, generation) == MailRead::Updated && generation == saved_generation + 1);
}

void Concurrent() {
    Table table;
    constexpr unsigned writers = 4, rounds = 12000;
    Owner owners[writers];
    for (auto &owner : owners) assert(table.Acquire("thread", owner) == Result::Ok);
    std::atomic<bool> start{false};
    std::atomic<unsigned> done{0}, snapshots{0}, readers_ready{0};
    std::vector<std::thread> threads;
    for (unsigned n = 0; n < writers; ++n) threads.emplace_back([&, n] {
        while (!start.load()) std::this_thread::yield();
        while (readers_ready.load() != 2) std::this_thread::yield();
        for (unsigned frame = 0; frame < rounds; ++frame) {
            const Mask bit = 1u << (8 + n);
            const Claim claim = frame % 2 ? Claim{bit, bit} : Claim{};
            Result result;
            do { result = table.Set(owners[n], claim); } while (result == Result::Busy);
            assert(result == Result::Ok || result == Result::Unchanged);
            // 単一コアや他のビルドで混雑していても、途中の公開値を読む時間を作る。
            if (frame % 512 == 511) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ++done;
    });
    for (unsigned n = 0; n < 2; ++n) threads.emplace_back([&] {
        Effective value{};
        std::uint32_t generation = 0;
        while (!start.load()) std::this_thread::yield();
        ++readers_ready;
        do {
            const auto old = generation;
            const auto before = value;
            const auto result = table.Read(value, generation);
            if (result == MailRead::Updated) {
                assert(generation > old && value.generation == generation);
                assert((value.block & ~part::Select) == value.defer);
                assert((value.block & part::Select) && !(value.defer & ~group::Face));
                ++snapshots;
            } else {
                assert(generation == old && value.generation == before.generation);
                assert(value.block == before.block && value.defer == before.defer);
            }
        } while (done.load() != writers);
    });
    start.store(true);
    for (auto &thread : threads) thread.join();
    assert(snapshots.load() > 10);
    std::printf("要求の表: 書き手 %u・読み手 2、置き換え %u 回、整合した公開値 %u 回\n", writers, writers * rounds, snapshots.load());
}
}
int main() {
    Basics(); Busy(false); Busy(true); Concurrent();
    std::puts("入力の遮断の要求: 成功");
}
