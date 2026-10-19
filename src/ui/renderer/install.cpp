#include "ui/renderer/renderer.hpp"
#include "core/frame.hpp"
#include "core/mailbox.hpp"
#include "game/lyt/renderer.hpp"

namespace gohan::ui::renderer {
namespace {
using game::lyt::ScreenDrawing;
constexpr unsigned ClientCount = static_cast<unsigned>(Client::Count);
// 終了のとき、ゲームの描画スレッドが排出を終えるのを待つ上限（ミリ秒）。
constexpr unsigned ShutdownWaitMs = 2000;
constexpr unsigned ShutdownPollMs = 10;

core::Mailbox<bool> requests[ClientCount];
core::Mailbox<ScreenDrawing> frames[2];
core::Mailbox<Status> published;
// 以下はゲームの描画スレッド（毎フレームの処理）だけが触る。
bool wanted[ClientCount];
std::uint32_t requestGeneration[ClientCount], frameGeneration[2];
Drawing drawing;
Status state = Status::Stopped;
// 取り外しの排出が終わったあとの状態。
Status afterStop = Status::Stopped;

Status Convert(game::lyt::InstallResult result) {
    using game::lyt::InstallResult;
    switch (result) {
    case InstallResult::Ready: return Status::Ready;
    case InstallResult::WrongTitle: return Status::WrongTitle;
    case InstallResult::Unreadable: return Status::Unreadable;
    case InstallResult::Mismatch: return Status::Mismatch;
    case InstallResult::MissingBindings: return Status::MissingBindings;
    case InstallResult::ResourceFailed: return Status::ResourceFailed;
    }
    return Status::MissingBindings;
}

bool AnyWanted() {
    for (const bool value : wanted)
        if (value)
            return true;
    return false;
}

void BeginStop(Status then) {
    state = Status::Stopping;
    afterStop = then;
}

void FrameStep() {
    bool changed = false;
    for (unsigned i = 0; i < ClientCount; ++i) {
        bool value = false;
        if (requests[i].ReadIfNew(value, requestGeneration[i]) == core::MailRead::Updated) {
            wanted[i] = value;
            changed = true;
        }
    }
    // 排出中は毎フレーム呼ぶ。済んだら、まだ誰かが求めていて失敗でなければ導入し直す。
    if (state == Status::Stopping && game::lyt::DetachRenderer()) {
        state = afterStop;
        if (state == Status::Stopped && AnyWanted())
            state = Convert(game::lyt::InstallRenderer(Fonts()));
    } else if (state != Status::Stopping && changed) {
        if (!AnyWanted()) {
            if (state == Status::Ready)
                BeginStop(Status::Stopped);
            else
                state = Status::Stopped;
        } else if (state != Status::Ready) {
            state = Convert(game::lyt::InstallRenderer(Fonts()));
        }
    }
    for (unsigned i = 0; i < 2; ++i)
        if (frames[i].ReadIfNew(drawing.screens[i], frameGeneration[i]) == core::MailRead::Updated)
            ++drawing.revisions[i];
    if (state == Status::Ready && !game::lyt::Present(drawing))
        BeginStop(Status::PresentFailed);
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

// ゲームの毎フレームのフックを止める前に、資源を排出して返す。
// そのため、この機能の終了の段は「ゲームのフック」とし、フックの機能より後に登録する。
void Shutdown() {
    for (unsigned i = 0; i < ClientCount; ++i)
        for (unsigned tries = 0; tries < 100 && !requests[i].TryPublish(false); ++tries)
            game::lyt::WaitMilliseconds(1);
    // 描画スレッドが動いている間だけ待つ。導入していなければ状態は動かない。
    Status last = Status::Waiting;
    std::uint32_t generation = 0;
    for (unsigned waited = 0; waited <= ShutdownWaitMs; waited += ShutdownPollMs) {
        published.ReadIfNew(last, generation);
        if (last != Status::Ready && last != Status::Stopping)
            break;
        game::lyt::WaitMilliseconds(ShutdownPollMs);
    }
    core::frame::Global().Remove(FrameStep);
    published.TryPublish(Status::Stopped);
}
const core::Feature feature{"ui.renderer", Wire, Shutdown, core::ShutdownStage::GameHooks};
} // namespace

bool SetEnabled(bool enabled) { return SetEnabled(Client::Default, enabled); }
bool SetEnabled(Client client, bool enabled) {
    return requests[static_cast<unsigned>(client)].TryPublish(enabled);
}
bool Commit(const Canvas &canvas) {
    const bool top = Commit(canvas, Screen::Top);
    const bool bottom = Commit(canvas, Screen::Bottom);
    return top && bottom;
}
bool Commit(const Canvas &canvas, Screen screen) {
    const unsigned index = screen == Screen::Bottom ? 1 : 0;
    return frames[index].TryPublish(canvas.Data().screens[index]);
}
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
    case Status::MissingBindings: return "描画: 必要なゲームの欄が使えないため導入しません";
    case Status::PresentFailed: return "描画: 確定できませんでした";
    case Status::ResourceFailed: return "描画: ゲームの資源を確保できないため導入しません";
    case Status::Stopping: return "描画: 取り外し中";
    }
    return "描画: 不明";
}
const core::Feature &RendererFeature() { return feature; }
} // namespace gohan::ui::renderer
