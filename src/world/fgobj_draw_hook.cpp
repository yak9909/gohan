#include "world/fgobj_draw_hook.hpp"
#include "world/frame_order.hpp"
#include "core/frame.hpp"
#include "game/draw_slots.hpp"
#include <array>
namespace gohan::world {
namespace {
struct Entry { const void *owner = nullptr; FgobjDrawHook::Where where{}; FgobjDrawHook::Draw draw = nullptr; void *data = nullptr; };
std::array<Entry, 4> entries;
game::draw::Pair pair;
bool wired = false, registered = false;
void Deliver(std::uintptr_t ctx, FgobjDrawHook::Where where) {
    for (auto e : entries) if (e.owner && e.where == where) e.draw(ctx, e.data);
}
void First(std::uintptr_t ctx, std::uintptr_t data) {
    Deliver(ctx, FgobjDrawHook::Where::Before);
    game::draw::Original0()(ctx, data);
    Deliver(ctx, FgobjDrawHook::Where::After);
}
void Second(std::uintptr_t ctx, std::uintptr_t data) {
    Deliver(ctx, FgobjDrawHook::Where::BeforeLayer1);
    game::draw::Original1()(ctx, data);
}
void Step() {
    const auto slots = game::draw::CurrentSlots();
    if (pair.Owner() && !pair.Matches(slots)) pair.Abandon();
    bool any = false;
    for (auto e : entries) any = any || e.owner;
    if (any && slots.owner) {
        pair.Install(slots, reinterpret_cast<std::uintptr_t>(game::draw::Original0()),
            reinterpret_cast<std::uintptr_t>(game::draw::Original1()),
            reinterpret_cast<std::uintptr_t>(&First), reinterpret_cast<std::uintptr_t>(&Second));
    } else if (!any && pair.Restore()) {
        const auto removed = core::frame::Global().Remove(Step);
        if (removed.result == core::frame::Result::Queued || removed.result == core::frame::Result::Unchanged)
            registered = false;
    }
}
bool Register() {
    if (registered) return true;
    const auto r = core::frame::Global().Add(Step, order::Draw);
    registered = r.result == core::frame::Result::Queued || r.result == core::frame::Result::Unchanged;
    return registered;
}
bool Wire() { wired = game::draw::Wire(); return wired; }
void Shutdown() {
    entries = {};
    // このサービスのラッパーは静的寿命。他者が保持していても呼び出し先を失わない。
    Step();
    game::draw::Unwire(); wired = false;
}
}
bool FgobjDrawHook::Available() { return wired && game::draw::CurrentSlots().owner; }
bool FgobjDrawHook::Add(const void *owner, Where where, Draw draw, void *data) {
    if (!owner || !draw || !Available() || !Register()) return false;
    auto slots = game::draw::CurrentSlots();
    if (pair.Owner() && !pair.Matches(slots)) pair.Abandon();
    if (!pair.Install(slots, reinterpret_cast<std::uintptr_t>(game::draw::Original0()),
        reinterpret_cast<std::uintptr_t>(game::draw::Original1()),
        reinterpret_cast<std::uintptr_t>(&First), reinterpret_cast<std::uintptr_t>(&Second))) return false;
    for (auto &e : entries) if (e.owner == owner) { e = {owner, where, draw, data}; return true; }
    for (auto &e : entries) if (!e.owner) { e = {owner, where, draw, data}; return true; }
    return false;
}
void FgobjDrawHook::Remove(const void *owner) {
    for (auto &e : entries) if (e.owner == owner) e = {};
    Step();
}
const core::Feature &FgobjDrawHook::Feature() {
    static const core::Feature f{"fgobj_draw", Wire, Shutdown, core::ShutdownStage::DrawHooks}; return f;
}
}
