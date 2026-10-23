#include "core/input_block.hpp"

namespace gohan::core::input_block {
static_assert(limits::InputBlockOwners > 1 && limits::InputBlockOwners < 0xFF);
Table::Table() {
    owners_[0] = {"reserved", {part::Select, 0}};
    Publish(0, owners_[0].claim);
}

bool Table::Valid(const Owner &owner) const {
    return owner.slot > 0 && owner.slot < owners_.size() && owners_[owner.slot].name;
}

bool Table::Publish(std::size_t slot, const Claim &replacement) {
    Effective next{};
    for (std::size_t i = 0; i < owners_.size(); ++i) {
        const Claim claim = i == slot ? replacement : owners_[i].claim;
        next.block |= claim.block;
        next.defer |= claim.defer;
    }
    next.generation = generation_ + 1;
    if (!next.generation) ++next.generation;
    // 公開に失敗した時は、表も世代も触らない。
    if (!published_.TryPublish(next)) return false;
    generation_ = next.generation;
    return true;
}

Result Table::Acquire(const char *name, Owner &owner) {
    if (!name || !*name || owner.slot != 0xFF) return Result::Invalid;
    if (!mutex_.TryLock()) return BusyWrite();
    Result result = Result::Full;
    for (std::size_t i = 1; i < owners_.size(); ++i) {
        if (owners_[i].name) continue;
        if (!Publish(i, {})) result = BusyWrite();
        else {
            owners_[i] = {name, {}};
            owner.slot = static_cast<std::uint8_t>(i);
            result = Result::Ok;
        }
        break;
    }
    mutex_.Unlock();
    return result;
}

Result Table::Set(const Owner &owner, const Claim &claim) {
    if (claim.defer & ~group::Buttons) return Result::Invalid;
    if (!mutex_.TryLock()) return BusyWrite();
    Result result = Result::Invalid;
    if (Valid(owner)) {
        if (owners_[owner.slot].claim == claim) result = Result::Unchanged;
        else if (!Publish(owner.slot, claim)) result = BusyWrite();
        else {
            owners_[owner.slot].claim = claim;
            result = Result::Ok;
        }
    }
    mutex_.Unlock();
    return result;
}

Result Table::Release(Owner &owner) {
    if (!mutex_.TryLock()) return BusyWrite();
    Result result = Result::Invalid;
    if (Valid(owner)) {
        if (!Publish(owner.slot, {})) result = BusyWrite();
        else {
            owners_[owner.slot] = {};
            owner = {};
            result = Result::Ok;
        }
    }
    mutex_.Unlock();
    return result;
}

MailRead Table::Read(Effective &out, std::uint32_t &generation) {
    const auto result = published_.ReadIfNew(out, generation);
    if (result == MailRead::Busy) ++read_busy_;
    return result;
}

std::size_t Table::List(OwnerInfo *out, std::size_t capacity) {
    if (!out || !capacity || !mutex_.TryLock()) return 0;
    std::size_t count = 0;
    for (const auto &owner : owners_) {
        if (owner.name && count < capacity) out[count++] = owner;
    }
    mutex_.Unlock();
    return count;
}

namespace {
// 関数内の static は初回に排他（__cxa_guard）を使うので、名前空間の static にする。
Table table;
}
Table &Global() { return table; }
} // namespace gohan::core::input_block
