#pragma once
#include "game/heap.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <cstring>

namespace gohan::game::scene {
struct File { std::string path; std::uint32_t bytes = 0; };
enum class ModelKind { Skeletal, ModelOrSkeletal };
inline bool AcceptsModel(ModelKind kind, std::uint32_t type) {
    return type == 0x40000092u || (kind == ModelKind::ModelOrSkeletal && type == 0x40000012u);
}
inline bool IsCgfx(const void *data, std::size_t bytes) {
    return data && bytes >= 4 && std::memcmp(data, "CGFX", 4) == 0;
}
template<class Instances, class Holders, class Heaps>
void ReleaseInOrder(Instances instances, Holders holders, Heaps heaps) {
    instances(); holders(); heaps();
}
struct Plan {
    std::vector<File> files;
    std::string model;
    std::uint32_t resource_bytes = 0, instance_bytes = 0, reserve_bytes = 0;
    std::size_t model_file = 0;
    ModelKind kind = ModelKind::Skeletal;
    std::vector<std::pair<std::size_t, std::size_t>> links;
    bool cursor_animations = false;
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
