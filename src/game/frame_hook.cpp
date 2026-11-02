#include "game/frame_hook.hpp"
#include "core/frame.hpp"
#include "core/mailbox.hpp"
#include "game/generated/api.hpp"
#include "game/frame_timing.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdint>

extern "C" void gohan_frame_callback();
extern "C" void gohan_dispatch_frame() {
    namespace timing = gohan::game::frame_timing;
    timing::Begin();
    const timing::Scope watch(timing::Stage::Total);
    gohan::core::frame::Dispatch();
}

namespace gohan::game {
namespace {
using CTRPluginFramework::Hook;
using CTRPluginFramework::HookResult;
using CTRPluginFramework::Process;

enum class Status { Waiting, Ready, WrongTitle, Unreadable, Mismatch, Failed, Stopped };
core::Mailbox<Status> status;
Hook hook;

// 停止した状態で照合から書き換えまでを済ませる。既存の停止は解除しない。
class PauseGuard {
  public:
    PauseGuard() : resume_(!Process::IsPaused()) {
        if (resume_)
            Process::Pause();
    }
    ~PauseGuard() {
        if (resume_)
            Process::Play();
    }

  private:
    bool resume_;
};

bool Fail(Status reason) {
    status.TryPublish(reason);
    return false;
}

bool Wire() {
    if (hook.IsEnabled())
        return true;
    if (Process::GetTitleID() != 0x0004000000086200ULL)
        return Fail(Status::WrongTitle);
    PauseGuard pause;
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < generated::FrameDispatch_len; ++i) {
        std::uint8_t byte;
        if (!Process::Read8(generated::FrameDispatch_addr + i, byte))
            return Fail(Status::Unreadable);
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
    }
    if ((crc ^ 0xFFFFFFFFu) != generated::FrameDispatch_crc32)
        return Fail(Status::Mismatch);
    // 元の命令は Hook が実機メモリから保存し、解除時に復元する。
    hook.Initialize(generated::FrameDispatch_addr,
                    reinterpret_cast<std::uintptr_t>(&gohan_frame_callback));
    if (hook.Enable() != HookResult::Success)
        return Fail(Status::Failed);
    status.TryPublish(Status::Ready);
    return true;
}

void Shutdown() {
    PauseGuard pause;
    hook.Disable();
    core::frame::Global().Reset();
    status.TryPublish(Status::Stopped);
}

const core::Feature feature{"game.frame", Wire, Shutdown, core::ShutdownStage::GameHooks};
} // namespace

const core::Feature &FrameHookFeature() { return feature; }

const char *FrameHookStatus() {
    // この読み手はメニューのスレッドだけ。
    static Status last = Status::Waiting;
    static std::uint32_t generation = 0;
    status.ReadIfNew(last, generation);
    switch (last) {
    case Status::Ready:
        return "毎フレームの接続: 照合済み・導入済み";
    case Status::WrongTitle:
        return "毎フレームの接続: 対応するタイトルではありません";
    case Status::Unreadable:
        return "毎フレームの接続: メモリを読めないため導入しません";
    case Status::Mismatch:
        return "毎フレームの接続: 照合不一致のため導入しません";
    case Status::Failed:
        return "毎フレームの接続: 導入できませんでした";
    case Status::Stopped:
        return "毎フレームの接続: 終了済み";
    case Status::Waiting:
        return "毎フレームの接続: 未導入";
    }
    return "毎フレームの接続: 不明";
}
} // namespace gohan::game
