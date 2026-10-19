#include "core/feature.hpp"
#include "core/format.hpp"
#include "core/limits.hpp"
#include "game/frame_hook.hpp"
#ifdef GOHAN_DEV
#include "ui/renderer/renderer.hpp"
#endif
#include <CTRPluginFramework.hpp>

namespace {
gohan::core::FeatureRegistry features;

#ifdef GOHAN_DEV
void ToggleRenderer(CTRPluginFramework::MenuEntry *) {
    namespace renderer = gohan::ui::renderer;
    static bool requested = false;
    static renderer::Canvas canvas;
    canvas.Begin(renderer::Screen::Top);
    canvas.FrameRect({20, 20, 120, 40}, 0xFFFFFFFFu);
    canvas.Text(32, 32, "gohan", 0xFFFFFFFFu, 2);
    if (renderer::Commit(canvas) && renderer::SetEnabled(!requested))
        requested = !requested;
}
#endif

void ShowStatus(CTRPluginFramework::MenuEntry *) {
    char text[gohan::core::limits::StatusText];
    gohan::core::TextWriter writer(text, sizeof(text));
    writer.Append("版: ").Unsigned(3).Append(".0.0\n");
#ifdef GOHAN_DEV
    writer.Append("開発版\n");
#else
    writer.Append("通常版\n");
#endif
    writer.Append("骨格: 機能登録・毎フレーム処理・通信・文字処理\n")
        .Append(gohan::game::FrameHookStatus());
#ifdef GOHAN_DEV
    writer.Append("\n").Append(gohan::ui::renderer::StatusText());
#endif
    CTRPluginFramework::MessageBox("gohan（作り直し中）", text)();
}
} // namespace

namespace CTRPluginFramework {
void OnProcessExit() { features.ShutdownAll(); }

int main() {
    PluginMenu menu("gohan", 3, 0, 0, "日本版の無印＋更新に対応。作り直し中です。");
    menu.SynchronizeWithFrame(true);
    menu +=
        new MenuEntry("gohan（作り直し中）", nullptr, ShowStatus, "版と骨格の状態を表示します。");
    features.Add(gohan::game::FrameHookFeature());
#ifdef GOHAN_DEV
    menu += new MenuEntry("開発: 自前描画の入・切", nullptr, ToggleRenderer,
                          "上画面の枠と gohan を要求します。導入結果は状態表示で確認できます。");
    features.Add(gohan::ui::renderer::RendererFeature());
#endif
    features.WireAll();
    menu.Run();
    features.ShutdownAll();
    return 0;
}
} // namespace CTRPluginFramework
