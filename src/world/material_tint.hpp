#pragma once
#include "game/material_tint.hpp"
namespace gohan::world {
class MaterialTint {
  public:
    bool Push(game::material::View view, game::material::Colour colour) { return patch_.Push(view, colour); }
    void Restore() { patch_.Restore(); }
    bool Active() const { return patch_.Active(); }
  private:
    game::material::Patch patch_;
};
} // namespace gohan::world
