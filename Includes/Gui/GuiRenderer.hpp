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
        // ゲームの字形（FONT_GAME）を元の大きさの scale 倍で描く（チャットの漢字候補欄。Simulator の 0.72 倍）。
        //   (x, y) = 文字セルの左上（画素）。要求の大きさ = FINF の幅・高さ x scale、ペインの高さ = FINF の高さ x scale。
        void        DrawTextNative(Screen screen, int x, int y, const char *text, u32 color, float scale);
        // 上の描き方での幅（CWDH の送り x scale の合計。GPU の送りと同じ小数のまま）
        float       MeasureTextNative(const char *text, float scale);
        // ★ゲームのテクスチャを貼る矩形（2026-09-26、チャットの自前キー）。
        //   slot 1..2 に TexMap（nw::lyt::TexMap の 32 B。+0x04 番地・+0x08 使う大きさ・+0x0C 全体の大きさ・+0x10 書式、
        //   派生は TexMap_UpdateGpuRegs 済み）と色[0] を入れてから FillTextured で描く。色は上と下の頂点色。
        //   ★テクスチャはゲームの資源なので、持ち主（キーボード）が消えたら ClearGameTextures する。
        bool        SetGameTexture(int slot, const u32 *texMap, u32 color0);
        void        ClearGameTextures(void);
        void        FillTextured(Screen screen, int x, int y, int w, int h, int slot, u32 topColor, u32 bottomColor);
        // ★下画面の切り抜き（2026-10-06。漢字候補欄）。BeginClip から EndClip までの下画面の FillRect / FillTextured は
        //   専用の Layout ノードへ入り、[x, x + w) x [y, y + h) の外は画素単位で切られる（ゲームの一覧と同じシザー）。
        //   切り抜き層は主ノードより後ろ（ゲームの UI より手前）に描かれる。段 1 は矩形だけ（文字は主ノードのまま）。
        //   Begin(SCREEN_BOTTOM) で空になる。
        void        BeginClip(int x, int y, int w, int h);
        void        EndClip(void);
        bool        ClipAvailable(void);    // 毎フレームの相乗りを載せられた（偽なら切り抜き層は描かれない）
        void        ClipFrameStep(void);    // ゲームのスレッドから毎フレーム（Install が GridCursor に載せる）
        void        Commit(void);

        // ---- 寸法（左上原点・画素）----
        int         MeasureText(const char *text, int scale = 1, Font font = FONT_MAIN);
        // UTF-8 の 1 文字を読み進め、その送り幅を返す（省略・折り返しに使う）
        int         NextCharWidth(const char *text, int &index, int scale = 1,
                                  Font font = FONT_MAIN);
        int         TextHeight(int scale = 1);

        // ---- 状態の問い合わせ（診断用）----
        u32         BorrowBytes(void);  // ★借りる大きさの正本。OwnGui はこれを使う
        u32         Generation(void);   // Install のたびに増える（借りた領域の番地が変わりうる）
        // 借りたヒープ（GPU から読める）のアトラスの後ろの空き。VA を返し、大きさを bytes に入れる。PA = VA - 0x10000000。
        //   チャットの自前キーのテクスチャの写しに使う（2026-09-26）。書いたら svcFlushProcessDataCache すること。
        u32         GpuSpare(u32 &bytes);
        // 借りたヒープの末尾の、没アイテムのアイコンのキャッシュ（2026-09-27。1,024 B x 16 枠）。VA を返す。PA = VA - 0x10000000。ゲームのスレッドだけが使う
        u32         GpuIconCache(u32 &bytes);
        u32         HeapBase(void);
        u32         HeapSize(void);
        u32         AtlasVa(void);      // CAVE_N のキャッシュ吐き出し先に使う
        u32         AtlasBytes(void);
        u32         RecordedBytes(Screen screen);   // ノード +0x108
        const char *LastError(void);
        bool        DumpLog(void);                  // gohan.3gx と同じフォルダの gohan_gui.txt へ書き出す（診断の書き出しのときだけ）

        // ---- 容量（基準仕様 v2 §4.7 の安全弁）----
        int         MaxRects(Screen screen);

        // ================================================================
        // ★下画面のタッチ遮断と暗幕（F-350）
        //   暗幕は下画面 UI の出現量（0..1）を掛けて描く（Simulator の backdrop と同じ）。
        //   キーボードや下画面リストボックスは**自前で暗幕を描かず** DrawBottomDim を使う。
        // ================================================================
        void        SetTouchBlock(bool on);      // ゲーム側のタッチ遮断（入力遮断ケーブの旗 +1）
        // 押し切るまでボタンの遮断（設定の A/B/X/Y/START = bit0..4）。入力遮断ケーブ B が使う状態の番地と初期化
        void        SetHoldUntilRelease(u8 settingsMask);
        u32         HoldStateAddress(void);
        void        ResetHoldState(void);
        void        DrawBottomDim(u32 color, float amount);   // ★BuildBottom の先頭で 1 回だけ
        // ゲーム側のボタン遮断（スライドパッドは残る）。buttons が優先。
        //   dpadOnly: 十字キーだけ遮断する（座標移動の「十字キーの無効化」。入力遮断ケーブの値 2）
        void        SetButtonBlock(bool buttons, bool dpadOnly = false, bool everything = false);   // everything はスライドパッドも
        int         MaxTexts(Screen screen);
        int         MaxChars(Screen screen);
    }
}

#endif
