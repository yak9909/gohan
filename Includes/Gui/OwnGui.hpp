#ifndef OWNGUI_HPP
#define OWNGUI_HPP

#include <CTRPluginFramework.hpp>

namespace CTRPluginFramework
{
    // 自前 GUI の描画基盤を組み込む / 素へ戻す（基準仕様 v2 §5）。
    //
    // 組み込むもの
    //   1. nw::lyt のヒープから借りる  … 確保ケーブを Render_FrameEnd に 1 回だけ
    //   2. ノード登録のケーブ 3 本      … A1 / A2（登録）と N（安全スタブ）
    //   3. .bss                         … 自前 vtable / ノード 2 つ / root Pane 2 つ
    //   4. フック 2 本                  … RenderTop / RenderBottom
    //   ★描画のためのフックは 1 本も入れない（基準仕様 v2 §1）。
    //
    // 借りたヒープの中身（Picture / フォント資源 / TextBox）は GuiRenderer が組む。
    void    OwnGuiToggle(MenuEntry *entry);

    // 診断の書き出し（CTRPF の別項目）。溜めた記録を gohan.3gx と同じフォルダの gohan_owngui.txt / gohan_gui.txt へ書く。
    //   組み込み（OwnGuiToggle）だけではファイルを作らない（利用者 2026-10-06）
    void    OwnGuiWriteDiagnostics(MenuEntry *entry);

    // プラグイン終了時に必ず素へ戻すため main から呼ぶ。
    void    OwnGuiShutdown(void);

    bool    OwnGuiIsEnabled(void);
}

#endif
