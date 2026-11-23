#pragma once
#include "game/scene_resources.hpp"
#include "game/scene_guard.hpp"
#include <cstdint>

namespace gohan::world {
class SceneResources {
  public:
    enum class Stage { Off, Allocate, Load, Setup, Find, Instance, Ready, Failed };
    enum class Shortage { Stop, Retry };
    enum class Error { None, Invalid, Space, Allocation, Setup, Model, Instance };
    explicit SceneResources(game::scene::Backend &backend) : backend_(backend) {}
    SceneResources(const SceneResources &) = delete;
    SceneResources &operator=(const SceneResources &) = delete;
    ~SceneResources();
    bool Start(game::scene::Plan plan, Shortage shortage);
    void Cancel();
    void Step();
    // false: 読み込みが終わらず保持中。Backend 自体とその記憶域も保持すること。
    bool Shutdown(unsigned polls);
    Stage State() const { return stage_; }
    Error Failure() const { return error_; }
    std::uint64_t LoadPolls() const { return polls_; }
    std::uint32_t ParentBytes() const { return allocated_ ? resource_bytes_ + plan_.instance_bytes : 0; }
    bool Loading() const { return in_flight_; }
  private:
    void Clear();
    void Fail(Error error);
    game::scene::Backend &backend_;
    game::scene::Plan plan_;
    game::scene::Identity identity_{};
    game::SceneGuard guard_;
    Stage stage_ = Stage::Off;
    Shortage shortage_ = Shortage::Stop;
    Error error_ = Error::None;
    std::uint32_t resource_bytes_ = 0;
    std::size_t file_ = 0;
    std::uint64_t polls_ = 0;
    bool allocated_ = false, cancelled_ = false, in_flight_ = false;
};
} // namespace gohan::world
