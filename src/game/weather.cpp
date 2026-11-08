#include "game/weather.hpp"

namespace gohan::game {
bool Weather::Decode(std::uint32_t word, std::uint32_t original, std::int32_t *value) {
    if (word == original) { *value = 0; return true; }
    if ((word & 0xFFFFFF00u) != 0xE3A00000u || (word & 255) > 6) return false;
    *value = static_cast<std::int32_t>(word & 255) + 1;
    return true;
}
std::uint32_t Weather::Encode(std::int32_t value, std::uint32_t original) {
    return value == 0 ? original : 0xE3A00000u | static_cast<std::uint32_t>(value - 1);
}
bool Weather::Read(std::int32_t *value) const {
    std::uint32_t word;
    return value && patch_.ReadSingleWord(word) && Decode(word, patch_.Original(), value);
}
Result Weather::Write(std::int32_t value) {
    if (value < 0 || value > 7) return Result::Failed;
    std::int32_t current;
    if (!Read(&current)) return Result::Unsupported;
    return patch_.SetSingleWord(Encode(value, patch_.Original()));
}
} // namespace gohan::game
