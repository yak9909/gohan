#include "core/jobs.hpp"
#include "game/item_names.hpp"
#include "features/pocket_items/pocket_items.hpp"
#include "features/hidden_items/hidden_items.hpp"
#include "features/drop_item/drop_item.hpp"
#include "core/feature.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "features/status/status.hpp"
#include "features/player/player.hpp"
#include "features/field/field.hpp"
#include "features/town/town.hpp"
#include "features/gameplay/gameplay.hpp"
#include "features/resources/resources.hpp"
#include "game/frame_hook.hpp"
#include "game/memory.hpp"
#include "features/movement/movement.hpp"
#include "features/actions/actions.hpp"
#include "features/keep_items/keep_items.hpp"
#include "features/shizue_skip/shizue_skip.hpp"
#include "game/input_hook.hpp"
#include "ui/renderer/renderer.hpp"
#include "ui/native_panels.hpp"
#include "core/frame.hpp"
#include <CTRPluginFramework.hpp>

namespace {
gohan::core::FeatureRegistry features;

#ifdef GOHAN_DEV
void PollCatalogDecision() {
    const int n = gohan::ui::native_panels::Catalog().TakeDecision();
    if (n >= 0) CTRPluginFramework::OSD::Notify("選んだ行: " + std::to_string(n));
}
void ToggleCatalog(CTRPluginFramework::MenuEntry *) {
    static bool shown = false;
    if (shown) gohan::ui::native_panels::Catalog().Close();
    else {
        std::vector<gohan::ui::lists::Row> rows;
        for (unsigned i = 0; i < 40; ++i)
            rows.push_back({"試しの行 " + std::to_string(i), 0, std::to_string(i), " 番"});
        if (!gohan::ui::native_panels::Catalog().Show(std::move(rows))) return;
    }
    shown = !shown;
}
void ToggleLabels(CTRPluginFramework::MenuEntry *) {
    static bool shown = false;
    std::vector<gohan::ui::LabelLine> lines;
    if (!shown) lines = {{"上画面の箱", 0}, {"二段目の試し", 1}};
    if (gohan::ui::native_panels::Labels().Declare(0, std::move(lines))) shown = !shown;
}
void ToggleLease(CTRPluginFramework::MenuEntry *) {
    static bool shown = false;
    if (gohan::ui::native_panels::LeaseOnly(!shown)) shown = !shown;
}
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

// CTRPF の終了の合図（PLG_ABOUT_TO_EXIT）のスレッドで動く。CTRPF が作業の管理と fs を閉じる前に、保存の作業だけを書き切らせる。
// features.ShutdownAll は呼ばない: ゲームのフックの Shutdown は Process::Pause（上限の無い WaitFramePaused）を通り、
// 終了の途中でコマが止まっていると合図の返答まで届かなくなるおそれがある。ShutdownAll は menu.Run() の後だけ。
// ほかの合図（スリープ・HOME・入れ替え）では何もしない。
void OnProcessEvent(CTRPluginFramework::Process::Event event) {
    if (event == CTRPluginFramework::Process::Event::EXIT) {
        gohan::features::menu_screen::StopSavingForExit();
        gohan::core::jobs::StopForExit();
    }
}
} // namespace

namespace CTRPluginFramework {
int main() {
    Process::SetProcessEventCallback(OnProcessEvent);
    PluginMenu menu("gohan", 3, 0, 0, "日本版の無印＋更新に対応。作り直し中です。");
    menu.SynchronizeWithFrame(true);
    // 起動時の CTRPF の「Plugin ready!」は出さない（公開の API。同梱の CTRPF は変えない）。
    menu.ShowWelcomeMessage(false);
    menu += new MenuEntry("gohan（作り直し中）", nullptr, gohan::features::status::Show,
                          "版と骨格の状態を表示します。");
    // 終了は同じ段で登録と逆の順。描画の部品は、毎フレームのフックを止める前に資源を排出する。
    features.Add(gohan::core::jobs::JobsFeature());
    features.Add(gohan::game::FrameHookFeature());
    features.Add(gohan::game::InputPadFeature());
    features.Add(gohan::game::InputControllerFeature());
    features.Add(gohan::game::memory::Feature());
    features.Add(gohan::ui::renderer::RendererFeature());
    features.Add(gohan::ui::native_panels::Feature());
    features.Add(gohan::game::items::NamesFeature());
    features.Add(gohan::features::pocket_items::Feature());
    features.Add(gohan::features::hidden_items::Feature());
    features.Add(gohan::features::drop_item::Feature());
    features.Add(gohan::features::player::Feature());
    features.Add(gohan::features::field::Feature());
    features.Add(gohan::features::town::Feature());
    features.Add(gohan::features::gameplay::Feature());
    features.Add(gohan::features::resources::Feature());
    features.Add(gohan::features::movement::Feature());
    features.Add(gohan::features::actions::Feature());
    features.Add(gohan::features::keep_items::Feature());
    features.Add(gohan::features::shizue_skip::Feature());
    for (auto bindings : {gohan::features::player::Bindings(), gohan::features::field::Bindings(),
                          gohan::features::town::Bindings(), gohan::features::gameplay::Bindings(),
                          gohan::features::resources::Bindings(), gohan::features::movement::Bindings(),
                          gohan::features::actions::Bindings(), gohan::features::keep_items::Bindings(),
                          gohan::features::shizue_skip::Bindings(), gohan::features::pocket_items::Bindings(),
                          gohan::features::hidden_items::Bindings(), gohan::features::drop_item::Bindings()}) {
        if (!gohan::features::menu_screen::BindItems(bindings.data(), bindings.size())) return 1;
    }
    features.Add(gohan::features::menu_screen::Feature());
    // Select 単独は自前のメニューに譲る。Select+十字上は従来どおり CTRPF のメニュー。
    menu.OnOpening = OpenCtrpfMenu;
#ifdef GOHAN_DEV
    menu += new MenuEntry("状態を txt に書き出す", nullptr, gohan::features::status::Export,
                          "表示と同じ状態を設定ファイルと同じ場所に保存し、パスを表示します。");
    menu += new MenuEntry("開発: 一覧の開閉", nullptr, ToggleCatalog,
                          "40行・追加テキスト・半速スライダーを試します。選んだ番号を通知します。");
    menu += new MenuEntry("開発: 箱の開閉", nullptr, ToggleLabels,
                          "上画面左上に二段の箱を出します。");
    menu += new MenuEntry("開発: 下画面の貸し出し", nullptr, ToggleLease,
                          "地図とタブの出し入れだけを試します。");
    gohan::core::frame::Global().Add(PollCatalogDecision, 130);
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
#ifdef GOHAN_DEV
    gohan::core::frame::Global().Remove(PollCatalogDecision);
#endif
    return 0;
}
} // namespace CTRPluginFramework
