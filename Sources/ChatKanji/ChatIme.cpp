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

#include <cstdio>
#include <cstring>

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
            // gui::KanaKeySet の OnKey（0x4F556C）が InputChar を呼ぶ 3 か所（字 x2・濁点キー）
            const u32   kKanaCall[3]    = { 0x004F5654, 0x004F56A4, 0x004F57A4 };
            const u32   kKanaCallWord[3] = { 0xEB00B028, 0xEB00B014, 0xEB00AFD4 };   // BL 0x5216FC

            const u32   TM_LEN = 0x08, TM_MAX = 0x0C, TM_BUF = 0x10, TM_CURSOR = 0x14, TM_18 = 0x18;
            const u32   TM_ANCHOR = 0x1C, TM_SEL = 0x20, TM_PEND = 0x24, TM_EXTRA = 0x28, TM_90 = 0x90;
            const int   kReadingMax = 32;               // ChatKanji::RequestText の上限（普通のチャットの最大字数）

            typedef int     (*InsertCharFn)(u32 tm, u32 ch, int pos, int romaji, float f);
            typedef void    (*DeleteRangeFn)(u32 tm, int pos, int n);
            typedef int     (*FinishCellFn)(u32 tm, int flag);

            // ---- 候補欄（Simulator 670e44f chat-kanji-preview.js の CANDIDATE_BAR / CANDIDATE_COLORS）----
            const int   kBarX = 8, kBarY = 49, kBarW = 304, kBarH = 17;
            const int   kBarGap = 2, kBarPad = 4, kBarTextY = 48;
            const float kBarScale = 0.72f;
            const u32   kColBarPanel = 0xFF102852;      // #522810
            const u32   kColBarSel   = 0x3DD6FFEF;      // rgba(239,255,214,.24)
            const u32   kColBarText  = 0xFFD6F3FF;      // #fff3d6
            const u32   kColBarHint  = 0x9ED6F3FF;      // rgba(255,243,214,.62)
            // 1 フレームに描くゲームの字形の上限（記録リストの見積もり。verify_plugin_port_v2 群 4 が読む）
            const int   kBarMaxChars = 32;
            const int   kBarMaxCells = 16;              // ゲームの字形の枠（GuiRenderer kNativeSlots）
            const int   kMaxCand = 300;                 // SwkbdEngine::MaxCandidates

            // HotkeyBit（GuiMenu.cpp の kHotkeyKeys の並び）
            const u16   HB_LEFT = 1u << 6, HB_RIGHT = 1u << 7;

            // ---- 共有（メニュースレッドが書き、ゲームのスレッドが読む）----
            volatile bool   g_convOn = false;
            volatile bool   g_compOn = false;
            volatile bool   g_chatOpen = false;
            volatile bool   g_broken = false;   // スレッドの取り違えなどで止めた

            // ---- 依頼（メニュー -> ゲーム）----
            enum { R_NONE = 0, R_START, R_APPLY, R_ABORT };
            volatile u32    g_reqKind = R_NONE;
            volatile s32    g_reqArg = 0;
            enum { START_OK = 0, START_NO_TARGET, START_TOO_LONG, START_NO_TM };
            volatile u32    g_startResult = START_OK;

            // ---- 変換の区切り（ゲームのスレッドが書く。g_state だけメニューも読む）----
            enum { S_IDLE = 0, S_WAIT_ENGINE, S_ACTIVE };
            volatile u32    g_state = S_IDLE;
            u16             g_reading[kReadingMax + 1];
            volatile int    g_readingLen = 0;
            int             g_start = 0;
            int             g_curLen = 0;
            int             g_applied = -1;
            u32             g_sessionTm = 0;

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

            // ---- メニュースレッド側 ----
            enum { M_IDLE = 0, M_STARTING, M_ENGINE, M_SHOW };
            int         m_phase = M_IDLE;
            u64         m_startTick = 0;                // M_STARTING に入った時刻（ゲームが受け取らないときの打ち切り）
            int         m_kanjiIndex = -1;
            int         m_compIndex = -1;
            bool        m_hooked = false;
            bool        m_hookFailed = false;
            bool        m_hotPrev = false;
            u16         m_heldPrev = 0;
            int         m_sel = 0;
            float       m_scroll = 0.0f;
            int         m_count = 0;
            float       m_cellX[kMaxCand];
            float       m_cellW[kMaxCand];
            float       m_textW[kMaxCand];
            float       m_content = 0.0f;
            bool        m_touchPrev = false;
            bool        m_dragging = false;
            int         m_dragStartX = 0;
            float       m_dragStartScroll = 0.0f;

            Hook        g_hInput;
            Hook        g_hBack;
            Hook        g_hWait;

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

                return pend == g_curLen && pend > 0 && cur - pend == g_start;
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
                    start = s;
                    n = e - s;
                    if (n > kReadingMax)
                        return START_TOO_LONG;
                    // 選択を未確定に変える（以後は未確定と同じ流れ。飾りも未確定のものになる）
                    W8(tm + TM_SEL, 0);
                    WI(tm, TM_CURSOR, e);
                    WI(tm, TM_ANCHOR, e);
                    WI(tm, TM_90, -1);
                    WI(tm, TM_EXTRA, 0);
                    WI(tm, TM_PEND, n);
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
                g_state = S_WAIT_ENGINE;
                return START_OK;
            }

            void    ApplyCandidate(u32 tm, int index)
            {
                int         len = 0;
                const u16   *cand;

                if (g_state == S_IDLE || !Consistent(tm))
                    return;
                cand = ChatKanji::Candidate(index, len);
                if (cand == nullptr || len <= 0)
                    return;
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

            // ---- フック本体（ゲームのスレッド）----
            __attribute__((noinline)) int ChatImeInputChar(u32 tm, u32 ch, u32 romaji, u32 combine)
            {
                HookContext &ctx = HookContext::GetCurrent();
                const u32   ret = (u32)__builtin_return_address(0);

                g_threadInput = ThreadTag();
                if (!g_convOn || g_broken || !TmSane(tm))
                    return ctx.OriginalFunction<int>(tm, ch, romaji, combine);

                // 変換中に字を打ったら変換を確定する（候補はそのまま確定した文字になる）。Enter は素通し（ゲームが確定する）。
                if (g_state != S_IDLE && tm == g_sessionTm && ch != 10)
                {
                    if (Consistent(tm))
                        WI(tm, TM_PEND, 0);
                    g_state = S_IDLE;
                }

                const bool  kana = ret == kKanaCall[0] + 4 || ret == kKanaCall[1] + 4 || ret == kKanaCall[2] + 4;

                if (!kana && romaji != 0 && tm == g_ownTm && g_ownPend > 0
                    && RI(tm, TM_PEND) == g_ownPend && RI(tm, TM_CURSOR) == g_ownCursor)
                    WI(tm, TM_PEND, 0);                 // かなの未確定を確定してからローマ字を打たせる
                g_ownTm = 0;
                g_ownPend = 0;
                if (!g_compOn || !kana || !g_chatOpen || ch == 0x20 || ch == 0x3000 || ch == 10)
                    return ctx.OriginalFunction<int>(tm, ch, romaji, combine);

                const bool  sel = R8(tm + TM_SEL) != 0 && RI(tm, TM_ANCHOR) != RI(tm, TM_CURSOR);
                const int   keep = sel ? 0 : RI(tm, TM_PEND);
                const int   before = RI(tm, TM_LEN);

                // 元関数はローマ字モード 0 のとき先に未確定を確定するので、一時的に 0 にしておく
                WI(tm, TM_PEND, 0);

                const int   r = ctx.OriginalFunction<int>(tm, ch, romaji, combine);
                const int   delta = RI(tm, TM_LEN) - before;
                int         pend = sel ? (r != 0 ? 1 : 0) : keep + delta;
                const int   cur = RI(tm, TM_CURSOR);

                if (pend < 0)
                    pend = 0;
                if (pend > cur)
                    pend = cur;
                WI(tm, TM_EXTRA, 0);
                WI(tm, TM_PEND, pend);
                g_ownTm = tm;
                g_ownCursor = cur;
                g_ownPend = pend;
                return r;
            }

            __attribute__((noinline)) int ChatImeBackspace(u32 tm, u32 silent)
            {
                HookContext &ctx = HookContext::GetCurrent();

                if (g_convOn && !g_broken && g_state != S_IDLE && Consistent(tm))
                {
                    if (g_state == S_ACTIVE && g_applied >= 0)
                    {
                        // 変換を戻して読みの未確定へ（一般的な IME と同じ）
                        Replace(tm, g_reading, g_readingLen);
                        g_applied = -1;
                        g_state = S_IDLE;
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
                    const u32   kind = __atomic_load_n(&g_reqKind, __ATOMIC_ACQUIRE);

                    if (kind == R_START)
                        g_startResult = (u32)StartSession(tm);
                    else if (kind == R_APPLY)
                        ApplyCandidate(tm, (int)g_reqArg);
                    else if (kind == R_ABORT)
                        g_state = S_IDLE;
                    if (kind != R_NONE)
                        __atomic_store_n(&g_reqKind, (u32)R_NONE, __ATOMIC_RELEASE);
                    // Enter・送信・カーソル移動で未確定が確定したら終わり
                    if (g_state != S_IDLE && !Consistent(tm))
                        g_state = S_IDLE;
                }
                return ctx.OriginalFunction<int>(self);
            }

            // ---- メニュースレッド ----
            bool    CodeMatches(void)
            {
                const u32   words[][2] = {
                    { kInputChar, kInputCharOrig }, { kBackspace, kBackspaceOrig }, { kWaitCalc, kWaitCalcOrig },
                    { kDeleteRange, kDeleteRangeOrig }, { kInsertChar, kInsertCharOrig }, { kFinishCell, kFinishCellOrig },
                    { kKanaCall[0], kKanaCallWord[0] }, { kKanaCall[1], kKanaCallWord[1] }, { kKanaCall[2], kKanaCallWord[2] },
                };

                if (Process::GetTitleID() != 0x0004000000086200ULL)
                    return false;
                for (const auto &w : words)
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
                if (g_hInput.Enable() != HookResult::Success || g_hBack.Enable() != HookResult::Success
                    || g_hWait.Enable() != HookResult::Success)
                {
                    // 入った分は外す（まだ何も走っていない。旗も立てていない）
                    g_hInput.Disable();
                    g_hBack.Disable();
                    g_hWait.Disable();
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
                if (m_phase == M_SHOW || m_phase == M_ENGINE)
                    ChatKanji::Dismiss();
                m_phase = M_IDLE;
                m_count = 0;
                m_sel = 0;
                m_scroll = 0.0f;
                m_dragging = false;
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
                if (m_cellX[i] < m_scroll)
                    m_scroll = m_cellX[i];
                else if (m_cellX[i] + m_cellW[i] > m_scroll + view)
                    m_scroll = m_cellX[i] + m_cellW[i] - view;
                ClampScroll();
            }

            bool    CandidateUtf8(int i, char *out, unsigned cap)
            {
                int         len = 0;
                const u16   *c = ChatKanji::Candidate(i, len);

                return c != nullptr && ChatKanjiText::Utf8(c, (size_t)len, out, cap);
            }

            // chat-kanji-preview.js candidateLayout（幅は GPU の送りと同じく字形ごとに丸めない）
            void    BuildLayout(void)
            {
                char    buf[200];
                float   x = 0.0f;

                m_count = ChatKanji::CandidateCount();
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
                m_sel = ((i % m_count) + m_count) % m_count;
                EnsureVisible(m_sel);
                Post(R_APPLY, m_sel);
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
                return x >= kBarX && x < kBarX + kBarW && y >= kBarY && y < kBarY + kBarH;
            }

            void    HandleTouch(void)
            {
                const bool  down = Touch::IsDown();
                const UIntVector pos = Touch::GetPosition();
                const int   x = (int)pos.x, y = (int)pos.y;

                if (down && !m_touchPrev && InsideBar(x, y))
                {
                    m_dragging = true;
                    m_dragStartX = x;
                    m_dragStartScroll = m_scroll;
                    if (m_phase == M_SHOW)
                    {
                        const int i = CandidateAt(x);

                        if (i >= 0 && i != m_sel)
                            Select(i);
                    }
                }
                if (m_dragging)
                {
                    if (!down)
                        m_dragging = false;
                    else
                    {
                        m_scroll = m_dragStartScroll - (float)(x - m_dragStartX);
                        ClampScroll();
                    }
                    GuiMenu::BlockGameTouch();          // 欄の上で始めた指はゲームへ渡さない
                }
                m_touchPrev = down;
            }

            void    Step(int index, u16 held)
            {
                const u16   hk = GuiMenu::ItemAppliedHotkey(index);
                const bool  hot = hk != 0 && (held & hk) == hk;
                const bool  hotEdge = hot && !m_hotPrev;
                const u16   pressed = (u16)(held & ~m_heldPrev);

                m_hotPrev = hot;
                m_heldPrev = held;

                if (m_phase == M_IDLE)
                {
                    if (hotEdge)
                    {
                        Post(R_START, 0);
                        m_phase = M_STARTING;
                        m_startTick = svcGetSystemTick();
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
                        {
                            GuiMenu::NotifyRed(kKanji, r == START_TOO_LONG ? u8"変換する文字が長すぎます。"
                                                                          : u8"変換する文字がありません。");
                            m_phase = M_IDLE;
                        }
                        else
                        {
                            const ChatKanji::RequestResult rr =
                                ChatKanji::RequestText(g_reading, (size_t)g_readingLen);

                            if (rr == ChatKanji::REQUEST_OK)
                                m_phase = M_ENGINE;
                            else
                            {
                                GuiMenu::NotifyRed(kKanji, rr == ChatKanji::REQUEST_BUSY ? u8"変換処理中です。"
                                                           : rr == ChatKanji::REQUEST_BAD_INPUT ? u8"変換できない文字です。"
                                                           : rr == ChatKanji::REQUEST_NO_FONT ? u8"フォントを取得できません。"
                                                           : u8"変換を開始できません。");
                                Post(R_ABORT, 0);
                                m_phase = M_IDLE;
                            }
                        }
                    }
                }
                else if (m_phase == M_ENGINE)
                {
                    if (ChatKanji::Poll())
                    {
                        if (ChatKanji::Error()[0] != '\0' || ChatKanji::CandidateCount() <= 0)
                        {
                            GuiMenu::NotifyRed(kKanji, ChatKanji::Error()[0] != '\0' ? ChatKanji::Error()
                                                                                     : "NO CANDIDATES");
                            Post(R_ABORT, 0);
                            Reset();
                        }
                        else if (g_state == S_IDLE)
                            Reset();                    // 待っている間に確定・取り消しされた
                        else
                        {
                            BuildLayout();
                            m_scroll = 0.0f;
                            m_phase = M_SHOW;
                            Select(0);
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
                    if (hotEdge || (pressed & HB_RIGHT) != 0)
                        Select(m_sel + 1);
                    else if ((pressed & HB_LEFT) != 0)
                        Select(m_sel - 1);
                }
                // 変換の途中は十字キーをゲームへ渡さない（渡すとカーソルが動いて確定してしまう）
                if (m_phase != M_IDLE)
                    GuiMenu::BlockGameDpad();
                HandleTouch();
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
            return true;
        }

        bool    BarVisible(void)
        {
            return g_convOn && g_chatOpen && m_hooked && !g_broken;
        }

        void    DrawBar(void)
        {
            const GuiRenderer::Screen BOT = GuiRenderer::SCREEN_BOTTOM;
            const float left = (float)(kBarX + 1), right = (float)(kBarX + kBarW - 1);
            char        buf[200];
            int         chars = 0;
            int         cells = 0;

            if (!BarVisible())
                return;
            GuiRenderer::FillRect(BOT, kBarX, kBarY, kBarW, kBarH, kColBarPanel);
            if (!ChatKanji::FontReady())
                return;
            if (m_phase == M_STARTING || m_phase == M_ENGINE)
            {
                GuiRenderer::DrawTextNative(BOT, kBarX + 1 + kBarPad, kBarTextY, u8"変換中", kColBarText, kBarScale);
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
                if (i == m_sel)
                {
                    float   x0 = x < left ? left : x;
                    float   x1 = x + m_cellW[i] > right ? right : x + m_cellW[i];
                    const int ix = (int)(x0 + 0.5f), iw = (int)(x1 + 0.5f) - ix;

                    if (iw > 0)
                        GuiRenderer::FillRect(BOT, ix, kBarY + 1, iw, kBarH - 2, kColBarSel);
                }
                const float tx = x + (float)kBarPad;

                if (tx < left || tx + m_textW[i] > right || cells >= kBarMaxCells)
                    continue;
                if (!CandidateUtf8(i, buf, sizeof(buf)))
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
                GuiRenderer::DrawTextNative(BOT, (int)(tx + 0.5f), kBarTextY, buf, kColBarText, kBarScale);
            }
            const float m = MaxScroll();

            if (m > 0.0f)
            {
                if (m_scroll > 0.0f)
                    GuiRenderer::FillRect(BOT, kBarX + 2, kBarY + 3, 2, kBarH - 6, kColBarHint);
                if (m_scroll < m)
                    GuiRenderer::FillRect(BOT, kBarX + kBarW - 4, kBarY + 3, 2, kBarH - 6, kColBarHint);
            }
        }
    }
}
