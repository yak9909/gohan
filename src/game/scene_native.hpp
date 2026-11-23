#pragma once
#include "game/scene_resources.hpp"
#include "game/instanced_draw.hpp"
#include "game/material_tint.hpp"
#include <array>
namespace gohan::game::scene {
class Native final : public Backend, public instance::Backend {
  public:
    // Destroy が呼べない読み込み中の状態は、デストラクタでも返さない。
    ~Native() override = default;
    Identity Current() override;
    heap::Room Room() override;
    bool Create(const Plan &, std::uint32_t) override;
    int Load(std::size_t) override;
    bool Setup(std::size_t) override;
    bool Find(const Plan &) override;
    bool Build() override;
    void Destroy() override;
    void Wait() override;
    bool Prepare(std::uintptr_t, const instance::Matrix &) override;
    void ResetMatrixCache(std::uintptr_t) override;
    void Meshes(std::uintptr_t) override;
    bool Material(material::View &);
    void Animate();
    static bool PositionMatrix(const std::array<float, 3> &, instance::Matrix &);
    static bool PlayerPosition(std::array<float, 3> &);
    static void Pause();
    static void Resume();
  private:
    struct State;
    State *state_ = nullptr;
};
} // namespace gohan::game::scene
