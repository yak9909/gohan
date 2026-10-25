#include "core/input_block.hpp"
#include "game/generated/api.hpp"
#include "game/input_filter.hpp"
#include "game/input_hook.hpp"
#include <CTRPluginFramework.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace gohan::core {
struct MailboxTestAccess { template <class T> static SpinMutex &Mutex(Mailbox<T> &box) { return box.mutex_; } };
}
namespace gohan::core::input_block {
struct TableTestAccess { static SpinMutex &Publication(Table &t) { return MailboxTestAccess::Mutex(t.published_); } };
}

using Pad = gohan::game::generated::AcnlPadState;
using GameController = gohan::game::generated::SeadControllerBasePrefix;
extern "C" void gohan_input_pad(Pad *);
extern "C" void gohan_input_controller(GameController *, std::uint32_t *);
// PC ではアセンブラ入口の代わりに同じ引数を渡す。ARM 入口は別途検査する。
extern "C" void gohan_input_pad_callback(Pad *pad) { gohan_input_pad(pad); }
extern "C" void gohan_input_controller_callback(GameController *controller, std::uint32_t *old) {
    gohan_input_controller(controller, old);
}

namespace {
using namespace gohan;
using namespace CTRPluginFramework;
namespace generated = game::generated;
namespace block = core::input_block;
int pauses = 0, resumes = 0;
void Previous(bool pause) { pause ? ++pauses : ++resumes; }
std::string Status() {
    char text[core::limits::StatusText]; core::TextWriter writer(text, sizeof(text));
    game::AppendInputStatus(writer); assert(writer.Complete()); return text;
}
void Stop() { game::InputPadFeature().Shutdown(); game::InputControllerFeature().Shutdown(); }
void Reset() {
    Stop();
    Fake::title = 0x0004000000086200ULL;
    Fake::readable = true; Fake::unreadable = Fake::failEnable = 0;
    Fake::memory = {{generated::InputPadRead_addr, "123456789"}, {generated::InputControllerCalc_addr, "123456789"}};
    Fake::enableResult = HookResult::Success;
}
void InstallCases() {
    for (bool fail_pad : {false, true}) {
        const auto bad_address = fail_pad ? generated::InputPadRead_addr : generated::InputControllerCalc_addr;
        for (int kind = 0; kind < 4; ++kind) {
            Reset();
            if (kind == 0) Fake::title = 0;
            if (kind == 1) Fake::unreadable = bad_address;
            if (kind == 2) Fake::memory[bad_address][0] = 'x';
            if (kind == 3) Fake::failEnable = bad_address;
            const bool p = game::InputPadFeature().Wire(), c = game::InputControllerFeature().Wire();
            assert(p == (kind != 0 && !fail_pad));
            assert(c == (kind != 0 && fail_pad));
            const char *reason[] = {"タイトル違い", "読めない", "照合不一致", "導入失敗"};
            assert(Status().find(reason[kind]) != std::string::npos);
            assert(game::InputControllerReady() == c);
        }
    }
    Reset();
    Fake::paused = true;
    const auto play_count = Fake::playCalls;
    assert(game::InputPadFeature().Wire() && game::InputControllerFeature().Wire());
    assert(Fake::paused && Fake::playCalls == play_count);
    assert(Fake::hooks.at(generated::InputPadRead_addr).flags == HOOK_DEFAULT_PARAMS);
    assert(Fake::hooks.at(generated::InputControllerCalc_addr).flags == (USE_LR_TO_RETURN | EXECUTE_OI_AFTER_CB));
    const auto read_count = Fake::reads;
    assert(game::InputPadFeature().Wire() && game::InputControllerFeature().Wire());
    assert(Fake::reads == read_count);
    Stop(); Stop();
    assert(!Fake::hooks.at(generated::InputPadRead_addr).enabled && !Fake::hooks.at(generated::InputControllerCalc_addr).enabled);
    Fake::paused = false;
}
void ControllerCall(GameController &c, std::uint32_t (&old)[2]) {
    Fake::Invoke(generated::InputControllerCalc_addr, &c, old);
}
GameController Sample() {
    GameController c;
    std::memset(&c, 0x5A, sizeof(c));
    c.vtable = reinterpret_cast<void *>(generated::vtbl_sead_CtrController_addr);
    return c;
}
void OnlyFields() {
    Reset();
    block::Owner owner; assert(block::Global().Acquire("hook-test", owner) == block::Result::Ok);
    assert(block::Global().Set(owner, {block::group::Everything, 0}) == block::Result::Ok);
    // 導入前の要求も最初の呼び出しから効く。
    assert(game::InputPadFeature().Wire() && game::InputControllerFeature().Wire());
    Pad pad; std::memset(&pad, 0xFF, sizeof(pad));
    auto expected_pad = pad;
    expected_pad.held &= ~4u; expected_pad.pressed &= ~4u; expected_pad.released &= ~4u;
    Fake::Invoke(generated::InputPadRead_addr, &pad);
    assert(std::memcmp(&pad, &expected_pad, sizeof(pad)) == 0);
    // SELECT の水準を外して、全区分の要求そのものを検査する。
    pad.held = 0; Fake::Invoke(generated::InputPadRead_addr, &pad);
    struct Guarded { std::array<unsigned char, 32> before; GameController value; std::array<unsigned char, 32> after; };
    Guarded guarded;
    std::memset(&guarded, 0xB6, sizeof(guarded));
    guarded.value = Sample();
    auto &c = guarded.value;
    c.held = 0xFFFFFFFFu;
    c.pointer_flags = 0xB;
    auto expected_guard = guarded;
    auto &expected = expected_guard.value;
    std::uint32_t old[] = {c.held, 0xD};
    expected.held &= ~game::input_filter::HeldBits(block::group::Everything | block::part::Select);
    expected.pointer_flags &= ~1u;
    expected.axis0.x = expected.axis0.y = 0;
    ControllerCall(c, old);
    assert(std::memcmp(&guarded, &expected_guard, sizeof(guarded)) == 0);
    assert(old[0] == expected.held && old[1] == 0xC); // R9 のほかの旗は保存。
    auto wrong = Sample(); wrong.vtable = nullptr;
    const auto before = wrong;
    std::uint32_t untouched[] = {123, 13};
    ControllerCall(wrong, untouched);
    assert(std::memcmp(&wrong, &before, sizeof(wrong)) == 0 && untouched[0] == 123 && untouched[1] == 13);
    assert(block::Global().Release(owner) == block::Result::Ok);
    Stop();
}
void ResumeSequence() {
    Reset(); Process::OnPauseResume = Previous;
    assert(game::InputPadFeature().Wire() && game::InputControllerFeature().Wire());
    auto c = Sample(); c.held = 0; c.pointer_flags = 0;
    std::uint32_t old[] = {0, 0};
    ControllerCall(c, old);
    game::InputCtrpfOpening();
    ControllerCall(c, old); // 開く許可後・停止前。ここで通知を消費してはいけない。
    assert(!game::InputTakeCtrpfResumed()); // 停止前・停止中は、メニューにも再開の知らせは無い。
    Process::Pause(); assert(!game::InputTakeCtrpfResumed());
    Process::Play(); // OnClosing は呼ばない（B で閉じた場合）。
    auto &publication = block::TableTestAccess::Publication(block::Global());
    publication.Lock(); // 表の読み取りや状態の公開の成功は、再開通知を受け取る条件にしない。
    c.held = 2; old[0] = 0; ControllerCall(c, old);
    publication.Unlock();
    assert(block::Global().ActivityCounts().reads != 0);
    assert(c.held == 0 && old[0] == 0); // SELECT 無し・時間差無しでも再開の B は隠す。
    // 入力の計算が先に受け取っても、メニューの知らせは残る（別の読み手）。一度だけ。
    assert(game::InputTakeCtrpfResumed() && !game::InputTakeCtrpfResumed());
    c.held = 0; ControllerCall(c, old);
    c.held = 2; ControllerCall(c, old); assert(c.held == 2);
    // 逆の順: メニューが先に受け取っても、入力の計算の知らせは残る。
    c.held = 0; ControllerCall(c, old);
    game::InputCtrpfOpening(); Process::Pause(); Process::Play();
    assert(game::InputTakeCtrpfResumed());
    c.held = 2; ControllerCall(c, old); assert(c.held == 0);
    c.held = 0; ControllerCall(c, old);
    // 別の停止再開では、開く許可が無ければ掛からない。
    c.held = 0; ControllerCall(c, old);
    Process::Pause(); Process::Play();
    assert(!game::InputTakeCtrpfResumed());
    c.held = 2; ControllerCall(c, old); assert(c.held == 2);
    // 再開の知らせを残したまま再導入しても持ち越さない。
    game::InputCtrpfOpening(); Process::Pause(); Process::Play();
    Stop(); assert(Process::OnPauseResume == Previous);
    assert(!game::InputTakeCtrpfResumed());
    // 入力の計算が入っていない間は、開く許可を記憶せず、再開の知らせも出ない。
    game::InputCtrpfOpening(); Process::Pause(); Process::Play();
    assert(!game::InputTakeCtrpfResumed());
    assert(game::InputControllerFeature().Wire());
    assert(!game::InputTakeCtrpfResumed());
    c.held = 2; old[0] = 0; ControllerCall(c, old); assert(c.held == 2);
    assert(pauses && resumes);
    // 30 呼び出しごとの開発版の公開と、容量を確認する。
    for (int i = 0; i < 30; ++i) ControllerCall(c, old);
    assert(Status().find("呼出") != std::string::npos);
    Stop(); Process::OnPauseResume = nullptr;
}
void ReinstallAndCapacity() {
    Reset();
    for (int i = 0; i < 100; ++i) {
        game::ToggleInputHooks(); assert(game::InputControllerReady());
        game::ToggleInputHooks(); assert(!game::InputControllerReady());
        assert(Process::OnPauseResume == nullptr);
    }
    std::array<block::Owner, core::limits::InputBlockOwners - 1> owners;
    for (auto &owner : owners) {
        assert(block::Global().Acquire("owner", owner) == block::Result::Ok);
        assert(block::Global().Set(owner, {block::group::Everything, block::group::Buttons}) == block::Result::Ok);
    }
    const auto text = Status(); // 全16枠を列挙しても容量内。
    assert(text.find("owner") != std::string::npos && text.find("スレッド") != std::string::npos);
    for (auto &owner : owners) assert(block::Global().Release(owner) == block::Result::Ok);
}
} // namespace
int main() { InstallCases(); OnlyFields(); ResumeSequence(); ReinstallAndCapacity(); std::puts("input hook: ok"); }
