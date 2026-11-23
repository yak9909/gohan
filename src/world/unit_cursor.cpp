#include "world/unit_cursor.hpp"
#include "world/scene_resources.hpp"
#include "world/instanced_draw.hpp"
#include "world/material_tint.hpp"
#include "world/fgobj_draw_hook.hpp"
#include "world/frame_order.hpp"
#include "game/scene_native.hpp"
#include "core/frame.hpp"
#include "core/sync.hpp"
#include <memory>
#include <vector>

namespace gohan::world {
namespace {
using Native = game::scene::Native;
struct Wanted {
    const void *owner = nullptr;
    bool shown = false;
    std::vector<UnitCursor::Position> positions;
    game::material::Colour colour;
};
struct Session {
    Native native;
    SceneResources resources{native};
    InstancedDraw drawer{native};
    MaterialTint tint;
    const void *owner = nullptr;
    game::scene::Identity identity{};
    std::vector<game::instance::Matrix> matrices;
    game::material::Colour colour;
    unsigned quiet = 0, drawn = 0;
};
core::SpinMutex requests, execution;
UnitCursor::Ownership ownership;
Wanted wanted;
UnitCursor::Report report;
std::unique_ptr<Session> session;
bool wired = false, registered = false, stopping = false;
static_assert(sizeof(Wanted) + sizeof(UnitCursor::Ownership) + sizeof(UnitCursor::Report) +
    sizeof(session) + 2 * sizeof(core::SpinMutex) + 3 <= core::limits::WorldResidentBytes);
void Step();
bool Register() {
    if (registered) return true;
    const auto change = core::frame::Global().Add(Step, order::Resources);
    registered = change.result == core::frame::Result::Queued || change.result == core::frame::Result::Unchanged;
    return registered;
}
void Draw(std::uintptr_t ctx, void *data) {
    if (!execution.TryLock()) return;
    auto &s = *static_cast<Session *>(data);
    game::material::View view;
    if (s.native.Material(view) && s.tint.Push(view, s.colour)) {
        s.drawn = s.drawer.Draw(ctx, s.matrices);
        s.tint.Restore();
    }
    execution.Unlock();
}
game::scene::Plan Plan() {
    game::scene::Plan p;
    p.files = {{"Ftr/Chip/UnitCursor.bcres", 16256}};
    p.model = "UnitCursor"; p.resource_bytes = core::limits::WorldResourceBytes;
    p.instance_bytes = core::limits::WorldInstanceBytes;
    p.reserve_bytes = core::limits::WorldParentReserveBytes; p.cursor_animations = true;
    return p;
}
void SaveReport() {
    core::LockGuard lock(requests);
    if (!session) { report.stage = 0; report.parent_bytes = 0; report.positions = 0; return; }
    report.stage = static_cast<unsigned>(session->resources.State());
    report.failure = static_cast<unsigned>(session->resources.Failure());
    report.load_polls = session->resources.LoadPolls();
    report.parent_bytes = session->resources.ParentBytes();
    report.positions = session->matrices.size(); report.drawn = session->drawn;
}
void Step() {
    if (!execution.TryLock()) return;
    Wanted desired;
    if (!requests.TryLock()) { execution.Unlock(); return; }
    desired = wanted;
    const bool stop = stopping;
    requests.Unlock();
    if (stop) { execution.Unlock(); return; }
    if (!session && desired.shown && desired.owner && FgobjDrawHook::Available())
        session = std::make_unique<Session>();
    if (session) {
        auto &s = *session;
        const bool same_scene = !s.owner || s.identity == s.native.Current();
        const bool want = desired.shown && desired.owner && (!s.owner || s.owner == desired.owner) &&
            same_scene && FgobjDrawHook::Available();
        if (!want) {
            FgobjDrawHook::Remove(&s);
            const auto stage = s.resources.State();
            const bool used = stage == SceneResources::Stage::Ready || stage == SceneResources::Stage::Find ||
                stage == SceneResources::Stage::Instance || stage == SceneResources::Stage::Setup;
            if (used && ++s.quiet < 4) { SaveReport(); execution.Unlock(); return; }
            s.resources.Cancel();
        } else s.quiet = 0;
        s.resources.Step();
        const auto stage = s.resources.State();
        if (want && stage == SceneResources::Stage::Off) {
            s.owner = desired.owner; s.identity = s.native.Current();
            s.resources.Start(Plan(), SceneResources::Shortage::Retry);
        } else if (want && stage == SceneResources::Stage::Ready) {
            s.matrices.resize(desired.positions.size());
            for (unsigned i = 0; i < desired.positions.size(); ++i)
                Native::PositionMatrix(desired.positions[i], s.matrices[i]);
            s.colour = desired.colour;
            s.native.Animate();
            FgobjDrawHook::Add(&s, FgobjDrawHook::Where::Before, Draw, &s);
        }
        SaveReport();
        if (stage == SceneResources::Stage::Off && !want) session.reset();
    }
    if (!session && !desired.shown) {
        const auto change = core::frame::Global().Remove(Step);
        if (change.result == core::frame::Result::Queued || change.result == core::frame::Result::Unchanged) {
            core::LockGuard lock(requests);
            // Remove と次の要求が競合したら、希望に合わせて登録を戻す。
            registered = false;
            if (wanted.shown) Register();
        }
    }
    execution.Unlock();
}
bool Wire() { core::LockGuard lock(requests); stopping = false; wired = true; return true; }
void Shutdown() {
    core::LockGuard run(execution);
    { core::LockGuard lock(requests); stopping = true; wired = false; wanted = {}; }
    if (session) {
        Native::Pause();
        FgobjDrawHook::Remove(session.get());
        Native::Resume();
        // 描いたコマンドが使い終わるまで4フレーム相当。I/O は別の読み込み係が進める。
        if (!session->resources.Loading()) for (unsigned i = 0; i < 4; ++i) session->native.Wait();
        const bool drained = session->resources.Shutdown(core::limits::WorldShutdownPolls);
        SaveReport();
        if (!drained) {
            core::LockGuard lock(requests);
            report.retained_on_shutdown = true;
            // holder、パス、allocator も含む Session 全体を残す。
            session.release();
        } else session.reset();
    }
    core::frame::Global().Remove(Step);
    registered = false;
}
}
bool UnitCursor::Acquire(const void *owner) {
    core::LockGuard lock(requests);
    if (!wired || stopping || !ownership.Acquire(owner)) return false;
    wanted.owner = owner; return true;
}
bool UnitCursor::Release(const void *owner) {
    core::LockGuard lock(requests);
    if (!ownership.Release(owner)) return false;
    wanted = {}; return true;
}
bool UnitCursor::Desired(const void *owner, bool shown, std::span<const Position> positions, game::material::Colour colour) {
    core::LockGuard lock(requests);
    if (!wired || stopping || !ownership.Is(owner) || positions.size() > core::limits::WorldPositions) return false;
    if (shown && !Register()) return false;
    wanted.owner = owner; wanted.shown = shown; wanted.colour = colour;
    wanted.positions.assign(positions.begin(), positions.end()); return true;
}
bool UnitCursor::Busy(const void *owner) { core::LockGuard lock(requests); return ownership.Get() && !ownership.Is(owner); }
bool UnitCursor::PlayerPosition(Position &position) { return Native::PlayerPosition(position); }
UnitCursor::Report UnitCursor::Status() { core::LockGuard lock(requests); return report; }
const core::Feature &UnitCursor::Feature() {
    static const core::Feature f{"unit_cursor", Wire, Shutdown, core::ShutdownStage::DrawHooks}; return f;
}
}
