#include "game/lyt/icon_cache.hpp"
#include <cstring>
#include <limits>

namespace gohan::game::lyt {
void IconCache::Clear() {
    for (auto &slot : slots_)
        slot = {};
    for (auto &user : users_)
        user = {};
    ++generation_;
}
bool IconCache::Bind(void *allocation, std::size_t bytes, Flush flush, Physical physical) {
    Unbind();
    const auto begin = reinterpret_cast<std::uintptr_t>(allocation);
    constexpr auto size = core::limits::RendererIconCacheBytes;
    if (!allocation || !flush || !physical || bytes < size ||
        bytes > std::numeric_limits<std::uintptr_t>::max() - begin)
        return false;
    // 借りた領域の末尾から切り下げる。切り上げでは最後の枠が範囲を越える。
    const auto start = (begin + bytes - size) & ~std::uintptr_t(127);
    if (start < begin)
        return false;
    base_ = reinterpret_cast<std::uint8_t *>(start);
    flush_ = flush;
    physical_ = physical;
    return true;
}
void IconCache::Unbind() {
    Clear();
    base_ = nullptr;
    flush_ = nullptr;
    physical_ = nullptr;
}
void IconCache::Release(const void *widget) {
    if (!widget)
        return;
    for (auto &user : users_)
        if (user.widget == widget) {
            if (slots_[user.slot].references)
                --slots_[user.slot].references;
            user = {};
            return;
        }
}
std::uint32_t IconCache::Place(const void *widget, std::uint16_t image, const std::uint8_t *pixels) {
    Release(widget);
    if (!base_ || !widget || !pixels)
        return 0;
    User *user = nullptr;
    for (auto &candidate : users_)
        if (!candidate.widget) {
            user = &candidate;
            break;
        }
    if (!user)
        return 0;
    unsigned pick = Slots;
    for (unsigned i = 0; i < Slots; ++i) {
        if (slots_[i].loaded && slots_[i].image == image) {
            pick = i;
            break;
        }
        if (!slots_[i].references && pick == Slots)
            pick = i;
    }
    if (pick == Slots)
        return 0;
    auto *target = base_ + pick * SlotBytes;
    const auto address = physical_(target);
    if (!address)
        return 0;
    auto &slot = slots_[pick];
    if (!slot.loaded || slot.image != image) {
        slot.loaded = false;
        std::memcpy(target, pixels, SlotBytes);
        if (!flush_(target, SlotBytes))
            return 0;
        slot.image = image;
        slot.loaded = true;
    }
    ++slot.references;
    *user = {widget, pick};
    return address;
}
IconCache &SharedIconCache() {
    static IconCache cache;
    return cache;
}
} // namespace gohan::game::lyt
