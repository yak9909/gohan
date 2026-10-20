#include "core/feature.hpp"
#include "core/format.hpp"
#include "core/limits.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "game/frame_hook.hpp"
#include "ui/renderer/renderer.hpp"
#include <CTRPluginFramework.hpp>

namespace {
gohan::core::FeatureRegistry features;

#ifdef GOHAN_DEV
void ToggleRenderer(CTRPluginFramework::MenuEntry *) {
    namespace renderer = gohan::ui::renderer;
    static bool requested = false;
    static renderer::Canvas canvas;
    // 下画面だけを使う（上画面は gohan のメニューが使う）。切るときは空の下画面を確定する。
    canvas.Begin(renderer::Screen::Bottom);
    if (!requested) {
        canvas.FrameRect({20, 20, 120, 40}, 0xFFFFFFFFu);
        canvas.Text(32, 32, "gohan", 0xFFFFFFFFu, 2);
    }
    if (renderer::Commit(canvas, renderer::Screen::Bottom) && renderer::SetEnabled(!requested))
        requested = !requested;
}

void ToggleGohanMenu(CTRPluginFramework::MenuEntry *) {
    // 次のフレームで開閉する。競合した時は、もう一度押す。
    gohan::features::menu_screen::RequestToggle();
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
    writer.Append("\n").Append(gohan::ui::renderer::StatusText());
    writer.Append("\n").Append(gohan::ui::renderer::TitleBackgroundStatusText());
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
    // 終了は同じ段で登録と逆の順。描画の部品は、毎フレームのフックを止める前に資源を排出する。
    features.Add(gohan::game::FrameHookFeature());
    features.Add(gohan::ui::renderer::RendererFeature());
    features.Add(gohan::features::menu_screen::Feature());
    // Select 単独は自前のメニューに譲る。Select+十字上は従来どおり CTRPF のメニュー。
    menu.OnOpening = gohan::features::menu_screen::ShouldOpenCtrpfMenu;
#ifdef GOHAN_DEV
    menu += new MenuEntry("開発: 自前描画の入・切", nullptr, ToggleRenderer,
                          "下画面の枠と gohan を要求します。導入結果は状態表示で確認できます。");
    menu += new MenuEntry("開発: gohan のメニューの開閉", nullptr, ToggleGohanMenu,
                          "上画面の gohan のメニューを開閉します。Select 単独でも開閉できます。");
#endif
    features.WireAll();
    menu.Run();
    features.ShutdownAll();
    return 0;
}
} // namespace CTRPluginFramework
