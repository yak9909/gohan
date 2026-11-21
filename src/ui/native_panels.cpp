#include "ui/native_panels.hpp"
#include "ui/input/buttons.hpp"
#include "core/frame.hpp"
#include "core/input_block.hpp"
#include <CTRPluginFramework.hpp>
namespace gohan::ui::native_panels {
namespace {
lists::CatalogList catalog;
GameLabel labels;
core::input_block::Owner input_owner;
bool wired = false;
bool CleanCatalog() { return catalog.Cleanup(); }
bool CleanLabels() { return labels.Cleanup(); }
bool CleanLease() { return true; }
void Step() {
    const auto state = bottom_screen_lease::Status();
    auto input = ui::input::Read();
    if (CTRPluginFramework::GuiMenu::IsVisible()) input = {};
    unsigned dirs = (input.buttons >> 4) & 15;
    catalog.Step(state, dirs, ui::input::NowMs(), input.touching, input.x, input.y,
                 (input.buttons & ui::input::button::A) != 0);
    labels.Step(state);
    core::input_block::Global().Set(input_owner, {catalog.Drained() ? 0u : core::input_block::group::Everything, 0});
}
void StopFeature() { Shutdown(); }
}
lists::CatalogList &Catalog() { return catalog; }
GameLabel &Labels() { return labels; }
bool LeaseOnly(bool shown) {
    if (shown) return bottom_screen_lease::Acquire(4);
    return bottom_screen_lease::Release(4) && bottom_screen_lease::ResourcesReturned(4);
}
bool Wire() {
    if (wired) return true;
    if (!bottom_screen_lease::ConfigureCleanup(0, CleanCatalog) ||
        !bottom_screen_lease::ConfigureCleanup(1, CleanLabels) ||
        !bottom_screen_lease::ConfigureCleanup(2, CleanLease)) return false;
    if (!game::CatalogContent::Wire()) return false;
    if (!bottom_screen_lease::Wire()) { game::CatalogContent::Shutdown(); return false; }
    if (core::input_block::Global().Acquire("native catalog", input_owner) != core::input_block::Result::Ok) {
        bottom_screen_lease::Shutdown(); game::CatalogContent::Shutdown(); return false;
    }
    const auto change = core::frame::Global().Add(Step, 120);
    wired = change.result == core::frame::Result::Queued || change.result == core::frame::Result::Unchanged;
    if (!wired) {
        bottom_screen_lease::Shutdown(); game::CatalogContent::Shutdown();
        core::input_block::Global().Release(input_owner);
    }
    return wired;
}
bool Shutdown() {
    catalog.Stop(); labels.Stop();
    const bool drained = bottom_screen_lease::Shutdown();
    if (!drained || !catalog.Drained() || !labels.Drained()) return false;
    game::CatalogContent::Shutdown();
    if (wired) {
        const auto removed = core::frame::Global().Remove(Step);
        for (unsigned n = 0; !core::frame::Global().IsApplied(removed.revision); ++n) {
            if (n >= 200) return false;
            game::BottomScreenWait(10);
        }
    }
    core::input_block::Global().Release(input_owner); wired = false; return true;
}
const core::Feature &Feature() {
    static const core::Feature f{"ui.native_panels", Wire, StopFeature, core::ShutdownStage::GameHooks}; return f;
}
}
