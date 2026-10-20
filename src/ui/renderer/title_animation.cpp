#include "ui/renderer/renderer.hpp"
#include "game/lyt/renderer.hpp"
#include "core/mailbox.hpp"
#include "ui_values.hpp"
#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#endif
#include <cstring>

namespace gohan::ui::renderer {
namespace {
namespace v = core::design::ca2;
static_assert(v::Titlebg::Selection::UpdateFrames > 0 && v::Titlebg::Loop == v::Time::Titlebg);
struct Request { std::uint32_t frame; bool stop; };
core::Mailbox<Request> requested;
core::Mailbox<unsigned> completed;
core::Mailbox<bool> stopRequested;
std::uint32_t stopGeneration = 0;
std::uint32_t requestGeneration = 0, completedGeneration = 0;
unsigned selected = 0;
std::uint8_t work[v::Titlebg::Selection::ImageSize[0] * v::Titlebg::Selection::ImageSize[1] / 2];
bool pending = false;
unsigned pendingSlot = 0;

bool Pump() {
    bool stop = false;
    if (stopRequested.ReadIfNew(stop, stopGeneration) == core::MailRead::Updated && stop) return true;
    Request request{};
    if (pending) {
        if (completed.TryPublish(pendingSlot)) pending = false;
        // 停止は公開待ちでも受け取る。
    }
    if (requested.ReadIfNew(request, requestGeneration) != core::MailRead::Updated) return false;
    if (request.stop) return true;
    if (pending || !TitleBackgroundSources()) return false;
    if (!game::titlebg::BakeSelectionL4(TitleBackgroundSources(), v::Titlebg::Selection::ImageSize[0],
        v::Titlebg::Selection::ImageSize[1], v::Row::W + v::Titlebg::Selection::Inset[2],
        v::Row::H + v::Titlebg::Selection::Inset[3], work, request.frame)) return false;
#ifdef __3DS__
    if (!game::lyt::UpdateSelection(work, sizeof(work), pendingSlot)) return false;
#else
    // PC は GPU を使わないが同じ合成結果を保存する。排出と世代の安全性は Engine の試験で確認する。
    pendingSlot = selected ^ 1;
    std::memcpy(const_cast<std::uint8_t *>(TitleBackgroundImage(3 + pendingSlot).data), work, sizeof(work));
#endif
    pending = !completed.TryPublish(pendingSlot);
    return false;
}
#ifdef __3DS__
std::int32_t Run(void *) {
    while (!Pump()) CTRPluginFramework::Sleep(CTRPluginFramework::Milliseconds(1));
    return 0;
}
CTRPluginFramework::Task task(Run);
bool started = false;
#endif
}
void StartTitleAnimation() {
#ifdef __3DS__
    if (TitleBackgroundSources()) started = task.Start() == 0;
#endif
}
void StopTitleAnimation() {
#ifdef __3DS__
    if (!started) return;
    while (!stopRequested.TryPublish(true)) CTRPluginFramework::Sleep(CTRPluginFramework::Milliseconds(1));
    task.Wait(); started = false;
#endif
}
void StepTitleAnimation(std::uint32_t frame, Drawing &drawing) {
    bool visible = false;
    for (const auto &screen : drawing.screens) for (std::size_t n = 0; n < screen.count; ++n)
        visible |= screen.commands[n].texture == Texture::TitleSelection || screen.commands[n].texture == Texture::TitleSelectionBack;
    if (visible && frame % v::Titlebg::Selection::UpdateFrames == 0) requested.TryPublish({frame, false});
#ifndef __3DS__
    Pump();
#endif
    const unsigned before = selected;
    completed.ReadIfNew(selected, completedGeneration);
    // 描画元の指示は論理的に 1 枚。確定した最新の画像へ両画面を同じ世代で写す。
    for (unsigned screenIndex = 0; screenIndex < 2; ++screenIndex) {
      auto &screen = drawing.screens[screenIndex];
      bool uses = false;
      for (std::size_t n = 0; n < screen.count; ++n) {
        auto &c = screen.commands[n];
        if (c.texture == Texture::TitleSelection || c.texture == Texture::TitleSelectionBack) {
            c.texture = selected ? Texture::TitleSelectionBack : Texture::TitleSelection;
            uses = true;
        }
      }
      if (uses && before != selected) ++drawing.revisions[screenIndex];
    }
}
} // namespace gohan::ui::renderer
