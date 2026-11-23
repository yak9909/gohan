#pragma once
#include "game/heap.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gohan::game::scene {
struct File { std::string path; std::uint32_t bytes = 0; };
enum class ModelKind { Skeletal, ModelOrSkeletal };
struct Plan {
    std::vector<File> files;
    std::string model;
    std::uint32_t resource_bytes = 0, instance_bytes = 0, reserve_bytes = 0;
    std::size_t model_file = 0;
    ModelKind kind = ModelKind::Skeletal;
};
struct Identity { const void *owner = nullptr; const void *resource = nullptr; std::uint32_t room = 0; };
inline bool operator==(Identity a, Identity b) {
    return a.owner == b.owner && a.resource == b.resource && a.room == b.room;
}
// Native の状態は非同期 I/O の参照先でもある。Load が完了するまで Destroy しない。
class Backend {
  public:
    virtual ~Backend() = default;
    virtual Identity Current() = 0;
    virtual heap::Room Room() = 0;
    virtual bool Create(const Plan &, std::uint32_t resource_bytes) = 0;
    virtual int Load(std::size_t file) = 0;
    virtual bool Setup(std::size_t file) = 0;
    virtual bool Find(const Plan &) = 0;
    virtual bool Build() = 0;
    virtual void Destroy() = 0;
    virtual void Wait() = 0;
};
} // namespace gohan::game::scene
