#include "world/scene_resources.hpp"
#include <limits>
#include <utility>

namespace gohan::world {
SceneResources::~SceneResources() { Shutdown(0); }
bool SceneResources::Start(game::scene::Plan plan, Shortage shortage) {
    if (stage_ != Stage::Off && stage_ != Stage::Failed) return false;
    std::uint64_t bytes = plan.resource_bytes;
    if (!bytes) {
        for (const auto &f : plan.files) {
            if (!f.bytes) return false;
            bytes += f.bytes;
        }
        bytes += bytes / 2 + 16384;
    }
    const auto total = bytes + plan.instance_bytes + plan.reserve_bytes;
    if (plan.files.empty() || plan.model.empty() || plan.model_file >= plan.files.size() ||
        !plan.instance_bytes || total > std::numeric_limits<std::uint32_t>::max()) return false;
    for (const auto &f : plan.files) if (f.path.empty()) return false;
    for (const auto &link : plan.links)
        if (link.first >= plan.files.size() || link.second >= plan.files.size()) return false;
    plan_ = std::move(plan); shortage_ = shortage;
    resource_bytes_ = static_cast<std::uint32_t>(bytes);
    file_ = 0; polls_ = 0; cancelled_ = false; error_ = Error::None;
    stage_ = Stage::Allocate;
    return true;
}
void SceneResources::Clear() {
    if (allocated_) backend_.Destroy();
    allocated_ = false; in_flight_ = false; guard_.Reset();
    stage_ = Stage::Off; file_ = 0;
}
void SceneResources::Fail(Error error) { Clear(); error_ = error; stage_ = Stage::Failed; }
void SceneResources::Cancel() {
    cancelled_ = true;
    if (!in_flight_) Clear();
}
void SceneResources::Step() {
    if (stage_ == Stage::Off || stage_ == Stage::Failed) return;
    const auto current = backend_.Current();
    if (allocated_ && (!guard_.Matches(current.owner, current.room) || !(identity_ == current)))
        cancelled_ = true;
    if (cancelled_ && !in_flight_) { Clear(); return; }
    switch (stage_) {
    case Stage::Allocate: {
        if (!current.owner) return;
        const auto room = backend_.Room();
        const auto total = resource_bytes_ + plan_.instance_bytes + plan_.reserve_bytes;
        const auto largest = resource_bytes_ > plan_.instance_bytes ? resource_bytes_ : plan_.instance_bytes;
        if (!room.Allows(total, largest)) {
            error_ = Error::Space;
            if (shortage_ == Shortage::Stop) Fail(Error::Space);
            return;
        }
        if (!backend_.Create(plan_, resource_bytes_)) {
            // 部分確保も Backend が Destroy できる約束。
            backend_.Destroy();
            error_ = Error::Allocation;
            if (shortage_ == Shortage::Stop) stage_ = Stage::Failed;
            return;
        }
        allocated_ = true; identity_ = current;
        guard_.Capture(current.owner, current.room);
        error_ = Error::None; stage_ = Stage::Load;
        break;
    }
    case Stage::Load:
        in_flight_ = true;
        if (polls_ != std::numeric_limits<std::uint64_t>::max()) ++polls_;
        if (backend_.Load(file_) == 1) {
            in_flight_ = false;
            if (cancelled_) { Clear(); return; }
            if (++file_ == plan_.files.size()) { file_ = 0; stage_ = Stage::Setup; }
        }
        break;
    case Stage::Setup:
        if (!backend_.Setup(file_)) { Fail(Error::Setup); return; }
        if (++file_ == plan_.files.size()) stage_ = Stage::Find;
        break;
    case Stage::Find:
        if (!backend_.Find(plan_)) { Fail(Error::Model); return; }
        stage_ = Stage::Instance;
        break;
    case Stage::Instance:
        if (!backend_.Build()) { Fail(Error::Instance); return; }
        stage_ = Stage::Ready;
        break;
    default: break;
    }
}
bool SceneResources::Shutdown(unsigned polls) {
    Cancel();
    for (unsigned i = 0; in_flight_ && i < polls; ++i) {
        Step();
        if (in_flight_) backend_.Wait();
    }
    return !in_flight_;
}
} // namespace gohan::world
