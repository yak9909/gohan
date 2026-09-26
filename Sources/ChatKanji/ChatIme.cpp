// ============================================================================
// 漢字変換（チャット）とかな入力のコンポジション（gohan.md §12）。
// 根拠は IDA-opus-5.5-F043 / F044（work/FINDINGS.md）。設計は ChatIme.hpp の冒頭。
// ============================================================================
//
// ★スレッドの分担
//   ゲームのスレッド（フック 3 本の中）: 入力欄（TextManager）を読む・書く。
//   メニュースレッド（Tick / DrawBar）: ホットキー・十字キー・タッチを読み、依頼を置き、
//     変換エンジン（ChatKanji）を回し、候補欄を描く。入力欄には触らない。
//   依頼は 1 語の種類 + 1 語の引数（g_reqKind / g_reqArg）。ゲームのスレッドが処理して種類を 0 に戻す。
//
// ★入力欄の状態（sys::TextManager、F043 / F044）
//   +0x08 文字数 / +0x0C 最大字数 / +0x10 UTF-16 / +0x14 カーソル / +0x18 (-1 に戻す欄)
//   +0x1C 選択の起点 / +0x20 選択中(byte) / +0x24 未確定の字数 / +0x28 余りの字数 / +0x90 (-1 に戻す欄)
//   未確定の範囲 = [カーソル - +0x24, カーソル)。ゲームはこの範囲に飾りを描く。

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ChatIme.hpp"
#include "ChatKanji.hpp"
#include "ChatKanjiText.hpp"
#include "Cheats.hpp"
#include "GuiMenu.hpp"
#include "GuiRenderer.hpp"

namespace CTRPluginFramework
{
    namespace ChatIme
    {
        namespace
        {
            using Cheats::kKanji;
            using Cheats::kComposition;

            // ---- 番地（JPN 無印＋更新版。ChatKanji が TitleID と 6 地点を照合したうえで使う）----
            const u32   kTmPtr          = 0x00958114;   // [ ] = sys::TextManager
            const u32   kInputChar      = 0x005216FC;   // TextManager_InputChar(tm, ch, romaji, combine)
            const u32   kInputCharOrig  = 0xE3510000;   // CMP R1,#0
            const u32   kBackspace      = 0x005226EC;   // TextManager_Backspace(tm, silent)
            const u32   kBackspaceOrig  = 0xE92D41F0;   // PUSH {R4-R8,LR}
            const u32   kWaitCalc       = 0x0057B778;   // BsSkb_Wait_Calc(bsskb)
            const u32   kWaitCalcOrig   = 0xE92D5FF0;   // PUSH {R4-R12,LR}
            const u32   kDeleteRange    = 0x005220D4;   // TextManager_DeleteRange(tm, pos, n)
            const u32   kDeleteRangeOrig = 0xE92D41F0;
            const u32   kInsertChar     = 0x00521B28;   // TextManager_InsertChar(tm, ch, pos, romaji, f)
            const u32   kInsertCharOrig = 0xE92D4FF0;
            const u32   kFinishCell     = 0x005224B8;   // TextManager_FinishCellPhonePending(tm, 1)
            const u32   kFinishCellOrig = 0xE92D4038;
            const u32   kSetCursor      = 0x00522020;   // TextManager_SetCursor(tm, pos, clearSel, moveAnchor)
            const u32   kSetCursorOrig  = 0xE92D41F0;
            const u32   kPlaySound      = 0x0058C7D4;   // Game_PlaySound(id)
            const u32   kPlaySoundOrig  = 0xE1A01000;
            const u32   kTexMapUpdate   = 0x004B9830;   // nwlyt_TexMap_UpdateGpuRegs(texMap)
            const u32   kTexMapUpdateOrig = 0xE52D4004;
            const u32   kGetTexture     = 0x004B5844;   // nw::lyt::ArcResourceAccessor::GetTexture (vtable +0x10)
            const u32   kGetTextureOrig = 0xE92D4070;
            const u32   kVtAccessorVram = 0x009005E4;   // ssys::ma::lyt::ArcResourceAccessorVRAM
            const u32   kBsSkbPtr       = 0x0094A654;   // BsSkb（キーボード）
            const u32   kBsSkbKeysetAcc = 1448;         // BsSkb+1436 の ArcResAccReader（今のキー配列の swkbd_*.arc）+12 = アクセサ
            const u32   kBsSkbKeyset    = 4416;         // BsSkb+4416 = 今のキー配列（gui::*KeySet）
            const u32   kKeysetKind     = 0x00AD0544;   // 0 qwerty / 1 かな / 2・3 grid / 4 ケータイ（BsSkb_InitStep）
            const u32   kWindowInCalc   = 0x0057CB20;   // BsSkb 状態 #0 "window in" の calc（開くアニメの間）
            const u32   kKeysetLoadCalc = 0x0057D444;   // BsSkb 状態 #25 "keyset load" の calc（キー配列の切り替え）
            const u32   kKeysetLoadCalcOrig = 0xE92D47F0;
            const u32   kFindPane       = 0x007461D0;   // lyt_Object_FindPane(object, name) = (*(object+72))->vt+44(name, 1)
            const u32   kFindPaneOrig   = 0xE5900048;
            const u32   kSetTranslate   = 0x004B6530;   // nw::lyt::Pane の平行移動を書き、+0xB7 bit4-5 を落とす
            const u32   kSetTranslateOrig = 0xE1C120D0;
            const u32   kKeysetLayout   = 0x110;        // gui::*KeySet の vt+0x20 = LDR R0,[R0,#0x110]（4 種とも）
            // キー配列の背面 W_ktpShade（4 種とも RootPane/N_keytop/N_key の子、320x142、平行移動 (0,-27)、キーより先に描かれる）。
            //   上端を画面 y65 -> y47 へ伸ばす（初めは y46 = 変換行の 3px 上。利用者の指示で 1px 下げた）: 高さ +18、中心 +9（下端はそのまま）
            const float kShadeOrigH = 142.0f, kShadeOrigTy = -27.0f, kShadeGrow = 18.0f;
            const u32   kWindowInCalcOrig = 0xE92D41F0;
            const u32   kBsSkbBgLayout  = 1372;         // BsSkb+1372 = BG レイアウト（N_All に BG_in / BG_out が当たる）
            // gui::KanaKeySet の OnKey（0x4F556C）が InputChar を呼ぶ 3 か所（字 x2・濁点キー）
            const u32   kKanaCall[3]    = { 0x004F5654, 0x004F56A4, 0x004F57A4 };
            const u32   kKanaCallWord[3] = { 0xEB00B028, 0xEB00B014, 0xEB00AFD4 };   // BL 0x5216FC

            // ---- キーの入力の待ち（IDA-opus-5.5-F046。利用者の指示 2026-09-26: 次の入力までの待ちを無くす）----
            //   キーボードの部品の更新 gui_UpdateButtons(0x4FD7B0) は、まず一覧（0xAD0630）の部品のどれかが +12（押さえている）なら
            //   忙しい旗 0x9580E6 を立て、立っていれば +12 の部品以外の更新を飛ばす。字のキー gui::InputButton は
            //   押した瞬間（状態 0）に入力し +12 を立て、離すと状態 2、次のフレームで状態 0、そのまた次のフレームで +12 を落とす
            //   → 離したあと 2 フレームは他のキーの「押した瞬間」を取りこぼす。更新の前に、その 2 フレーム分の後片付けを前倒しする。
            const u32   kKeyUpdate      = 0x004FD7B0;   // gui_UpdateButtons(flags)（BsSkb の calc 0x57F744 から 1 回/フレーム）
            const u32   kKeyUpdateOrig  = 0xE92D47F0;   // PUSH {R4-R10,LR}
            const u32   kKeyList        = 0x00AD0630;   // 部品の一覧の番兵（次 = [番兵]、部品 = 節 - 4）
            const u32   kVtInputButton  = 0x008FD060;   // gui::InputButton（字・Enter・濁点のキー）
            const u32   kVtRepeatInput  = 0x008FD2F0;   // gui::RepeatInputButton（消去など長押しで繰り返すキー）
            const u32   kButtonAnimDone = 0x004FD9C8;   // gui_Button_AnimsDone(button, 段) = 状態 2 の「待ち」の判定
            const u32   kButtonAnimDoneOrig = 0xE92D4010;
            // 状態ごとの処理（vt+0x28 / +0x2C / +0x30）が F046 で読んだものと同じかを確かめる
            const u32   kKeyVtCheck[][2] = {
                { kVtInputButton + 0x28, 0x004F6264 }, { kVtInputButton + 0x2C, 0x004F62F0 }, { kVtInputButton + 0x30, 0x004F6A8C },
                { kVtRepeatInput + 0x28, 0x004FCB50 }, { kVtRepeatInput + 0x2C, 0x004FCBEC },
            };
            const u32   kKeyUpdateOff   = 0x0095819C;   // 0 でないと元の処理は部品を更新しない（意味は未確定。条件だけ写す）
            const u32   BTN_HOLD = 0x0C, BTN_STATE = 0x10, BTN_TOGGLE = 0x119;

            const u32   TM_LEN = 0x08, TM_MAX = 0x0C, TM_BUF = 0x10, TM_CURSOR = 0x14, TM_18 = 0x18;
            const u32   TM_ANCHOR = 0x1C, TM_SEL = 0x20, TM_PEND = 0x24, TM_EXTRA = 0x28, TM_90 = 0x90;
            const int   kReadingMax = 32;               // ChatKanji::RequestText の上限（普通のチャットの最大字数）
#define SWKBD_TEXT_UNITS 56                             // SwkbdEngine::TextUnits（候補は 55 字まで + 終端）

            typedef int     (*InsertCharFn)(u32 tm, u32 ch, int pos, int romaji, float f);
            typedef void    (*DeleteRangeFn)(u32 tm, int pos, int n);
            typedef int     (*FinishCellFn)(u32 tm, int flag);

            // ---- 候補欄（Simulator 639b4e6 chat-kanji-preview.js の CHAT_LAYOUT / CANDIDATE_BAR / CANDIDATE_COLORS）----
            //   変換行 = キーボードの幅 x1..318、y49..67（高さ 19）。候補欄はその左、右端の列（x279..318）が「全選択」。
            const int   kRowX = 1, kRowW = 318, kRowY = 49, kRowH = 19;
            const int   kBarX = kRowX, kBarY = kRowY, kBarW = 279 - kRowX, kBarH = kRowH;
            // 文字セルの上端。行の中央に文字セル（24 x 0.72 = 17.28）の中央を合わせる: 58.5 - 8.64 = 49.86 -> 50
            //   （Simulator の 48 だと上に寄って見えた。利用者の指摘 2026-09-26）
            const int   kBarGap = 2, kBarPad = 4, kBarTextY = 50;
            const float kBarScale = 0.72f;
            const u32   kColBarPanel = 0xFF102852;      // #522810
            const u32   kColRowEdge  = 0xFF0C1D3A;      // #3a1d0c（CONTROL_COLORS.rowEdge）
            const u32   kColBarSel   = 0x3DD6FFEF;      // rgba(239,255,214,.24)
            const u32   kColBarText  = 0xFFD6F3FF;      // #fff3d6
            const u32   kColBarBusy  = 0x80D6F3FFu;     // 「変換中」は半透明（利用者の指示 2026-09-26）
            const u32   kColBarHint  = 0x9ED6F3FF;      // rgba(255,243,214,.62)
            // 1 フレームに描くゲームの字形の上限（記録リストの見積もり。verify_plugin_port_v2 群 4 が読む）。
            //   ゲームの字形の枠は 16 本（GuiRenderer kNativeSlots）。キーの文字 3 本（全選択 / ← / →、5 字）を先に取る。
            const int   kBarMaxChars = 32;
            const int   kBarMaxCells = 13;
            const int   kKeyTexts = 3, kKeyChars = 5;
            const int   kMaxCand = 300;                 // SwkbdEngine::MaxCandidates

            // ---- 自前キー（全選択・左・右）。見た目は今のキー配列の「空白」キー（利用者の指示 2026-09-26）----
            //   全選択は Simulator の SELECT_ALL_BUTTON（消去の真上）。左右は右上で、24x22 は大きいとの指摘で 22x19（全選択と同じ高さ）。
            //   文字は横も縦もキーの中央（字幅は GPU の送りで測る）。
            struct KeyRect { int x, y, w, h; const char *label; float scale; };
            enum { KEY_SELECT_ALL = 0, KEY_LEFT, KEY_RIGHT, KEY_COUNT };
            //   2026-09-26（利用者の指示）: 全選択は幅を 1px 縮める（左に揃える）。左右は 2 つ合わせて全選択と同じ幅・X、間は 1px、前より 1px 下げる。
            //   2026-09-26（3 回目）: 全選択はやはり 1px 左（x279..317）。左右もそれに揃える。
            const KeyRect kKeys[KEY_COUNT] = {
                { 279, 49, 39, 19, u8"全選択", 0.72f },
                { 279,  2, 19, 19, u8"←",     0.8f },
                { 299,  2, 19, 19, u8"→",     0.8f },
            };
            // 左右キーの背面の地（変換欄と同じ色）。キーを 1px ずつ囲み、左側だけさらに 1px（利用者の指示）
            const int   kArrowBackX = 277, kArrowBackY = 1, kArrowBackW = 42, kArrowBackH = 21;

            // ---- キーの角の丸み（利用者の指示 2026-09-26）----
            //   ゲームのキーは、外周に面した角だけテクスチャのアルファで丸めている（例 Ktp50onKeyBsp の右上: 角 0、隣 0.73、その次 0.87）。
            //   自前のキーは 1 枚のテクスチャを使い回し、ABC などの空白キーは L4（アルファ無し）なので、角の画素を背面の色で半透明に塗って丸める。
            //   ROUND_MID = 消去キーの角と同じ形（角 1.0 / 隣 2 つ 0.27 / 縦にもう 1 つ 0.13）、ROUND_LOW = 角 1 画素だけ 0.5。
            enum { ROUND_NONE = 0, ROUND_LOW, ROUND_MID };
            struct KeyRound { u8 leftTop, leftBottom, rightTop, rightBottom; };
            const KeyRound kKeyRound[KEY_COUNT] = {
                { ROUND_LOW, ROUND_LOW, ROUND_MID, ROUND_MID },     // 全選択: 消去・空白と同じく左は少し、右はそこそこ
                { ROUND_MID, ROUND_MID, ROUND_NONE, ROUND_NONE },   // 左: 左側の上下
                { ROUND_NONE, ROUND_NONE, ROUND_MID, ROUND_MID },   // 右: 右側の上下
            };
            const u32   kColKeyGround = 0x00102852;     // 背面の色 (82,40,16)。アルファは角ごとに入れる
            const int   kCornerRects = 26;              // 1 フレームの角の矩形の最大（全選択 2+8、左 8、右 8。verify_plugin_port_v2 が読む）
            const char  kLabelDeselect[] = u8"解除";         // 全体が選択されている間の全選択キー
            // 空白キー（どのキー配列も P_key_Spc / T_key_Spc と *_n0s1 の値が同じ。フレーム 0 = 通常、1 = 押下）
            const u32   kKeyColor0     = 0x0023418C;    // material 色[0] (140,65,35,0)
            const u32   kKeyTopNormal  = 0xFF468AD2;    // 頂点色 上 (210,138,70)
            const u32   kKeyBotNormal  = 0xFF2D5F96;    //        下 (150,95,45)
            const u32   kKeyTopPressed = 0xFF81B1FF;    // 押下   上 (255,177,129)
            const u32   kKeyBotPressed = 0xFFC3E8FF;    //        下 (255,232,195)
            const u32   kKeyText       = 0xFF00143C;    // T_key_Spc 色[1] (60,20,0)
            const u32   kKeyTextPressed = 0xFF3A5EB8;   // 押下 (184,94,58)。文字は右下へ 1px（T_key_Spc の CLPA）
            const u32   kSoundChangeKeySet = 0x010003E0;    // SE_SYS_SWK_CHANGE_KEY_SET（全選択。「ABC」「あいう」の切り替えの音）
            const u32   kSoundBackspace    = 0x010003DA;    // SE_SYS_SWK_BACKSPACE（左右。Backspace で消せたときの音）
            // ---- 候補欄のタッチ（利用者の指示 2026-09-26: 1 回のタッチで確定・慣性スクロール・揺れの補正）----
            const float kTapSlop = 6.0f;                // 触れた所から（補正後の位置で）これ以上動いたらスクロール（確定しない）
            // 揺れの補正（係数は解析 repo tools/chat_kanji/sim_touch.py で決めた）:
            //   1. 飛び値の保留: 前の値から 1 回で kSpikePx を越えて飛んだ値は 1 回保留し、次の値が続かなければ捨てる
            //      （3 点の中央値だと 1500px/s のスワイプで約 28px 遅れたのでやめた）
            //   2. 1€ フィルタ（Casiez 2012）: 遅い動きほど強く均し、速い動きは遅らせない
            //   3. 表示のヒステリシス: 補正後の位置が kHysteresisPx を越えて動くまでスクロール位置を変えない（1px の揺れで字が震えない）
            const float kSpikePx = 40.0f;               // 1 回（約 16ms）で 40px = 2500px/s を越える動きは飛び値とみなす
            const float kEuroMinCutoff = 1.0f;          // Hz。止まっているときの均し具合
            const float kEuroBeta = 0.05f;              // 速さ（px/s）に応じて均しを弱める度合い
            const float kEuroDCutoff = 5.0f;            // Hz。速さの推定の均し
            const float kHysteresisPx = 1.0f;
            // 慣性スクロール
            const float kFlingWindow = 0.1f;            // s。離す直前のこの間の動きから速さを求める
            const float kFlingMin = 60.0f;              // px/s。これより遅ければ投げない
            const float kFlingMax = 2500.0f;            // px/s
            const float kFlingTau = 0.35f;              // s。速さが 1/e になる時間（指数関数的に減速）
            const float kFlingStop = 15.0f;             // px/s。これより遅くなったら止める
            const int   kTouchHist = 16;                // 速さを求めるための履歴

            // HotkeyBit（GuiMenu.cpp の kHotkeyKeys の並び）
            const u16   HB_LEFT = 1u << 6, HB_RIGHT = 1u << 7;

            // ---- 共有（メニュースレッドが書き、ゲームのスレッドが読む）----
            volatile bool   g_convOn = false;
            volatile bool   g_compOn = false;
            volatile bool   g_chatOpen = false;
            volatile bool   g_broken = false;   // スレッドの取り違えなどで止めた
            volatile bool   g_reverted = false; // Backspace で読みへ戻した（ゲーム -> メニュー。同じ読みでも取り直す）

            // ---- 依頼（メニュー -> ゲーム）----
            enum { R_NONE = 0, R_START, R_APPLY, R_ABORT, R_ENTER, R_SELECT_ALL, R_LEFT, R_RIGHT, R_DESELECT };
            volatile u32    g_reqKind = R_NONE;
            volatile s32    g_reqArg = 0;
            // R_APPLY で入れる文字列（メニューが写してから依頼する。ゲームのスレッドはキャッシュを読まない）
            u16             g_applyText[SWKBD_TEXT_UNITS];
            int             g_applyLen = 0;
            enum { START_OK = 0, START_NO_TARGET, START_TOO_LONG, START_NO_TM };
            volatile u32    g_startResult = START_OK;

            // ---- キーのテクスチャ（ゲームのスレッドが BsSkb ごとに 1 回取り、メニューが描画器へ渡す）----
            u32             g_texOwner = 0;             // 取った BsSkb
            u32             g_texKeyset = 0;            //        キー配列
            u32             g_texKind = 0xFFFFFFFF;     //        キー配列の種類
            u32             g_texMap[2][8];             // [0] 空白キー / [1] その押下（…on）の TexMap（32 B）
            // キー配列の背面（ゲームのスレッドが決める。メニューは読むだけ）
            volatile bool   g_shadeOn = false;          // 伸ばしてある（メニューは変換行の地と縁を塗らない）
            // 写し（2026-09-26）: 最初に取ったテクスチャを借りたヒープへ写し、以後はそれを使う（キー配列を切り替えても剥がれない）
            bool            g_texCopied = false;
            u32             g_texCopyGen = 0;           // 写したときの GuiRenderer::Generation()
            // ★写すのは取ってから kTexCopyDelay 回あと（2026-09-26。押下のテクスチャが乱れた）:
            //   初めて取るテクスチャはその場で VRAM への読み込みが始まり、転送は GPU の DMA 要求として積まれるだけ
            //   （ssys_LoadBclimToVram 0x5676C8 → 0x7B0044 が g_GpuCmdListMgr の列へ積む）。取った直後に写すと転送前の中身を写す。
            //   それまでは VRAM のものをそのまま使う（写しを入れる前はこれで押下の見た目も PASS だった）。
            const u32       kTexCopyDelay = 10;         // BsSkb の calc の回数（1 回/フレーム）
            u32             g_texInfo[2][5];            // 取ったテクスチャ（TextureInfo）
            u32             g_texAge = 0;               // 取ってからの回数
            bool            g_texCopyTried = false;     // 写そうとした（写せなくても繰り返さない）
            volatile u32    g_texSeq = 0;               // 取り直すたびに増える（奇数 = 書いている途中）
            volatile bool   g_texOk = false;

            // ---- 変換の区切り（ゲームのスレッドが書く。g_state だけメニューも読む）----
            enum { S_IDLE = 0, S_WAIT_ENGINE, S_ACTIVE };
            volatile u32    g_state = S_IDLE;
            u16             g_reading[kReadingMax + 1];
            volatile int    g_readingLen = 0;
            int             g_start = 0;
            int             g_curLen = 0;
            int             g_applied = -1;
            u32             g_sessionTm = 0;
            bool            g_sessionSel = false;   // 対象が選択範囲（まだ未確定にしていない）

            // ---- かなで作った未確定（ゲームのスレッドだけが触る）----
            //   ローマ字の入力（QwertyKeySet・ローマ字モード）は未確定の字をローマ字として変換し直すので、
            //   かなで作った未確定が残ったままローマ字を打つと、かなを「ローマ字」として扱ってしまう。
            //   こちらが作った未確定（位置と字数が最後に置いたものと同じ）なら、先に確定する。
            u32             g_ownTm = 0;
            int             g_ownCursor = -1;
            int             g_ownPend = 0;

            // ---- スレッドの照合（InputChar と Wait_Calc が同じスレッドで走ることの実行時確認）----
            volatile u32    g_threadInput = 0;
            volatile u32    g_threadWait = 0;

            // ---- いま候補欄に出している候補（メニュースレッドだけ）----
            //   2026-09-26 利用者の指示でキャッシュは削除（変換が十分速く、キャッシュが逆に遅くしうる）。
            //   エンジンの結果は次の依頼で消えるので、出している 1 件だけ写して持つ。
            struct CacheEntry
            {
                bool                used;
                std::vector<u16>    text;               // 候補を続けて並べたもの
                std::vector<u16>    start;              // 候補 i は text[start[i] .. start[i+1])（要素数は候補数 + 1）
            };
            CacheEntry  m_cache[1];
            int         m_entry = -1;                   // いま候補欄に出しているもの（0 か -1）

            // ChatKanji の結果を写す。写した番号（0）を返す
            int     TakeResult(void)
            {
                const int   count = ChatKanji::CandidateCount();
                CacheEntry &e = m_cache[0];

                e.text.clear();
                e.start.clear();
                e.start.reserve((size_t)count + 1);
                for (int i = 0; i < count; i++)
                {
                    int         len = 0;
                    const u16  *c = ChatKanji::Candidate(i, len);

                    e.start.push_back((u16)e.text.size());
                    for (int k = 0; c != nullptr && k < len; k++)
                        e.text.push_back(c[k]);
                }
                e.start.push_back((u16)e.text.size());
                e.used = true;
                return 0;
            }

            int     EntryCount(void)
            {
                return m_entry >= 0 && m_cache[m_entry].used ? (int)m_cache[m_entry].start.size() - 1 : 0;
            }

            const u16 *EntryCandidate(int i, int &len)
            {
                len = 0;
                if (i < 0 || i >= EntryCount())
                    return nullptr;

                const CacheEntry &e = m_cache[m_entry];

                len = (int)e.start[(size_t)i + 1] - (int)e.start[(size_t)i];
                return e.text.data() + e.start[(size_t)i];
            }

            // ---- メニュースレッド側 ----
            enum { M_IDLE = 0, M_STARTING, M_ENGINE, M_SHOW };
            int         m_phase = M_IDLE;
            u64         m_startTick = 0;                // M_STARTING に入った時刻（ゲームが受け取らないときの打ち切り）
            // 自動の取得（利用者の指示 2026-09-26: 入力して 300ms 待ってから自動で取る）
            const u64   kSettleTicks = 0;               // 2026-09-26: 300 -> 100 -> 0ms（利用者の指示: 待たずに取る）
            const u64   kRepeatDelayTicks = (u64)SYSCLOCK_ARM11 * 200 / 1000;  // メニューの kRepeatDelay と同じ
            const u64   kRepeatEveryTicks = (u64)SYSCLOCK_ARM11 * 60 / 1000;   // kRepeatEvery と同じ
            u32         m_key = 0;                      // いまの対象（未確定か選択）の指紋。0 = 対象なし
            u64         m_keySince = 0;                 // m_key になった時刻
            u32         m_doneKey = 0;                  // 取り終えた（または取れなかった）対象
            int         m_kanjiIndex = -1;
            int         m_compIndex = -1;
            bool        m_hooked = false;
            bool        m_hookFailed = false;
            u16         m_heldPrev = 0;
            bool        m_hotPrev = false;
            bool        m_wantEnter = false;            // ホットキー（Enter の代わり）。依頼の枠が空いたら出す
            int         m_wantApply = -1;               // 選んだ候補。依頼の枠が空いたら出す
            bool        m_preloadTried = false;         // このチャットで下準備を始めた
            int         m_sel = -1;
            float       m_scroll = 0.0f;
            int         m_count = 0;
            float       m_cellX[kMaxCand];
            float       m_cellW[kMaxCand];
            float       m_textW[kMaxCand];
            float       m_content = 0.0f;
            bool        m_touchPrev = false;
            bool        m_dragging = false;             // 候補欄の上で触れている
            bool        m_scrolling = false;            // 触れてからタッチスロップを越えた（以後は確定しない）
            int         m_tapCandidate = -1;            // 触れた候補（離したときに確定。スクロールになったら -1）
            float       m_flingV = 0.0f;                // 慣性の速さ（m_scroll の増え方、px/s）
            u64         m_lastTick = 0;                 // 前回の HandleTouch の時刻
            // 揺れの補正
            float       m_lastRaw = 0.0f;               // 最後に受け入れた生の x
            bool        m_spikeHeld = false;            // 飛び値を 1 回保留している
            float       m_spikeDt = 0.0f;               // 保留した回の経過時間（次の回に足す）
            float       m_fx = 0.0f, m_fdx = 0.0f;      // 1€ フィルタの位置と速さ
            float       m_hx = 0.0f;                    // ヒステリシスをかけた位置（スクロールに使う）
            float       m_touchStartX = 0.0f;           // 触れた所（補正後）
            float       m_histX[kTouchHist];            // 補正後の位置と時刻（速さの推定）
            u64         m_histT[kTouchHist];
            int         m_histCount = 0, m_histHead = 0;
            int         m_keyDown = -1;                 // 押している自前キー
            bool        m_keyInside = false;            // 押している指がまだキーの上か（押下の見た目）
            int         m_wantKey = -1;                 // 離したキー。依頼の枠が空いたら出す
            int         m_dy = 0;                       // 開閉アニメ（BG の N_All の平行移動）ぶんのずれ
            u32         m_texSeqSeen = 0;
            u32         m_texGen = 0;
            u64         m_keyRepeatAt = 0;              // 左右キーの長押しの次の時刻
            bool        m_texReady = false;
            u64         m_repeatAt = 0;                 // 十字キー左右の長押しの次の時刻
            u16         m_repeatBit = 0;
            float       m_dragStartX = 0.0f;            // スクロールを始めた所（補正後）
            float       m_dragStartScroll = 0.0f;

            Hook        g_hInput;
            Hook        g_hBack;
            Hook        g_hWait;
            Hook        g_hWindowIn;
            Hook        g_hKeysetLoad;
            Hook        g_hKeyUpdate;

            inline u32  R32(u32 a)          { return *(volatile u32 *)a; }
            inline void W32(u32 a, u32 v)   { *(volatile u32 *)a = v; }
            inline u8   R8(u32 a)           { return *(volatile u8 *)a; }
            inline void W8(u32 a, u8 v)     { *(volatile u8 *)a = v; }
            inline int  RI(u32 tm, u32 off) { return (int)R32(tm + off); }
            inline void WI(u32 tm, u32 off, int v) { W32(tm + off, (u32)v); }

            u32     ThreadTag(void)
            {
                return (u32)getThreadLocalStorage();
            }

            // 読める番地か（svcQueryMemory。VRAM の写しの前に）
            bool    ReadableRange(u32 address, u32 bytes)
            {
                u64 cursor = address;
                const u64 end = (u64)address + bytes;

                while (cursor < end)
                {
                    MemInfo     info;
                    PageInfo    page;

                    if (R_FAILED(svcQueryMemory(&info, &page, (u32)cursor)) || !(info.perm & MEMPERM_READ)
                        || info.base_addr > cursor || (u64)info.base_addr + info.size <= cursor)
                        return false;
                    cursor = (u64)info.base_addr + info.size;
                }
                return true;
            }

            // TextManager の数値が筋の通った範囲か（書き換える前に毎回）
            bool    TmSane(u32 tm)
            {
                if (tm < 0x08000000u || tm >= 0x40000000u || (tm & 3) != 0)
                    return false;
                const int   len = RI(tm, TM_LEN), max = RI(tm, TM_MAX), cur = RI(tm, TM_CURSOR);
                const int   pend = RI(tm, TM_PEND);
                const u32   buf = R32(tm + TM_BUF);

                return buf >= 0x08000000u && buf < 0x40000000u && (buf & 1) == 0
                       && max > 0 && max <= 1024 && len >= 0 && len <= max + pend
                       && cur >= 0 && cur <= len && pend >= 0 && pend <= cur;
            }

            bool    Consistent(u32 tm)
            {
                if (tm != g_sessionTm || !TmSane(tm))
                    return false;
                const int   cur = RI(tm, TM_CURSOR), pend = RI(tm, TM_PEND);

                if (g_sessionSel)
                {
                    const int   a = RI(tm, TM_ANCHOR);
                    const int   s0 = a < cur ? a : cur;
                    const int   e0 = a < cur ? cur : a;

                    return R8(tm + TM_SEL) != 0 && pend == 0 && s0 == g_start && e0 - s0 == g_curLen;
                }
                return pend == g_curLen && pend > 0 && cur - pend == g_start;
            }

            // 選択範囲を未確定に変える（候補を初めて入れるとき。以後は未確定と同じ流れ）
            void    SelectionToPending(u32 tm)
            {
                const int   e = g_start + g_curLen;

                W8(tm + TM_SEL, 0);
                WI(tm, TM_CURSOR, e);
                WI(tm, TM_ANCHOR, e);
                WI(tm, TM_90, -1);
                WI(tm, TM_EXTRA, 0);
                WI(tm, TM_PEND, g_curLen);
                g_sessionSel = false;
            }

            // 範囲 [g_start, g_start + g_curLen) を text で置き換え、未確定にする。
            //   ゲーム自身のローマ字変換と同じ手順（DeleteRange -> 字ごとに InsertChar）。字ごとの声もゲームと同じ。
            bool    Replace(u32 tm, const u16 *text, int n)
            {
                const DeleteRangeFn del = (DeleteRangeFn)kDeleteRange;
                const InsertCharFn  ins = (InsertCharFn)kInsertChar;
                int                 k = 0;

                del(tm, g_start, g_curLen);             // 未確定 +0x24 は重なりの分だけ減って 0、カーソルは g_start
                WI(tm, TM_PEND, 0);
                while (k < n)
                {
                    if (ins(tm, text[k], g_start + k, 0, 0.0f) == 0)
                        break;
                    k++;
                }
                WI(tm, TM_EXTRA, 0);
                WI(tm, TM_PEND, k);
                g_curLen = k;
                return k == n;
            }

            int     StartSession(u32 tm)
            {
                if (!TmSane(tm))
                    return START_NO_TM;
                ((FinishCellFn)kFinishCell)(tm, 1);

                const int   cur = RI(tm, TM_CURSOR), pend = RI(tm, TM_PEND);
                int         start, n;

                bool        selection = false;

                if (pend > 0)
                {
                    start = cur - pend;
                    n = pend;
                }
                else if (R8(tm + TM_SEL) != 0 && RI(tm, TM_ANCHOR) != cur)
                {
                    const int   a = RI(tm, TM_ANCHOR);
                    const int   s = a < cur ? a : cur;
                    const int   e = a < cur ? cur : a;

                    if (s < 0 || e > RI(tm, TM_LEN))
                        return START_NO_TARGET;
                    selection = true;
                    start = s;
                    n = e - s;
                    // 選択はここでは変えない（候補を選んだときに未確定へ変える）
                }
                else
                    return START_NO_TARGET;
                if (n > kReadingMax)
                    return START_TOO_LONG;

                const u32   buf = R32(tm + TM_BUF);

                for (int i = 0; i < n; i++)
                    g_reading[i] = *(volatile u16 *)(buf + (u32)(start + i) * 2);
                g_reading[n] = 0;
                g_readingLen = n;
                g_start = start;
                g_curLen = n;
                g_applied = -1;
                g_sessionTm = tm;
                g_sessionSel = selection;
                g_state = S_WAIT_ENGINE;
                return START_OK;
            }

            void    ApplyCandidate(u32 tm, int index)
            {
                const int   len = g_applyLen;
                const u16   *cand = g_applyText;

                if (g_state == S_IDLE || !Consistent(tm))
                    return;
                if (len <= 0 || len >= SWKBD_TEXT_UNITS)
                    return;
                if (g_sessionSel)
                    SelectionToPending(tm);
                if (!Replace(tm, cand, len))
                {
                    // 入り切らない候補は読みへ戻す（読みは入っていたので入る）
                    Replace(tm, g_reading, g_readingLen);
                    g_applied = -1;
                    g_state = S_ACTIVE;
                    return;
                }
                g_applied = index;
                g_state = S_ACTIVE;
            }

            void    SetKeyTexMaps(const u32 *copyPa);

            // 今のキー配列の「空白」キーのテクスチャを、そのキー配列の arc のアクセサから取る（初めてなら読み込みが始まる）
            bool    CaptureKeyTextures(u32 bsskb, u32 kind)
            {
                typedef void (*GetTextureFn)(u32 *out, u32 accessor, const char *name);
                const u32   acc = bsskb + kBsSkbKeysetAcc;
                const char *names[2];
                u32         info[2][5];

                if (bsskb < 0x08000000u || bsskb >= 0x40000000u || R32(acc) != kVtAccessorVram
                    || R32(kVtAccessorVram + 0x10) != kGetTexture)
                    return false;
                switch (kind)
                {
                case 0:  names[0] = "KtpQweKeySpc.bclim";        names[1] = "KtpQweKeySpcon.bclim";        break;
                case 1:  names[0] = "Ktp50onKeySpc.bclim";       names[1] = "Ktp50onKeySpcon.bclim";       break;
                case 2:
                case 3:  names[0] = "KtpGrdKeySpc.bclim";        names[1] = "KtpGrdKeySpcon.bclim";        break;
                case 4:  names[0] = "KtpCellKeyOpt01min1.bclim"; names[1] = "KtpCellKeyOpt01min1on.bclim"; break;
                default: return false;
                }
                for (int i = 0; i < 2; i++)
                {
                    std::memset(info[i], 0, sizeof(info[i]));
                    ((GetTextureFn)kGetTexture)(info[i], acc, names[i]);
                    if (info[i][1] == 0 || (info[i][2] & 0xFFFF) == 0 || (info[i][3] & 0xFFFF) == 0)
                        return false;
                }
                std::memcpy(g_texInfo, info, sizeof(g_texInfo));
                g_texAge = 0;
                g_texCopyTried = false;
                SetKeyTexMaps(nullptr);                 // まずは VRAM のものを使う（写すのは転送が済んでから）
                return true;
            }

            // TexMap を組み直して公開する（copyPa が null なら VRAM のもの）
            void    SetKeyTexMaps(const u32 *copyPa)
            {
                typedef void (*UpdateFn)(u32 *texMap);
                const u32   (&info)[2][5] = g_texInfo;

                __atomic_add_fetch(&g_texSeq, 1u, __ATOMIC_ACQ_REL);   // 奇数: 書いている途中
                for (int i = 0; i < 2; i++)
                {
                    // Material の ctor と同じ組み方（0x4BCEA8）: 番地・大きさ・書式、ラップ = クランプ、
                    //   フィルタ = 資源の rawS/rawT 4（KeytopModeSelect の material）→ bits4-6 = 1 / bit7 = 1
                    std::memset(g_texMap[i], 0, sizeof(g_texMap[i]));
                    g_texMap[i][0] = info[i][0];
                    g_texMap[i][1] = copyPa != nullptr ? copyPa[i] : info[i][1];
                    g_texMap[i][2] = info[i][2];
                    g_texMap[i][3] = info[i][3];
                    g_texMap[i][4] = ((info[i][4] & 0xFFu) << 8 & 0xF00u) | 0x10u | 0x80u;
                    ((UpdateFn)kTexMapUpdate)(g_texMap[i]);
                }
                g_texOk = true;
                g_texCopied = copyPa != nullptr;
                g_texCopyGen = GuiRenderer::Generation();
                __atomic_add_fetch(&g_texSeq, 1u, __ATOMIC_ACQ_REL);   // 偶数: 揃った
            }

            // 取ったテクスチャを借りたヒープへ写し、以後はそれを使う（キー配列を切り替えても剥がれない）。ゲームのスレッド
            void    CopyKeyTextures(void)
            {
                const u32   (&info)[2][5] = g_texInfo;

                g_texCopyTried = true;
                // VRAM（PA 0x18000000..0x18600000、VA = PA + 0x07000000）にあり、読めるなら、借りたヒープへ写す
                u32         copyPa[2] = { 0, 0 };
                {
                    u32         spare = 0;
                    const u32   va = GuiRenderer::GpuSpare(spare);
                    u32         at = 0;
                    bool        ok = va != 0;

                    for (int i = 0; i < 2 && ok; i++)
                    {
                        // nw::lyt の書式 → 1 画素のビット数（F291 の写像表: 0 L8 / 1 A8 / 2 LA4 / 3 LA8 / 4 HILO8 / 5 RGB565 /
                        //   6 RGB8 / 7 RGBA5551 / 8 RGBA4 / 9 RGBA8 / 10 ETC1 / 11 ETC1A4 / 12 L4 / 13 A4）
                        static const u8 kBits[14] = { 8, 8, 8, 16, 16, 16, 24, 16, 16, 32, 4, 8, 4, 4 };
                        const u32   fmt = info[i][4] & 0xFFu;
                        const u32   w = info[i][3] & 0xFFFFu, h = info[i][3] >> 16;
                        const u32   src = info[i][1];
                        const u32   bytes = fmt < 14 ? w * h * kBits[fmt] / 8 : 0;

                        if (bytes == 0 || src < 0x18000000u || src + bytes > 0x18600000u || at + bytes > spare
                            || !ReadableRange(src + 0x07000000u, bytes))
                        {
                            ok = false;
                            break;
                        }
                        std::memcpy((void *)(va + at), (const void *)(src + 0x07000000u), bytes);
                        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, va + at, bytes);   // GPU は D-cache を見ない
                        copyPa[i] = va + at - 0x10000000u;
                        at = (at + bytes + 0x7Fu) & ~0x7Fu;
                    }
                    if (!ok)
                        return;                         // 写せない: VRAM のものを使い続ける
                }
                SetKeyTexMaps(copyPa);
            }

            inline float RF(u32 a)
            {
                float v;

                std::memcpy(&v, (const void *)a, 4);
                return v;
            }

            // キー配列の背面 W_ktpShade を変換行の上まで伸ばす／戻す（利用者の指示 2026-09-26）。ゲームのスレッド
            //   ★ペインの番地は覚えない。キー配列を切り替えると作り直され、同じ番地に別のものが来ることがある
            //   （覚えていると伸ばし直さずに消えた。利用者の報告）。毎回いまのキー配列のレイアウトから探し、
            //   いまの高さと位置で「元の大きさ／伸ばし済み」を見分け、違うときだけ書く。
            void    AdjustShade(bool want)
            {
                typedef u32  (*FindPaneFn)(u32 object, const char *name);
                typedef void (*SetTranslateFn)(u32 pane, const float *v);
                const u32   bsskb = R32(kBsSkbPtr);
                bool        on = false;

                if (bsskb >= 0x08000000u && bsskb < 0x40000000u)
                {
                    const u32   keyset = R32(bsskb + kBsSkbKeyset);
                    const u32   object = keyset >= 0x08000000u && keyset < 0x40000000u ? R32(keyset + kKeysetLayout) : 0;
                    u32         pane = 0;

                    if (object >= 0x08000000u && object < 0x40000000u && R32(object + 72) != 0)
                        pane = ((FindPaneFn)kFindPane)(object, "W_ktpShade");
                    if (pane >= 0x08000000u && pane < 0x40000000u
                        && std::memcmp((const void *)(pane + 0xB8), "W_ktpShade", 11) == 0)
                    {
                        const float h = RF(pane + 0x4C), ty = RF(pane + 0x2C);
                        const float hx = kShadeOrigH + kShadeGrow, tyx = kShadeOrigTy + kShadeGrow * 0.5f;
                        const bool  orig = h == kShadeOrigH && ty == kShadeOrigTy;
                        const bool  ext = h == hx && ty == tyx;

                        if ((orig || ext) && want != ext)
                        {
                            const float v[3] = { RF(pane + 0x28), want ? tyx : kShadeOrigTy, RF(pane + 0x30) };
                            const float nh = want ? hx : kShadeOrigH;

                            std::memcpy((void *)(pane + 0x4C), &nh, 4);
                            ((SetTranslateFn)kSetTranslate)(pane, v);   // 行列を作り直させる（ゲームの BsSkb_WindowOut_Calc と同じ関数）
                            on = want;
                        }
                        else
                            on = ext;
                    }
                }
                g_shadeOn = on;
            }

            // キー配列が変わっていたら取り直す（開くアニメの間は window in、以後は wait から。ゲームのスレッド）
            void    RefreshKeyTextures(void)
            {
                const u32   bsskb = R32(kBsSkbPtr);

                if (bsskb < 0x08000000u || bsskb >= 0x40000000u)
                    return;

                const u32   keyset = R32(bsskb + kBsSkbKeyset);
                const u32   kind = R32(kKeysetKind);

                // 写しがあれば取り直さない（描画器が組み直されていれば写しも無効 → 取り直して、また数回あとに写す）
                if (g_texCopied && g_texOk && g_texCopyGen == GuiRenderer::Generation())
                    return;
                if (g_texOk && g_texCopyGen != GuiRenderer::Generation())
                    g_texOwner = 0;
                if (bsskb == g_texOwner && keyset == g_texKeyset && kind == g_texKind)
                {
                    // 同じキー配列のまま kTexCopyDelay 回たったら写す（VRAM への転送はとうに済んでいる）
                    if (g_texOk && !g_texCopied && !g_texCopyTried && ++g_texAge >= kTexCopyDelay)
                        CopyKeyTextures();
                    return;
                }
                g_texOk = false;
                __atomic_add_fetch(&g_texSeq, 2u, __ATOMIC_ACQ_REL);   // 前の分はもう使わない（偶数のまま進める）
                if (keyset != 0)
                    CaptureKeyTextures(bsskb, kind);    // 取れなければこのキー配列では文字だけのキー
                g_texOwner = bsskb;
                g_texKeyset = keyset;
                g_texKind = kind;
            }

            // 全選択（「クリア」キー。利用者の指示で全選択にした）: 起点 0、カーソルを末尾へ（選択中にする）
            void    SelectAll(u32 tm)
            {
                typedef int (*SetCursorFn)(u32 tm, int pos, int clearSel, int moveAnchor);
                const SetCursorFn set = (SetCursorFn)kSetCursor;

                set(tm, 0, 1, 1);                       // 途中の入力を確定し、起点 0
                if (RI(tm, TM_LEN) > 0)
                    set(tm, RI(tm, TM_LEN), 1, 0);      // 起点を残して末尾へ = 全体を選択
            }

            // 選択を解く（全選択キーが「解除」のとき）: カーソルの位置で解く
            void    Deselect(u32 tm)
            {
                typedef int (*SetCursorFn)(u32 tm, int pos, int clearSel, int moveAnchor);

                ((SetCursorFn)kSetCursor)(tm, RI(tm, TM_CURSOR), 1, 1);
            }

            // 左右: 選択中なら選択の端へ寄せて解く。そうでなければ 1 字動かす（途中の入力はゲームの処理で確定）
            void    MoveCursor(u32 tm, int dir)
            {
                typedef int (*SetCursorFn)(u32 tm, int pos, int clearSel, int moveAnchor);
                const int   cur = RI(tm, TM_CURSOR), anchor = RI(tm, TM_ANCHOR);
                int         pos = cur + dir;

                if (R8(tm + TM_SEL) != 0 && anchor != cur)
                    pos = dir < 0 ? (anchor < cur ? anchor : cur) : (anchor < cur ? cur : anchor);
                ((SetCursorFn)kSetCursor)(tm, pos, 1, 1);
            }

            // ---- フック本体（ゲームのスレッド）----
            // 入力欄の見た目の状態（字が入ったかどうかを見分ける）
            struct TextSnap { int len, cur, pend, anchor; u8 sel; u16 prev; };

            TextSnap Snap(u32 tm)
            {
                TextSnap    t;
                const u32   buf = R32(tm + TM_BUF);

                t.len = RI(tm, TM_LEN);
                t.cur = RI(tm, TM_CURSOR);
                t.pend = RI(tm, TM_PEND);
                t.anchor = RI(tm, TM_ANCHOR);
                t.sel = R8(tm + TM_SEL);
                t.prev = t.cur > 0 ? *(volatile u16 *)(buf + (u32)(t.cur - 1) * 2) : 0;
                return t;
            }

            bool    SameText(const TextSnap &a, const TextSnap &b)
            {
                return a.len == b.len && a.cur == b.cur && a.prev == b.prev && a.sel == b.sel && a.anchor == b.anchor;
            }

            __attribute__((noinline)) int ChatImeInputChar(u32 tm, u32 ch, u32 romaji, u32 combine)
            {
                HookContext &ctx = HookContext::GetCurrent();
                const u32   ret = (u32)__builtin_return_address(0);

                g_threadInput = ThreadTag();
                if (!g_convOn || g_broken || !TmSane(tm))
                    return ctx.OriginalFunction<int>(tm, ch, romaji, combine);

                const TextSnap  before = Snap(tm);
                // 変換の区切りがある間に字を打った（Enter は素通し。ゲームが確定する）
                const bool      session = g_state != S_IDLE && tm == g_sessionTm && ch != 10;
                const u32       stateBefore = g_state;

                // 候補を選んだあとなら、その候補で確定してから打たせる（字が入らなかったら下で戻す）
                if (session && g_state == S_ACTIVE && g_applied >= 0 && Consistent(tm))
                    WI(tm, TM_PEND, 0);

                const bool  kana = ret == kKanaCall[0] + 4 || ret == kKanaCall[1] + 4 || ret == kKanaCall[2] + 4;

                if (!kana && romaji != 0 && tm == g_ownTm && g_ownPend > 0
                    && RI(tm, TM_PEND) == g_ownPend && RI(tm, TM_CURSOR) == g_ownCursor)
                    WI(tm, TM_PEND, 0);                 // かなの未確定を確定してからローマ字を打たせる

                int         r;
                const bool  compose = g_compOn && kana && g_chatOpen && ch != 0x20 && ch != 0x3000 && ch != 10;

                // 元の処理に渡す直前の未確定の長さ（Enter はここを 0 にして確定する。文字列は変わらない）
                const int   pendIn = RI(tm, TM_PEND);

                if (!compose)
                    r = ctx.OriginalFunction<int>(tm, ch, romaji, combine);
                else
                {
                    const bool  sel = R8(tm + TM_SEL) != 0 && RI(tm, TM_ANCHOR) != RI(tm, TM_CURSOR);
                    const int   keep = sel ? 0 : RI(tm, TM_PEND);
                    const int   len0 = RI(tm, TM_LEN);

                    // 元関数はローマ字モード 0 のとき先に未確定を確定するので、一時的に 0 にしておく
                    WI(tm, TM_PEND, 0);
                    r = ctx.OriginalFunction<int>(tm, ch, romaji, combine);

                    const int   delta = RI(tm, TM_LEN) - len0;
                    int         pend = sel ? (r != 0 ? 1 : 0) : keep + delta;
                    const int   cur = RI(tm, TM_CURSOR);

                    if (pend < 0)
                        pend = 0;
                    if (pend > cur)
                        pend = cur;
                    WI(tm, TM_EXTRA, 0);
                    WI(tm, TM_PEND, pend);
                }

                // ★字が入らなかった（文字数の上限など）: 何も変わっていないので、未確定と変換の区切りを元に戻す
                //   （以前は入らなくても変換を終え、選択中の候補が消えた。利用者の報告 2026-09-26）
                //   ★未確定の長さが元の処理で変わったとき（Enter の確定など）は「入らなかった」ではない（2026-09-26 利用者の報告:
                //   未変換のまま Enter で確定されなかった）。合成の経路は未確定を自分で書くので、文字列だけで見る。
                if (SameText(before, Snap(tm)) && (compose || RI(tm, TM_PEND) == pendIn))
                {
                    WI(tm, TM_PEND, before.pend);
                    if (session)
                        g_state = stateBefore;
                    return r;
                }
                if (session)
                    g_state = S_IDLE;
                g_ownTm = 0;
                g_ownPend = 0;
                if (compose)
                {
                    g_ownTm = tm;
                    g_ownCursor = RI(tm, TM_CURSOR);
                    g_ownPend = RI(tm, TM_PEND);
                }
                return r;
            }

            __attribute__((noinline)) int ChatImeBackspace(u32 tm, u32 silent)
            {
                HookContext &ctx = HookContext::GetCurrent();

                if (g_convOn && !g_broken && g_state != S_IDLE && Consistent(tm))
                {
                    if (g_state == S_ACTIVE && g_applied >= 0)
                    {
                        // 変換を戻して読みの未確定へ（一般的な IME と同じ）。候補はメニュー側が取り直す
                        Replace(tm, g_reading, g_readingLen);
                        g_applied = -1;
                        g_state = S_IDLE;
                        g_reverted = true;
                        return 1;
                    }
                    g_state = S_IDLE;
                }
                return ctx.OriginalFunction<int>(tm, silent);
            }

            __attribute__((noinline)) int ChatImeWaitCalc(u32 self)
            {
                HookContext &ctx = HookContext::GetCurrent();

                g_threadWait = ThreadTag();
                if (g_threadInput != 0 && g_threadInput != g_threadWait)
                    g_broken = true;                    // 前提（同じスレッド）が崩れた。書き換えを止める
                if (g_convOn && !g_broken)
                {
                    const u32   tm = R32(kTmPtr);

                    RefreshKeyTextures();
                    const u32   kind = __atomic_load_n(&g_reqKind, __ATOMIC_ACQUIRE);

                    if (kind == R_START)
                        g_startResult = (u32)StartSession(tm);
                    else if (kind == R_APPLY)
                        ApplyCandidate(tm, (int)g_reqArg);
                    else if (kind == R_ABORT)
                        g_state = S_IDLE;
                    else if (kind == R_ENTER && TmSane(tm))
                        ((int (*)(u32, u32, u32, u32))kInputChar)(tm, 10, 0, 0);   // フック経由。字 10 は素通しでゲームが確定する
                    else if ((kind == R_SELECT_ALL || kind == R_DESELECT || kind == R_LEFT || kind == R_RIGHT) && TmSane(tm))
                    {
                        if (kind == R_SELECT_ALL)
                            SelectAll(tm);
                        else if (kind == R_DESELECT)
                            Deselect(tm);
                        else
                            MoveCursor(tm, kind == R_LEFT ? -1 : 1);
                        // 全選択・解除は切り替えの音、左右は Backspace の音（利用者の指示 2026-09-26）
                        ((void (*)(u32))kPlaySound)(kind == R_LEFT || kind == R_RIGHT ? kSoundBackspace : kSoundChangeKeySet);
                    }
                    if (kind != R_NONE)
                        __atomic_store_n(&g_reqKind, (u32)R_NONE, __ATOMIC_RELEASE);
                    // Enter・送信・カーソル移動で未確定が確定したら終わり
                    if (g_state != S_IDLE && !Consistent(tm))
                        g_state = S_IDLE;
                }
                {
                    const int r = ctx.OriginalFunction<int>(self);

                    AdjustShade(g_convOn && g_chatOpen && !g_broken);
                    return r;
                }
            }

            // 開くアニメ（BsSkb の window in）の間にもテクスチャを取る（利用者の指示: 開いた段階で読み込みを終わらせる）
            __attribute__((noinline)) int ChatImeWindowInCalc(u32 self)
            {
                HookContext &ctx = HookContext::GetCurrent();

                if (g_convOn && !g_broken)
                    RefreshKeyTextures();

                const int r = ctx.OriginalFunction<int>(self);

                AdjustShade(g_convOn && g_chatOpen && !g_broken);
                return r;
            }

            // キー配列の切り替え（BsSkb の keyset load）: 元の処理が arc を読み終えた回に新しいキー配列を作るので、
            //   その直後（同じフレーム、描く前）に背面を伸ばす（切り替えでちらつかないように）
            __attribute__((noinline)) int ChatImeKeysetLoadCalc(u32 self)
            {
                HookContext &ctx = HookContext::GetCurrent();
                const int   r = ctx.OriginalFunction<int>(self);

                AdjustShade(g_convOn && g_chatOpen && !g_broken);
                return r;
            }

            // キーの入力の待ちを無くす（F046）。ゲームが次の 2 フレームでやる後片付けを、この回の更新の前にやる:
            //   InputButton の状態 2（離すアニメの待ち）は、ゲームの判定 gui_Button_AnimsDone(button, 2) が真なら状態 0 へ（状態 2 の処理と同じ）。
            //   状態 0 に戻った字のキー・繰り返しキーの +12 を落とす（状態 0 の処理の最初と同じ）。これで忙しい旗が立たず、どのキーもすぐ押せる。
            void    SkipKeyWait(void)
            {
                typedef int (*AnimDoneFn)(u32 button, int phase);
                u32         node = R32(kKeyList);

                for (int n = 0; n < 1024 && node != kKeyList && node >= 0x08000000u && node < 0x40000000u; n++, node = R32(node))
                {
                    const u32   b = node - 4;
                    const u32   vt = R32(b);

                    if (vt != kVtInputButton && vt != kVtRepeatInput)
                        continue;
                    if (vt == kVtInputButton && R32(b + BTN_STATE) == 2 && R8(b + BTN_TOGGLE) == 0
                        && ((AnimDoneFn)kButtonAnimDone)(b, 2) != 0)
                        W32(b + BTN_STATE, 0);
                    if (R32(b + BTN_STATE) == 0 && R8(b + BTN_HOLD) != 0)
                        W8(b + BTN_HOLD, 0);
                }
            }

            __attribute__((noinline)) int ChatImeKeyUpdate(u32 flags)
            {
                HookContext &ctx = HookContext::GetCurrent();

                // 旗 0x107 か 0x95819C があると元の処理は部品を更新しない（そのときは触らない）
                if (g_convOn && !g_broken && (flags & 0x107u) == 0 && R32(kKeyUpdateOff) == 0)
                    SkipKeyWait();
                return ctx.OriginalFunction<int>(flags);
            }

            // ---- メニュースレッド ----
            bool    CodeMatches(void)
            {
                const u32   words[][2] = {
                    { kInputChar, kInputCharOrig }, { kBackspace, kBackspaceOrig }, { kWaitCalc, kWaitCalcOrig },
                    { kDeleteRange, kDeleteRangeOrig }, { kInsertChar, kInsertCharOrig }, { kFinishCell, kFinishCellOrig },
                    { kKanaCall[0], kKanaCallWord[0] }, { kKanaCall[1], kKanaCallWord[1] }, { kKanaCall[2], kKanaCallWord[2] },
                    { kSetCursor, kSetCursorOrig }, { kPlaySound, kPlaySoundOrig }, { kTexMapUpdate, kTexMapUpdateOrig },
                    { kGetTexture, kGetTextureOrig }, { kVtAccessorVram + 0x10, kGetTexture },
                    { kWindowInCalc, kWindowInCalcOrig }, { kKeysetLoadCalc, kKeysetLoadCalcOrig }, { kFindPane, kFindPaneOrig }, { kSetTranslate, kSetTranslateOrig },
                    { kKeyUpdate, kKeyUpdateOrig }, { kButtonAnimDone, kButtonAnimDoneOrig },
                };

                if (Process::GetTitleID() != 0x0004000000086200ULL)
                    return false;
                for (const auto &w : words)
                    if (R32(w[0]) != w[1])
                        return false;
                for (const auto &w : kKeyVtCheck)
                    if (R32(w[0]) != w[1])
                        return false;
                return true;
            }

            bool    InstallHooks(void)
            {
                if (m_hooked)
                    return true;
                if (m_hookFailed)
                    return false;
                if (!CodeMatches())
                {
                    m_hookFailed = true;
                    GuiMenu::NotifyRed(kKanji, u8"対応していない版です。");
                    return false;
                }
                g_hInput.InitializeForMitm(kInputChar, (u32)ChatImeInputChar);
                g_hBack.InitializeForMitm(kBackspace, (u32)ChatImeBackspace);
                g_hWait.InitializeForMitm(kWaitCalc, (u32)ChatImeWaitCalc);
                g_hWindowIn.InitializeForMitm(kWindowInCalc, (u32)ChatImeWindowInCalc);
                g_hKeysetLoad.InitializeForMitm(kKeysetLoadCalc, (u32)ChatImeKeysetLoadCalc);
                g_hKeyUpdate.InitializeForMitm(kKeyUpdate, (u32)ChatImeKeyUpdate);
                if (g_hInput.Enable() != HookResult::Success || g_hBack.Enable() != HookResult::Success
                    || g_hWait.Enable() != HookResult::Success || g_hWindowIn.Enable() != HookResult::Success
                    || g_hKeysetLoad.Enable() != HookResult::Success || g_hKeyUpdate.Enable() != HookResult::Success)
                {
                    // 入った分は外す（まだ何も走っていない。旗も立てていない）
                    g_hInput.Disable();
                    g_hBack.Disable();
                    g_hWait.Disable();
                    g_hWindowIn.Disable();
                    g_hKeysetLoad.Disable();
                    g_hKeyUpdate.Disable();
                    m_hookFailed = true;
                    GuiMenu::NotifyRed(kKanji, u8"フックを入れられません。");
                    return false;
                }
                m_hooked = true;
                return true;
            }

            void    Post(u32 kind, s32 arg)
            {
                g_reqArg = arg;
                __atomic_store_n(&g_reqKind, kind, __ATOMIC_RELEASE);
            }

            void    Reset(void)
            {
                if (m_phase == M_ENGINE)
                    ChatKanji::Dismiss();
                m_phase = M_IDLE;
                m_count = 0;
                m_entry = -1;
                m_wantApply = -1;
                m_sel = -1;
                m_scroll = 0.0f;
                m_flingV = 0.0f;
                m_dragging = false;
            }

            // 変換の対象（未確定、無ければ選択範囲）の指紋。読むだけ（書き換えはゲームのスレッド）。
            //   取り違えても害は無い: 実際の読みはゲームのスレッドが R_START で取り直し、候補を入れる前にも照合する。
            u32     TargetKey(void)
            {
                const u32   tm = R32(kTmPtr);

                if (!TmSane(tm))
                    return 0;
                const int   cur = RI(tm, TM_CURSOR), pend = RI(tm, TM_PEND), len = RI(tm, TM_LEN);
                int         start = 0, n = 0;
                u32         h = 2166136261u;

                if (pend > 0)
                {
                    start = cur - pend;
                    n = pend;
                }
                else if (R8(tm + TM_SEL) != 0 && RI(tm, TM_ANCHOR) != cur)
                {
                    const int a = RI(tm, TM_ANCHOR);

                    start = a < cur ? a : cur;
                    n = (a < cur ? cur : a) - start;
                    h ^= 0x5E1u;
                }
                if (n <= 0 || n > kReadingMax || start < 0 || start + n > len)
                    return 0;

                const u32   buf = R32(tm + TM_BUF);

                h = (h ^ (u32)start) * 16777619u;
                h = (h ^ (u32)n) * 16777619u;
                for (int i = 0; i < n; i++)
                    h = (h ^ *(volatile u16 *)(buf + (u32)(start + i) * 2)) * 16777619u;
                return h != 0 ? h : 1u;
            }

            float   MaxScroll(void)
            {
                const float m = m_content - (float)(kBarW - 2);

                return m > 0.0f ? m : 0.0f;
            }

            void    ClampScroll(void)
            {
                const float m = MaxScroll();

                if (m_scroll > m)
                    m_scroll = m;
                if (m_scroll < 0.0f)
                    m_scroll = 0.0f;
            }

            // chat-kanji-preview.js ensureCandidateVisible
            void    EnsureVisible(int i)
            {
                const float view = (float)(kBarW - 2);

                if (i < 0 || i >= m_count)
                    return;
                m_flingV = 0.0f;
                if (m_cellX[i] < m_scroll)
                    m_scroll = m_cellX[i];
                else if (m_cellX[i] + m_cellW[i] > m_scroll + view)
                    m_scroll = m_cellX[i] + m_cellW[i] - view;
                ClampScroll();
            }

            bool    CandidateUtf8(int i, char *out, unsigned cap)
            {
                int         len = 0;
                const u16   *c = EntryCandidate(i, len);

                return c != nullptr && ChatKanjiText::Utf8(c, (size_t)len, out, cap);
            }

            // chat-kanji-preview.js candidateLayout（幅は GPU の送りと同じく字形ごとに丸めない）
            void    BuildLayout(void)
            {
                char    buf[200];
                float   x = 0.0f;

                m_count = EntryCount();
                if (m_count > kMaxCand)
                    m_count = kMaxCand;
                for (int i = 0; i < m_count; i++)
                {
                    const float tw = CandidateUtf8(i, buf, sizeof(buf))
                                     ? GuiRenderer::MeasureTextNative(buf, kBarScale) : 0.0f;

                    m_textW[i] = tw;
                    m_cellW[i] = tw + (float)(kBarPad * 2);
                    m_cellX[i] = x;
                    x += m_cellW[i] + (float)kBarGap;
                }
                m_content = m_count > 0 ? x - (float)kBarGap : 0.0f;
            }

            void    Select(int i)
            {
                if (m_count <= 0)
                    return;
                m_sel = ((i % m_count) + m_count) % m_count;     // 未選択（-1）から左で最後、右で最初
                EnsureVisible(m_sel);
                m_wantApply = m_sel;                    // 依頼の枠が空いたら写して出す（FlushWants）
            }

            // 依頼の枠が空いているときだけ出す（ゲームのスレッドが読んでいる文字列を書き換えない）
            bool    AllSelected(void);

            void    FlushWants(void)
            {
                if (__atomic_load_n(&g_reqKind, __ATOMIC_ACQUIRE) != R_NONE)
                    return;
                if (m_wantApply >= 0 && m_phase == M_SHOW)
                {
                    int         len = 0;
                    const u16   *c = EntryCandidate(m_wantApply, len);

                    if (c != nullptr && len > 0 && len < SWKBD_TEXT_UNITS)
                    {
                        std::memcpy(g_applyText, c, (size_t)len * 2);
                        g_applyLen = len;
                        Post(R_APPLY, m_wantApply);
                    }
                    m_wantApply = -1;
                    return;
                }
                if (m_wantEnter)
                {
                    m_wantEnter = false;
                    Post(R_ENTER, 0);
                    return;
                }
                if (m_wantKey >= 0)
                {
                    Post(m_wantKey == KEY_SELECT_ALL ? (AllSelected() ? R_DESELECT : R_SELECT_ALL)
                         : m_wantKey == KEY_LEFT ? R_LEFT : R_RIGHT, 0);
                    m_wantKey = -1;
                }
            }

            // 対象が決まった（R_START を受け取った）あと: エンジンへ
            void    ShowEntry(int entry)
            {
                m_entry = entry;
                BuildLayout();
                m_scroll = 0.0f;
                m_flingV = 0.0f;
                m_tapCandidate = -1;           // 前の候補の上で触れていた指で、新しい候補を確定しない
                m_sel = -1;                             // まだ入力欄には入れない（選んだときに入れる）
                m_phase = m_count > 0 ? M_SHOW : M_IDLE;
            }

            // 開閉アニメ: BsSkb の BG レイアウトの N_All の平行移動 y（BG_in -240 -> 5 -> 0 / BG_out 0 -> -240）。
            //   ゲーム自身も N_All の値をキーボードの各レイアウトへ毎フレーム写している（0x57CF04）。読むだけ。
            float   ReadNAllY(bool &ok)
            {
                const u32   bsskb = R32(kBsSkbPtr);
                u32         stack[16];
                int         top = 0, visits = 0;

                ok = false;
                if (bsskb < 0x08000000u || bsskb >= 0x40000000u)
                    return 0.0f;

                const u32   wrapper = R32(bsskb + kBsSkbBgLayout);

                if (wrapper < 0x08000000u || wrapper >= 0x40000000u)
                    return 0.0f;
                stack[top++] = R32(wrapper + 72);       // 根のペイン（sub_7461D0 と同じ）
                while (top > 0 && visits < 64)
                {
                    const u32 pane = stack[--top];

                    visits++;
                    if (pane < 0x08000000u || pane >= 0x40000000u || (pane & 3) != 0)
                        continue;
                    if (std::memcmp((const void *)(pane + 0xB8), "N_All", 6) == 0)
                    {
                        float y;

                        std::memcpy(&y, (const void *)(pane + 0x2C), 4);
                        ok = y > -1000.0f && y < 1000.0f;
                        return y;
                    }
                    // 子リスト: 番兵 pane+0x14、節 = 子 + 4、次 = [節]
                    u32 node = R32(pane + 0x14);
                    int guard = 0;

                    while (node != pane + 0x14 && node != 0 && guard < 32 && top < 16)
                    {
                        stack[top++] = node - 4;
                        node = R32(node);
                        guard++;
                    }
                }
                return 0.0f;
            }

            // 全体が選択されているか（全選択キーを「解除」にする）。読むだけ
            bool    AllSelected(void)
            {
                const u32   tm = R32(kTmPtr);

                if (!TmSane(tm) || R8(tm + TM_SEL) == 0)
                    return false;

                const int   len = RI(tm, TM_LEN), a = RI(tm, TM_ANCHOR), c = RI(tm, TM_CURSOR);

                return len > 0 && ((a == 0 && c == len) || (c == 0 && a == len));
            }

            int     KeyAt(int x, int y)
            {
                for (int i = 0; i < KEY_COUNT; i++)
                    if (x >= kKeys[i].x && x < kKeys[i].x + kKeys[i].w
                        && y >= kKeys[i].y + m_dy && y < kKeys[i].y + kKeys[i].h + m_dy)
                        return i;
                return -1;
            }

            int     CandidateAt(int x)
            {
                const float cx = (float)(x - kBarX - 1) + m_scroll;

                for (int i = 0; i < m_count; i++)
                    if (cx >= m_cellX[i] && cx < m_cellX[i] + m_cellW[i])
                        return i;
                return -1;
            }

            bool    InsideBar(int x, int y)
            {
                return x >= kBarX && x < kBarX + kBarW && y >= kBarY + m_dy && y < kBarY + kBarH + m_dy;
            }

            // ---- タッチの揺れの補正（メニュースレッド）----
            void    HistPush(float x, u64 now);

            void    FilterReset(float x, u64 now)
            {
                m_lastRaw = x;
                m_spikeHeld = false;
                m_fx = x;
                m_hx = x;
                m_fdx = 0.0f;
                m_histCount = 0;
                m_histHead = 0;
                HistPush(x, now);
            }

            static float Alpha(float cutoff, float dt)
            {
                const float tau = 1.0f / (2.0f * 3.14159265f * cutoff);

                return 1.0f / (1.0f + tau / dt);
            }

            void    FilterPush(float x, float dt, u64 now)
            {
                // 飛び値の保留: 保留の次の値は、そのそばに続いた（本当に動いた）でも戻った（飛び値）でも、今の値を使えば済む
                const float med = x;

                if (m_spikeHeld)
                {
                    m_spikeHeld = false;
                    dt += m_spikeDt;
                }
                else if (x - m_lastRaw > kSpikePx || m_lastRaw - x > kSpikePx)
                {
                    m_spikeHeld = true;
                    m_spikeDt = dt;
                    return;
                }
                m_lastRaw = med;
                if (dt <= 0.0f)
                    return;
                // 1€ フィルタ
                const float dx = (med - m_fx) / dt;

                m_fdx += Alpha(kEuroDCutoff, dt) * (dx - m_fdx);

                const float speed = m_fdx < 0.0f ? -m_fdx : m_fdx;

                m_fx += Alpha(kEuroMinCutoff + kEuroBeta * speed, dt) * (med - m_fx);
                if (m_fx - m_hx > kHysteresisPx)
                    m_hx = m_fx - kHysteresisPx;
                else if (m_hx - m_fx > kHysteresisPx)
                    m_hx = m_fx + kHysteresisPx;
                HistPush(m_fx, now);
            }

            void    HistPush(float x, u64 now)
            {
                m_histX[m_histHead] = x;
                m_histT[m_histHead] = now;
                m_histHead = (m_histHead + 1) % kTouchHist;
                if (m_histCount < kTouchHist)
                    m_histCount++;
            }

            // 離す直前 kFlingWindow の間の平均の速さ（px/s、指の向き）。止めてから離したら 0 に近い
            float   ReleaseVelocity(u64 now)
            {
                const u64   window = (u64)(kFlingWindow * (float)SYSCLOCK_ARM11);
                const int   last = (m_histHead + kTouchHist - 1) % kTouchHist;
                int         first = last;

                if (m_histCount < 2 || now - m_histT[last] > window)
                    return 0.0f;
                for (int n = 1; n < m_histCount; n++)
                {
                    const int i = (last + kTouchHist - n) % kTouchHist;

                    if (now - m_histT[i] > window)
                        break;
                    first = i;
                }

                const float dt = (float)(m_histT[last] - m_histT[first]) / (float)SYSCLOCK_ARM11;

                return dt >= 0.02f ? (m_histX[last] - m_histX[first]) / dt : 0.0f;
            }

            void    HandleTouch(void)
            {
                const bool  down = Touch::IsDown();
                const UIntVector pos = Touch::GetPosition();
                const int   x = (int)pos.x, y = (int)pos.y;

                // 自前キー: 押している間は押下の見た目、キーの上で離したら実行（音もゲームの処理から鳴らす）
                const u64   now = svcGetSystemTick();

                if (down && !m_touchPrev)
                {
                    const int k = KeyAt(x, y);

                    if (k >= 0)
                    {
                        m_keyDown = k;
                        m_keyInside = true;
                        // 左右は押した瞬間に 1 回、押し続けると 200ms 後から 60ms ごと（利用者の指示 2026-09-26）
                        if (k == KEY_LEFT || k == KEY_RIGHT)
                        {
                            m_wantKey = k;
                            m_keyRepeatAt = now + kRepeatDelayTicks;
                        }
                    }
                }
                if (m_keyDown >= 0)
                {
                    if (down)
                    {
                        m_keyInside = KeyAt(x, y) == m_keyDown;
                        if ((m_keyDown == KEY_LEFT || m_keyDown == KEY_RIGHT) && m_keyInside && now >= m_keyRepeatAt)
                        {
                            m_wantKey = m_keyDown;
                            m_keyRepeatAt = now + kRepeatEveryTicks;
                        }
                    }
                    else
                    {
                        if (m_keyInside && m_keyDown == KEY_SELECT_ALL)
                            m_wantKey = m_keyDown;      // 全選択は離したときに
                        m_keyDown = -1;
                        m_keyInside = false;
                    }
                    GuiMenu::BlockGameTouch();
                    m_touchPrev = down;
                    return;
                }
                const float dt = m_lastTick != 0 ? (float)(now - m_lastTick) / (float)SYSCLOCK_ARM11 : 0.0f;

                m_lastTick = now;
                if (down && !m_touchPrev && InsideBar(x, y))
                {
                    // 慣性で動いている最中のタッチは止めるだけ（確定しない。よくある操作と同じ）
                    const bool  wasFlinging = m_flingV != 0.0f;

                    m_flingV = 0.0f;
                    FilterReset((float)x, now);
                    m_dragging = true;
                    m_scrolling = false;
                    m_touchStartX = m_fx;
                    m_tapCandidate = !wasFlinging && m_phase == M_SHOW ? CandidateAt(x) : -1;
                }
                if (m_dragging)
                {
                    if (!down)
                    {
                        m_dragging = false;
                        if (m_scrolling)
                        {
                            // 離す直前の速さで投げる
                            const float v = ReleaseVelocity(now);

                            if (v > kFlingMin || v < -kFlingMin)
                                m_flingV = -(v > kFlingMax ? kFlingMax : v < -kFlingMax ? -kFlingMax : v);
                        }
                        else if (m_tapCandidate >= 0 && m_phase == M_SHOW && m_tapCandidate < m_count
                                 && CandidateAt((int)(m_fx + 0.5f)) == m_tapCandidate)
                        {
                            // 動かさずに離した（タップ）: その候補を入れて確定（入れる依頼のあとに Enter の依頼が出る）
                            if (m_tapCandidate != m_sel)
                                Select(m_tapCandidate);
                            m_wantEnter = true;
                        }
                        m_tapCandidate = -1;
                        m_scrolling = false;
                    }
                    else
                    {
                        FilterPush((float)x, dt, now);
                        if (!m_scrolling)
                        {
                            const float moved = m_fx - m_touchStartX;

                            if (moved > kTapSlop || moved < -kTapSlop)
                            {
                                // スロップを越えた所から動かす（越えた分だけ飛ばない）
                                m_scrolling = true;
                                m_tapCandidate = -1;
                                m_dragStartX = m_hx;
                                m_dragStartScroll = m_scroll;
                            }
                        }
                        if (m_scrolling)
                        {
                            m_scroll = m_dragStartScroll - (m_hx - m_dragStartX);
                            ClampScroll();
                        }
                    }
                    GuiMenu::BlockGameTouch();          // 欄の上で始めた指はゲームへ渡さない
                }
                else if (m_flingV != 0.0f)
                {
                    // 慣性: 指数関数的に減速し、端か十分遅くなったら止める
                    const float before = m_scroll;

                    m_scroll += m_flingV * dt;
                    ClampScroll();
                    m_flingV *= expf(-dt / kFlingTau);
                    if ((m_flingV < kFlingStop && m_flingV > -kFlingStop) || (dt > 0.0f && m_scroll == before))
                        m_flingV = 0.0f;
                }
                m_touchPrev = down;
            }

            void    Step(int index, u16 held)
            {
                const u16   pressed = (u16)(held & ~m_heldPrev);
                const u64   now = svcGetSystemTick();
                const u32   key = TargetKey();

                // 開閉アニメに合わせてずらす（読めなければ 0）
                {
                    bool        ok = false;
                    const float y = ReadNAllY(ok);

                    m_dy = ok ? (int)(-y + (y > 0.0f ? -0.5f : 0.5f)) : 0;
                }
                // キーのテクスチャ（ゲームのスレッドが BsSkb ごとに取る）を描画器へ
                {
                    const u32 seq = __atomic_load_n(&g_texSeq, __ATOMIC_ACQUIRE);

                    if (m_texGen != GuiRenderer::Generation())
                    {
                        m_texGen = GuiRenderer::Generation();
                        m_texSeqSeen = 0xFFFFFFFFu;     // 組み直された: 登録し直す（写しは無効なので次の取得を待つ）
                        m_texReady = false;
                    }
                    if ((seq & 1u) == 0 && seq != m_texSeqSeen)
                    {
                        m_texSeqSeen = seq;
                        m_texReady = g_texOk && g_texCopyGen == GuiRenderer::Generation()
                                     && GuiRenderer::SetGameTexture(1, g_texMap[0], kKeyColor0)
                                     && GuiRenderer::SetGameTexture(2, g_texMap[1], kKeyColor0);
                    }
                    // キー配列が切り替わった（テクスチャはそのキー配列の資源）: 取り直されるまで使わない
                    if (m_texReady && !(g_texCopied && g_texCopyGen == GuiRenderer::Generation()))
                    {
                        const u32 bsskb = R32(kBsSkbPtr);

                        if (bsskb != g_texOwner || (bsskb >= 0x08000000u && bsskb < 0x40000000u
                            && (R32(bsskb + kBsSkbKeyset) != g_texKeyset || R32(kKeysetKind) != g_texKind)))
                        {
                            GuiRenderer::ClearGameTextures();
                            m_texReady = false;
                        }
                    }
                }
                const u16   hk = GuiMenu::ItemAppliedHotkey(index);
                const bool  hot = hk != 0 && (held & hk) == hk;

                m_heldPrev = held;
                // ホットキーは Enter の代わり（利用者の指示 2026-09-26）。ゲームの Enter と同じ処理（字 10 の入力）を呼ぶ
                if (hot && !m_hotPrev)
                    m_wantEnter = true;
                m_hotPrev = hot;
                // エンジンの下準備（辞書・コードの読み込みと初期化）をチャットを開いたときに裏で済ませておく
                if (!m_preloadTried && !ChatKanji::Busy() && !ChatKanji::EngineResident())
                    m_preloadTried = ChatKanji::Preload();
                if (m_phase != M_ENGINE)
                    ChatKanji::Poll();                  // 下準備だけの仕事を片付ける（公開するものは無い）
                if (key != m_key)
                {
                    m_key = key;
                    m_keySince = now;
                }
                if (g_reverted)
                {
                    // Backspace で読みへ戻した: 同じ読みを取り直す
                    g_reverted = false;
                    m_doneKey = 0;
                    m_keySince = now - kSettleTicks;
                }

                if (m_phase == M_IDLE)
                {
                    // 候補を選んで入れた後（S_ACTIVE）は取り直さない。対象が 300ms 変わらなければ取る。
                    if (g_state == S_IDLE && key != 0 && key != m_doneKey && now - m_keySince >= kSettleTicks)
                    {
                        m_doneKey = key;
                        Post(R_START, 0);
                        m_phase = M_STARTING;
                        m_startTick = now;
                    }
                }
                else if (m_phase == M_STARTING)
                {
                    // キーボードが入力待ち（BsSkb の wait）でないと依頼は受け取られない。1 秒で諦める
                    if (__atomic_load_n(&g_reqKind, __ATOMIC_ACQUIRE) != R_NONE
                        && svcGetSystemTick() - m_startTick > (u64)SYSCLOCK_ARM11)
                    {
                        __atomic_store_n(&g_reqKind, (u32)R_NONE, __ATOMIC_RELEASE);
                        m_phase = M_IDLE;
                    }
                    else if (__atomic_load_n(&g_reqKind, __ATOMIC_ACQUIRE) == R_NONE)
                    {
                        const u32 r = g_startResult;

                        if (r != START_OK)
                            m_phase = M_IDLE;           // 自動で取るので通知しない（打ち終わる前に消えた等）
                        else
                        {
                            const ChatKanji::RequestResult rr =
                                ChatKanji::RequestText(g_reading, (size_t)g_readingLen);

                            if (rr == ChatKanji::REQUEST_OK)
                                m_phase = M_ENGINE;
                            else if (rr == ChatKanji::REQUEST_BUSY)
                            {
                                // 下準備の途中など。少し後で取り直す
                                Post(R_ABORT, 0);
                                m_doneKey = 0;
                                m_keySince = now;
                                m_phase = M_IDLE;
                            }
                            else
                            {
                                // 変換できない文字（記号など）は通知せず、同じ対象では取り直さない（m_doneKey）
                                if (rr == ChatKanji::REQUEST_NO_FONT || rr == ChatKanji::REQUEST_NO_THREAD
                                    || rr == ChatKanji::REQUEST_UNSUPPORTED)
                                    GuiMenu::NotifyRed(kKanji, rr == ChatKanji::REQUEST_NO_FONT ? u8"フォントを取得できません。"
                                                               : rr == ChatKanji::REQUEST_UNSUPPORTED ? u8"対応していない版です。"
                                                               : u8"変換を開始できません。");
                                Post(R_ABORT, 0);
                                m_phase = M_IDLE;
                            }
                        }
                    }
                }
                else if (m_phase == M_ENGINE)
                {
                    // ★Poll の真は 1 回しか返らない。ほかの所（旧リストボックスの PollChatKanji）が先に受け取ると
                    //   ここでは永久に偽になり「変換中」で止まった（利用者報告 2026-09-26）。終わったかは Busy でも見る。
                    const bool  finished = ChatKanji::Poll() || !ChatKanji::Busy();

                    if (finished)
                    {
                        if (ChatKanji::Error()[0] != '\0')
                        {
                            GuiMenu::NotifyRed(kKanji, ChatKanji::Error());
                            Post(R_ABORT, 0);
                            Reset();
                        }
                        else if (ChatKanji::CandidateCount() <= 0 || g_state == S_IDLE)
                        {
                            // 候補なし、または待っている間に読みが変わった
                            Post(R_ABORT, 0);
                            Reset();
                        }
                        else
                        {
                            const int entry = TakeResult();

                            ChatKanji::Dismiss();       // 結果は写した。次の依頼を受けられるようにする
                            if (entry < 0)
                            {
                                Post(R_ABORT, 0);
                                Reset();
                            }
                            else
                                ShowEntry(entry);
                        }
                    }
                }
                else if (m_phase == M_SHOW)
                {
                    const bool  applying = __atomic_load_n(&g_reqKind, __ATOMIC_ACQUIRE) != R_NONE;

                    if (!applying && g_state == S_IDLE)
                    {
                        Reset();                        // Enter・Backspace・字の入力・カーソル移動で終わった
                        return;
                    }
                    // 左右: 押した瞬間に 1 つ、押し続けると 200ms 後から 60ms ごとに（メニューの ControlRepeater と同じ）
                    u16 step = 0;

                    if ((pressed & (HB_LEFT | HB_RIGHT)) != 0)
                    {
                        step = (u16)(pressed & HB_RIGHT ? HB_RIGHT : HB_LEFT);
                        m_repeatBit = step;
                        m_repeatAt = now + kRepeatDelayTicks;
                    }
                    else if (m_repeatBit != 0 && (held & m_repeatBit) != 0 && now >= m_repeatAt)
                    {
                        step = m_repeatBit;
                        m_repeatAt = now + kRepeatEveryTicks;
                    }
                    if ((held & m_repeatBit) == 0)
                        m_repeatBit = 0;
                    if (step == HB_RIGHT)
                        Select(m_sel + 1);
                    else if (step == HB_LEFT)
                        Select(m_sel < 0 ? -1 : m_sel - 1);
                }
                // 候補が出ている間は十字キーをゲームへ渡さない（渡すとカーソルが動いて確定してしまう）
                if (m_phase == M_SHOW)
                    GuiMenu::BlockGameDpad();
                HandleTouch();
                FlushWants();
            }
        }

        void    Wire(void)
        {
            m_kanjiIndex = GuiMenu::FindItem(kKanji);
            m_compIndex = GuiMenu::FindItem(kComposition);
        }

        bool    Tick(int index, uint16_t held)
        {
            if (index == m_compIndex && index >= 0)
                return true;                            // 漢字変換の Tick が読む
            if (index != m_kanjiIndex || index < 0)
                return false;
            if (!InstallHooks())
                return true;
            g_compOn = m_compIndex >= 0 && GuiMenu::ItemApplied(m_compIndex) != 0;
            g_chatOpen = ChatKanji::NormalChatOpen();
            g_convOn = true;
            if (g_broken)
            {
                static bool told = false;

                if (!told)
                    GuiMenu::NotifyRed(kKanji, u8"想定と違うスレッドで動いたので止めました。");
                told = true;
                Reset();
                return true;
            }
            if (!g_chatOpen)
            {
                if (m_phase != M_IDLE)
                {
                    Post(R_ABORT, 0);
                    Reset();
                }
                m_key = 0;
                m_doneKey = 0;
                m_wantEnter = false;
                m_hotPrev = false;
                // チャットが無い間はゲームのスレッドが依頼を受け取らない。残すと次に開いたときに古い依頼が走る
                __atomic_store_n(&g_reqKind, (u32)R_NONE, __ATOMIC_RELEASE);
                g_state = S_IDLE;
                // キーのテクスチャ: 写しなら残す。キーボードの資源のままなら閉じたら使わない（次に開いたらゲームのスレッドが取り直す）
                if (m_texReady && !(g_texCopied && g_texCopyGen == GuiRenderer::Generation()))
                {
                    GuiRenderer::ClearGameTextures();
                    m_texReady = false;
                    g_texOwner = 0;
                }
                m_keyDown = -1;
                m_wantKey = -1;
                m_dy = 0;
                if (ChatKanji::ReleaseEngine())         // エンジンの下準備（5 MiB）を返す。動いている間は次の周期で
                    m_preloadTried = false;
                m_touchPrev = Touch::IsDown();
                return true;
            }
            Step(index, held);
            return true;
        }

        bool    Disable(int index)
        {
            if (index == m_compIndex && index >= 0)
            {
                g_compOn = false;
                return true;
            }
            if (index != m_kanjiIndex || index < 0)
                return false;
            // フックは入れたまま。旗を落とせば素通しになる（走っているフックを外しに行かない）。
            g_convOn = false;
            g_compOn = false;
            Reset();
            m_key = 0;
            m_doneKey = 0;
            m_wantEnter = false;
            // 走っているエンジンは止めない。止まってから返す（Tick が来ないので、ここで返せなければ次に ON にしたとき）
            if (ChatKanji::ReleaseEngine())
                m_preloadTried = false;
            return true;
        }

        bool    BarVisible(void)
        {
            return g_convOn && g_chatOpen && m_hooked && !g_broken;
        }

        void    DrawCandidates(float left, float right, int dy, char *buf, unsigned cap, int &chars, int &cells);

        // 角 1 つを丸める（sx / sy = 角から内側への向き）
        void    RoundCorner(int cx, int cy, int sx, int sy, int kind)
        {
            const GuiRenderer::Screen BOT = GuiRenderer::SCREEN_BOTTOM;

            if (kind == ROUND_LOW)
                GuiRenderer::FillRect(BOT, cx, cy, 1, 1, kColKeyGround | 0x80000000u);          // 0.5
            else if (kind == ROUND_MID)
            {
                GuiRenderer::FillRect(BOT, cx, cy, 1, 1, kColKeyGround | 0xFF000000u);          // 1.0
                GuiRenderer::FillRect(BOT, cx + sx, cy, 1, 1, kColKeyGround | 0x45000000u);     // 0.27
                GuiRenderer::FillRect(BOT, cx, cy + sy, 1, 1, kColKeyGround | 0x45000000u);     // 0.27
                GuiRenderer::FillRect(BOT, cx, cy + 2 * sy, 1, 1, kColKeyGround | 0x21000000u); // 0.13
            }
        }

        void    DrawBar(void)
        {
            const GuiRenderer::Screen BOT = GuiRenderer::SCREEN_BOTTOM;
            const float left = (float)(kBarX + 1), right = (float)(kBarX + kBarW - 1);
            const int   dy = m_dy;
            char        buf[200];
            int         chars = 0;
            int         cells = 0;

            if (!BarVisible())
                return;
            // キーボードの背面（W_ktpShade）を上へ伸ばしてあれば、その上に載せる（地と縁はゲームの背面）。
            //   伸ばせなかったときだけ Simulator の drawCandidateBar / drawPreviewControls の地と縁を自前で塗る。
            const bool  own = !g_shadeOn;

            if (own)
                GuiRenderer::FillRect(BOT, kRowX, kRowY + dy, kRowW, kRowH, kColBarPanel);
            if (ChatKanji::FontReady())
                DrawCandidates(left, right, dy, buf, sizeof(buf), chars, cells);
            if (own)
            {
                GuiRenderer::FillRect(BOT, kRowX, kRowY + dy, kRowW, 1, kColRowEdge);
                GuiRenderer::FillRect(BOT, kRowX, kRowY + dy, 1, kRowH, kColRowEdge);
                GuiRenderer::FillRect(BOT, kRowX + kRowW - 1, kRowY + dy, 1, kRowH, kColRowEdge);
                GuiRenderer::FillRect(BOT, kRowX, kRowY + kRowH - 1 + dy, kRowW, 1, kColRowEdge);
            }
            // 左右キーの背面の地（変換欄と同じ色。利用者の指示 2026-09-26）
            GuiRenderer::FillRect(BOT, kArrowBackX, kArrowBackY + dy, kArrowBackW, kArrowBackH, kColBarPanel);
            for (int i = 0; i < KEY_COUNT; i++)
            {
                const KeyRect  &k = kKeys[i];
                const bool      on = m_keyDown == i && m_keyInside;
                const char     *label = i == KEY_SELECT_ALL && AllSelected() ? kLabelDeselect : k.label;

                if (m_texReady)
                {
                    const KeyRound &r = kKeyRound[i];
                    const int       x0 = k.x, x1 = k.x + k.w - 1, y0 = k.y + dy, y1 = k.y + k.h - 1 + dy;

                    GuiRenderer::FillTextured(BOT, k.x, k.y + dy, k.w, k.h, on ? 2 : 1,
                                              on ? kKeyTopPressed : kKeyTopNormal, on ? kKeyBotPressed : kKeyBotNormal);
                    RoundCorner(x0, y0, 1, 1, r.leftTop);
                    RoundCorner(x0, y1, 1, -1, r.leftBottom);
                    RoundCorner(x1, y0, -1, 1, r.rightTop);
                    RoundCorner(x1, y1, -1, -1, r.rightBottom);
                }
                if (ChatKanji::FontReady())
                {
                    // 中央: 字幅は GPU の送り、文字セルの高さ = FINF の高さ x 倍率。押下は右下へ 1px（T_key_Spc の CLPA）
                    const float tw = GuiRenderer::MeasureTextNative(label, k.scale);
                    const float th = (float)ChatKanji::FontCellHeight() * k.scale;
                    const int   tx = k.x + (int)(((float)k.w - tw) * 0.5f + 0.5f) + (on ? 1 : 0);
                    const int   ty = k.y + (int)(((float)k.h - th) * 0.5f + 0.5f) + (on ? 1 : 0);

                    GuiRenderer::DrawTextNative(BOT, tx, ty + dy, label, on ? kKeyTextPressed : kKeyText, k.scale);
                }
            }
        }

        void    DrawCandidates(float left, float right, int dy, char *buf, unsigned cap, int &chars, int &cells)
        {
            const GuiRenderer::Screen BOT = GuiRenderer::SCREEN_BOTTOM;

            if (m_phase == M_STARTING || m_phase == M_ENGINE)
            {
                GuiRenderer::DrawTextNative(BOT, kBarX + 1 + kBarPad, kBarTextY + dy, u8"変換中", kColBarBusy, kBarScale);
                return;
            }
            if (m_phase != M_SHOW)
                return;
            for (int i = 0; i < m_count; i++)
            {
                const float x = left + m_cellX[i] - m_scroll;

                if (x + m_cellW[i] < (float)kBarX || x > (float)(kBarX + kBarW))
                    continue;
                // 切り抜きが無いので、選択の塗りは内側へ詰め、文字は丸ごと入る候補だけ描く（Simulator は切り抜く）
                if (i == m_sel || (m_dragging && !m_scrolling && i == m_tapCandidate))
                {
                    float   x0 = x < left ? left : x;
                    float   x1 = x + m_cellW[i] > right ? right : x + m_cellW[i];
                    const int ix = (int)(x0 + 0.5f), iw = (int)(x1 + 0.5f) - ix;

                    if (iw > 0)
                        GuiRenderer::FillRect(BOT, ix, kBarY + 1 + dy, iw, kBarH - 2, kColBarSel);
                }
                const float tx = x + (float)kBarPad;

                if (tx < left || tx + m_textW[i] > right || cells >= kBarMaxCells)
                    continue;
                if (!CandidateUtf8(i, buf, cap))
                    continue;

                int n = 0, k = 0;

                while (buf[k] != '\0')
                {
                    const unsigned char c = (unsigned char)buf[k];

                    k += c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : 3;
                    n++;
                }
                if (chars + n > kBarMaxChars)
                    break;
                chars += n;
                cells++;
                GuiRenderer::DrawTextNative(BOT, (int)(tx + 0.5f), kBarTextY + dy, buf, kColBarText, kBarScale);
            }
            const float m = MaxScroll();

            if (m > 0.0f)
            {
                if (m_scroll > 0.0f)
                    GuiRenderer::FillRect(BOT, kBarX + 2, kBarY + 3 + dy, 2, kBarH - 6, kColBarHint);
                if (m_scroll < m)
                    GuiRenderer::FillRect(BOT, kBarX + kBarW - 4, kBarY + 3 + dy, 2, kBarH - 6, kColBarHint);
            }
        }
    }
}
