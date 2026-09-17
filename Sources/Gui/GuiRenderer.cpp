// ============================================================================
// GuiRenderer — 基準仕様 v2 の描画本体
// ============================================================================
//
// 参照: BASELINE/gui_spec_v2/README.md
//       §2 契約 / §3 文字 / §4 領域 / §4.7 容量の上限 / §5 手順
//
// ★ここは**データしか書かない**。コードケーブは OwnGui.cpp が扱う。
//   Picture / Material / texMap / ResFont / FINF / TGLP / CMAP / 記述子 /
//   TextBox / バッチ / 文字列 の全部が「借りたヒープに置くただのデータ」。
//
// ★数値の正本
//   ・ケーブと番地      … Includes/GuiCaves.h（PATCHES/export_gui_v2.py が生成）
//   ・フォント          … Includes/GuiFontUi.h（PATCHES/make_font_ui.py が生成）
//     美咲ゴシック第2 362 字（日本語 275）+ PixelMplus 数字 27 字を
//     **1 枚の 256x256 シートに同居**させ、ResFont を 2 つ作る（F-312 / F-315）
//   ・フォント資源の欄  … PATCHES/own_font_data.py が正本。
//     ここの組み立てと 1 バイトずつ突き合わせるのが
//     PATCHES/verify_plugin_port_v2.py。**片方だけ直すと落ちる。**

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "GuiRenderer.hpp"
#include "GuiCaves.h"
#include "GuiFontUi.h"
#include "ChatKanji.hpp"

namespace CTRPluginFramework
{
    namespace GuiRenderer
    {
        namespace
        {
            // ------------------------------------------------------------------
            // 容量（★固定。実行時に伸びるものを 1 つも作らない。基準仕様 v2 §4.4）
            // ------------------------------------------------------------------
            // ★Frame1px が縁4辺+内地1枚の 5 枚・重なりゼロになったので（A案 F-323）、
            //   説明+インライン+ダイアログが同時に開くと最大 34 枚要る。
            //   段 6/段 7（F-325）で画面リストボックス 8 枚・通知 12 枚が増え、
            //   上画面の最悪値は 54 枚。56 枚に余裕を見る。
            // ★矩形の枠は**画面ごとに違う**（F-343）。
            //   通知は上画面にしか出ないので、上だけ増やす。下も一緒に増やすと
            //   下画面の記録の最悪値が LIST_SIZE 0x8000 の 80% を超える。
            //   通知 1 件 = 矩形 6 枚（Frame1px の 5 枚 + 縁の帯 1 枚）なので
            //   2 件（12 枚）-> 7 件（42 枚）で上画面に +30 枚要る。
            const int   kMaxRectTop  = 86;      // 上画面の矩形（F-343 で 56 -> 86、F-344 で 74、通知 7 件で 86）
            const int   kMaxRectBot  = 336;      // 下画面の矩形（据え置き）
            const int   kMaxRect     = 336;      // ★配列の大きさ。上下の大きい方
            const int   kMaxRectAll  = 422;     // Picture の総数（上 + 下）
                                                //   ★検証器の群 4 が 74 + 56 と突き合わせる

            // その画面で使える矩形の枚数（F-343。上下で違う）
            inline int  RectsOf(Screen screen)
            {
                return screen == SCREEN_TOP ? kMaxRectTop : kMaxRectBot;
            }

            // ★文字スロットは**不均一**にする（F-321）。
            //   TextBox 1 本の費用はほぼバッチで、24 字なら 8,816 B、8 字なら 3,120 B。
            //   Simulator の画面は「2 字の H 印」から「28 字の説明」まで幅があるので、
            //   全部を最大字数で取ると領域が 3 倍近くに膨らむ。
            //   **昇順に並べ、DrawText は入る中で一番小さい空きを取る。**
            //   内訳（2026-09-17 Simulator 0cabaed の移植。合計 上 93 本 / 下は据え置き）:
            //     上  2 字 x14  … ホットキー印 H x4 + [X] の赤い X x10
            //         4 字 x11  … 行のアイコン x10（"[ ]" が最長）+ 説明パネルの SYNC
            //         8 字 x19  … 行の値 x10（"OFF" / "0x2001" / 選択肢）+ 上画面リスト（見出し + 8 行）
            //        16 字 x33  … 見出し 2 + インラインリスト 6 + ダイアログの選択肢 2 + 余裕 2
            //                     + 行の項目名 10 + 下帯 3 + 通知の本文 7 + 長押しの表示 1
            //        24 字 x 7  … 通知の題（"CHEAT ENABLED 999" = 17 字）
            //        28 字 x 9  … 説明パネル（題 + HOTKEY + 本文 6 行）+ ダイアログの題
            //     下  8 字 x24  … 下画面リストボックス（見出し + 8 行）／入力待ち／スライダーの短文
            //        16 字 x 1  … 入力待ちの見出し（"HOTKEY INPUT" = 12 字）／スライダーの項目名
            //        28 字 x 1  … 入力待ちの組み合わせ表示／スライダーの操作説明
            //   ★Trim が画素幅で切るので ASCII ばかりの長い名前だけ溢れる。
            //     溢れた分は一番大きい空きへ入る（切れ端は出るが壊れない）。
            const u8    kTopSlotCaps[] = {
                 2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
                 4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
                 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
                 8,  8,  8,  8,  8,  8,  8,  8,  8,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16,
                16, 16, 16, 16, 16, 16, 16,     // 通知の本文（7 件）
                16,                             // 長押しの表示
                24, 24, 24, 24, 24, 24, 24,     // 通知の題（7 件）
                28, 28, 28, 28, 28, 28, 28, 28, 28
            };
            const u8    kBotSlotCaps[] = {
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 16, 28,
                28, 28, 28, 28, 28, 28, 28, 28
            };
            const int   kNativeSlots = 8; // dedicated multi-sheet game font rows

            const int   kTopSlots    = (int)(sizeof(kTopSlotCaps));
            const int   kBotSlots    = (int)(sizeof(kBotSlotCaps));
            const int   kSlots       = kTopSlots + kBotSlots;
            const int   kMaxSlots    = kTopSlots > kBotSlots ? kTopSlots : kBotSlots;
            const int   kTopChars    = 28;      // 上画面で一番大きいスロット
            const int   kBotChars    = 28;      // 下画面で一番大きいスロット（入力待ちの組み合わせ）
            const int   kMaxChars    = 28;      // どの画面でも入らない長さはここで切る

            // 記録コマンドリストの安全弁（§4.7。**溢れても誰も止めてくれない**）
            // 記録量の実測式（F-315。実機の 4 例に語数レベルで一致）
            //   記録 = 552 B x 文字列の本数 + 48 B x 文字数 + 204 B x Picture の枚数
            const u32   kRecPerRect  = 204;     // Picture 1 枚
            const u32   kRecPerChar  = 48;      // 1 文字
            const u32   kRecPerStr   = 552;     // ★文字列 1 本の固定費（バッチ先頭+ペイン）
            const u32   kRecLimitPct = 80;      // LIST_SIZE のこの割合まで

            // 借りる量。§4.7 の予算表に照らして決める。
            // 借用。★F-315 で 0x50000 を実機確認済み。F-321 で 0x60000 へ。
            //   F-325（段 6/段 7）で 0x6C000 へ。配置 438,784 B。
            //   最悪時の空き 738,876 B（F-310）に照らすとゲームへ約 296KB 残る。
            //   300KB の目安をわずかに割るので、場面転換の soak で見ること。
            //   ★この値は OwnGui::Enable() が GuiRenderer::BorrowBytes() で読む。
            //     以前は OwnGui 側に 0x30000 が直書きされていて食い違っていた。
            const u32   kBorrowBytes = 0x11000; // 69,632 B（★アトラスぶんだけ）
            // ★2026-09-17: アトラスを 256x128 -> 256x256（65,536 B）に広げたので 0x9000 から増やした。
            //   手紙の場面の空きは 0x9000 のとき約 457,000 B なので、増分 32,768 B を引いても約 424,000 B 残る。
            //   ★実機では未確認（手紙を開いて落ちないかを確かめること）。
            // ★F-345: **アトラスぶんだけ**借りる（32,768 B + 0x80 整列の余白）。
            //   ほかは全部 GuiRenderer の g_mem（プラグインの .bss）に置く。
            //   手紙は資源 131,456 B + テクスチャ 131,072 B = 262,528 B を
            //   この同じヒープから取るので、借りっぱなしだと確保に失敗し、
            //   **ゲームは失敗を検査せず null を参照して落ちる**。
            //   借用 0x9000 なら手紙の場面でも空きが約 457,000 B 残る。
            // ★F-344 の記録: 0x6C000 だと手紙を開いた瞬間に落ちた。
            //   手紙は `Item/Model/letter_shic.bcres` を **131,456 B / 整列 0x80** で
            //   nw::lyt のヒープから取るが、ゲーム自身が 3,376,620 B 使うので
            //   0x6C000 借りると空きが 47,636 B しか残らず確保に失敗する。
            //   **ゲームは確保の失敗を検査せずに null を参照する**（F-344）。
            //   0x50000 なら空き 162,324 B で、手紙を読んだあとも 30,868 B 残る。
            //   ★借用量は「起動直後の空き 1,331,224 B」ではなく
            //     **一番きつい場面の空き（手紙で 490,004 B）**で決めること。

            const u32   kPicBytes    = 0x158;   // Picture の実体
            const u32   kTextBoxLen  = 0x108;
            const u32   kMaterialLen = 0x50;
            const u32   kResFontLen  = 0x20;
            const u32   kFinfLen     = 0x18;
            const u32   kTglpLen     = 0x18;
            const u32   kCmapLen     = 0x10;
            const u32   kDescLen     = 0x14;
            const u32   kPicaLA4     = 9;
            // ★TextBox の整列（`+0xFC`）と基準位置（`+0xB6`）。
            //   組み合わせ選択子で 横 = 値 % 3、縦 = 値 / 3（0=左/上 1=中央 2=右/下）。
            //   ★横は**左**にする（F-322）。中央にすると経路に「幅/2」が 3 箇所
            //     現れ、幅が奇数の文字列（例: "0.7" = 15px）で字形が崩れる。
            //     縦は中央のまま（行送り 8 = 文字高 8 でどちらも偶数）。
            const int   kAlignTB     = 3;       // 横 0（左）+ 縦 1（中央）
            const u32   kBasePosTB   = 0x13;    // 同上。上位ビット 0x10 は素のまま残す

            // ------------------------------------------------------------------
            // 状態
            // ------------------------------------------------------------------
            struct RectItem
            {
                s16 x, y, w, h;
                u32 color;
            };

            struct TextItem
            {
                s16     x, y;
                u8      scale;
                u8      font;                   // 0=美咲ゴシック / 1=PixelMplus 数字
                u32     color;
                u8      slot;                   // Commit が割り当てるスロット番号
                // ★UTF-8 で持つ。日本語は 1 文字 3 バイトなので 3 倍取る。
                char    s[kMaxChars * 3 + 1];
            };

            struct ScreenState
            {
                RectItem    rects[kMaxRect];
                TextItem    texts[kMaxSlots];
                // ★描いた順番そのままの表示リスト（F-324）。
                //   旧版は Picture 全部→文字全部の順に繋いでいたので、
                //   後から開くパネル（リストボックス等）の背景 Picture が、
                //   先に描いた行の文字 TextBox より後ろになり、
                //   後ろの文字が背景に溶けず突き抜けて見えた。
                //   0=矩形 / 1=文字。値は rects / texts の添え字。
                u8          oKind[kMaxRect + kMaxSlots];
                u16         oIdx[kMaxRect + kMaxSlots];
                int         oCount;
                int         rectCount;
                int         textCount;
            };

            // ★F-345: **GPU が読むのはアトラスの画素だけ。**
            //   Picture / Pane / Material / フォント資源 / バッチはすべて CPU しか読まない
            //   （記録は `Layout_RecordPaneTree`、コマンド化は `CopyToCommandBuffer` の
            //     `Mem_Copy`。バッチのフラグを 0 にしてあるので `RangeCacheOp` も通らない）。
            //   だからそれらは**プラグイン自身の .bss** に置き、
            //   nw::lyt のヒープからはアトラスぶんしか借りない。
            //   借りっぱなしにすると手紙（資源 131,456 B + テクスチャ 131,072 B）が
            //   確保に失敗してゲームが落ちる（F-344 / F-345）。
            bool        g_ready   = false;
            u32         g_base    = 0;          // ★プラグイン側の置き場（g_mem）
            u32         g_gpuBase = 0;          // ★借りたヒープの先頭（アトラス専用）
            u32         g_size    = 0;
            ScreenState g_scr[2];
            bool        g_dirty[2] = { true, true };
            std::string g_log;
            char        g_err[128] = { 0 };

            // ★プラグイン側の置き場。ここに Picture / フォント資源 / 文字スロットが入る。
            //   大きさは Layout() が検査する（足りなければ Install が失敗する）。
            //   0x80 境界に置いておく（中で 0x20 / 0x80 の整列を仮定しているため）。
            const u32   kPlugBytes = 0x90000;   // plugin-private; game atlas borrow unchanged
            u8          g_mem[kPlugBytes] __attribute__((aligned(0x80)));

            // 配置（Install() で決める。アトラスだけ g_gpuBase 起点）
            u32         g_offPic   = 0;
            u32         g_offFont  = 0;         // ResFont から記述子まで
            u32         g_offSlot0 = 0;
            u32         g_slotStep[kSlots];
            u32         g_slotOff[kSlots];
            u32         g_offAtlas = 0;
            u32         g_total    = 0;

            // ------------------------------------------------------------------
            // 小道具
            // ------------------------------------------------------------------
            void    Log(const char *fmt, ...)
            {
                char    buf[256];
                va_list ap;

                va_start(ap, fmt);
                vsnprintf(buf, sizeof(buf), fmt, ap);
                va_end(ap);
                g_log += buf;
                g_log += "\n";
            }

            void    Fail(const char *fmt, ...)
            {
                va_list ap;

                va_start(ap, fmt);
                vsnprintf(g_err, sizeof(g_err), fmt, ap);
                va_end(ap);
                Log("[!] %s", g_err);
            }

            inline void W32(u32 a, u32 v)   { *(volatile u32 *)a = v; }
            inline void W16(u32 a, u16 v)   { *(volatile u16 *)a = v; }
            inline void W8(u32 a, u8 v)     { *(volatile u8 *)a = v; }
            inline u32  R32(u32 a)          { return *(volatile u32 *)a; }

            inline void WF(u32 a, float v)
            {
                u32 bits;

                std::memcpy(&bits, &v, 4);
                W32(a, bits);
            }

            u32     AlignUp(u32 v, u32 a)   { return (v + a - 1) & ~(a - 1); }

            // スロット i の容量（字数）。i は上画面 0..kTopSlots-1、
            // 続けて下画面 kTopSlots..kSlots-1。
            int     SlotChars(int i)
            {
                return i < kTopSlots ? (int)kTopSlotCaps[i]
                                     : (int)kBotSlotCaps[i - kTopSlots];
            }

            bool NativeSlot(int i) { return i >= kSlots - kNativeSlots; }
            bool FontFitsSlot(int i, int font) { return NativeSlot(i) == (font == FONT_GAME); }

            // ★コマンド語の器（F-342）。**ゲーム流の `(78n+57)&~3` 語は取りすぎ。**
            //   ゲームは 1 字 312 B 取るが、実際に積まれるのは 1 字 48 B しかない。
            //   実際の語数（F-341。実機 23 点で誤差 0。正本 PATCHES/batch_cost_exact.py）
            //       46 + 12n + 28*ceil(n/16)         46 = ひな型 24 + 群の頭 22
            //   ここは「1 字目で色替えが起きる最悪」(+20 / +40) と余裕 32 語も足す。
            //   ★器の大きさ `batch+0x20` は描画にも提出にも読まれない（F-342 §4 で
            //     ResetBatch / EmitGlyphQuad / BuildCommandStream / CopyToCommandBuffer /
            //     EmitStatePrefix を全部読んで確認）。だから詰めてよい。
            //     **逆に、この式を間違えると誰も止めてくれない**ので
            //     `verify_plugin_port_v2.py` の群 4 で Python の正本と突き合わせる。
            const u32   kCmdMargin = 32;        // ★batch_cost_exact.CMD_MARGIN と同じ値

            u32     BatchCmdWords(u32 n)
            {
                const u32   f = (n + 15) / 16;  // 吐き出しの回数（16 字ごと）

                return 46 + 12 * n + 28 * f + (f == 1 ? 20 : 40) + kCmdMargin;
            }

            // 標準 TextBatch に要るバイト数（基準仕様 v2 §4.3。器だけ F-342 で詰めた）。
            //   align16(44n + 0x28 + align4(ceil(n/8))) + 4 * BatchCmdWords(n)
            u32     SlotCmdWords(int i)
            {
                const u32 n = (u32)SlotChars(i);
                // ACNL 0x4D76A8: stock worst case for texture switches per glyph.
                return NativeSlot(i) ? ((78 * n + 57) & ~3u) : BatchCmdWords(n);
            }

            u32     BatchSize(int i)
            {
                const u32 n = (u32)SlotChars(i);
                u32 bitmap = AlignUp((n + 7) >> 3, 4);
                u32 head   = AlignUp(44 * n + 0x28 + bitmap, 16);
                u32 cmd    = 4 * SlotCmdWords(i);

                return head + cmd;
            }

            // 1 スロットが要る大きさ（TextBox / Material / 文字列 / バッチ）
            u32     SlotBytes(int i)
            {
                const u32   n = (u32)SlotChars(i);

                return AlignUp(kTextBoxLen, 0x20) + AlignUp(kMaterialLen, 0x20)
                     + AlignUp(2 * (n + 1), 0x20) + AlignUp(BatchSize(i), 0x20);
            }

            // ------------------------------------------------------------------
            // 文字の引き方（CMAP 方式 2 + CWDH）
            // ------------------------------------------------------------------
            //   ★ゲームの `MapCharToGlyph 0x743768` の方式 2 と**同じ二分探索**を写す。
            //     ここで出す幅は表示に使うだけだが、ゲームと食い違うと
            //     ペインの寸法が文字の寸法と合わなくなり半画素ずれる（F-311）。
            int     GlyphIndex(u32 code, int font)
            {
                if (font == FONT_GAME) return ChatKanji::Glyph(code);
                const unsigned short   *p = font ? kUiCmapNumeric : kUiCmapMisaki;
                int                     lo = 0;
                int                     hi = (int)(font ? kUiCmapNumericCount
                                                        : kUiCmapMisakiCount) - 1;
                const u32               b = font ? kUiCmapNumericBegin : kUiCmapMisakiBegin;
                const u32               e = font ? kUiCmapNumericEnd : kUiCmapMisakiEnd;

                if (code < b || code > e)
                    return -1;
                while (lo <= hi)
                {
                    const int   mid = (lo + hi) / 2;
                    const u32   c = p[mid * 2];

                    if (c > code)
                        hi = mid - 1;
                    else if (c < code)
                        lo = mid + 1;
                    else
                        return (int)p[mid * 2 + 1];
                }
                return -1;
            }

            int     GlyphAdvance(int gi, int font = FONT_MAIN)
            {
                if (font == FONT_GAME) return ChatKanji::GlyphAdvance(gi);
                return gi < 0 ? 0 : (int)kUiCwdh[gi * 3 + 2];
            }

            // UTF-8 を 1 文字読む。index は次へ進む。読めなければ 0xFFFD。
            u32     NextCodepoint(const char *t, int &i)
            {
                const unsigned char c = (unsigned char)t[i];

                if (c < 0x80)
                {
                    i += 1;
                    return c;
                }
                if ((c & 0xE0) == 0xC0 && (t[i + 1] & 0xC0) == 0x80)
                {
                    const u32 v = ((u32)(c & 0x1F) << 6) | (u32)(t[i + 1] & 0x3F);

                    i += 2;
                    return v;
                }
                if ((c & 0xF0) == 0xE0 && (t[i + 1] & 0xC0) == 0x80 && (t[i + 2] & 0xC0) == 0x80)
                {
                    const u32 v = ((u32)(c & 0x0F) << 12) | ((u32)(t[i + 1] & 0x3F) << 6)
                                  | (u32)(t[i + 2] & 0x3F);

                    i += 3;
                    return v;
                }
                // ★4 バイト（BMP 外）は扱わない。フォントにも無い。
                i += 1;
                return 0xFFFD;
            }

            // ------------------------------------------------------------------
            // 領域の重なり検査（★基準仕様 v2 §4.4。1 件でも当たれば書き込まない）
            // ------------------------------------------------------------------
            struct Region { const char *name; u32 off; u32 len; };

            bool    CheckRegions(const Region *r, int n, u32 cap)
            {
                int     i = 0;
                bool    ok = true;

                while (i < n)
                {
                    if (r[i].off + r[i].len > cap)
                    {
                        Fail("%s が領域を超える (0x%X + 0x%X > 0x%X)",
                             r[i].name, (unsigned int)r[i].off,
                             (unsigned int)r[i].len, (unsigned int)cap);
                        ok = false;
                    }
                    int j = i + 1;
                    while (j < n)
                    {
                        const u32   a0 = r[i].off, a1 = r[i].off + r[i].len;
                        const u32   b0 = r[j].off, b1 = r[j].off + r[j].len;

                        if (a0 < b1 && b0 < a1)
                        {
                            Fail("%s と %s が重なる", r[i].name, r[j].name);
                            ok = false;
                        }
                        j++;
                    }
                    i++;
                }
                return ok;
            }

            const u32   kFontFontStep = 0x80;
            const u32   kFontCwdhOff  = 0x100;

            u32     FontCwdhAddr(void)
            {
                return g_base + g_offFont + kFontCwdhOff;
            }

            u32     FontCwdhLen(void)
            {
                return AlignUp(8 + 3 * kUiGlyphCount, 0x20);
            }

            u32     FontCmapAddr(int font)
            {
                const u32   c0 = FontCwdhAddr() + FontCwdhLen();

                if (font == 0)
                    return c0;
                return c0 + AlignUp(0x0E + 4 * kUiCmapMisakiCount, 0x20);
            }

            u32     FontBlockLen(void)
            {
                return (FontCmapAddr(1) + 0x0E + 4 * kUiCmapNumericCount) - (g_base + g_offFont);
            }

            u32     FontResFontAddr(int font)
            {
                if (font == FONT_GAME) return ChatKanji::FontAddress();
                return g_base + g_offFont + (u32)font * kFontFontStep;
            }

            // ------------------------------------------------------------------
            // 配置を決める（base が決まってから。アトラスは 0x80 境界へ）
            // ------------------------------------------------------------------
            bool    Layout(u32 gpuBase, u32 cap)
            {
                u32 off = kShared;

                g_offPic = off;
                off += (u32)kMaxRectAll * kPicStep;
                off = AlignUp(off, 0x20);

                g_offFont = off;
                // ResFont/FINF/TGLP/記述子 を 2 組（0x80 ずつ）+ CWDH + CMAP 2 本
                off += kFontCwdhOff + FontCwdhLen()
                       + AlignUp(0x0E + 4 * kUiCmapMisakiCount, 0x20)
                       + AlignUp(0x0E + 4 * kUiCmapNumericCount, 0x20);
                off = AlignUp(off, 0x20);

                g_offSlot0 = off;
                int i = 0;
                while (i < kSlots)
                {
                    g_slotOff[i] = off;
                    g_slotStep[i] = SlotBytes(i);
                    off += g_slotStep[i];
                    i++;
                }

                g_total = off;                  // ★ここまでがプラグイン側

                if (g_total > kPlugBytes)
                {
                    Fail("プラグイン側の配置 0x%X が置き場 0x%X に収まらない",
                         (unsigned int)g_total, (unsigned int)kPlugBytes);
                    return false;
                }

                // ★アトラスだけ借りたヒープへ。**VA** が 0x80 境界に乗るようにずらす（§4.2）
                u32 ga = 0;
                while (((gpuBase + ga) & 0x7F) != 0)
                    ga += 4;
                g_offAtlas = ga;
                if (g_offAtlas + kUiSheetBytes > cap)
                {
                    Fail("アトラス 0x%X が借用 0x%X に収まらない",
                         (unsigned int)(g_offAtlas + kUiSheetBytes), (unsigned int)cap);
                    return false;
                }

                // 重なり検査
                Region  r[5 + kSlots];
                int     n = 0;

                r[n].name = "共有 Material/texMap"; r[n].off = 0;         r[n].len = kShared; n++;
                r[n].name = "Picture 群";          r[n].off = g_offPic;  r[n].len = (u32)kMaxRectAll * kPicStep; n++;
                r[n].name = "フォント資源";         r[n].off = g_offFont;
                r[n].len = kFontCwdhOff + FontCwdhLen()
                           + AlignUp(0x0E + 4 * kUiCmapMisakiCount, 0x20)
                           + AlignUp(0x0E + 4 * kUiCmapNumericCount, 0x20); n++;
                i = 0;
                while (i < kSlots)
                {
                    r[n].name = "文字スロット";
                    r[n].off = g_slotOff[i];
                    r[n].len = g_slotStep[i];
                    n++;
                    i++;
                }
                // ★アトラスは別の領域（借りたヒープ）にあるので、ここでは検査しない
                return CheckRegions(r, n, kPlugBytes);
            }

            // ------------------------------------------------------------------
            // 共有 Material と texMap（Picture が全部これを指す）
            // ------------------------------------------------------------------
            void    WriteSharedMaterial(void)
            {
                const u32   mat = g_base;
                const u32   blk = g_base + 0x80;
                const u32   atlas = g_gpuBase + g_offAtlas;
                const u32   pa = atlas - 0x10000000;
                int         k;

                std::memset((void *)mat, 0, kShared);
                W32(mat + 0x00, kVtMaterial);
                W32(mat + 0x08, mat + 0x08);
                W32(mat + 0x0C, mat + 0x08);
                k = 0;
                while (k < 7)
                {
                    W32(mat + 0x10 + k * 4, 0xFFFFFFFF);
                    k++;
                }
                W32(mat + 0x10, 0x00000000);        // 色[0] -> reg 0x0DB
                W32(mat + 0x2C, 0x15);              // texMap1 / texSRT1 / texCoordGen1
                W32(mat + 0x30, 0x15);
                W32(mat + 0x34, blk);
                W8(mat + 0x4D, 0x04);               // bit1=0 / bit2=1

                W32(blk + 0x04, pa);
                W32(blk + 0x08, (kUiSheetH << 16) | kUiSheetW);
                W32(blk + 0x0C, (kUiSheetH << 16) | kUiSheetW);
                W32(blk + 0x10, kFmtLA4 << 8);
                W32(blk + 0x14, 0);
                W32(blk + 0x18, (kUiSheetW << 16) | kUiSheetH);
                W32(blk + 0x1C, kPicaLA4);
                WF(blk + 0x2C, 1.0f);
                WF(blk + 0x30, 1.0f);
            }

            // ------------------------------------------------------------------
            // Pane / Picture / TextBox 共通の欄（nwlyt_Pane_Ctor と同じ初期化）
            // ------------------------------------------------------------------
            void    PaneCommon(u32 o, u32 vt, float w, float h, float tx, float ty, float tz)
            {
                int m, r, c;

                W32(o + 0x00, vt);
                W32(o + 0x04, 0);
                W32(o + 0x08, 0);
                W32(o + 0x10, 0);
                W32(o + 0x14, o + 0x14);        // 子リストの番兵は自分の +0x14
                W32(o + 0x18, o + 0x14);
                W32(o + 0x1C, 0);
                W32(o + 0x20, o + 0x20);
                W32(o + 0x24, o + 0x20);
                WF(o + 0x40, 1.0f);
                WF(o + 0x44, 1.0f);
                WF(o + 0x48, w);
                WF(o + 0x4C, h);
                m = 0;
                while (m < 2)
                {
                    const u32 b = o + (m == 0 ? 0x50 : 0x80);

                    r = 0;
                    while (r < 3)
                    {
                        c = 0;
                        while (c < 4)
                        {
                            WF(b + r * 16 + c * 4, r == c ? 1.0f : 0.0f);
                            c++;
                        }
                        r++;
                    }
                    m++;
                }
                WF(o + 0x80 + 12, tx);          // 行列 [0][3] = X
                WF(o + 0x90 + 12, ty);          // [1][3] = Y
                WF(o + 0xA0 + 12, tz);          // [2][3] = Z
                W8(o + 0xB4, 0xFF);
                W8(o + 0xB5, 0xFF);             // alpha
                W8(o + 0xB6, (u8)kBasePosCenter);
                W8(o + 0xB7, 0x01);             // bit0 = 可視
            }

            // ★文字ペイン専用（F-322）。横は左アンカー + 左寄せなので
            //   ペイン幅が位置に効かない。cx がそのまま文字の左端になる。
            //   縦は従来どおり中央（アンカー +高さ/2 と整列 -文字高/2 が打ち消す）。
            void    ToTextOrigin(float sw, float sh, int x, int y, int h,
                                 float &cx, float &cy)
            {
                cx = (float)x - sw / 2.0f;
                cy = sh / 2.0f - ((float)y + (float)h / 2.0f);
            }

            // 左上原点 -> 画面中央原点（F-290 で実測した写像）
            void    ToCenter(float sw, float sh, int x, int y, int w, int h,
                             float &cx, float &cy)
            {
                cx = (float)x + (float)w / 2.0f - sw / 2.0f;
                cy = sh / 2.0f - ((float)y + (float)h / 2.0f);
            }

            float   ScreenW(Screen s) { return s == SCREEN_TOP ? (float)kGuiTopW : (float)kGuiBotW; }
            float   ScreenH(Screen s) { (void)s; return (float)kGuiTopH; }

            // 塗り潰しのマス（F-311）。
            //   ★v1 と同じく「マス 1 つを塗り潰す」方式にした。
            //     初版は '|' の中心 1 画素を指していたが、UV の幅が 1 テクセルしかなく
            //     フィルタや丸めで隣の消灯画素を拾いうる。マスの内側を広く取る。
            //   セル内側は (PITCH*col+1 .. +CELL_W)。さらに 1 画素内側を使う。
            void    SolidUv(float &u0, float &v0, float &u1, float &v1)
            {
                const u32   col = kUiSolidIndex % kUiCols;
                const u32   row = kUiSolidIndex / kUiCols;
                const float x0 = (float)((kUiCellW + 1) * col + 2);
                const float y0 = (float)((kUiCellH + 1) * row + 2);
                const float x1 = (float)((kUiCellW + 1) * col + kUiCellW);
                const float y1 = (float)((kUiCellH + 1) * row + kUiCellH);

                u0 = x0 / (float)kUiSheetW;
                v0 = ((float)kUiSheetH - y0) / (float)kUiSheetH;
                u1 = x1 / (float)kUiSheetW;
                v1 = ((float)kUiSheetH - y1) / (float)kUiSheetH;
            }

            // ------------------------------------------------------------------
            // フォント資源（★2 つ作る。字形は 1 枚のシートに同居。F-312 / F-315）
            // ------------------------------------------------------------------
            //   配置（g_offFont から）
            //     +0x000 ResFont(主) +0x020 FINF +0x040 TGLP +0x060 記述子
            //     +0x080 ResFont(数) +0x0A0 FINF +0x0C0 TGLP +0x0E0 記述子
            //     +0x100 CWDH（1 ノードで全字形。両方で共有）
            //     続けて CMAP(主) / CMAP(数)  ※方式 2
            //   ★値は PATCHES/own_font_data.py と PATCHES/make_font_ui.py が正本。
            //     PATCHES/verify_plugin_port_v2.py が突き合わせる。
            void    WriteCwdh(void)
            {
                const u32   cw = FontCwdhAddr();
                u32         i = 0;

                W16(cw + 0x00, 0);                          // 開始字形番号
                W16(cw + 0x02, (u16)(kUiGlyphCount - 1));   // 終了
                W32(cw + 0x04, 0);                          // 次は無し
                while (i < kUiGlyphCount)
                {
                    W8(cw + 8 + i * 3 + 0, kUiCwdh[i * 3 + 0]);   // left（0 固定）
                    W8(cw + 8 + i * 3 + 1, kUiCwdh[i * 3 + 1]);   // glyphWidth
                    W8(cw + 8 + i * 3 + 2, kUiCwdh[i * 3 + 2]);   // advance
                    i++;
                }
            }

            // CMAP 方式 2 = ソート済み (コード, 字形番号) の対を二分探索
            void    WriteCmap(int font)
            {
                const u32               a = FontCmapAddr(font);
                const unsigned short   *p = font ? kUiCmapNumeric : kUiCmapMisaki;
                const u32               n = font ? kUiCmapNumericCount : kUiCmapMisakiCount;
                u32                     i = 0;

                W16(a + 0x00, (u16)(font ? kUiCmapNumericBegin : kUiCmapMisakiBegin));
                W16(a + 0x02, (u16)(font ? kUiCmapNumericEnd : kUiCmapMisakiEnd));
                W16(a + 0x04, 2);                   // ★方式 2
                W16(a + 0x06, 0);
                W32(a + 0x08, 0);                   // 次は無し
                W16(a + 0x0C, (u16)n);              // 対の件数
                while (i < n)
                {
                    W16(a + 0x0E + i * 4, p[i * 2]);        // コード
                    W16(a + 0x10 + i * 4, p[i * 2 + 1]);    // 字形番号
                    i++;
                }
            }

            void    WriteOneFont(int font)
            {
                const u32   rf = FontResFontAddr(font);
                const u32   finf = rf + 0x20;
                const u32   tglp = rf + 0x40;
                const u32   desc = rf + 0x60;
                const u32   atlas = g_gpuBase + g_offAtlas;

                std::memset((void *)rf, 0, kFontFontStep);

                // ---- ResFont ----
                W32(rf + 0x00, kVtResFont);
                W32(rf + 0x04, 0);
                W32(rf + 0x08, finf);
                W32(rf + 0x0C, desc);
                W32(rf + 0x10, 0);              // フィルタ（0 = ニアレスト）
                W16(rf + 0x14, 0xFFFF);
                W16(rf + 0x16, 0xFFFF);

                // ---- FINF ----
                W8(finf + 0x00, 1);
                W8(finf + 0x01, (u8)kUiLinefeed);
                W16(finf + 0x02, 0);
                W8(finf + 0x04, 0);                     // 既定 CWDH（CWDH が引ければ不使用）
                W8(finf + 0x05, (u8)kUiCellW);
                W8(finf + 0x06, (u8)(kUiCellW + 1));
                W8(finf + 0x07, 1);                     // ★1 以外だと PC=0（F-304）
                W32(finf + 0x08, tglp);
                W32(finf + 0x0C, FontCwdhAddr());       // ★CWDH（可変幅）
                W32(finf + 0x10, FontCmapAddr(font));
                W8(finf + 0x14, (u8)kUiMetricH);        // scaleY = 要求高 / これ
                W8(finf + 0x15, (u8)kUiMetricW);        // scaleX = 要求幅 / これ
                W8(finf + 0x16, (u8)kUiBaseline);       // ★TGLP+0x02 と同値にして Y を相殺

                // ---- TGLP ----
                W8(tglp + 0x00, (u8)kUiCellW);
                W8(tglp + 0x01, (u8)kUiCellH);
                W8(tglp + 0x02, (u8)kUiBaseline);
                W8(tglp + 0x03, (u8)(kUiCellW + 1));
                W32(tglp + 0x04, kUiSheetBytes);
                W16(tglp + 0x08, 1);                    // 枚数
                W16(tglp + 0x0A, (u16)kUiPicaLA4);
                W16(tglp + 0x0C, (u16)kUiCols);
                W16(tglp + 0x0E, (u16)kUiRows);
                W16(tglp + 0x10, (u16)kUiSheetW);
                W16(tglp + 0x12, (u16)kUiSheetH);
                W32(tglp + 0x14, atlas);

                // ---- シート記述子 ----
                W32(desc + 0x00, 0);
                W32(desc + 0x04, rf);                       // ★ResFont 自身
                W32(desc + 0x08, (atlas - 0x10000000) >> 3); // reg 0x085
                // ★★F-316: **(幅<<16) | 高さ**。逆だと非正方形のシートで文字化けする。
                //   128x128 では同じ値になるので気付けない。256x128 を実機に載せて判明した。
                W32(desc + 0x0C, (kUiSheetW << 16) | kUiSheetH);
                W8(desc + 0x10, (u8)kUiPicaLA4);
            }

            void    WriteFontResource(void)
            {
                WriteCwdh();
                WriteCmap(0);
                WriteCmap(1);
                WriteOneFont(0);
                WriteOneFont(1);
            }

            // ------------------------------------------------------------------
            // 1 つの文字スロットを組む
            // ------------------------------------------------------------------
            u32     SlotTextBox(int i)  { return g_base + g_slotOff[i]; }

            void    SlotAddrs(int i, u32 &tb, u32 &mat, u32 &str, u32 &batch)
            {
                const u32   n = (u32)SlotChars(i);

                tb    = g_base + g_slotOff[i];
                mat   = tb + AlignUp(kTextBoxLen, 0x20);
                str   = mat + AlignUp(kMaterialLen, 0x20);
                batch = str + AlignUp(2 * (n + 1), 0x20);
            }

            // 空のバッチ（文字数 n ぶん）を初期化する。基準仕様 v2 §4.3 の配置。
            void    WriteBatch(u32 bt, int i)
            {
                const u32   n = (u32)SlotChars(i);
                const u32   sz = BatchSize(i);
                const u32   bits = bt + 44 * n + 0x28;
                const u32   cmd = AlignUp(bits + ((n + 7) >> 3), 16);

                std::memset((void *)bt, 0, sz);
                W32(bt + 0x00, n);              // 最大文字数
                W16(bt + 0x04, 0);              // 件数
                W16(bt + 0x06, (u16)n);         // ★上限。BuildCommandStream が
                                                //   n = min(件数, +0x06) で切る（F-342 §4）
                W8(bt + 0x08, 0);               // 構築済み
                W8(bt + 0x09, 0);               // ★フラグは 0（Mem_Copy で記録列へ複製させる）
                W32(bt + 0x0C, bits);
                W32(bt + 0x10, cmd);
                W32(bt + 0x14, cmd);
                W32(bt + 0x1C, 0);
                W32(bt + 0x20, SlotCmdWords(i));   // capacity includes native texture switches
            }

            // 文字スロットを「見えない」状態で組む。Commit() が中身を入れる。
            void    BuildSlot(int i, Screen s)
            {
                u32         tb, mat, str, batch;
                const u32   n = (u32)SlotChars(i);

                SlotAddrs(i, tb, mat, str, batch);

                std::memset((void *)tb, 0, kTextBoxLen);
                PaneCommon(tb, kVtTextBox, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
                W8(tb + 0xB7, 0x00);            // 既定は不可視

                std::memset((void *)mat, 0, kMaterialLen);
                W32(mat + 0x00, kVtMaterial);
                W32(mat + 0x08, mat + 0x08);
                W32(mat + 0x0C, mat + 0x08);
                W32(mat + 0x10, 0x00000000);
                W32(mat + 0x14, 0xFFFFFFFF);

                std::memset((void *)str, 0, 2 * (n + 1));
                WriteBatch(batch, i);

                W32(tb + 0xD4, str);
                W32(tb + 0xD8, 0xFFFFFFFF);     // 上の色
                W32(tb + 0xDC, 0xFFFFFFFF);     // 下の色
                W32(tb + 0xE0, FontResFontAddr(0));
                // 要求サイズ。FINF+0x15/+0x14（= kUiMetricW/H）と同値で倍率 1。
                WF(tb + 0xE4, (float)kUiMetricW);
                WF(tb + 0xE8, (float)kUiMetricH);
                WF(tb + 0xEC, 0.0f);
                WF(tb + 0xF0, 0.0f);
                W32(tb + 0xF4, 0);
                W16(tb + 0xF8, (u16)(2 * (n + 1)));
                W16(tb + 0xFA, 0);              // 文字数
                W8(tb + 0xFC, (u8)kAlignTB);
                // ★アンカーも横を「左」にする。PaneCommon は中央（0x14）を入れるので
                //   ここで上書きする。整列だけ左にしてもアンカーが -幅/2 を出す。
                W8(tb + 0xB6, (u8)kBasePosTB);
                W8(tb + 0xFD, 0);
                W8(tb + 0xFE, 1);
                W32(tb + 0x100, mat);
                W32(tb + 0x104, batch);
                (void)s;
            }

            // 1 行ぶんの中身を入れる。戻り値 = 実際に入れた文字数。
            int     FillSlot(int i, Screen s, const TextItem &t)
            {
                u32         tb, mat, str, batch;
                const int   cap = SlotChars(i);
                int         n = 0;
                int         k = 0;

                int         wpx = 0;

                SlotAddrs(i, tb, mat, str, batch);
                // ★UTF-8 を読んで UTF-16LE で置く。幅は CWDH の送り（可変幅）を足す。
                //   ゲームの MapCharToGlyph（方式 2）と同じ二分探索で引くので、
                //   ペインの幅が実際に描かれる幅と一致する（F-311 の半画素ずれ対策）。
                while (t.s[k] != '\0' && n < cap)
                {
                    const u32   cp = NextCodepoint(t.s, k);
                    const int   gi = GlyphIndex(cp, (int)t.font);

                    // ★字形が無いコードは**書かない**。書くとゲーム側は
                    //   代替字形（FINF+0x02 = 0）を描くので、こちらが幅 0 と
                    //   数えた分だけペインが狭くなり中央揃えがずれる。
                    if (gi < 0)
                        continue;
                    W16(str + (u32)n * 2, (u16)cp);
                    wpx += GlyphAdvance(gi, (int)t.font);
                    n++;
                }
                W16(str + (u32)n * 2, 0);

                const int   scale = t.scale < 1 ? 1 : (int)t.scale;
                const int   wpxs = wpx * scale;
                // ★ペイン幅は**折り返し幅**にしか使われない
                //   （`0x4BADA0` が `textbox+0x48` を `writer+0x4C` へ写す）。
                //   左寄せにしたので位置には効かない。偶数へ切り上げておく。
                const int   w = wpxs + (wpxs & 1);
                // ★★★ペインの高さは「セル高」ではなく「行送り」にする（F-311）。
                //   整列 4 は垂直中央（`nwlyt_TextBox_ConfigureTextWriter 0x4BADA0` が
                //   `+0xFC / 3 == 1` を 0x100 に写し、`ApplyAlignment 0x7E7968` が
                //   `y -= 文字高 * 0.5` する）。
                //   このとき **文字上端 = ペイン中心 - 文字高/2** なので、
                //   `ペイン高 == 文字高` でないと 0.5 画素ずれる。
                //   セル高 7 を入れていたら実機で字形の上 1 行が丸ごと欠けた
                //   （'A' が 'H'、'C' が 'L' に見えた）。ずれ 0.5 = |7 - 文字高|/2 から
                //   **文字高 = 8 = 行送り**と逆算できる。
                //   ★横は w = 文字数 * 送り が測定幅と一致するので元から整数だった。
                const int   h = (int)kUiLinefeed * scale;
                float       cx, cy;

                ToTextOrigin(ScreenW(s), ScreenH(s), t.x, t.y, h, cx, cy);
                WF(tb + 0x48, (float)w);
                WF(tb + 0x4C, (float)h);
                WF(tb + 0x80 + 12, cx);
                WF(tb + 0x90 + 12, cy);
                WF(tb + 0xA0 + 12, -1.0f);
                W32(tb + 0xD8, t.color);
                W32(tb + 0xDC, t.color);
                WF(tb + 0xE4, (float)((int)kUiMetricW * scale));   // scaleX = scale
                WF(tb + 0xE8, (float)((int)kUiMetricH * scale));   // scaleY = scale
                W16(tb + 0xFA, (u16)n);
                // ★行ごとにフォントを選ぶ（美咲／PixelMplus 数字）
                W32(tb + 0xE0, FontResFontAddr((int)t.font));
                W8(tb + 0xB7, n > 0 ? 0x01 : 0x00);

                // ★文字列を差し替えたらバッチを組み直させる（構築済みフラグを落とす）
                W8(batch + 0x08, 0);
                W16(batch + 0x04, 0);
                // ★TextBox 側の dirty（+0xFE）も立て直す。基準仕様 v2 §3.1。
                //   BuildSlot で 1 回立てるだけだと、一度描いたあとに文字を
                //   変えても組み直されない恐れがある。
                W8(tb + 0xFE, 1);
                return n;
            }

            // ------------------------------------------------------------------
            // Picture — 組み立て（Install で 1 回だけ）と更新（Commit）を分ける
            // ------------------------------------------------------------------
            // ★★★分けている理由（F-311）
            //   初版は Commit のたびに memset してから組み直していた。
            //   memset は **vtable を 0 にする**ので、その瞬間ゲームの描画スレッドが
            //   その Picture を子リスト経由で辿って DrawSelf を呼ぶと `BLX 0` で死ぬ。
            //   Commit は GuiMenu のスレッドから走るので、これは実際に起こりうる。
            //   → **生きているオブジェクトを memset しない。**
            //     Install で全枚数を有効な状態（不可視）で作り、
            //     Commit では位置・大きさ・色・可視だけを書き換える。
            u32     PicAddr(int index)
            {
                return g_base + g_offPic + (u32)index * kPicStep;
            }

            void    InitPicture(int index)
            {
                const u32   o = PicAddr(index);
                float       u0, v0, u1, v1;
                int         k;

                std::memset((void *)o, 0, kPicBytes);
                PaneCommon(o, kVtPicture, 1.0f, 1.0f, 0.0f, 0.0f, -1.0f);
                W8(o + 0xB7, 0x00);             // 既定は不可視
                SolidUv(u0, v0, u1, v1);
                W8(o + 0xD4, 1);
                W32(o + 0xD8, 1);
                WF(o + 0xDC, u0);
                WF(o + 0xE0, v0);
                WF(o + 0xE4, u1);
                WF(o + 0xE8, v1);
                W32(o + 0x13C, g_base);         // 共有 Material
                k = 0;
                while (k < 4)
                {
                    W32(o + 0x140 + (u32)k * 4, 0);
                    k++;
                }
            }

            void    UpdatePicture(int index, Screen s, const RectItem &r)
            {
                const u32   o = PicAddr(index);
                float       cx, cy;
                int         k;

                ToCenter(ScreenW(s), ScreenH(s), r.x, r.y, r.w, r.h, cx, cy);
                WF(o + 0x48, (float)r.w);
                WF(o + 0x4C, (float)r.h);
                WF(o + 0x80 + 12, cx);
                WF(o + 0x90 + 12, cy);
                WF(o + 0xA0 + 12, -1.0f);
                k = 0;
                while (k < 4)
                {
                    W32(o + 0x140 + (u32)k * 4, r.color);   // 頂点カラーで色を決める
                    k++;
                }
                W8(o + 0xB7, 0x01);             // 可視
            }

            // ------------------------------------------------------------------
            // 子リスト（輪）を張り替える
            //   節の番地 = 実体 + 4。next が [節]、prev が [節+4]。
            //   ★文字のペインは必ず最後（基準仕様 v2 §2.6）
            // ------------------------------------------------------------------
            void    LinkRing(u32 root, const u32 *objs, int n)
            {
                const u32   sent = root + 0x14;
                int         i;

                if (n <= 0)
                {
                    W32(root + 0x14, sent);
                    W32(root + 0x18, sent);
                    return;
                }
                i = 0;
                while (i < n)
                {
                    const u32 node = objs[i] + 4;

                    W32(node, i + 1 < n ? objs[i + 1] + 4 : sent);      // next
                    W32(node + 4, i > 0 ? objs[i - 1] + 4 : sent);      // prev
                    i++;
                }
                // ★鎖を全部つないでから **最後に先頭を公開する**。
                //   ゲームは root+0x14 から辿り始めるので、途中の状態を見せない。
                W32(root + 0x18, objs[n - 1] + 4);
                W32(root + 0x14, objs[0] + 4);
            }

            // ------------------------------------------------------------------
            // 記録コマンドリストの安全弁（§4.7。溢れは無検査なので自前で持つ）
            // ------------------------------------------------------------------
            // 記録量 = 552 B x 文字列の本数 + 48 B x 字数 + 204 B x Picture（F-315）
            u32     EstimateRecorded(int rects, int strs, int chars, int nativeChars)
            {
                return (u32)rects * kRecPerRect + (u32)strs * kRecPerStr
                     + (u32)chars * kRecPerChar + (u32)nativeChars * (312 - kRecPerChar);
            }

            // ★リストの大きさは画面ごとに違う（OwnGui::WriteCaves が焼き直す）
            u32     RecordLimit(Screen screen)
            {
                const u32   sz = (screen == SCREEN_TOP) ? kGuiListSizeTop
                                                        : kGuiListSizeBot;

                return sz * kRecLimitPct / 100;
            }

            // 実際に置かれる字数（UTF-8 を数え、字形の無いコードは除く）。
            // ★FillSlot と同じ勘定でなければ安全弁が意味を持たない。
            int     CountChars(const TextItem &t, int cap)
            {
                int i = 0;
                int n = 0;

                while (t.s[i] != '\0' && n < cap)
                {
                    const u32   cp = NextCodepoint(t.s, i);

                    if (GlyphIndex(cp, (int)t.font) >= 0)
                        n++;
                }
                return n;
            }
        }

        // ==================================================================
        // 公開部
        // ==================================================================

        bool    IsReady(void)       { return g_ready; }
        u32     HeapBase(void)      { return g_base; }
        u32     HeapSize(void)      { return g_size; }
        u32     AtlasVa(void)       { return g_base + g_offAtlas; }
        u32     AtlasBytes(void)    { return kUiSheetBytes; }
        const char *LastError(void) { return g_err; }

        int     MaxRects(Screen screen) { return RectsOf(screen); }
        int     MaxTexts(Screen screen)
        {
            return screen == SCREEN_TOP ? kTopSlots : kBotSlots;
        }
        int     MaxChars(Screen screen)
        {
            // ★一番大きいスロットの容量。スロットは不均一なので、
            //   これ以下でも空きが無ければ入らないことがある（Commit が記録に残す）。
            return screen == SCREEN_TOP ? kTopChars : kBotChars;
        }

        u32     BorrowBytes(void)   { return kBorrowBytes; }

        // UTF-8 の 1 文字を読み進め、その送り幅（画素）を返す。
        // ★字形が無い文字は 0 を返す（FillSlot も書かずに飛ばすので勘定が合う）。
        int     NextCharWidth(const char *text, int &index, int scale, Font font)
        {
            const u32   cp = NextCodepoint(text, index);

            return GlyphAdvance(GlyphIndex(cp, (int)font), (int)font) * (scale < 1 ? 1 : scale);
        }

        int     TextHeight(int scale)
        {
            // 行の間隔。★ペインの高さもこれに合わせる（FillSlot の注記）。
            return (int)kUiLinefeed * (scale < 1 ? 1 : scale);
        }

        int     MeasureText(const char *text, int scale, Font font)
        {
            int i = 0;
            int w = 0;

            if (text == nullptr)
                return 0;
            // ★可変幅なので「文字数 x 送り」では測れない。1 文字ずつ CWDH を引く。
            while (text[i] != '\0')
                w += GlyphAdvance(GlyphIndex(NextCodepoint(text, i), (int)font), (int)font);
            return w * (scale < 1 ? 1 : scale);
        }

        u32     RecordedBytes(Screen screen)
        {
            const u32 node = screen == SCREEN_TOP ? kGuiNodeTop : kGuiNodeBot;

            return R32(node + 0x108);
        }

        // ==================================================================
        // ★下画面のタッチ遮断と暗幕（F-350）
        //   タッチ遮断と暗幕は別々に決める。暗幕は下画面 UI の出現量に合わせて描き、
        //   タッチ遮断は UI が退場し終わり、さらに指が離れるまで続ける（GuiMenu が決める）。
        // ==================================================================
        namespace
        {
            // 入力遮断ケーブの制御ブロック（+0 ボタン / +1 タッチ）
            void    WriteInputFlag(u32 off, bool on)
            {
                *(volatile u8 *)(kGuiInputCtl + off) = on ? 1 : 0;
            }
        }

        void    SetTouchBlock(bool on)  { WriteInputFlag(1, on); }

        void    SetButtonBlock(bool on) { WriteInputFlag(0, on); }

        void    DrawBottomDim(u32 color, float amount)
        {
            const float a = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);

            if (a <= 0.0f)
                return;

            const u32   al = (u32)((float)((color >> 24) & 0xFF) * a + 0.5f);

            FillRect(SCREEN_BOTTOM, 0, 0, 320, 240, (color & 0x00FFFFFF) | (al << 24));
        }

        void    Begin(Screen screen)
        {
            g_scr[screen].rectCount = 0;
            g_scr[screen].textCount = 0;
            g_scr[screen].oCount = 0;
        }

        void    FillRect(Screen screen, int x, int y, int w, int h, u32 color)
        {
            ScreenState &st = g_scr[screen];

            if (st.rectCount >= RectsOf(screen) || w <= 0 || h <= 0)
                return;
            if (st.oCount < kMaxRect + kMaxSlots)
            {
                st.oKind[st.oCount] = 0;
                st.oIdx[st.oCount] = (u16)st.rectCount;
                st.oCount++;
            }
            st.rects[st.rectCount].x = (s16)x;
            st.rects[st.rectCount].y = (s16)y;
            st.rects[st.rectCount].w = (s16)w;
            st.rects[st.rectCount].h = (s16)h;
            st.rects[st.rectCount].color = color;
            st.rectCount++;
        }

        void    DrawText(Screen screen, int x, int y, const char *text, u32 color, int scale,
                         Font font)
        {
            ScreenState &st = g_scr[screen];
            const int   maxSlots = MaxTexts(screen);
            int         k = 0;

            if (text == nullptr || st.textCount >= maxSlots)
                return;
            if (st.oCount < kMaxRect + kMaxSlots)
            {
                st.oKind[st.oCount] = 1;
                st.oIdx[st.oCount] = (u16)st.textCount;
                st.oCount++;
            }
            TextItem &t = st.texts[st.textCount];

            t.x = (s16)x;
            t.y = (s16)y;
            t.scale = (u8)(scale < 1 ? 1 : scale);
            t.font = (u8)font;
            t.slot = 0xFF;              // Commit が決める
            t.color = color;
            // ★ここはバイト数で切る（字数の上限は FillSlot 側の cap で効く）。
            //   途中で切れて UTF-8 の続きバイトが残らないよう、残ったら戻す。
            while (text[k] != '\0' && k < (int)sizeof(t.s) - 1)
                k++;
            while (k > 0 && ((unsigned char)text[k] & 0xC0) == 0x80)
                k--;
            std::memcpy(t.s, text, (size_t)k);
            t.s[k] = '\0';
            st.textCount++;
        }

        void    Commit(void)
        {
            if (!g_ready)
                return;

            int s = 0;
            while (s < 2)
            {
                const Screen    sc = (Screen)s;
                ScreenState    &st = g_scr[s];
                const int       picBase = (s == 0) ? 0 : kMaxRectTop;
                const int       slotBase = (s == 0) ? 0 : kTopSlots;
                const u32       root = (s == 0) ? kGuiRootTop : kGuiRootBot;
                const u32       node = (s == 0) ? kGuiNodeTop : kGuiNodeBot;
                u32             objs[kMaxRect + kMaxSlots];
                int             n = 0;
                int             chars = 0;
                int             nativeChars = 0;
                int             i;

                // ---- 安全弁: 記録リストに収まるところまでで打ち切る ----
                int     rects = st.rectCount;
                int     texts = st.textCount;
                u32     limit = RecordLimit(sc);

                i = 0;
                while (i < texts)
                {
                    chars += CountChars(st.texts[i], kMaxChars);
                    if (st.texts[i].font == FONT_GAME)
                        nativeChars += CountChars(st.texts[i], kMaxChars);
                    i++;
                }
                while (rects > 0
                       && EstimateRecorded(rects, texts, chars, nativeChars) > limit)
                    rects--;
                while (texts > 0
                       && EstimateRecorded(rects, texts, chars, nativeChars) > limit)
                {
                    texts--;
                    chars -= CountChars(st.texts[texts], kMaxChars);
                    if (st.texts[texts].font == FONT_GAME)
                        nativeChars -= CountChars(st.texts[texts], kMaxChars);
                }
                if (rects != st.rectCount || texts != st.textCount)
                    Log("[!] 記録リストの安全弁: 矩形 %d->%d / 文字 %d->%d (上限 %u B)",
                        st.rectCount, rects, st.textCount, texts, (unsigned int)limit);

                // ---- 表示リスト（★描いた順。F-324）----
                //   Picture と文字を FillRect / DrawText を呼んだ順に繋ぐ。
                //   旧版は Picture 全部→文字全部だったので、後開きパネル
                //   （リストボックス等）の背景が先の行文字より後ろになり、
                //   後ろの文字が背景に溶けず突き抜けた。
                //   スロット割り当て（best-fit）は順序と無関係に先に決める。
                //   TextBox の DrawSelf は溜まりを流してから書くので、
                //   混在させても前後関係は保たれる（基準仕様 v2 §2.6）。
                //   ★スロットは不均一なので、**長いものから順に、入る中で
                //     一番小さい空き**へ入れる（best-fit decreasing）。
                //     短い文字列が大きな枠を先に取ると、長い行が入らなくなる。
                {
                    const int   slotCount = MaxTexts(sc);
                    bool        taken[kMaxSlots];
                    int         order[kMaxSlots];
                    int         need[kMaxSlots];
                    int         dropped = 0;

                    std::memset(taken, 0, sizeof(taken));
                    i = 0;
                    while (i < texts)
                    {
                        need[i] = CountChars(st.texts[i], kMaxChars);
                        order[i] = i;
                        st.texts[i].slot = 0xFF;
                        i++;
                    }
                    // 挿入ソート（字数の多い順）。texts は高々 kMaxSlots 本。
                    i = 1;
                    while (i < texts)
                    {
                        const int   key = order[i];
                        int         j = i - 1;

                        while (j >= 0 && need[order[j]] < need[key])
                        {
                            order[j + 1] = order[j];
                            j--;
                        }
                        order[j + 1] = key;
                        i++;
                    }
                    i = 0;
                    while (i < texts)
                    {
                        const int   t = order[i];
                        int         pick = -1;
                        int         j = 0;

                        // 容量表は昇順なので、最初に見つかる空きが最小の枠
                        while (j < slotCount)
                        {
                            if (!taken[j] && FontFitsSlot(slotBase + j, st.texts[t].font)
                                && SlotChars(slotBase + j) >= need[t])
                            {
                                pick = j;
                                break;
                            }
                            j++;
                        }
                        if (pick < 0)
                        {
                            // 入る枠が無い -> 一番大きい空きへ入れて切る
                            j = slotCount - 1;
                            while (j >= 0)
                            {
                                if (!taken[j] && FontFitsSlot(slotBase + j, st.texts[t].font))
                                {
                                    pick = j;
                                    break;
                                }
                                j--;
                            }
                        }
                        if (pick < 0)
                            dropped++;
                        else
                        {
                            taken[pick] = true;
                            st.texts[t].slot = (u8)pick;
                        }
                        i++;
                    }
                    if (dropped)
                        Log("[!] 文字スロットが足りない: %d 本を捨てた（画面 %d）",
                            dropped, s);

                    // 描いた順に繋ぐ。安全弁で切り捨てた分は飛ばす。
                    i = 0;
                    while (i < st.oCount)
                    {
                        if (st.oKind[i] == 0)
                        {
                            const int r = (int)st.oIdx[i];

                            if (r < rects)
                            {
                                UpdatePicture(picBase + r, sc, st.rects[r]);
                                objs[n++] = PicAddr(picBase + r);
                            }
                        }
                        else
                        {
                            const int t = (int)st.oIdx[i];

                            if (t < texts)
                            {
                                const u8 slot = st.texts[t].slot;

                                if (slot != 0xFF
                                    && FillSlot(slotBase + (int)slot, sc,
                                                st.texts[t]) > 0)
                                    objs[n++] = SlotTextBox(slotBase + (int)slot);
                            }
                        }
                        i++;
                    }
                    // 使わなかった Picture は不可視にしておく
                    // （表示リストは全矩形 0..rectCount-1 を1回ずつ含むので、
                    //  繋がった分 = rects 未満と等しい）
                    i = rects;
                    while (i < RectsOf(sc))
                    {
                        W8(PicAddr(picBase + i) + 0xB7, 0x00);      // 使わない分は不可視
                        i++;
                    }
                    // 使わなかったスロットは不可視にしておく
                    i = 0;
                    while (i < slotCount)
                    {
                        if (!taken[i])
                            W8(SlotTextBox(slotBase + i) + 0xB7, 0x00);
                        i++;
                    }
                }

                LinkRing(root, objs, n);

                // TextBox を使うときだけ DrawInfo+0x80 が要る（§3.1 / F-302）
                if (texts > 0)
                {
                    const u32 mgr = R32(kGLayoutMgr);

                    if (mgr != 0)
                        W32(node + 0xB0, mgr + kWorkObjOff);
                }
                W8(node + 0x11D, 1);            // 記録し直させる
                s++;
            }
        }

        // ==================================================================
        // 組み立て（基準仕様 v2 §5.1 の 3〜5 段。1〜2 段は OwnGui.cpp）
        // ==================================================================
        bool    Install(void)
        {
            u32 gpuBase = R32(kGuiHeapSlot);
            u32 cap = R32(kGuiHeapSlot + 4);

            g_err[0] = '\0';
            if (gpuBase == 0 || cap == 0)
            {
                Fail("ヒープを借りられていない");
                return false;
            }
            // ★F-345: 借りたヒープに置くのは**アトラスだけ**。
            //   ほかは全部プラグイン自身の .bss（g_mem）に置く。
            g_base = (u32)g_mem;
            g_gpuBase = gpuBase;
            g_size = cap;

            if (!Layout(gpuBase, cap))
                return false;
            Log("置き場 0x%08X (0x%X)  Picture 0x%X / フォント 0x%X / 文字 0x%X",
                (unsigned int)g_base, (unsigned int)kPlugBytes, (unsigned int)g_offPic,
                (unsigned int)g_offFont, (unsigned int)g_offSlot0);
            Log("使う量 0x%X / 置き場 0x%X (%u%%)", (unsigned int)g_total,
                (unsigned int)kPlugBytes, (unsigned int)(g_total * 100 / kPlugBytes));
            Log("アトラス 0x%08X (+0x%X) / 借用 0x%08X (0x%X)",
                (unsigned int)(gpuBase + g_offAtlas), (unsigned int)g_offAtlas,
                (unsigned int)gpuBase, (unsigned int)cap);

            // ---- アトラス ----
            std::memcpy((void *)(gpuBase + g_offAtlas), kUiAtlas, kUiSheetBytes);
            std::memset(g_mem, 0, g_total);   // ★プラグイン側は毎回ゼロから
            // ★GPU は D-cache を見ない。載せたら必ず吐き出す。
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE,
                                     gpuBase + g_offAtlas, kUiSheetBytes);

            // ---- 共有 Material / フォント資源 / Picture / 文字スロット ----
            //   ★ここで**全部**を有効な状態（不可視）で作る。
            //     Commit は以降 memset しない（生きているオブジェクトの vtable を消さない）。
            WriteSharedMaterial();
            WriteFontResource();
            {
                int i = 0;

                while (i < kMaxRectAll)
                {
                    InitPicture(i);
                    i++;
                }
                i = 0;
                while (i < kSlots)
                {
                    BuildSlot(i, i < kTopSlots ? SCREEN_TOP : SCREEN_BOTTOM);
                    i++;
                }
            }

            // ---- root Pane（.bss）----
            std::memset((void *)kGuiRootTop, 0, kGuiRootLen);
            std::memset((void *)kGuiRootBot, 0, kGuiRootLen);
            PaneCommon(kGuiRootTop, kVtPane, (float)kGuiTopW, (float)kGuiTopH, 0.0f, 0.0f, 0.0f);
            PaneCommon(kGuiRootBot, kVtPane, (float)kGuiBotW, (float)kGuiBotH, 0.0f, 0.0f, 0.0f);

            // ---- ノードを root と DrawInfo に結ぶ ----
            W32(kGuiNodeTop + 0x20, kGuiRootTop);
            W32(kGuiNodeBot + 0x20, kGuiRootBot);
            W32(kGuiNodeTop + 0x30, kVpDrawInfo);
            W32(kGuiNodeBot + 0x30, kVpDrawInfo);

            // ★TextBox を描くときに要る DrawInfo+0x80（= LayoutMgr+0x1C。F-302）。
            //   Commit でも入れているが、そちらは文字があるときだけなので
            //   ここでも入れておく（.bss を消す InitBss はもう走らない順序にしてある）。
            {
                const u32 mgr = R32(kGLayoutMgr);

                if (mgr != 0)
                {
                    W32(kGuiNodeTop + 0xB0, mgr + kWorkObjOff);
                    W32(kGuiNodeBot + 0xB0, mgr + kWorkObjOff);
                    Log("DrawInfo+0x80 = 0x%08X (LayoutMgr+0x%X)",
                        (unsigned int)(mgr + kWorkObjOff), (unsigned int)kWorkObjOff);
                }
                else
                    Log("[!] LayoutMgr が取れない。文字は Commit まで待つ。");
            }

            // ---- 既定 TagProcessor（遅延初期化がまだなら入れる。F-304）----
            if (R32(kTagProcSlot) == 0)
            {
                W32(kTagProcSlot, kTagProcDefault);
                W32(kTagProcGuard, 1);
                Log("既定 TagProcessor を初期化した");
            }

            g_ready = true;
            g_dirty[0] = g_dirty[1] = true;
            Log("GuiRenderer 組み立て完了（矩形 %d 枚 / 文字スロット 上 %d・下 %d）",
                kMaxRectAll, kTopSlots, kBotSlots);
            return true;
        }

        void    Uninstall(void)
        {
            if (!g_ready)
                return;
            // ★§5.2: root Pane を 0 にして描画を止める。**解放はここではしない。**
            //   記録し直させないと前フレームの列が再生され続ける。
            W32(kGuiNodeTop + 0x20, 0);
            W32(kGuiNodeBot + 0x20, 0);
            W8(kGuiNodeTop + 0x11D, 1);
            W8(kGuiNodeBot + 0x11D, 1);
            W32(kGuiNodeTop + 0xB0, 0);
            W32(kGuiNodeBot + 0xB0, 0);
            g_ready = false;
        }

        void    DumpLog(void)
        {
            File    f;

            if (File::Open(f, "/gohan_gui.txt",
                           File::RWC | File::TRUNCATE | File::SYNC) != File::SUCCESS)
                return;
            f.Write(g_log.c_str(), g_log.size());
            f.Close();
        }
    }
}
