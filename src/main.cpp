#include "core/feature.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "features/status/status.hpp"
#include "game/frame_hook.hpp"
#include "game/input_hook.hpp"
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
void ToggleInput(CTRPluginFramework::MenuEntry *) { gohan::game::ToggleInputHooks(); }
#endif

bool OpenCtrpfMenu() {
    const bool allow = gohan::features::menu_screen::ShouldOpenCtrpfMenu();
    if (allow) gohan::game::InputCtrpfOpening();
    return allow;
}

} // namespace

namespace CTRPluginFramework {
void OnProcessExit() { features.ShutdownAll(); }

int main() {
    PluginMenu menu("gohan", 3, 0, 0, "日本版の無印＋更新に対応。作り直し中です。");
    menu.SynchronizeWithFrame(true);
    menu += new MenuEntry("gohan（作り直し中）", nullptr, gohan::features::status::Show,
                          "版と骨格の状態を表示します。");
    // 終了は同じ段で登録と逆の順。描画の部品は、毎フレームのフックを止める前に資源を排出する。
    features.Add(gohan::game::FrameHookFeature());
    features.Add(gohan::game::InputPadFeature());
    features.Add(gohan::game::InputControllerFeature());
    features.Add(gohan::ui::renderer::RendererFeature());
    features.Add(gohan::features::menu_screen::Feature());
    // Select 単独は自前のメニューに譲る。Select+十字上は従来どおり CTRPF のメニュー。
    menu.OnOpening = OpenCtrpfMenu;
#ifdef GOHAN_DEV
    menu += new MenuEntry("状態を txt に書き出す", nullptr, gohan::features::status::Export,
                          "表示と同じ状態を設定ファイルと同じ場所に保存し、パスを表示します。");
    menu += new MenuEntry("開発: 入力の遮断 入・切", nullptr, ToggleInput,
                          "入力のフックを取り外し、再び導入します。結果は状態表示で確認できます。");
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
