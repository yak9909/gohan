// ============================================================================
// GuiMenuDraw — app.js の描画の写し
// ============================================================================
//
// drawMenu / drawInlineList / drawDescriptionPanel / drawHoldProgress /
// drawDialog / drawListbox / drawHotkeyCapture / drawSlider / drawNotice。
//
// ★Simulator との既知の差（描画基盤による）
//   1. 切り抜き（clip）が無い。窓からはみ出す行は描かない。
//   2. 角丸が無い（キーの角は四角い）。
//   3. 枠線は内地と重ねずに描く（F-323。フェード途中で縁だけ濃く残らない）。
//   4. issue-overlay.js（F 印・値の固定の印・説明欄の操作の文字・長押しの灰）は app.js の上に重ね描きする
//      作りだが、ここでは行や欄を描くときに一緒に描く。見た目は同じで、ダイアログや通知の上に印が
//      重なる（重ね描きの副作用）ところだけ写さない。

#include "GuiMenuInternal.hpp"
#include "GuiKeyboard.hpp"
#include "GuiDialog.hpp"
#include "ChatIme.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace CTRPluginFramework
{
    namespace GuiMenu
    {
        namespace detail
        {
            namespace
            {
                const GuiRenderer::Screen TOP = GuiRenderer::SCREEN_TOP;
                const GuiRenderer::Screen BOT = GuiRenderer::SCREEN_BOTTOM;
                const GuiRenderer::Font   FMAIN = GuiRenderer::FONT_MAIN;
                const GuiRenderer::Font   FNUM = GuiRenderer::FONT_NUM;

                char    g_buf[kTextBytes];
                char    g_buf2[kTextBytes];
                char    g_lines[kDescLines][kWrapBytes];
                char    g_noticeLines[8][kWrapBytes];

                // issue-overlay.js の色
                // 行の状態の印（Simulator 639b4e6 docs/menu-state-indicators.md）: 1px x 12px の縦線を上から等分、不透明度 60%
                const u32   kColFavorite  = 0xFF8AC9D6; // #d6c98a お気に入り
                const u32   kColRetained  = 0xFF4D48E5; // #e5484d この項目を保持
                const float kStatusAlpha  = 0.6f;
                const int   kStatusH      = 12;
                const u32   kColValueLock = 0xFFFFC85C; // #5cc8ff（値の固定の線と値）
                const u32   kColHoldMuted = 0xFF767B74; // #747b76（長押し 2/5 未満）
                const u32   kColCtrlBg    = 0xFA0B0D0A; // rgba(10,13,11,.98)（説明欄の操作の文字の下地）

                int     Round(float v) { return (int)std::floor(v + 0.5f); }

                // globalAlpha 相当（元の α に係数を掛ける）
                u32     Fade(u32 col, float a)
                {
                    const u32 src = (col >> 24) & 0xFF;
                    const u32 out = (u32)((float)src * Clamp01(a) + 0.5f);

                    return (col & 0x00FFFFFF) | (out << 24);
                }

                u32     WithAlpha(u32 col, float a)
                {
                    const u32 v = (u32)(Clamp01(a) * 255.0f + 0.5f);

                    return (col & 0x00FFFFFF) | (v << 24);
                }

                float   SelectionPulse(u32 now)
                {
                    const float phase = 6.2831853f * (float)(now % (u32)kPulseMs) / (float)kPulseMs;

                    return 0.48f + 0.07f * std::sin(phase);
                }

                float   SelectionOutlinePulse(u32 now)
                {
                    const float phase = 6.2831853f * (float)(now % (u32)kPulseMs) / (float)kPulseMs;
                    const float v = 0.7f + 0.2f * std::sin(phase);

                    return v < 0.5f ? 0.5f : (v > 0.9f ? 0.9f : v);
                }

                // trimBitmapText の写し。収まらなければ "..." を付けて切る。
                const char *Trim(const char *src, int maxWidth, char *buf, unsigned int cap,
                                 GuiRenderer::Font font = FMAIN)
                {
                    const int   dots = GuiRenderer::MeasureText("...", 1, font);
                    int         i = 0, w = 0, columns = 0;
                    unsigned    out = 0, fit = 0;

                    buf[0] = '\0';
                    while (src[i] != '\0')
                    {
                        const int       start = i;
                        const int       cw = GuiRenderer::NextCharWidth(src, i, 1, font);
                        const unsigned  len = (unsigned)(i - start);

                        if (out + len + 4 >= cap)
                            break;
                        if (w + cw > maxWidth || (font == GuiRenderer::FONT_GAME && columns >= 28))
                        {
                            std::memcpy(buf, src, fit);
                            std::snprintf(buf + fit, cap - fit, "...");
                            return buf;
                        }
                        std::memcpy(buf + out, src + start, len);
                        out += len;
                        columns++;
                        w += cw;
                        buf[out] = '\0';
                        if (w + dots <= maxWidth && (font != GuiRenderer::FONT_GAME || columns <= 25))
                            fit = out;
                    }
                    return buf;
                }

                // wrapBitmapText の写し。行数を返す。
                int     Wrap(const char *src, int maxWidth, int maxLines, char lines[][kWrapBytes])
                {
                    int         i = 0, n = 0, w = 0, chars = 0;
                    unsigned    out = 0;
                    // ★幅で折り返すだけだと、ASCII 混じりの行が文字スロットの最大字数を超えて末尾が切れる
                    //   （gohan.md の説明「[穴に落下しない] [どこでも掘れる] …」が 29 字）。字数でも折り返す。
                    const int   maxChars = GuiRenderer::MaxChars(GuiRenderer::SCREEN_TOP);

                    if (maxLines <= 0)
                        return 0;
                    lines[0][0] = '\0';
                    while (src[i] != '\0')
                    {
                        const int       start = i;
                        const int       cw = GuiRenderer::NextCharWidth(src, i);
                        const unsigned  len = (unsigned)(i - start);

                        if (out > 0 && (w + cw > maxWidth || chars >= maxChars))
                        {
                            lines[n][out] = '\0';
                            n++;
                            if (n >= maxLines)
                                return n;
                            out = 0;
                            w = 0;
                            chars = 0;
                            lines[n][0] = '\0';
                        }
                        if (out + len + 1 >= (unsigned)kWrapBytes)
                            continue;
                        std::memcpy(lines[n] + out, src + start, len);
                        out += len;
                        w += cw;
                        chars++;
                        lines[n][out] = '\0';
                    }
                    return out > 0 ? n + 1 : n;
                }

                // 1px の枠（縁 4 辺と内地を重ねない。F-323）
                void    Frame1px(GuiRenderer::Screen sc, int x, int y, int w, int h, u32 line, u32 fill)
                {
                    if (w <= 2 || h <= 2)
                    {
                        GuiRenderer::FillRect(sc, x, y, w, h, line);
                        return;
                    }
                    GuiRenderer::FillRect(sc, x, y, w, 1, line);
                    GuiRenderer::FillRect(sc, x, y + h - 1, w, 1, line);
                    GuiRenderer::FillRect(sc, x, y + 1, 1, h - 2, line);
                    GuiRenderer::FillRect(sc, x + w - 1, y + 1, 1, h - 2, line);
                    GuiRenderer::FillRect(sc, x + 1, y + 1, w - 2, h - 2, fill);
                }

                void    Outline(GuiRenderer::Screen sc, int x, int y, int w, int h, u32 col)
                {
                    GuiRenderer::FillRect(sc, x, y, w, 1, col);
                    GuiRenderer::FillRect(sc, x, y + h - 1, w, 1, col);
                    GuiRenderer::FillRect(sc, x, y + 1, 1, h - 2, col);
                    GuiRenderer::FillRect(sc, x + w - 1, y + 1, 1, h - 2, col);
                }

                // drawScrollBar の写し
                void    ScrollBar(GuiRenderer::Screen sc, int x, int y, int h, int visibleRows,
                                  int count, float scroll, float alpha)
                {
                    if (count <= visibleRows)
                        return;

                    int         thumbH = h * visibleRows / count;
                    const int   maxScroll = count - visibleRows;

                    if (thumbH < 10)
                        thumbH = 10;

                    const int thumbY = y + Round((float)(h - thumbH) * Clamp01(scroll / (float)maxScroll));

                    GuiRenderer::FillRect(sc, x, y, 2, h, Fade(kColBarTrack, alpha));
                    GuiRenderer::FillRect(sc, x, thumbY, 2, thumbH, Fade(kColBarThumb, alpha));
                }

                // 1/10 単位の値を toFixed(1) の形にする
                const char *Tenths(s32 v, char *buf, unsigned cap)
                {
                    const s32 a = v < 0 ? -v : v;

                    std::snprintf(buf, cap, "%s%d.%d", v < 0 ? "-" : "", (int)(a / 10), (int)(a % 10));
                    return buf;
                }

                // formatBound の写し
                const char *FormatBound(s32 v, u8 fmt, char *buf, unsigned cap)
                {
                    if (fmt == FMT_HEX)
                        std::snprintf(buf, cap, "0x%X", (unsigned)v);
                    else if (fmt == FMT_FLOAT)
                        Tenths(v, buf, cap);
                    else
                        std::snprintf(buf, cap, "%d", (int)v);
                    return buf;
                }

                const char *ItemIcon(const Item &it)
                {
                    if (it.type == ITEM_FOLDER)
                        return ">";
                    if (it.type == ITEM_CHECKBOX)
                        return it.value ? "[X]" : "[ ]";
                    if (it.type == ITEM_TOGGLE_ACTION || it.type == ITEM_ACTION)
                        return "A";
                    if (IsLinked(it))
                        return "S";
                    if (it.type == ITEM_LIST)
                        return "L";
                    return "V";
                }

                u32     ItemIconColor(const Item &it, u32 fallback)
                {
                    if (it.type == ITEM_FOLDER)
                        return kColAccent;
                    if (IsLinked(it))
                        return kColLinked;
                    return fallback;
                }

                // formatValue の写し
                const char *FormatValue(const Item &it, u32 now, char *buf, unsigned cap)
                {
                    buf[0] = '\0';
                    if (it.type == ITEM_CHECKBOX)
                        std::snprintf(buf, cap, "%s", it.value ? "ON" : "OFF");
                    else if (it.type == ITEM_TOGGLE_ACTION)
                    {
                        if (it.feedbackOn && (s32)(it.feedbackUntil - now) > 0)
                            std::snprintf(buf, cap, "OK");
                        else
                            std::snprintf(buf, cap, "%s", it.value ? "ON" : "OFF");
                    }
                    else if (it.type == ITEM_LIST || it.type == ITEM_LINKED_LIST)
                    {
                        if (it.options != nullptr && it.value >= 0 && it.value < (s32)it.optionCount)
                            std::snprintf(buf, cap, "%s", it.options[it.value]);
                    }
                    else if (it.fmt == FMT_HEX)
                        std::snprintf(buf, cap, "0x%04X", (unsigned)it.value);
                    else if (it.fmt == FMT_FLOAT)
                        Tenths(it.value, buf, cap);
                    else if (IsNumericItem(it))
                        std::snprintf(buf, cap, "%d", (int)it.value);
                    return buf;
                }

                // drawItemIcon。[X] は "[ ]" の上に X だけ別色で重ねる
                // （空白と X はどちらも送り 4px で、空白は画素を持たないので絵は同じ）。
                void    DrawItemIcon(const Item &it, int x, int y, u32 color)
                {
                    if (it.type == ITEM_CHECKBOX && it.value)
                    {
                        GuiRenderer::DrawText(TOP, x, y, "[ ]", color);
                        GuiRenderer::DrawText(TOP, x + GuiRenderer::MeasureText("["), y, "X", kColCheckMark);
                        return;
                    }
                    GuiRenderer::DrawText(TOP, x, y, ItemIcon(it), ItemIconColor(it, color));
                }

                // issue-overlay.js の操作の文字（説明欄の 3 行目。app.js の FAVORITE / START:FAVORITES を上書きする）
                const char *ControlText(char *buf, unsigned cap)
                {
                    int settings = -1;

                    for (int d = 0; d < g_depth; d++)
                        if (g_frames[d].kind == FR_SETTINGS)
                        {
                            settings = d;
                            break;
                        }
                    // Simulator 639b4e6: キーの案内は消した（フッターにある）。値の固定の状態だけ残す。設定画面は空
                    (void)settings;
                    buf[0] = '\0';
                    if (Cur().kind != FR_SETTINGS)
                    {
                        const Item &sel = Sel();

                        if (!IsSettingsItem(sel) && IsFixed(ItemIndex(sel)))
                            std::snprintf(buf, cap, u8"値を固定:ON");
                    }
                    return buf;
                }

                void    DrawDescription(float amount)
                {
                    const Item &it = Sel();
                    const int   x = 168, y = 8, w = 224;
                    const int   n = Wrap(it.desc, w - 16, kDescLines, g_lines);
                    const int   h = 46 + n * 11;

                    Frame1px(TOP, x, y, w, h, Fade(kColPanelEdge, amount), Fade(kColBoxBg, amount));
                    GuiRenderer::DrawText(TOP, x + 8, y + 7, Trim(it.label, w - 16, g_buf, sizeof(g_buf)),
                                          Fade(kColAccent, amount));
                    std::snprintf(g_buf2, sizeof(g_buf2), "HOTKEY:%s",
                                  it.type == ITEM_FOLDER ? "--" : FormatHotkey(it.hotkey, g_buf, sizeof(g_buf)));
                    GuiRenderer::DrawText(TOP, x + 8, y + 18, g_buf2, Fade(kColHotkey, amount));
                    if (IsLinked(it))
                    {
                        const int bw = GuiRenderer::MeasureText("SYNC");

                        GuiRenderer::DrawText(TOP, x + w - 8 - bw, y + 18, "SYNC", Fade(kColLinked, amount));
                    }
                    GuiRenderer::FillRect(TOP, 174, 35, 211, 11, Fade(kColCtrlBg, amount));
                    if (ControlText(g_buf, sizeof(g_buf))[0] != '\0')
                        GuiRenderer::DrawText(TOP, 176, 37, g_buf, Fade(kColHint, amount));
                    // 無効な項目の説明も読める色のまま（Simulator 639b4e6）
                    for (int i = 0; i < n; i++)
                        GuiRenderer::DrawText(TOP, x + 8, y + 42 + i * 11, g_lines[i], Fade(kColBody, amount));
                }

                void    DrawHoldProgress(int menuX, u32 now)
                {
                    float       progress;
                    const char *label;

                    if (!HoldProgress(now, progress, label))
                        return;

                    const int   x = menuX + 5, y = 201, w = kMenuW - 12;
                    // 2/5 未満は進んだ部分と枠を灰色（issue-overlay.js）。2/5 以降は黄色のまま、二色に分けない
                    const u32   col = HoldMuted(now) ? kColHoldMuted : kColDirty;

                    Frame1px(TOP, x, y, w, 14, col, kColHoldBg);
                    GuiRenderer::DrawText(TOP, x + 5, y + 2, label, kColWhite);
                    GuiRenderer::FillRect(TOP, x + 5, y + 10, w - 10, 2, kColHoldTrack);
                    GuiRenderer::FillRect(TOP, x + 5, y + 10, Round((float)(w - 10) * progress), 2, col);
                }

                void    DrawInlineList(int menuX, float start, u32 now)
                {
                    const Item &it = g_items[g_inline.item];
                    const float amount = AnimAmount(g_inline.anim, now);

                    if (amount <= 0.0f)
                        return;

                    const int   row = (int)((float)Cur().selection - start);
                    const int   rows = it.optionCount < kInlineRows ? (int)it.optionCount : kInlineRows;
                    const int   h = rows * 14 + 6;
                    int         y = 28 + row * kItemH + 14;
                    const int   x = Round(Mix((float)(menuX + kMenuW), (float)(menuX + 21), amount));
                    const float scroll = ListScroll(g_inline, now);
                    int         first = (int)scroll;
                    int         last = first + rows;

                    if (y > 212 - h)
                        y = 212 - h;
                    Frame1px(TOP, x, y, 132, h, Fade(kColBorder, amount), Fade(kColBoxBg, amount));
                    if (first < 0)
                        first = 0;
                    if (last > (int)it.optionCount - 1)
                        last = (int)it.optionCount - 1;
                    for (int i = first; i <= last; i++)
                    {
                        const int rowY = Round((float)(y + 3) + ((float)i - scroll) * 14.0f);

                        if (rowY < y + 2 || rowY + 12 > y + h - 2)
                            continue;
                        if (i == g_inline.index)
                            GuiRenderer::FillRect(TOP, x + 3, rowY, 122, 12, Fade(kColListSel, amount));
                        GuiRenderer::DrawText(TOP, x + 8, rowY + 2, Trim(it.options[i], 114, g_buf, sizeof(g_buf)),
                                              Fade(i == g_inline.index ? kColWhite : kColText, amount));
                    }
                    ScrollBar(TOP, x + 128, y + 3, h - 6, rows, (int)it.optionCount, scroll, amount);
                }

                // dialogHorizontalPosition の写し
                int     DialogX(float amount, float menuAmount)
                {
                    const float centeredX = (float)(400 - 224) / 2.0f;
                    const float menuOpenX = 168.0f;
                    const float targetX = g_dialog.type == DLG_TOGGLE_ACTION
                                          ? Mix(centeredX, menuOpenX, menuAmount) : menuOpenX;

                    return Round(Mix(targetX + 24.0f, targetX, amount));
                }

                void    DrawDialog(u32 now)
                {
                    const float amount = AnimAmount(g_dialog.anim, now);

                    if (amount <= 0.0f)
                        return;

                    static const char *const kCloseOpts[] = { u8"適用", u8"キャンセル" };
                    static const char *const kRevertOpts[] = { u8"戻す", u8"キャンセル" };
                    static const char *const kRunOpts[] = { u8"実行", u8"キャンセル" };
                    const int   w = 224, h = 90;
                    const int   x = DialogX(amount, OpenAmount(now));
                    const int   y = 75;
                    const char *title = u8"変更を適用しますか？";
                    const char *const *opts = kCloseOpts;

                    if (g_dialog.type == DLG_APPLY_ALL)
                        title = u8"全ての変更を適用しますか？";
                    else if (g_dialog.type == DLG_REVERT_ALL)
                    {
                        title = u8"全ての変更を戻しますか？";
                        opts = kRevertOpts;
                    }
                    else if (g_dialog.type == DLG_TOGGLE_ACTION)
                    {
                        std::snprintf(g_buf2, sizeof(g_buf2), u8"%sを実行しますか？",
                                      g_dialog.item >= 0 ? g_items[g_dialog.item].label : "");
                        title = g_buf2;
                        opts = kRunOpts;
                    }
                    Frame1px(TOP, x, y, w, h, Fade(kColDirty, amount), Fade(kColDlgBg, amount));
                    GuiRenderer::DrawText(TOP, x + 10, y + 10, title, Fade(kColWhite, amount));
                    for (int i = 0; i < 2; i++)
                    {
                        const int rowY = y + 33 + i * 23;

                        if (i == g_dialog.index)
                            Frame1px(TOP, x + 8, rowY - 5, w - 16, 19, Fade(kColDirty, amount),
                                     Fade(kColDlgSel, amount));
                        GuiRenderer::DrawText(TOP, x + 18, rowY, opts[i],
                                              Fade(i == g_dialog.index ? kColWhite : kColText, amount));
                    }
                }

                // OK だけのダイアログ（GuiDialog）。見た目は DrawDialog と同じ部品: 1px の縁・暗い地・選択の帯。
                //   幅 kMsgW、本文は幅で折り返す。縁と題は error なら赤（kColDanger）、そうでなければ金（kColDirty）。
                const int   kMsgW = 300, kMsgPad = 12, kMsgLine = 13, kMsgTitleH = 26, kMsgBtnW = 80, kMsgBtnH = 19;
                char        g_msgLines[GuiDialog::kMaxLines][kWrapBytes];
                int         g_msgLineCount;

                void    DrawMessageDialog(u32 now)
                {
                    const float amount = AnimAmount(g_message.anim, now);

                    if (amount <= 0.0f)
                        return;
                    g_msgLineCount = Wrap(g_message.body, kMsgW - 2 * kMsgPad, (int)GuiDialog::kMaxLines, g_msgLines);

                    const int   h = kMsgTitleH + g_msgLineCount * kMsgLine + 10 + kMsgBtnH + 10;
                    const int   x = (400 - kMsgW) / 2;
                    const int   y = (240 - h) / 2 + Round((1.0f - amount) * 12.0f);
                    const u32   edge = g_message.error ? kColDanger : kColDirty;

                    Frame1px(TOP, x, y, kMsgW, h, Fade(edge, amount), Fade(kColDlgBg, amount));
                    GuiRenderer::DrawText(TOP, x + 10, y + 10,
                                          Trim(g_message.title, kMsgW - 20, g_buf, sizeof(g_buf)),
                                          Fade(g_message.error ? kColDanger : kColWhite, amount));
                    for (int i = 0; i < g_msgLineCount; i++)
                        GuiRenderer::DrawText(TOP, x + kMsgPad, y + kMsgTitleH + i * kMsgLine, g_msgLines[i],
                                              Fade(kColWhite, amount));

                    const int   bx = x + (kMsgW - kMsgBtnW) / 2;
                    const int   by = y + h - 10 - kMsgBtnH;
                    const int   tw = GuiRenderer::MeasureText("OK");

                    Frame1px(TOP, bx, by, kMsgBtnW, kMsgBtnH, Fade(kColDirty, amount), Fade(kColDlgSel, amount));
                    GuiRenderer::DrawText(TOP, bx + (kMsgBtnW - tw) / 2, by + 5, "OK", Fade(kColWhite, amount));
                }

                // drawListbox の写し。上下共用。
                void    DrawScreenListbox(GuiRenderer::Screen sc, int baseX, int width, int offscreenX, u32 now)
                {
                    const float amount = AnimAmount(g_overlay.anim, now);

                    if (amount <= 0.0f)
                        return;

                    const int   x = Round(Mix((float)offscreenX, (float)baseX, amount));
                    const int   count = g_overlay.optionCount;
                    const int   rows = count < kScreenRows ? count : kScreenRows;
                    const GuiRenderer::Font rowFont = g_overlay.mode == OV_CHAT_KANJI
                                                      ? GuiRenderer::FONT_GAME : FMAIN;
                    const int   h = 25 + rows * 20;
                    const int   y = (240 - h + 1) / 2;
                    const float scroll = ListScroll(g_overlay, now);
                    int         first = (int)scroll;
                    int         last = first + rows;

                    if (first < 0)
                        first = 0;
                    if (last > count - 1)
                        last = count - 1;
                    Frame1px(sc, x, y, width, h, Fade(kColAccent, amount), Fade(kColBoxBg, amount));
                    GuiRenderer::DrawText(sc, x + 8, y + 7, Trim(g_overlay.title, width - 20, g_buf, sizeof(g_buf)),
                                          Fade(kColAccent, amount));
                    for (int i = first; i <= last; i++)
                    {
                        const int rowY = Round((float)(y + 23) + ((float)i - scroll) * 20.0f);

                        if (rowY < y + 20 || rowY + 8 > y + h - 2)
                            continue;
                        if (i == g_overlay.index)
                            GuiRenderer::FillRect(sc, x + 5, rowY - 4, width - 14, 16, Fade(kColListSel, amount));
                        GuiRenderer::DrawText(sc, x + 12, rowY,
                                              Trim(g_overlay.options[i], width - 24, g_buf, sizeof(g_buf), rowFont),
                                              Fade(i == g_overlay.index ? kColWhite : kColText, amount), 1, rowFont);
                    }
                    ScrollBar(sc, x + width - 6, y + 24, h - 29, rows, count, scroll, amount);
                }

                // drawKeyboardKey（入力待ちの 2 ボタン用。選択・無効・ラッチ無し）
                void    DrawPlainKey(int x, int y, int w, int h, const char *label, float a)
                {
                    const int tw = GuiRenderer::MeasureText(label);

                    Frame1px(BOT, x, y, w, h, Fade(kColKeyLine, a), Fade(kColKeyBg, a));
                    GuiRenderer::DrawText(BOT, x + Round((float)(w - tw) / 2.0f), y + Round((float)(h - 8) / 2.0f),
                                          label, Fade(kColKeyTxt, a));
                }

                void    DrawHotkeyCapture(u32 now)
                {
                    const float a = AnimAmount(g_capture.anim, now);

                    if (a <= 0.0f)
                        return;

                    const int   w = 224, h = 86;
                    const int   x = Round((float)(320 - w) / 2.0f);
                    const int   y = Round((float)(240 - h) / 2.0f);
                    const char *status = g_capture.phase == CAP_ARMING ? u8"ボタンを全て離す"
                                       : g_capture.phase == CAP_WAITING ? u8"ボタン入力待ち"
                                       : u8"入力中";
                    int         bx, by, bw, bh;

                    Frame1px(BOT, x, y, w, h, Fade(kColAccent, a), Fade(kColBoxBg, a));
                    GuiRenderer::DrawText(BOT, x + 8, y + 8, "HOTKEY INPUT", Fade(kColAccent, a));
                    GuiRenderer::DrawText(BOT, x + 8, y + 24, status, Fade(kColWhite, a));
                    GuiRenderer::DrawText(BOT, x + 8, y + 37,
                                          g_capture.captured != 0
                                          ? FormatHotkey(g_capture.captured, g_buf, sizeof(g_buf)) : "--",
                                          Fade(kColHotkey, a));
                    GuiRenderer::DrawText(BOT, x + 96, y + 37, u8"全て離すと決定", Fade(kColHint, a));
                    CaptureButtonRect(0, bx, by, bw, bh);
                    DrawPlainKey(bx, by, bw, bh, u8"無効", a);
                    CaptureButtonRect(1, bx, by, bw, bh);
                    DrawPlainKey(bx, by, bw, bh, u8"取消", a);
                }

                void    DrawSlider(u32 now)
                {
                    const float a = AnimAmount(g_slider.anim, now);

                    if (a <= 0.0f)
                        return;

                    const Item &it = g_items[g_slider.item];
                    const int   x = SliderTrackX(), y = SliderTrackY(), w = SliderTrackW();
                    const float progress = it.maximum > it.minimum
                                           ? (float)(g_slider.value - it.minimum) / (float)(it.maximum - it.minimum)
                                           : 0.0f;

                    GuiRenderer::DrawText(BOT, 12, 12, u8"数値スライダー", Fade(kColAccent, a));
                    GuiRenderer::DrawText(BOT, 12, 30, it.label, Fade(kColWhite, a));
                    FormatBound(g_slider.value, it.fmt, g_buf, sizeof(g_buf));
                    GuiRenderer::DrawText(BOT, 308 - GuiRenderer::MeasureText(g_buf, 1, FNUM), 30, g_buf,
                                          Fade(kColDirty, a), 1, FNUM);
                    GuiRenderer::FillRect(BOT, x, y, w, 4, Fade(kColSliderTrack, a));
                    GuiRenderer::FillRect(BOT, x, y, Round((float)w * progress), 4, Fade(kColSliderFill, a));
                    GuiRenderer::FillRect(BOT, Round((float)x + (float)w * progress) - 3, y - 5, 7, 14,
                                          Fade(kColSliderKnob, a));
                    FormatBound(it.minimum, it.fmt, g_buf, sizeof(g_buf));
                    GuiRenderer::DrawText(BOT, x, 122, g_buf, Fade(kColBound, a), 1, FNUM);
                    FormatBound(it.maximum, it.fmt, g_buf, sizeof(g_buf));
                    GuiRenderer::DrawText(BOT, x + w - GuiRenderer::MeasureText(g_buf, 1, FNUM), 122, g_buf,
                                          Fade(kColBound, a), 1, FNUM);
                    GuiRenderer::DrawText(BOT, 127, 122, "STEP:", Fade(kColBound, a));
                    // String(entry.step): 小数は必要なときだけ
                    if (it.fmt == FMT_FLOAT && it.step % 10 != 0)
                        Tenths(it.step, g_buf, sizeof(g_buf));
                    else
                        std::snprintf(g_buf, sizeof(g_buf), "%d", (int)(it.fmt == FMT_FLOAT ? it.step / 10 : it.step));
                    GuiRenderer::DrawText(BOT, 127 + GuiRenderer::MeasureText("STEP:"), 122, g_buf,
                                          Fade(kColBound, a), 1, FNUM);
                    GuiRenderer::DrawText(BOT, 72, 196, u8"左右:変更  A:決定  B:取消", Fade(kColText, a));
                }

                void    DrawNotices(u32 now)
                {
                    for (int i = 0; i < kNoticeMax; i++)
                    {
                        const Notice &nt = g_notices[i];

                        if (!NoticeAlive(nt, now) || (s32)(now - nt.createdAt) < 0)
                            continue;

                        const int x = Round(NoticeX(nt, now));
                        const int y = Round(NoticeY(nt, now));
                        const int h = nt.height > 0 ? nt.height : kNoticeH;
                        int       row = 0;

                        Frame1px(TOP, x, y, kNoticeW, h, nt.red ? kColDangerEdge : kColPanelEdge, kColNoticeBg);
                        GuiRenderer::FillRect(TOP, x + 1, y + 1, 2, h - 2, nt.red ? kColDanger : kColAccent);
                        // drawBrowserNotices: 題は幅 - 20、本文は幅 - 14 で折り、10px ずつ下げる
                        for (int n = WrapNotice(nt.title, kNoticeW - 20, g_noticeLines, 8), k = 0; k < n; k++)
                            GuiRenderer::DrawText(TOP, x + 7, y + 4 + row++ * kNoticeLineH, g_noticeLines[k], kColWhite);
                        for (int n = WrapNotice(nt.msg, kNoticeW - 14, g_noticeLines, 8), k = 0; k < n; k++)
                            GuiRenderer::DrawText(TOP, x + 7, y + 4 + row++ * kNoticeLineH, g_noticeLines[k], kColNoticeMsg);
                    }
                }
            }

            int     SliderTrackX(void) { return 20; }
            int     SliderTrackY(void) { return 103; }
            int     SliderTrackW(void) { return 280; }

            void    CaptureButtonRect(int which, int &x, int &y, int &w, int &h)
            {
                const int px = Round((float)(320 - 224) / 2.0f);
                const int py = Round((float)(240 - 86) / 2.0f);

                x = px + (which == 0 ? 8 : 116);
                y = py + 56;
                w = 100;
                h = 21;
            }

            // drawMenu の写し
            void    BuildTop(u32 now)
            {
                GuiRenderer::Begin(TOP);
                if (!g_visible)
                {
                    if (g_overlay.active && g_overlay.screen == 0)
                        DrawScreenListbox(TOP, Round(Mix(98.0f, 178.0f, 0.0f)), 204, 400, now);
                    if (g_dialog.type != DLG_NONE)
                        DrawDialog(now);
                    if (g_message.active)
                        DrawMessageDialog(now);
                    DrawNotices(now);
                    return;
                }

                const float amount = OpenAmount(now);
                const int   menuX = Round(-(float)kMenuW + (float)kMenuW * amount);
                const Frame &fr = Cur();
                const float start = ViewportStart(now);
                const float actOff = ActBounce(now);
                const int   changed = DirtyCount();

                GuiRenderer::FillRect(TOP, menuX, 0, kMenuW, 240, kColPanel);
                GuiRenderer::FillRect(TOP, menuX + kMenuW - 2, 0, 2, 240, kColBorder);
                GuiRenderer::FillRect(TOP, menuX, 0, kMenuW - 2, 23, kColHeader);
                GuiRenderer::DrawText(TOP, menuX + 7, 7, "CHEAT MENU", kColWhite);
                GuiRenderer::DrawText(TOP, menuX + 91, 7, Trim(fr.title, 62, g_buf, sizeof(g_buf)), kColCrumb);

                // 選択帯
                {
                    const float animRow = Clamp01((SelectionPosition(now) - start) / (float)(kVisibleRows - 1))
                                          * (float)(kVisibleRows - 1);
                    const int   hy = Round(28.0f + animRow * (float)kItemH + EdgeBounce(now));
                    const int   hx = menuX + 4 + Round(ValueBounce(now) + actOff);

                    GuiRenderer::FillRect(TOP, hx, hy - 3, kMenuW - 10, 15, WithAlpha(kColSelFill, SelectionPulse(now)));
                    Outline(TOP, hx, hy - 3, kMenuW - 10, 15, WithAlpha(kColSelLine, SelectionOutlinePulse(now)));
                }

                // 行（切り抜きが無いので窓に入る行だけ描く）
                {
                    int first = (int)start;
                    int last = first + kVisibleRows;

                    if (first < 0)
                        first = 0;
                    if (last > fr.count - 1)
                        last = fr.count - 1;
                    for (int i = first; i <= last; i++)
                    {
                        const Item &it = FrameItem(fr, i);
                        const bool  real = !IsSettingsItem(it);
                        const int   idx = real ? ItemIndex(it) : -1;
                        const bool  fixed = real && IsFixed(idx);
                        const int   y = Round(28.0f + ((float)i - start) * (float)kItemH);
                        const bool  selected = i == fr.selection;
                        const int   off = selected ? Round(actOff) : 0;
                        const u32   col = it.disabled ? kColDisabled
                                        : IsDirtyItem(it) ? kColDirty
                                        : selected ? kColWhite : kColText;

                        if (y < 25 || y > 204)
                            continue;
                        // 状態の印（お気に入り・この項目を保持・ホットキーの順に上から等分。値の固定は線を足さない）
                        {
                            u32 status[3];
                            int ns = 0;

                            if (real && IsFavorite(idx))
                                status[ns++] = kColFavorite;
                            if (real && IsItemRetained(idx))
                                status[ns++] = kColRetained;
                            if (it.type != ITEM_FOLDER && it.hotkey != 0)
                                status[ns++] = kColHotkey;
                            for (int s = 0; s < ns; s++)
                            {
                                const int top = kStatusH * s / ns, bottom = kStatusH * (s + 1) / ns;

                                GuiRenderer::FillRect(TOP, menuX + 4 + off, y - 2 + top, 1, bottom - top,
                                                      WithAlpha(status[s], kStatusAlpha));
                            }
                        }
                        DrawItemIcon(it, menuX + 8 + off, y, col);
                        FormatValue(it, now, g_buf2, sizeof(g_buf2));

                        const GuiRenderer::Font vf = IsNumericItem(it) ? FNUM : FMAIN;
                        const int   vw = g_buf2[0] != '\0' ? GuiRenderer::MeasureText(g_buf2, 1, vf) : 0;

                        GuiRenderer::DrawText(TOP, menuX + 27 + off, y,
                                              Trim(it.label, kMenuW - 33 - vw, g_buf, sizeof(g_buf)), col);
                        if (g_buf2[0] != '\0')
                            GuiRenderer::DrawText(TOP, menuX + kMenuW - 8 - vw + off, y, g_buf2,
                                                  fixed ? kColValueLock : col, 1, vf);
                    }
                }
                ScrollBar(TOP, menuX + 154, 28, 176, kVisibleRows, fr.count, start, 1.0f);

                GuiRenderer::FillRect(TOP, menuX, 216, kMenuW - 2, 24, kColFoot);
                // フッター（Simulator 639b4e6）: 字ごとに位置を固定する（変更数が 2 桁でも 2 行目がずれない）
                {
                    const int ax = menuX + 6, xx = menuX + 30, yx = menuX + 54, sx = menuX + 102;

                    GuiRenderer::DrawText(TOP, ax, 220, u8"A決定", kColFootTxt);
                    GuiRenderer::DrawText(TOP, xx, 220, u8"X適用", kColFootTxt);
                    GuiRenderer::DrawText(TOP, yx, 220, u8"Yホットキー", kColFootTxt);
                    GuiRenderer::DrawText(TOP, sx, 220, u8"START設定", kColFootTxt);
                    std::snprintf(g_buf, sizeof(g_buf), u8"%d変更", changed);
                    GuiRenderer::DrawText(TOP, menuX + 6, 230, g_buf, changed ? kColDirty : kColFootOff);
                    GuiRenderer::DrawText(TOP, yx, 230, u8"B戻る", kColFootTxt);
                    GuiRenderer::DrawText(TOP, sx, 230, u8"L戻し", kColFootTxt);
                    GuiRenderer::DrawText(TOP, sx + GuiRenderer::MeasureText(u8"START設定") + GuiRenderer::MeasureText(" "),
                                          230, u8"R星", kColFootTxt);
                }
                DrawHoldProgress(menuX, now);

                if (g_inline.active)
                    DrawInlineList(menuX, start, now);
                DrawDescription(amount);
                if (g_overlay.active && g_overlay.screen == 0)
                    DrawScreenListbox(TOP, Round(Mix(98.0f, 178.0f, amount)), 204, 400, now);
                if (g_dialog.type != DLG_NONE)
                    DrawDialog(now);
                if (g_message.active)
                    DrawMessageDialog(now);
                DrawNotices(now);
            }

            void    BuildBottom(u32 now)
            {
                GuiRenderer::Begin(BOT);
                // 暗幕は下画面 UI の出現量に合わせて 1 か所だけで描く（F-350 / gohan issue #2 #3）
                {
                    u32     dimColor;
                    float   dimAmount;

                    if (BottomDim(now, dimColor, dimAmount))
                        GuiRenderer::DrawBottomDim(dimColor, dimAmount);
                }
                if (g_overlay.active && g_overlay.screen == 1)
                    DrawScreenListbox(BOT, 52, 216, 320, now);
                else if (g_capture.active)
                    DrawHotkeyCapture(now);
                else if (g_slider.active)
                    DrawSlider(now);
                else if (GuiKeyboard::Active())
                    GuiKeyboard::Draw(now);
                else
                    ChatIme::DrawBar();         // 漢字変換の候補欄（下画面にほかの UI が無いときだけ。Simulator と同じ）
            }
        }
    }
}
