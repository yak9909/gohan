#include "ui/renderer/renderer.hpp"
#include "core/frame.hpp"
#include "core/mailbox.hpp"
#include "game/lyt/renderer.hpp"

namespace gohan::ui::renderer {
namespace {
core::Mailbox<bool> requested;
core::Mailbox<Drawing> frames;
core::Mailbox<Status> published;
Drawing drawing;
std::uint32_t requestGeneration = 0, frameGeneration = 0;
Status state = Status::Stopped;

Status Convert(game::lyt::InstallResult result) {
    using game::lyt::InstallResult;
    switch (result) {
    case InstallResult::Ready: return Status::Ready;
    case InstallResult::WrongTitle: return Status::WrongTitle;
    case InstallResult::Unreadable: return Status::Unreadable;
    case InstallResult::Mismatch: return Status::Mismatch;
    case InstallResult::MissingBindings: return Status::MissingBindings;
    }
    return Status::MissingBindings;
}

void FrameStep() {
    bool enable = false;
    if (requested.ReadIfNew(enable, requestGeneration) == core::MailRead::Updated) {
        if (!enable) {
            game::lyt::DetachRenderer();
            state = Status::Stopped;
        } else if (state != Status::Ready) {
            state = Convert(game::lyt::InstallRenderer(Fonts()));
        }
    }
    frames.ReadIfNew(drawing, frameGeneration);
    if (state == Status::Ready && !game::lyt::Present(drawing)) {
        game::lyt::DetachRenderer();
        state = Status::PresentFailed;
    }
    published.TryPublish(state);
}

bool Wire() {
    const auto change = core::frame::Global().Add(FrameStep, 100);
    if (change.result != core::frame::Result::Queued &&
        change.result != core::frame::Result::Unchanged)
        return false;
    published.TryPublish(Status::Waiting);
    return true;
}

void Shutdown() {
    // 現在の game 側は導入を拒否し、資源を所有しない。
    // 実接続を足す段階では、ゲームのフック停止前に資源の排出確認が必要。
    core::frame::Global().Remove(FrameStep);
    published.TryPublish(Status::Stopped);
}
const core::Feature feature{"ui.renderer", Wire, Shutdown, core::ShutdownStage::DrawHooks};
} // namespace

bool SetEnabled(bool enabled) { return requested.TryPublish(enabled); }
bool Commit(const Canvas &canvas) { return frames.TryPublish(canvas.Data()); }
Status ReadStatus() {
    // 読み手はメニューのスレッド一つだけ。
    static Status last = Status::Stopped;
    static std::uint32_t generation = 0;
    published.ReadIfNew(last, generation);
    return last;
}
const char *StatusText() {
    switch (ReadStatus()) {
    case Status::Stopped: return "描画: 停止";
    case Status::Waiting: return "描画: 毎フレームの要求待ち";
    case Status::Ready: return "描画: 導入済み";
    case Status::WrongTitle: return "描画: 対応するタイトルではありません";
    case Status::Unreadable: return "描画: 照合するメモリを読めません";
    case Status::Mismatch: return "描画: 照合不一致のため導入しません";
    case Status::MissingBindings: return "描画: 必要な型・大域変数が未公開のため導入しません";
    case Status::PresentFailed: return "描画: 確定できませんでした";
    }
    return "描画: 不明";
}
const core::Feature &RendererFeature() { return feature; }
} // namespace gohan::ui::renderer
