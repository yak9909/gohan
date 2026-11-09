#pragma once
#include "core/limits.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

// Wire と終了時だけ、ゲームを停止したプラグインのメインスレッドから使う。
namespace gohan::game::code_ownership {
struct Entry { std::uintptr_t address = 0; const void *owner = nullptr; };
inline std::array<Entry, core::limits::CodeWords> entries{};
inline void Release(const void *owner) {
    for (auto &entry : entries) if (entry.owner == owner) entry = {};
}
inline bool Take(const void *owner, std::uintptr_t address) {
    if (!owner || !address || (address & 3)) return false;
    for (const auto &entry : entries) if (entry.owner && entry.address == address) return false;
    for (auto &entry : entries) if (!entry.owner) { entry = {address, owner}; return true; }
    return false;
}
} // namespace gohan::game::code_ownership
