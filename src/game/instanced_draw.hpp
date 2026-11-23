#pragma once
#include <array>
#include <cstdint>
namespace gohan::game::instance {
using Matrix = std::array<float, 12>;
class Backend {
  public:
    virtual ~Backend() = default;
    virtual bool Prepare(std::uintptr_t context, const Matrix &) = 0;
    virtual void ResetMatrixCache(std::uintptr_t context) = 0;
    virtual void Meshes(std::uintptr_t context) = 0;
};
} // namespace gohan::game::instance
