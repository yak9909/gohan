#pragma once
#include "game/write_log.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace gohan::game::material {
using Tev = std::array<std::uint32_t, 61>;
enum class Plan { Free, Compact, Replace };
struct Colour { std::uint32_t rgb = 0; std::uint8_t strength = 0, alpha = 255; };
// View は材質の写しにも、描画コールバック中の一時変更にも使える。
// 書き戻すまで、指す全領域を生かしておくこと。
struct View {
    Tev *tev = nullptr;
    std::uint32_t *palette = nullptr; // Constant0 等を含む55語
    std::uint32_t *tev_key = nullptr;
    std::uint32_t *fb_read = nullptr, *blend = nullptr, *blend_colour = nullptr, *frag_key = nullptr;
    bool lut_valid = false;
};
Plan Prepare(Tev &, std::span<const std::uint32_t, 55> palette);
void ScaleAlpha(Tev &, bool tint);
std::uint32_t Constant(Colour, bool premultiplied, bool scale_alpha = false);
class Patch {
  public:
    Patch() = default;
    ~Patch() { Restore(); }
    bool Push(View, Colour);
    void Restore();
    bool Active() const { return static_cast<bool>(log_); }
  private:
    std::unique_ptr<WriteLog<12>> log_;
};
bool Resolve(std::uintptr_t material, View &);
} // namespace gohan::game::material
