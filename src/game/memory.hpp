#pragma once
#include <cstddef>
#include <cstdint>
#include "core/feature.hpp"

namespace gohan::game::memory {
struct Region { std::uintptr_t base; std::size_t size; bool readable; };
struct Access {
    bool (*query)(std::uintptr_t, Region &);
    bool (*copy)(std::uintptr_t, void *, std::size_t);
};
class Reader {
  public:
    explicit Reader(Access access) : access_(access) {}
    void BeginFrame() { count_ = next_ = 0; }
    bool Readable(std::uintptr_t address, std::size_t length);
    bool IsClass(std::uintptr_t object, const char *name);
  private:
    bool Copy(std::uintptr_t address, void *out, std::size_t size);
    Access access_;
    Region cache_[core::limits::ReadableRegions]{};
    unsigned count_ = 0, next_ = 0;
};
bool IsHeapPointer(std::uintptr_t address);
void BeginFrame();
bool Readable(std::uintptr_t address, std::size_t length);
bool IsClass(std::uintptr_t object, const char *name);
const core::Feature &Feature();
} // namespace gohan::game::memory
