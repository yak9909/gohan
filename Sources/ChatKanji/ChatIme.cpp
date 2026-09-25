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
            const u32   kBsSkbCommonAcc = 800;          // BsSkb+788 の ArcResAccReader（swkbd_common.arc）+12 = アクセサ
            const u32   kBsSkbBgLayout  = 1372;         // BsSkb+1372 = BG レイアウト（N_All に BG_in / BG_out が当たる）
            // gui::KanaKeySet の OnKey（0x4F556C）が InputChar を呼ぶ 3 か所（字 x2・濁点キー）
            const u32   kKanaCall[3]    = { 0x004F5654, 0x004F56A4, 0x004F57A4 };
            const u32   kKanaCallWord[3] = { 0xEB00B028, 0xEB00B014, 0xEB00AFD4 };   // BL 0x5216FC

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
            const int   kBarGap = 2, kBarPad = 4, kBarTextY = 48;
            const float kBarScale = 0.72f;
            const u32   kColBarPanel = 0xFF102852;      // #522810
            const u32   kColRowEdge  = 0xFF0C1D3A;      // #3a1d0c（CONTROL_COLORS.rowEdge）
            const u32   kColBarSel   = 0x3DD6FFEF;      // rgba(239,255,214,.24)
            const u32   kColBarText  = 0xFFD6F3FF;      // #fff3d6
            const u32   kColBarHint  = 0x9ED6F3FF;      // rgba(255,243,214,.62)
            // 1 フレームに描くゲームの字形の上限（記録リストの見積もり。verify_plugin_port_v2 群 4 が読む）。
            //   ゲームの字形の枠は 16 本（GuiRenderer kNativeSlots）。キーの文字 3 本（全選択 / ← / →、5 字）を先に取る。
            const int   kBarMaxChars = 32;
            const int   kBarMaxCells = 13;
            const int   kKeyTexts = 3, kKeyChars = 5;
            const int   kMaxCand = 300;                 // SwkbdEngine::MaxCandidates

            // ---- 自前キー（全選択・左・右）。見た目はゲームのキー（KeytopModeSelect の中央の部品）----
            //   位置は Simulator の SELECT_ALL_BUTTON / CURSOR_BUTTONS（左右は 24x22、入力欄の上）。
            struct KeyRect { int x, y, w, h; const char *label; float scale; int textX, textY; };
            enum { KEY_SELECT_ALL = 0, KEY_LEFT, KEY_RIGHT, KEY_COUNT };
            const KeyRect kKeys[KEY_COUNT] = {
                { 279, 49, 40, 19, u8"全選択", 0.72f, 281, 48 },    // drawAcNlControlKey: x + floor((w - 36) / 2)、文字セル y 48
                { 271,  1, 24, 22, u8"←",     1.0f,  275,  0 },    // 同: x + floor((24 - 16) / 2)、y - 1
                { 295,  1, 24, 22, u8"→",     1.0f,  299,  0 },
            };
            // KeytopModeSelect.bclyt の P_ktpMode_01（KtpModeC）と KeytopModeSelect_n0s1（フレーム 0 = 通常、1 = 押下）
            const u32   kKeyColor0     = 0x00192D50;    // material 色[0] (80,45,25,0)
            const u32   kKeyTopNormal  = 0xFF2D69B9;    // 頂点色 上 (185,105,45)
            const u32   kKeyBotNormal  = 0xFF082852;    //        下 (82,40,8)
            const u32   kKeyTopPressed = 0xFF66A0FF;    // 押下   上 (255,160,102)
            const u32   kKeyBotPressed = 0xFFD2EDFF;    //        下 (255,237,210)
            const u32   kKeyText       = 0xFFB9F0FF;    // T_ktpMode 色[1] (255,240,185)
            const u32   kKeyTextPressed = 0xFF3A5EB8;   // 押下 (184,94,58)
            const u32   kSoundChangeKeySet = 0x010003E0;    // SE_SYS_SWK_CHANGE_KEY_SET（「ABC」「あいう」の切り替え）
            const int   kTapSlop = 4;                   // これ以上動いたらスクロール（確定しない）

            // HotkeyBit（GuiMenu.cpp の kHotkeyKeys の並び）
            const u16   HB_LEFT = 1u << 6, HB_RIGHT = 1u << 7;

            // ---- 共有（メニュースレッドが書き、ゲームのスレッドが読む）----
            volatile bool   g_convOn = false;
            volatile bool   g_compOn = false;
            volatile bool   g_chatOpen = false;
            volatile bool   g_broken = false;   // スレッドの取り違えなどで止めた
            volatile bool   g_reverted = false; // Backspace で読みへ戻した（ゲーム -> メニュー。同じ読みでも取り直す）

            // ---- 依頼（メニュー -> ゲーム）----
            enum { R_NONE = 0, R_START, R_APPLY, R_ABORT, R_ENTER, R_SELECT_ALL, R_LEFT, R_RIGHT };
            volatile u32    g_reqKind = R_NONE;
            volatile s32    g_reqArg = 0;
            // R_APPLY で入れる文字列（メニューが写してから依頼する。ゲームのスレッドはキャッシュを読まない）
            u16             g_applyText[SWKBD_TEXT_UNITS];
            int             g_applyLen = 0;
            enum { START_OK = 0, START_NO_TARGET, START_TOO_LONG, START_NO_TM };
            volatile u32    g_startResult = START_OK;

            // ---- キーのテクスチャ（ゲームのスレッドが BsSkb ごとに 1 回取り、メニューが描画器へ渡す）----
            u32             g_texOwner = 0;             // 取った BsSkb
            u32             g_texMap[2][8];             // [0] KtpModeC / [1] KtpModeCon の TexMap（32 B）
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

            // ---- 変換の対象とその候補のキャッシュ（メニュースレッドだけ。利用者の指示 2026-09-26）----
            //   255 件まで。溢れたら一番長く使っていないものから消す。当たったら「いま使った」にする（よく使うものを残す）。
            //   件数とは別に、合計の大きさでも抑える（候補は最大 300 件 x 55 字。普段は 1 件数百バイト）。
            const int   kCacheMax = 255;
            const u32   kCacheBytesMax = 1024 * 1024;
            struct CacheEntry
            {
                bool                used;
                u32                 stamp;              // 最後に使った順（大きいほど新しい）
                u16                 reading[kReadingMax + 1];
                int                 readingLen;
                std::vector<u16>    text;               // 候補を続けて並べたもの
                std::vector<u16>    start;              // 候補 i は text[start[i] .. start[i+1])（要素数は候補数 + 1）
            };
            CacheEntry  m_cache[kCacheMax];
            u32         m_cacheClock = 0;
            u32         m_cacheBytes = 0;
            int         m_entry = -1;                   // いま候補欄に出しているもの

            u32     EntryBytes(const CacheEntry &e)
            {
                return (u32)(e.text.size() + e.start.size()) * 2 + (u32)sizeof(CacheEntry);
            }

            int     CacheFind(const u16 *reading, int n)
            {
                for (int i = 0; i < kCacheMax; i++)
                    if (m_cache[i].used && m_cache[i].readingLen == n
                        && std::memcmp(m_cache[i].reading, reading, (size_t)n * 2) == 0)
                    {
                        m_cache[i].stamp = ++m_cacheClock;
                        return i;
                    }
                return -1;
            }

            void    CacheDrop(int i)
            {
                m_cacheBytes -= EntryBytes(m_cache[i]);
                m_cache[i].used = false;
                std::vector<u16>().swap(m_cache[i].text);
                std::vector<u16>().swap(m_cache[i].start);
                if (m_entry == i)
                    m_entry = -1;
            }

            int     CacheOldest(void)
            {
                int best = -1;

                for (int i = 0; i < kCacheMax; i++)
                    if (m_cache[i].used && (best < 0 || m_cache[i].stamp < m_cache[best].stamp))
                        best = i;
                return best;
            }

            // ChatKanji の結果を入れる（同じ読みがあれば置き換える）。入れた番号を返す
            int     CacheInsert(const u16 *reading, int n)
            {
                const int   count = ChatKanji::CandidateCount();
                int         slot = -1;
                u32         total = 0;

                for (int i = 0; i < count; i++)
                {
                    int len = 0;

                    ChatKanji::Candidate(i, len);
                    total += (u32)len;
                }
                for (int i = 0; i < kCacheMax; i++)
                    if (m_cache[i].used && m_cache[i].readingLen == n
                        && std::memcmp(m_cache[i].reading, reading, (size_t)n * 2) == 0)
                        CacheDrop(i);

                const u32   need = (total + (u32)count + 1) * 2 + (u32)sizeof(CacheEntry);

                for (;;)
                {
                    int free = -1;

                    for (int i = 0; i < kCacheMax && free < 0; i++)
                        if (!m_cache[i].used)
                            free = i;
                    if (free >= 0 && m_cacheBytes + need <= kCacheBytesMax)
                    {
                        slot = free;
                        break;
                    }
                    const int old = CacheOldest();

                    if (old < 0)
                        break;
                    CacheDrop(old);
                }
                if (slot < 0)
                    return -1;

                CacheEntry &e = m_cache[slot];

                e.text.clear();
                e.start.clear();
                e.text.reserve(total);
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
                std::memcpy(e.reading, reading, (size_t)n * 2);
                e.reading[n] = 0;
                e.readingLen = n;
                e.used = true;
                e.stamp = ++m_cacheClock;
                m_cacheBytes += EntryBytes(e);
                return slot;
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
            const u64   kSettleTicks = (u64)SYSCLOCK_ARM11 * 100 / 1000;     // 2026-09-26: 300 -> 100ms（変換が速くなったので）
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
            bool        m_dragging = false;
            int         m_dragMax = 0;                  // 触れてから動いた最大の横幅（タップかスクロールか）
            int         m_tapCandidate = -1;            // 選択中の候補をもう一度触った（離したときに Enter）
            int         m_keyDown = -1;                 // 押している自前キー
            bool        m_keyInside = false;            // 押している指がまだキーの上か（押下の見た目）
            int         m_wantKey = -1;                 // 離したキー。依頼の枠が空いたら出す
            int         m_dy = 0;                       // 開閉アニメ（BG の N_All の平行移動）ぶんのずれ
            u32         m_texSeqSeen = 0;
            bool        m_texReady = false;
            u64         m_repeatAt = 0;                 // 十字キー左右の長押しの次の時刻
            u16         m_repeatBit = 0;
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

            // KeytopModeSelect の部品のテクスチャを swkbd_common.arc のアクセサから取る（読み込み済みの控えが返る）
            bool    CaptureKeyTextures(u32 bsskb)
            {
                typedef void (*GetTextureFn)(u32 *out, u32 accessor, const char *name);
                typedef void (*UpdateFn)(u32 *texMap);
                const u32   acc = bsskb + kBsSkbCommonAcc;
                const char *names[2] = { "KtpModeC.bclim", "KtpModeCon.bclim" };
                u32         info[2][5];

                if (bsskb < 0x08000000u || bsskb >= 0x40000000u || R32(acc) != kVtAccessorVram
                    || R32(kVtAccessorVram + 0x10) != kGetTexture)
                    return false;
                for (int i = 0; i < 2; i++)
                {
                    std::memset(info[i], 0, sizeof(info[i]));
                    ((GetTextureFn)kGetTexture)(info[i], acc, names[i]);
                    if (info[i][1] == 0 || (info[i][2] & 0xFFFF) == 0 || (info[i][3] & 0xFFFF) == 0)
                        return false;
                }
                __atomic_add_fetch(&g_texSeq, 1u, __ATOMIC_ACQ_REL);   // 奇数: 書いている途中
                for (int i = 0; i < 2; i++)
                {
                    // Material の ctor と同じ組み方（0x4BCEA8）: 番地・大きさ・書式、ラップ = クランプ、
                    //   フィルタ = 資源の rawS/rawT 4（KeytopModeSelect の material）→ bits4-6 = 1 / bit7 = 1
                    std::memset(g_texMap[i], 0, sizeof(g_texMap[i]));
                    g_texMap[i][0] = info[i][0];
                    g_texMap[i][1] = info[i][1];
                    g_texMap[i][2] = info[i][2];
                    g_texMap[i][3] = info[i][3];
                    g_texMap[i][4] = ((info[i][4] & 0xFFu) << 8 & 0xF00u) | 0x10u | 0x80u;
                    ((UpdateFn)kTexMapUpdate)(g_texMap[i]);
                }
                g_texOwner = bsskb;
                g_texOk = true;
                __atomic_add_fetch(&g_texSeq, 1u, __ATOMIC_ACQ_REL);   // 偶数: 揃った
                return true;
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
            __attribute__((noinline)) int ChatImeInputChar(u32 tm, u32 ch, u32 romaji, u32 combine)
            {
                HookContext &ctx = HookContext::GetCurrent();
                const u32   ret = (u32)__builtin_return_address(0);

                g_threadInput = ThreadTag();
                if (!g_convOn || g_broken || !TmSane(tm))
                    return ctx.OriginalFunction<int>(tm, ch, romaji, combine);

                // 候補を選んだあとに字を打ったら、その候補で確定する。Enter は素通し（ゲームが確定する）。
                // 候補を選ぶ前なら区切りを捨てるだけ（読みが変わるので、止まってから取り直す）。
                if (g_state != S_IDLE && tm == g_sessionTm && ch != 10)
                {
                    if (g_state == S_ACTIVE && g_applied >= 0 && Consistent(tm))
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
                    const u32   bsskb = R32(kBsSkbPtr);

                    if (bsskb != 0 && bsskb != g_texOwner)
                    {
                        g_texOk = false;
                        if (!CaptureKeyTextures(bsskb))
                            g_texOwner = bsskb;         // 取れなかった: この BsSkb では描かない（キーは矩形なしで文字だけ）
                    }
                    const u32   kind = __atomic_load_n(&g_reqKind, __ATOMIC_ACQUIRE);

                    if (kind == R_START)
                        g_startResult = (u32)StartSession(tm);
                    else if (kind == R_APPLY)
                        ApplyCandidate(tm, (int)g_reqArg);
                    else if (kind == R_ABORT)
                        g_state = S_IDLE;
                    else if (kind == R_ENTER && TmSane(tm))
                        ((int (*)(u32, u32, u32, u32))kInputChar)(tm, 10, 0, 0);   // フック経由。字 10 は素通しでゲームが確定する
                    else if ((kind == R_SELECT_ALL || kind == R_LEFT || kind == R_RIGHT) && TmSane(tm))
                    {
                        if (kind == R_SELECT_ALL)
                            SelectAll(tm);
                        else
                            MoveCursor(tm, kind == R_LEFT ? -1 : 1);
                        ((void (*)(u32))kPlaySound)(kSoundChangeKeySet);
                    }
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
                    { kSetCursor, kSetCursorOrig }, { kPlaySound, kPlaySoundOrig }, { kTexMapUpdate, kTexMapUpdateOrig },
                    { kGetTexture, kGetTextureOrig }, { kVtAccessorVram + 0x10, kGetTexture },
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
                if (m_phase == M_ENGINE)
                    ChatKanji::Dismiss();
                m_phase = M_IDLE;
                m_count = 0;
                m_entry = -1;
                m_wantApply = -1;
                m_sel = -1;
                m_scroll = 0.0f;
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
                    Post(m_wantKey == KEY_SELECT_ALL ? R_SELECT_ALL : m_wantKey == KEY_LEFT ? R_LEFT : R_RIGHT, 0);
                    m_wantKey = -1;
                }
            }

            // 対象が決まった（R_START を受け取った）あと: キャッシュにあればすぐ出す、無ければエンジンへ
            void    ShowEntry(int entry)
            {
                m_entry = entry;
                BuildLayout();
                m_scroll = 0.0f;
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

            void    HandleTouch(void)
            {
                const bool  down = Touch::IsDown();
                const UIntVector pos = Touch::GetPosition();
                const int   x = (int)pos.x, y = (int)pos.y;

                // 自前キー: 押している間は押下の見た目、キーの上で離したら実行（音もゲームの処理から鳴らす）
                if (down && !m_touchPrev)
                {
                    const int k = KeyAt(x, y);

                    if (k >= 0)
                    {
                        m_keyDown = k;
                        m_keyInside = true;
                    }
                }
                if (m_keyDown >= 0)
                {
                    if (down)
                        m_keyInside = KeyAt(x, y) == m_keyDown;
                    else
                    {
                        if (m_keyInside)
                            m_wantKey = m_keyDown;
                        m_keyDown = -1;
                        m_keyInside = false;
                    }
                    GuiMenu::BlockGameTouch();
                    m_touchPrev = down;
                    return;
                }
                if (down && !m_touchPrev && InsideBar(x, y))
                {
                    m_dragging = true;
                    m_dragStartX = x;
                    m_dragMax = 0;
                    m_dragStartScroll = m_scroll;
                    m_tapCandidate = -1;
                    if (m_phase == M_SHOW)
                    {
                        const int i = CandidateAt(x);

                        if (i >= 0 && i == m_sel)
                            m_tapCandidate = i;         // 選択中をもう一度: 離したときに Enter（動かしていなければ）
                        else if (i >= 0)
                            Select(i);
                    }
                }
                if (m_dragging)
                {
                    if (!down)
                    {
                        m_dragging = false;
                        // あまり動かさずに離したら決定（スクロールのために同じ所を触ったときは決定しない）
                        if (m_tapCandidate >= 0 && m_tapCandidate == m_sel && m_dragMax < kTapSlop)
                            m_wantEnter = true;
                        m_tapCandidate = -1;
                    }
                    else
                    {
                        const int moved = x > m_dragStartX ? x - m_dragStartX : m_dragStartX - x;

                        if (moved > m_dragMax)
                            m_dragMax = moved;
                        m_scroll = m_dragStartScroll - (float)(x - m_dragStartX);
                        ClampScroll();
                    }
                    GuiMenu::BlockGameTouch();          // 欄の上で始めた指はゲームへ渡さない
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

                    if ((seq & 1u) == 0 && seq != m_texSeqSeen)
                    {
                        m_texSeqSeen = seq;
                        m_texReady = g_texOk && GuiRenderer::SetGameTexture(1, g_texMap[0], kKeyColor0)
                                     && GuiRenderer::SetGameTexture(2, g_texMap[1], kKeyColor0);
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
                    // Backspace で読みへ戻した: 同じ読みはキャッシュにあるので待たずに出す
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

                        const int hit = r == START_OK ? CacheFind(g_reading, g_readingLen) : -1;

                        if (r != START_OK)
                            m_phase = M_IDLE;           // 自動で取るので通知しない（打ち終わる前に消えた等）
                        else if (hit >= 0)
                            ShowEntry(hit);             // キャッシュにある（エンジンを呼ばない）
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
                            // 候補なし、または待っている間に読みが変わった。取れた候補はキャッシュには入れておく
                            if (ChatKanji::CandidateCount() > 0)
                                CacheInsert(g_reading, g_readingLen);
                            Post(R_ABORT, 0);
                            Reset();
                        }
                        else
                        {
                            const int entry = CacheInsert(g_reading, g_readingLen);

                            ChatKanji::Dismiss();       // 結果はキャッシュへ写した。次の依頼を受けられるようにする
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
                // キーのテクスチャはキーボードの資源。閉じたら使わない（次に開いたらゲームのスレッドが取り直す）
                if (m_texReady)
                {
                    GuiRenderer::ClearGameTextures();
                    m_texReady = false;
                }
                g_texOwner = 0;
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
            // drawCandidateBar: 変換行全体を 1 回で塗る（候補欄 + 全選択で 1 つの部品に見せる）
            GuiRenderer::FillRect(BOT, kRowX, kRowY + dy, kRowW, kRowH, kColBarPanel);
            if (ChatKanji::FontReady())
                DrawCandidates(left, right, dy, buf, sizeof(buf), chars, cells);
            // drawPreviewControls: 行の縁（上・左・右・下）と自前キー
            GuiRenderer::FillRect(BOT, kRowX, kRowY + dy, kRowW, 1, kColRowEdge);
            GuiRenderer::FillRect(BOT, kRowX, kRowY + dy, 1, kRowH, kColRowEdge);
            GuiRenderer::FillRect(BOT, kRowX + kRowW - 1, kRowY + dy, 1, kRowH, kColRowEdge);
            GuiRenderer::FillRect(BOT, kRowX, kRowY + kRowH - 1 + dy, kRowW, 1, kColRowEdge);
            for (int i = 0; i < KEY_COUNT; i++)
            {
                const KeyRect  &k = kKeys[i];
                const bool      on = m_keyDown == i && m_keyInside;

                if (m_texReady)
                    GuiRenderer::FillTextured(BOT, k.x, k.y + dy, k.w, k.h, on ? 2 : 1,
                                              on ? kKeyTopPressed : kKeyTopNormal, on ? kKeyBotPressed : kKeyBotNormal);
                if (ChatKanji::FontReady())
                    GuiRenderer::DrawTextNative(BOT, k.textX, k.textY + dy, k.label, on ? kKeyTextPressed : kKeyText, k.scale);
            }
        }

        void    DrawCandidates(float left, float right, int dy, char *buf, unsigned cap, int &chars, int &cells)
        {
            const GuiRenderer::Screen BOT = GuiRenderer::SCREEN_BOTTOM;

            if (m_phase == M_STARTING || m_phase == M_ENGINE)
            {
                GuiRenderer::DrawTextNative(BOT, kBarX + 1 + kBarPad, kBarTextY + dy, u8"変換中", kColBarText, kBarScale);
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
