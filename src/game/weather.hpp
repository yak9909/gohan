#pragma once
#include "game/patch_set.hpp"

namespace gohan::game {
class Weather {
  public:
    explicit Weather(PatchSet &patch) : patch_(patch) {}
    bool Read(std::int32_t *value) const;
    Result Write(std::int32_t value);
    static bool Decode(std::uint32_t word, std::uint32_t original, std::int32_t *value);
    static std::uint32_t Encode(std::int32_t value, std::uint32_t original);
  private:
    PatchSet &patch_;
};
} // namespace gohan::game
