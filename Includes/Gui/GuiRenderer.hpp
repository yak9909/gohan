#ifndef GUIRENDERER_HPP
#define GUIRENDERER_HPP

// ============================================================================
// GuiRenderer — 基準仕様 v2 の描画（BASELINE/gui_spec_v2/README.md）
// ============================================================================
//
// nw::lyt の正規経路で描く。**GPU コマンドを 1 語も自分で書かない。**
//   ・矩形／パネル … nw::lyt::Picture（自前で組んだ Pane 木の子）
//   ・文字         … 自前フォント資源 + nw::lyt::TextBox（標準の文字経路）
//   ・記録／再生   … ゲームの Layout_RecordPaneTree / ReplayRecordedList
//
// 呼び出し側から見える形は v1 の GuiRender とほぼ同じだが、中身は別物。
//   Begin(screen) -> FillRect / DrawText を並べる -> Commit()
// Commit() で借りたヒープ上のオブジェクトを組み直し、子リストを張り替える。
//
// ★描くものが変わらないときは Commit() は何もしない（無駄な書き込みを避ける）。

#include <3ds.h>
#include <CTRPluginFramework.hpp>

namespace CTRPluginFramework
{
    namespace GuiRenderer
    {
        // ★A1 ケーブが画面番号 0、A2 が 1 を書く。値を変えないこと。
        enum Screen
        {
            SCREEN_TOP = 0,
            SCREEN_BOTTOM = 1
        };

        // ★2 つのフォントを状況で使い分ける（Includes/GuiFontUi.h）。
        //   同じ 1 枚のアトラス（256x128 LA4）に両方入っていて、
        //   CMAP（方式 2）と ResFont だけが 2 組ある。
        //     FONT_MAIN … 美咲ゴシック 2nd 7x7 相当。日本語 362 字・可変幅
        //     FONT_NUM  … PixelMplus 数字 5x8。数値表示用の等幅
        enum Font
        {
            FONT_MAIN = 0,
            FONT_NUM  = 1,
            FONT_GAME = 2 // candidate rows: borrowed native Japanese font, private lookup cache
        };

        // ---- 組み込み / 取り外し（基準仕様 v2 §5）----
        bool        Install(void);
        void        Uninstall(void);
        bool        IsReady(void);

        // ---- 描画（Commit() で反映）----
        void        Begin(Screen screen);
        void        FillRect(Screen screen, int x, int y, int w, int h, u32 color);
        void        DrawText(Screen screen, int x, int y, const char *text, u32 color,
                             int scale = 1, Font font = FONT_MAIN);
        void        Commit(void);

        // ---- 寸法（左上原点・画素）----
        int         MeasureText(const char *text, int scale = 1, Font font = FONT_MAIN);
        // UTF-8 の 1 文字を読み進め、その送り幅を返す（省略・折り返しに使う）
        int         NextCharWidth(const char *text, int &index, int scale = 1,
                                  Font font = FONT_MAIN);
        int         TextHeight(int scale = 1);

        // ---- 状態の問い合わせ（診断用）----
        u32         BorrowBytes(void);  // ★借りる大きさの正本。OwnGui はこれを使う
        u32         HeapBase(void);
        u32         HeapSize(void);
        u32         AtlasVa(void);      // CAVE_N のキャッシュ吐き出し先に使う
        u32         AtlasBytes(void);
        u32         RecordedBytes(Screen screen);   // ノード +0x108
        const char *LastError(void);
        void        DumpLog(void);                  // /gohan_gui.txt へ書き出す

        // ---- 容量（基準仕様 v2 §4.7 の安全弁）----
        int         MaxRects(Screen screen);

        // ================================================================
        // ★下画面のタッチ遮断と暗幕（F-350）
        //   暗幕は下画面 UI の出現量（0..1）を掛けて描く（Simulator の backdrop と同じ）。
        //   キーボードや下画面リストボックスは**自前で暗幕を描かず** DrawBottomDim を使う。
        // ================================================================
        void        SetTouchBlock(bool on);      // ゲーム側のタッチ遮断（入力遮断ケーブの旗 +1）
        void        DrawBottomDim(u32 color, float amount);   // ★BuildBottom の先頭で 1 回だけ
        // ゲーム側のボタン遮断（スライドパッドは残る）。buttons が優先。
        //   dpadOnly: 十字キーだけ遮断する（座標移動の「十字キーの無効化」。入力遮断ケーブの値 2）
        void        SetButtonBlock(bool buttons, bool dpadOnly = false, bool everything = false);   // everything はスライドパッドも
        int         MaxTexts(Screen screen);
        int         MaxChars(Screen screen);
    }
}

#endif
