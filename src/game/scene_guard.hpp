#pragma once
#include <cstdint>
namespace gohan::game {
// 場面の識別子は資源の持ち主が渡す。同じ部屋でも場面が作り直された場合を区別する。
class SceneGuard {
  public:
    void Capture(const void *scene, std::uint32_t room) { scene_ = scene; room_ = room; armed_ = true; }
    bool Matches(const void *scene, std::uint32_t room) const { return armed_ && scene_ == scene && room_ == room; }
    void Capture(const void *scene);
    bool Matches(const void *scene) const;
    void Reset() { armed_ = false; }
  private:
    const void *scene_ = nullptr;
    std::uint32_t room_ = 0;
    bool armed_ = false;
};
} // namespace gohan::game
