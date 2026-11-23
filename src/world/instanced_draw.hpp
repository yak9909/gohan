#pragma once
#include "game/instanced_draw.hpp"
#include <span>
namespace gohan::world {
class InstancedDraw {
  public:
    explicit InstancedDraw(game::instance::Backend &backend) : backend_(backend) {}
    unsigned Draw(std::uintptr_t context, std::span<const game::instance::Matrix> matrices) {
        unsigned drawn = 0;
        for (const auto &m : matrices) {
            if (!backend_.Prepare(context, m)) break;
            backend_.ResetMatrixCache(context);
            backend_.Meshes(context);
            ++drawn;
        }
        backend_.ResetMatrixCache(context);
        return drawn;
    }
  private:
    game::instance::Backend &backend_;
};
}
