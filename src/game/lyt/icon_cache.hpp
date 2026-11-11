#pragma once
#include "core/limits.hpp"
#include <cstddef>
#include <cstdint>

namespace gohan::game::lyt {
// TexMap が物理番地を保持している間、その枠の中身を変えない。ゲームのスレッド専用。
class IconCache {
  public:
    static constexpr unsigned Slots = 32, Users = 128, SlotBytes = 1024;
    using Flush = bool (*)(void *, std::size_t);
    using Physical = std::uint32_t (*)(const void *);
    bool Bind(void *allocation, std::size_t bytes, Flush flush, Physical physical);
    void Unbind();
    void Release(const void *widget);
    std::uint32_t Place(const void *widget, std::uint16_t image, const std::uint8_t *pixels);
    std::uint32_t Generation() const { return generation_; }
    unsigned References(unsigned slot) const { return slot < Slots ? slots_[slot].references : 0; }
    const void *Data() const { return base_; }

  private:
    struct Slot {
        std::uint16_t image = 0;
        unsigned references = 0;
        bool loaded = false;
    };
    struct User {
        const void *widget = nullptr;
        unsigned slot = 0;
    };
    void Clear();
    Slot slots_[Slots]{};
    User users_[Users]{};
    std::uint8_t *base_ = nullptr;
    Flush flush_ = nullptr;
    Physical physical_ = nullptr;
    std::uint32_t generation_ = 0;
};
static_assert(IconCache::Slots * IconCache::SlotBytes == core::limits::RendererIconCacheBytes);
IconCache &SharedIconCache();
} // namespace gohan::game::lyt
