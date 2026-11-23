#pragma once
#include "core/feature.hpp"
#include "game/material_tint.hpp"
#include <array>
#include <span>
namespace gohan::world {
class UnitCursor {
  public:
    using Position = std::array<float, 3>;
    class Ownership {
      public:
        bool Acquire(const void *owner) {
            if (!owner || (owner_ && owner_ != owner)) return false;
            owner_ = owner; return true;
        }
        bool Release(const void *owner) {
            if (!owner || owner_ != owner) return false;
            owner_ = nullptr; return true;
        }
        bool Is(const void *owner) const { return owner && owner_ == owner; }
        const void *Get() const { return owner_; }
      private:
        const void *owner_ = nullptr;
    };
    static bool Acquire(const void *owner);
    static bool Release(const void *owner);
    static bool Desired(const void *owner, bool shown, std::span<const Position>,
                        game::material::Colour colour = {});
    static bool Busy(const void *owner);
    static bool PlayerPosition(Position &);
    static const core::Feature &Feature();
    struct Report {
        unsigned stage = 0, failure = 0, positions = 0, drawn = 0;
        std::uint64_t load_polls = 0;
        std::uint32_t parent_bytes = 0;
        bool retained_on_shutdown = false;
    };
    static Report Status();
};
}
