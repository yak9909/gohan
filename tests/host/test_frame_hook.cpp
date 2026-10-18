#include "core/frame.hpp"
#include "game/frame_hook.hpp"
#include <CTRPluginFramework.hpp>
#include <cassert>
#include <string>

extern "C" void gohan_frame_callback() {}
extern "C" void gohan_dispatch_frame();
namespace {
unsigned calls = 0;
void Step() { ++calls; }
} // namespace

int main() {
    using CTRPluginFramework::Fake;
    using CTRPluginFramework::HookResult;
    const auto &feature = gohan::game::FrameHookFeature();
    assert(feature.stage == gohan::core::ShutdownStage::GameHooks);
    Fake::title = 0;
    assert(!feature.Wire());
    assert(Fake::reads == 0 && Fake::pauseCalls == 0 && Fake::initializeCalls == 0);
    assert(std::string(gohan::game::FrameHookStatus()).find("タイトル") != std::string::npos);

    Fake::title = 0x0004000000086200ULL;
    Fake::readable = false;
    assert(!feature.Wire() && !Fake::paused);
    assert(Fake::initializeCalls == 0 && Fake::pauseCalls == 1 && Fake::playCalls == 1);
    assert(std::string(gohan::game::FrameHookStatus()).find("読めない") != std::string::npos);

    Fake::readable = true;
    Fake::bytes[0] = 'x';
    assert(!feature.Wire());
    assert(Fake::initializeCalls == 0 && Fake::enableCalls == 0);
    assert(std::string(gohan::game::FrameHookStatus()).find("照合不一致") != std::string::npos);

    Fake::bytes[0] = '1';
    Fake::enableResult = HookResult::Failed;
    assert(!feature.Wire());
    assert(Fake::initializeCalls == 1 && Fake::enableCalls == 1 && !Fake::paused);
    assert(std::string(gohan::game::FrameHookStatus()).find("導入できません") != std::string::npos);

    Fake::enableResult = HookResult::Success;
    Fake::paused = true;
    const auto pauses = Fake::pauseCalls;
    const auto plays = Fake::playCalls;
    assert(feature.Wire());
    assert(Fake::pauseCalls == pauses && Fake::playCalls == plays && Fake::paused);
    assert(std::string(gohan::game::FrameHookStatus()).find("導入済み") != std::string::npos);
    const auto reads = Fake::reads;
    assert(feature.Wire() && Fake::reads == reads && Fake::enableCalls == 2);

    gohan::core::frame::Global().Add(Step);
    gohan_dispatch_frame();
    assert(calls == 1);
    feature.Shutdown();
    assert(Fake::disableCalls == 1 && Fake::paused);
    gohan_dispatch_frame();
    assert(calls == 1);
    assert(std::string(gohan::game::FrameHookStatus()).find("終了済み") != std::string::npos);
}
