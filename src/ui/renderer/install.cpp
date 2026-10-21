#include "ui/renderer/renderer.hpp"
#include "core/frame.hpp"
#include "ui/renderer/selection_frames.hpp"
#include "core/mailbox.hpp"
#include "game/lyt/renderer.hpp"
#include <cstdio>

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
core::Mailbox<game::lyt::BackgroundAllocation> publishedBackground;
core::Mailbox<game::lyt::RendererMemory> publishedMemory;
core::Mailbox<detail::SelectionTiming> publishedTiming;
game::lyt::BackgroundAllocation backgroundAllocation;
// 以下はゲームの描画スレッド（毎フレームの処理）だけが触る。
bool wanted[ClientCount];
std::uint32_t requestGeneration[ClientCount], frameGeneration[2];
Drawing drawing;
Status state = Status::Stopped;
// 取り外しの排出が終わったあとの状態。
Status afterStop = Status::Stopped;
std::uint32_t animationFrame = 0;

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
            state = Convert(game::lyt::InstallRenderer(Fonts(), &backgroundAllocation));
    } else if (state != Status::Stopping && changed) {
        if (!AnyWanted()) {
            if (state == Status::Ready)
                BeginStop(Status::Stopped);
            else
                state = Status::Stopped;
        } else if (state != Status::Ready) {
            state = Convert(game::lyt::InstallRenderer(Fonts(), &backgroundAllocation));
        }
    }
    for (unsigned i = 0; i < 2; ++i)
        if (frames[i].ReadIfNew(drawing.screens[i], frameGeneration[i]) == core::MailRead::Updated)
            ++drawing.revisions[i];
    if (state == Status::Ready) StepTitleAnimation(animationFrame, drawing);
    ++animationFrame;
    if (state == Status::Ready && !game::lyt::Present(drawing))
        BeginStop(Status::PresentFailed);
    if (changed || animationFrame % core::limits::RendererHeapSampleFrames == 0 || state == Status::Stopping) {
        const auto memory = game::lyt::ReadRendererMemory();
        publishedMemory.TryPublish(memory);
        publishedTiming.TryPublish(detail::ReadSelectionTiming());
        backgroundAllocation = memory.background;
    }
    published.TryPublish(state);
    publishedBackground.TryPublish(backgroundAllocation);
}

bool Wire() {
    PrepareTitleBackground();
    const auto change = core::frame::Global().Add(FrameStep, 100);
    if (change.result != core::frame::Result::Queued &&
        change.result != core::frame::Result::Unchanged)
        return false;
    StartTitleAnimation();
    published.TryPublish(Status::Waiting);
    return true;
}

// ゲームの毎フレームのフックを止める前に、資源を排出して返す。
// そのため、この機能の終了の段は「ゲームのフック」とし、フックの機能より後に登録する。
void Shutdown() {
    StopTitleAnimation();
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
const char *BackgroundAllocationStatusText() {
    static game::lyt::BackgroundAllocation last;
    static std::uint32_t generation = 0;
    static char text[384];
    publishedBackground.ReadIfNew(last, generation);
    using game::lyt::BackgroundResult;
    const char *result = "未試行";
    switch (last.result) {
    case BackgroundResult::NotAttempted: break;
    case BackgroundResult::Ready: result = "成功"; break;
    case BackgroundResult::HeapUnavailable: result = "ヒープを確認できない"; break;
    case BackgroundResult::InsufficientSpace: result = "常駐画像を確保できる空きがない"; break;
    case BackgroundResult::AllocateFailed: result = "失敗"; break;
    case BackgroundResult::FlushFailed: result = "排出失敗"; break;
    case BackgroundResult::BlockTooSmall: result = "連続した空き不足"; break;
    case BackgroundResult::BudgetExceeded: result = "借用の予算超過"; break;
    }
    if (last.measured)
        std::snprintf(text, sizeof(text), "全画像の常駐: %s\n確保前の空き: 合計 %u / 最大の塊 %u バイト",
                      result, last.freeBytes, last.largestBlock);
    else
        std::snprintf(text, sizeof(text), "全画像の常駐: %s\n確保前の空き: 未計測", result);
    return text;
}
const char *MemoryStatusText() {
    static game::lyt::RendererMemory last;
    static std::uint32_t generation = 0;
    static char text[1280];
    publishedMemory.ReadIfNew(last, generation);
    static detail::SelectionTiming timing;
    static std::uint32_t timingGeneration = 0;
    publishedTiming.ReadIfNew(timing, timingGeneration);
    const auto &a = last.before, &b = last.after, &c = last.current;
    std::snprintf(text, sizeof(text),
                  "描画資源のヒープ（バイト）\n確保直前: 合計 %u / 最大 %u %s\n確保直後: 合計 %u / 最大 %u %s\n現在: 合計 %u / 最大 %u %s\n字形 %u / UI（破線込み）%u / 窓 %u\n帯 %u（%u 面 × %u）\n常駐: 画像 %u / 管理分込み上限 %u / 予算 %u\nCPU用: プラグインのヒープ %u / 帯の%uコマ（常駐）%u\n別の描画命令用ヒープ: 記録列等 %u\n帯: 導入時の合成 %u us（%u コマ）\n帯の毎フレーム（コピー＋排出）us: 今 %u / 平均 %u / 最大 %u / 標本 %u\n面を更新できなかった回数 %u\n現在値は約30フレームごとに更新",
                  a.freeBytes, a.largestBlock, a.measured ? "" : "未計測",
                  b.freeBytes, b.largestBlock, b.measured ? "" : "未計測",
                  c.freeBytes, c.largestBlock, c.measured ? "" : "未計測",
                  last.fontBytes, last.uiBytes, last.windowBytes, last.selectionBytes,
                  core::limits::RendererSelectionSurfaces, unsigned(core::limits::RendererSelectionSurfaceBytes),
                  last.gameBytes, last.gameUpperBytes, core::limits::RendererResidentHeapLimit,
                  last.pluginBytes, core::limits::RendererSelectionPhases,
                  unsigned(core::limits::RendererSelectionCacheBytes), last.commandBytes,
                  timing.prepareMicros, core::limits::RendererSelectionPhases, timing.lastMicros,
                  timing.samples ? unsigned(timing.totalMicros / timing.samples) : 0,
                  timing.maxMicros, timing.samples, timing.unavailable);
    return text;
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
