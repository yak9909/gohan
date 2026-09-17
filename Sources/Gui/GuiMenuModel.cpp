// ============================================================================
// GuiMenuModel — ui-model.js の CheatMenuModel の写し（状態遷移）
// ============================================================================
//
// 関数名・分岐の順序は ui-model.js に合わせてある（handle / update /
// commitItem / activateHotkeyItem など）。差分試験は tools/patches/verify_menu_port.py。
//
// ★Simulator との意図した差
//   1. 上画面のタッチ（項目のタップ）は無い。3DS の上画面はタッチできない。
//   2. チャット漢字候補は実機の変換を呼ぶ（Simulator は案内の通知だけ）。
//   3. 通知の同時表示は 7 件だが、文字列は固定長（題 31 / 本文 95 バイト）で切る。

#include "GuiMenuInternal.hpp"
#include "GuiKeyboard.hpp"
#include "ChatKanji.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace CTRPluginFramework
{
    namespace GuiMenu
    {
        namespace detail
        {
            Item        g_items[kMaxItems];
            int         g_itemCount = 0;
            int         g_rootFirst = 0;
            int         g_rootCount = 0;
            Frame       g_frames[kMaxDepth];
            int         g_depth = 1;
            Behavior    g_behavior[kMaxItems];
            Listbox     g_inline;
            Listbox     g_overlay;
            Dialog      g_dialog;
            Capture     g_capture;
            Slider      g_slider;
            Hold        g_hold;
            Notice      g_notices[kNoticeMax];
            bool        g_visible = false;
            float       g_openFrom = 0.0f, g_openTarget = 0.0f;
            u32         g_openStart = 0;
            bool        g_needFinal = false;
            const char *g_longOpts[kLongList];
            void      (*g_noticeHook)(const char *title, const char *msg) = nullptr;
            ToggleHandlers g_toggleHdl;

            namespace
            {
                // ---- 選択とアニメーション（CheatMenuModel のフィールド）----
                float   g_selFrom = 0.0f, g_selTo = 0.0f;
                u32     g_selStart = 0;
                float   g_viewFrom = 0.0f, g_viewTarget = 0.0f;
                u32     g_viewStart = 0;
                int     g_valueDir = 0;
                u32     g_valueAt = 0;
                bool    g_valueOn = false;
                u32     g_actAt = 0;
                bool    g_actOn = false;
                int     g_edgeDir = 0;
                u32     g_edgeAt = 0;
                bool    g_edgeOn = false;
                int     g_edgeBlocked = 0;

                // ---- 入力（heldControls / activeHotkeyItems / ControlRepeater）----
                u16     g_held = 0;             // heldControls
                u16     g_prevHeld = 0;
                bool    g_prevTouch = false;
                bool    g_hotActive[kMaxItems]; // activeHotkeyItems
                u32     g_repAt[HB_COUNT];      // ControlRepeater の nextAt（十字のみ使う）

                // ---- 通知（NotificationTimeline）----
                int     g_noticeNextId = 1;
                bool    g_noticeEver = false;
                u32     g_noticeLastAt = 0;
                bool    g_noticesAlive = false;

                // ---- 直前の有効状態（ToggleHandlers の遷移検出）----
                bool    g_toggleState[kMaxItems];

                char    g_msg[128];
                // 最後に観測した効果（関数内定義）の ON/OFF。変化したときだけ通知する。
                bool    g_effectSeen[kMaxItems];
                bool    g_kbHandled = false;    // このフレームで GuiKeyboard::Handle を呼んだか

                bool    IsDir(int bit)
                {
                    return bit == HB_UP || bit == HB_DOWN || bit == HB_LEFT || bit == HB_RIGHT;
                }
            }

            // ================================================================
            // 小物
            // ================================================================
            float   Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
            int     ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

            float   EaseOutCubic(float t)
            {
                const float u = 1.0f - Clamp01(t);

                return 1.0f - u * u * u;
            }

            float   Mix(float a, float b, float t) { return a + (b - a) * t; }

            float   Progress(u32 start, u32 now, int ms)
            {
                if (ms <= 0)
                    return 1.0f;
                if (now <= start)
                    return 0.0f;
                return Clamp01((float)(now - start) / (float)ms);
            }

            float   AnimAmount(const Anim &a, u32 now)
            {
                if (a.ms <= 0)
                    return a.target;
                return Mix(a.from, a.target, EaseOutCubic(Progress(a.start, now, a.ms)));
            }

            static void AnimOpen(Anim &a, u32 now, int ms)
            {
                a.from = 0.0f;
                a.target = 1.0f;
                a.start = now;
                a.ms = ms;
                a.closing = false;
            }

            // closeListbox / closeBottomOverlay / closeDialogAnimation の共通部
            static bool AnimClose(Anim &a, u32 now, int ms)
            {
                if (a.closing)
                    return false;

                const float cur = AnimAmount(a, now);

                a.from = cur;
                a.target = 0.0f;
                a.start = now;
                a.ms = (int)((float)ms * cur);
                a.closing = true;
                return true;
            }

            static bool AnimExitComplete(const Anim &a, u32 now)
            {
                return a.closing && (s32)(now - a.start) >= a.ms;
            }

            float   ScrollTarget(int index, int count, int rows)
            {
                const int   hi = count - rows > 0 ? count - rows : 0;

                return (float)ClampI(index - rows / 2, 0, hi);
            }

            const char *FormatHotkey(u16 mask, char *buf, unsigned int cap)
            {
                static const char *const kLabels[HB_COUNT] = {
                    "ZL", "L", "R", "ZR", "UP", "DOWN", "LEFT", "RIGHT",
                    "A", "B", "X", "Y", "SELECT", "START"
                };
                unsigned int out = 0;
                bool         first = true;

                if (cap == 0)
                    return buf;
                if (mask == 0)
                {
                    std::snprintf(buf, cap, "%s", u8"なし");
                    return buf;
                }
                buf[0] = '\0';
                for (int i = 0; i < HB_COUNT; i++)
                {
                    if ((mask & (u16)(1u << i)) == 0)
                        continue;

                    const unsigned int n = (unsigned int)std::strlen(kLabels[i]);

                    if (!first && out + 1 < cap)
                        buf[out++] = '+';
                    if (out + n < cap)
                    {
                        std::memcpy(buf + out, kLabels[i], n);
                        out += n;
                    }
                    first = false;
                }
                buf[out < cap ? out : cap - 1] = '\0';
                return buf;
            }

            // ================================================================
            // 項目の問い合わせ
            // ================================================================
            int     ItemIndex(const Item &it) { return (int)(&it - &g_items[0]); }

            bool    IsLinked(const Item &it)
            {
                return it.type == ITEM_LINKED_VALUE || it.type == ITEM_LINKED_LIST;
            }

            bool    IsNumericItem(const Item &it)
            {
                return it.type == ITEM_VALUE || it.type == ITEM_SLIDER
                       || it.type == ITEM_LINKED_VALUE;
            }

            // Object.hasOwn(entry, "value")
            bool    HasValue(const Item &it)
            {
                return it.type != ITEM_FOLDER && it.type != ITEM_ACTION;
            }

            bool    IsDirtyItem(const Item &it)
            {
                if (it.type == ITEM_FOLDER)
                    return false;
                return (HasValue(it) && it.value != it.applied)
                       || it.hotkey != it.appliedHotkey;
            }

            // walkItems の順（深さ優先。根の並び → フォルダの直後に子）
            template <typename F>
            static void WalkRange(int first, int count, F &fn)
            {
                for (int i = 0; i < count; i++)
                {
                    Item &it = g_items[first + i];

                    fn(it);
                    if (it.type == ITEM_FOLDER)
                        WalkRange((int)it.childFirst, (int)it.childCount, fn);
                }
            }

            template <typename F>
            static void WalkItems(F &fn)
            {
                WalkRange(g_rootFirst, g_rootCount, fn);
            }

            int     DirtyCount(void)
            {
                int n = 0;

                for (int i = 0; i < g_itemCount; i++)
                    if (IsDirtyItem(g_items[i]))
                        n++;
                return n;
            }

            bool    EffectActive(int index)
            {
                const Behavior &b = g_behavior[index];

                return b.IsActive != nullptr && b.IsActive(index);
            }

            static bool HasEffect(int index)
            {
                return g_behavior[index].IsActive != nullptr
                       && g_behavior[index].SetActive != nullptr;
            }

            Frame  &Cur(void)   { return g_frames[g_depth - 1]; }
            Item   &Sel(void)   { return g_items[Cur().first + Cur().selection]; }

            // ================================================================
            // アニメーション（CheatMenuModel と同じ式）
            // ================================================================
            float   OpenAmount(u32 now)
            {
                return Mix(g_openFrom, g_openTarget,
                           EaseOutCubic(Progress(g_openStart, now, kEnterMs)));
            }

            float   SelectionPosition(u32 now)
            {
                return Mix(g_selFrom, g_selTo,
                           EaseOutCubic(Progress(g_selStart, now, kSelMoveMs)));
            }

            float   ViewportStart(u32 now)
            {
                return Mix(g_viewFrom, g_viewTarget,
                           EaseOutCubic(Progress(g_viewStart, now, kScrollMs)));
            }

            static float Bounce(bool on, u32 at, u32 now, int ms, float split)
            {
                if (!on || now < at || now - at >= (u32)ms)
                    return 0.0f;

                const float p = (float)(now - at) / (float)ms;

                return p < split ? EaseOutCubic(p / split)
                                 : 1.0f - EaseOutCubic((p - split) / (1.0f - split));
            }

            float   ValueBounce(u32 now)
            {
                return (float)g_valueDir * 4.0f
                       * Bounce(g_valueOn, g_valueAt, now, kValueBounceMs, 0.28f);
            }

            float   ActBounce(u32 now)
            {
                return 3.0f * Bounce(g_actOn, g_actAt, now, kActBounceMs, 0.30f);
            }

            float   EdgeBounce(u32 now)
            {
                return (float)g_edgeDir * 4.0f
                       * Bounce(g_edgeOn, g_edgeAt, now, kSelMoveMs, 0.28f);
            }

            float   ListScroll(const Listbox &l, u32 now)
            {
                return Mix(l.scrollFrom, l.scrollTarget,
                           EaseOutCubic(Progress(l.scrollStart, now, kListScrollMs)));
            }

            bool    OverlayActive(void)
            {
                return g_overlay.active || g_capture.active || g_slider.active
                       || GuiKeyboard::Active();
            }

            bool    Animating(u32 now)
            {
                if (g_openTarget != OpenAmount(now))
                    return true;
                if (SelectionPosition(now) != g_selTo || ViewportStart(now) != g_viewTarget)
                    return true;
                if (ValueBounce(now) != 0.0f || ActBounce(now) != 0.0f
                    || EdgeBounce(now) != 0.0f)
                    return true;
                if (g_inline.active || g_dialog.type != DLG_NONE || g_hold.active)
                    return true;
                if (OverlayActive())
                    return true;
                for (int i = 0; i < g_itemCount; i++)
                    if (g_items[i].feedbackOn)
                        return true;
                for (int i = 0; i < kNoticeMax; i++)
                    if (NoticeAlive(g_notices[i], now))
                        return true;
                return false;
            }

            // ================================================================
            // 通知（NotificationTimeline）
            // ================================================================
            float   NoticeX(const Notice &nt, u32 now)
            {
                const float targetX = (float)(400 - kNoticeMargin - kNoticeW);
                const s32   age = (s32)(now - nt.createdAt);

                if (age < 0)
                    return 400.0f;
                if (age < kNoticeEnterMs)
                    return Mix(400.0f, targetX, EaseOutCubic((float)age / (float)kNoticeEnterMs));
                if (age < kNoticeEnterMs + kNoticeHoldMs)
                    return targetX;
                return Mix(targetX, 400.0f,
                           EaseOutCubic((float)(age - kNoticeEnterMs - kNoticeHoldMs)
                                        / (float)kNoticeExitMs));
            }

            float   NoticeY(const Notice &nt, u32 now)
            {
                return Mix(nt.moveFromY, nt.targetY,
                           EaseOutCubic(Progress(nt.moveStartedAt, now, kNoticeEnterMs)));
            }

            bool    NoticeAlive(const Notice &nt, u32 now)
            {
                return nt.active
                       && (s32)(now - nt.createdAt)
                          < kNoticeEnterMs + kNoticeHoldMs + kNoticeExitMs;
            }

            void    AddNotice(const char *title, const char *msg, u32 now, bool red)
            {
                u32 at = now;

                if (g_noticeHook != nullptr)
                    g_noticeHook(title, msg);
                for (int i = 0; i < kNoticeMax; i++)
                    if (!NoticeAlive(g_notices[i], now))
                        g_notices[i].active = false;
                if (g_noticeEver && (s32)(g_noticeLastAt + (u32)kNoticeSpawnMs - at) > 0)
                    at = g_noticeLastAt + (u32)kNoticeSpawnMs;
                g_noticeEver = true;
                g_noticeLastAt = at;

                // 生きている順（古い順）に前へ詰め、満杯なら一番古いものを押し出す
                {
                    int n = 0;

                    for (int i = 0; i < kNoticeMax; i++)
                        if (g_notices[i].active)
                            g_notices[n++] = g_notices[i];
                    for (int i = n; i < kNoticeMax; i++)
                        g_notices[i].active = false;
                    if (n >= kNoticeMax)
                    {
                        for (int i = 1; i < kNoticeMax; i++)
                            g_notices[i - 1] = g_notices[i];
                        g_notices[kNoticeMax - 1].active = false;
                    }
                }
                for (int i = 0; i < kNoticeMax; i++)
                {
                    Notice &ex = g_notices[i];

                    if (!ex.active)
                        continue;
                    ex.moveFromY = NoticeY(ex, at);
                    ex.targetY -= (float)(kNoticeH + kNoticeGap);
                    ex.moveStartedAt = at;
                }
                for (int i = 0; i < kNoticeMax; i++)
                {
                    Notice &nt = g_notices[i];

                    if (nt.active)
                        continue;
                    nt.active = true;
                    nt.id = g_noticeNextId++;
                    std::snprintf(nt.title, sizeof(nt.title), "%s", title);
                    std::snprintf(nt.msg, sizeof(nt.msg), "%s", msg);
                    nt.red = red;
                    nt.createdAt = at;
                    nt.moveFromY = (float)(240 - kNoticeMargin - kNoticeH);
                    nt.targetY = nt.moveFromY;
                    nt.moveStartedAt = at;
                    break;
                }
                if (g_noticeNextId > 999)
                    g_noticeNextId = 1;
            }

            // ================================================================
            // 選択（moveSelection / normalizeSelection / resetSelectionAnimation）
            // ================================================================
            static void NormalizeSelection(void)
            {
                Frame &fr = Cur();

                if (!g_items[fr.first + fr.selection].disabled)
                    return;
                for (int i = 0; i < fr.count; i++)
                {
                    if (!g_items[fr.first + i].disabled)
                    {
                        fr.selection = i;
                        return;
                    }
                }
            }

            static void ResetSelectionAnimation(u32 now)
            {
                NormalizeSelection();

                const Frame &fr = Cur();

                g_selFrom = (float)fr.selection;
                g_selTo = (float)fr.selection;
                g_selStart = now;
                g_viewFrom = ScrollTarget(fr.selection, fr.count, kVisibleRows);
                g_viewTarget = g_viewFrom;
                g_viewStart = now;
                g_edgeBlocked = 0;
            }

            static bool MoveSelection(int dir, u32 now, bool allowWrap)
            {
                Frame      &fr = Cur();
                const int   previous = fr.selection;
                int         next = previous;

                for (int attempt = 0; attempt < fr.count; attempt++)
                {
                    int candidate = next + dir;

                    if (!allowWrap && (candidate < 0 || candidate >= fr.count))
                    {
                        if (g_edgeBlocked != dir)
                        {
                            g_edgeDir = dir;
                            g_edgeAt = now;
                            g_edgeOn = true;
                            g_edgeBlocked = dir;
                        }
                        return false;
                    }
                    candidate = (candidate + fr.count) % fr.count;
                    next = candidate;
                    if (!g_items[fr.first + next].disabled)
                        break;
                }
                if (next == previous || g_items[fr.first + next].disabled)
                    return false;
                g_edgeBlocked = 0;
                g_viewFrom = ViewportStart(now);
                g_viewTarget = ScrollTarget(next, fr.count, kVisibleRows);
                g_viewStart = now;
                g_selFrom = SelectionPosition(now);
                g_selTo = (float)next;
                g_selStart = now;
                fr.selection = next;
                return true;
            }

            static void AnimateTo(float target, u32 now)
            {
                g_openFrom = OpenAmount(now);
                g_openTarget = target;
                g_openStart = now;
                if (target == 1.0f)
                    g_visible = true;
            }

            // ================================================================
            // リスト（createListboxAnimation / moveListboxSelection）
            // ================================================================
            static void OpenList(Listbox &l, int screen, const char *title,
                                 const char *const *options, int count, int mode,
                                 int item, int selected, int rows, u32 now)
            {
                const float s = ScrollTarget(selected, count, rows);

                l.active = true;
                l.screen = (u8)screen;
                l.mode = (u8)mode;
                l.item = item;
                l.title = title;
                l.options = options;
                l.optionCount = count;
                l.index = ClampI(selected, 0, count - 1);
                AnimOpen(l.anim, now, kListAnimMs);
                l.scrollFrom = s;
                l.scrollTarget = s;
                l.scrollStart = now;
            }

            static void MoveList(Listbox &l, int dir, u32 now, int rows, bool allowWrap)
            {
                const int   count = l.optionCount;
                const int   next = l.index + dir;

                if (!allowWrap && (next < 0 || next >= count))
                    return;
                l.scrollFrom = ListScroll(l, now);
                l.index = (next + count) % count;
                l.scrollTarget = ScrollTarget(l.index, count, rows);
                l.scrollStart = now;
            }

            static void CloseList(Listbox &l, u32 now)
            {
                if (!l.active || l.anim.closing)
                    return;
                if (&l == &g_overlay && l.mode == OV_CHAT_KANJI)
                    ChatKanji::Dismiss();
                AnimClose(l.anim, now, kListAnimMs);
            }

            // ================================================================
            // 効果と適用（setCheckboxEffect / commitItem / applyAll / discard）
            // ================================================================
            static void Execute(Item &it)
            {
                const int idx = ItemIndex(it);

                if (g_behavior[idx].Execute != nullptr)
                    g_behavior[idx].Execute(idx);
            }

            static bool SetCheckboxEffect(Item &it, bool active)
            {
                const int idx = ItemIndex(it);

                if (it.type != ITEM_CHECKBOX || !HasEffect(idx))
                    return false;
                if (EffectActive(idx) == active)
                    return false;
                g_behavior[idx].SetActive(idx, active);
                return true;
            }

            // 値の反映（execute）。連動型はゲームへ書き、ほかは Apply を呼ぶ。
            static void ExecuteValue(Item &it)
            {
                const int       idx = ItemIndex(it);
                const Behavior &b = g_behavior[idx];

                if (IsLinked(it))
                {
                    if (b.LinkedWrite != nullptr)
                        b.LinkedWrite(idx, it.applied);
                }
                else if (b.Apply != nullptr)
                    b.Apply(idx, it.applied);
            }

            static void ExecuteToggleAction(Item &it, u32 now)
            {
                Execute(it);
                it.value = 0;
                it.applied = 0;
                it.feedbackOn = true;
                it.feedbackUntil = now + (u32)kFeedbackMs;
                std::snprintf(g_msg, sizeof(g_msg), u8"%sを実行しました", it.label);
                AddNotice("ACTION", g_msg, now);
            }

            static bool CommitHotkeyValue(Item &it, s32 value)
            {
                const bool changed = it.applied != value;

                it.value = value;
                it.applied = value;
                if (changed)
                    ExecuteValue(it);
                return changed;
            }

            static bool CommitItem(Item &it, u32 now)
            {
                if (!IsDirtyItem(it))
                    return false;

                const bool  valueChanged = HasValue(it) && it.value != it.applied;

                if (HasValue(it))
                    it.applied = it.value;
                it.appliedHotkey = it.hotkey;

                if (it.type == ITEM_TOGGLE_ACTION && valueChanged)
                {
                    if (it.applied != 0)
                        ExecuteToggleAction(it, now);
                }
                else if (it.type == ITEM_CHECKBOX && valueChanged)
                {
                    // ON 適用: 束縛なしなら効果 ON、束縛ありならアームのみ。
                    // OFF 適用: 必ず効果 OFF（gohan-menu.md §4.3）。
                    // ★項目の ON/OFF の適用では通知しない。通知は効果の変化から出す（PollEffects）。
                    if (HasEffect(ItemIndex(it)))
                        SetCheckboxEffect(it, it.applied != 0 && it.appliedHotkey == 0);
                }
                else if (valueChanged
                         && (it.type == ITEM_VALUE || it.type == ITEM_SLIDER
                             || it.type == ITEM_LIST || IsLinked(it)))
                    ExecuteValue(it);
                // ホットキー設定だけの適用では効果に触れない。
                return true;
            }

            static void ApplyAll(u32 now)
            {
                struct { u32 now; void operator()(Item &it) { CommitItem(it, now); } } fn = { now };

                WalkItems(fn);
            }

            static bool DiscardItem(Item &it)
            {
                if (!IsDirtyItem(it))
                    return false;
                if (HasValue(it))
                    it.value = it.applied;
                it.hotkey = it.appliedHotkey;
                return true;
            }

            static void DiscardAll(void)
            {
                struct { void operator()(Item &it) { DiscardItem(it); } } fn;

                WalkItems(fn);
            }

            // ================================================================
            // ダイアログ
            // ================================================================
            static bool OpenDialog(u8 type, u32 now, int item = -1)
            {
                if (g_dialog.type != DLG_NONE)
                    return false;
                g_dialog.type = type;
                g_dialog.item = item;
                g_dialog.index = 0;
                g_dialog.confirmed = false;
                AnimOpen(g_dialog.anim, now, kDialogMs);
                return true;
            }

            static bool ChooseDialog(bool confirm, u32 now)
            {
                if (g_dialog.type == DLG_NONE || g_dialog.anim.closing)
                    return false;
                g_dialog.confirmed = confirm;
                return AnimClose(g_dialog.anim, now, kDialogMs);
            }

            // ================================================================
            // 長押し（holdAction）
            // ================================================================
            bool    HoldProgress(u32 now, float &progress, const char *&label)
            {
                if (!g_hold.active)
                    return false;
                progress = Clamp01((float)(now - g_hold.start) / (float)kHoldMs);
                label = g_hold.bit == HB_X ? u8"X 全項目適用" : u8"L 全項目戻し";
                return true;
            }

            static bool BeginHoldAction(int bit, u32 now)
            {
                if ((bit != HB_X && bit != HB_L) || DirtyCount() == 0)
                    return false;
                g_hold.active = true;
                g_hold.bit = bit;
                g_hold.start = now;
                return true;
            }

            static bool TriggerLongHold(int bit, u32 now)
            {
                g_hold.active = false;
                if (bit == HB_X)
                    return OpenDialog(DLG_APPLY_ALL, now);
                if (bit == HB_L)
                    return OpenDialog(DLG_REVERT_ALL, now);
                return false;
            }

            static bool UpdateHoldAction(u32 now)
            {
                if (!g_hold.active || now - g_hold.start < (u32)kHoldMs)
                    return false;
                return TriggerLongHold(g_hold.bit, now);
            }

            static bool FinishHoldAction(int bit, u32 now)
            {
                if (!g_hold.active || g_hold.bit != bit)
                    return false;

                const u32 elapsed = now - g_hold.start;

                g_hold.active = false;
                if (elapsed >= (u32)kHoldMs)
                    return TriggerLongHold(bit, now);
                if (bit == HB_X)
                    CommitItem(Sel(), now);
                else if (bit == HB_L)
                    DiscardItem(Sel());
                return true;
            }

            // ================================================================
            // 下画面の入力 UI（openNumeric / openSlider / openTextKeyboard）
            // ================================================================
            static void CloseOtherOverlays(void)
            {
                // ui-model.js は overlay を 1 つだけ持ち、開くと差し替える。
                g_overlay.active = false;
                g_capture.active = false;
                g_slider.active = false;
            }

            static void OpenNumeric(Item &it, bool applyOnConfirm, u32 now)
            {
                const GuiKeyboard::Format fmt = it.fmt == FMT_FLOAT ? GuiKeyboard::FLOAT_TENTHS
                    : it.fmt == FMT_HEX ? GuiKeyboard::HEXADECIMAL : GuiKeyboard::DECIMAL;

                CloseOtherOverlays();
                GuiKeyboard::OpenNumber(ItemIndex(it), it.value, it.minimum, it.maximum,
                                        fmt, applyOnConfirm, now);
            }

            static void OpenSlider(Item &it, bool applyOnConfirm, u32 now)
            {
                CloseOtherOverlays();
                g_slider.active = true;
                g_slider.item = ItemIndex(it);
                g_slider.value = it.value;
                g_slider.applyOnConfirm = applyOnConfirm;
                AnimOpen(g_slider.anim, now, kBottomAnimMs);
            }

            static void OpenTextKeyboard(bool compact, u32 now)
            {
                CloseOtherOverlays();
                GuiKeyboard::OpenText(compact, now);
            }

            static void OpenListbox(int screen, const char *title, const char *const *options,
                                    int count, int mode, int item, int selected, u32 now)
            {
                CloseOtherOverlays();
                OpenList(g_overlay, screen, title, options, count, mode, item, selected,
                         kScreenRows, now);
            }

            static void RunAction(Item &it, u32 now)
            {
                const int self = ItemIndex(it);

                Execute(it);
                if (it.action == ACT_SAVE)
                    AddNotice("ACTION", u8"セーブ関数を呼び出しました", now);
                else if (it.action == ACT_TEXT)
                    OpenTextKeyboard(false, now);
                else if (it.action == ACT_COMPACT_TEXT)
                    OpenTextKeyboard(true, now);
                else if (it.action == ACT_TOP_LIST)
                    OpenListbox(0, u8"上画面リスト", g_longOpts, kLongList, OV_GENERIC, -1, 0, now);
                else if (it.action == ACT_BOTTOM_LIST)
                    OpenListbox(1, u8"下画面リスト", g_longOpts, kLongList, OV_GENERIC, -1, 0, now);
                else if (it.action == ACT_CHAT_KANJI)
                {
                    const ChatKanji::RequestResult result = ChatKanji::Request();

                    if (result == ChatKanji::REQUEST_OK)
                    {
                        static const char *const loading[] = { u8"変換中です。Bで閉じる" };

                        OpenListbox(1, u8"漢字候補", loading, 1, OV_CHAT_KANJI, self, 0, now);
                    }
                    else
                    {
                        const char *message = result == ChatKanji::REQUEST_BUSY ? u8"変換処理中です。"
                            : result == ChatKanji::REQUEST_EMPTY ? u8"チャットに文字を入力してください。"
                            : result == ChatKanji::REQUEST_NO_CHAT ? u8"普通のチャットを開いてください。"
                            : result == ChatKanji::REQUEST_BAD_INPUT ? u8"入力文字を確認してください。"
                            : result == ChatKanji::REQUEST_NO_FONT ? u8"フォントを取得できません。"
                            : result == ChatKanji::REQUEST_NO_THREAD ? u8"変換を開始できません。"
                            : u8"対応していないゲームの版です。";

                        AddNotice(it.label, message, now, true);
                    }
                }
            }

            // ================================================================
            // 選択項目の決定とホットキー
            // ================================================================
            static bool ActivateSelected(u32 now)
            {
                Item &it = Sel();

                if (it.disabled)
                    return false;
                g_actAt = now;
                g_actOn = true;
                if (it.type == ITEM_FOLDER)
                {
                    if (g_depth >= kMaxDepth)
                        return false;
                    g_frames[g_depth].title = it.label;
                    g_frames[g_depth].first = (int)it.childFirst;
                    g_frames[g_depth].count = (int)it.childCount;
                    g_frames[g_depth].selection = 0;
                    g_depth++;
                    ResetSelectionAnimation(now);
                }
                else if (it.type == ITEM_CHECKBOX || it.type == ITEM_TOGGLE_ACTION)
                    it.value = it.value ? 0 : 1;
                else if (it.type == ITEM_ACTION)
                    RunAction(it, now);
                else if (it.type == ITEM_LIST || it.type == ITEM_LINKED_LIST)
                    OpenList(g_inline, 0, it.label, it.options, (int)it.optionCount,
                             OV_GENERIC, ItemIndex(it), it.value, kInlineRows, now);
                else if (it.type == ITEM_VALUE || it.type == ITEM_LINKED_VALUE)
                    OpenNumeric(it, false, now);
                else if (it.type == ITEM_SLIDER)
                    OpenSlider(it, false, now);
                return true;
            }

            static bool ActivateHotkeyItem(Item &it, u32 now)
            {
                const int idx = ItemIndex(it);

                if (it.disabled)
                    return false;
                if (it.type == ITEM_CHECKBOX)
                {
                    // ホットキーでは項目の applied を反転しない。反転するのは効果だけで、
                    // 項目 OFF なら発火自体を無視する。
                    if (it.applied == 0 || !HasEffect(idx))
                        return false;
                    return SetCheckboxEffect(it, !EffectActive(idx));
                }
                if (it.type == ITEM_TOGGLE_ACTION)
                    return OpenDialog(DLG_TOGGLE_ACTION, now, idx);
                if (it.type == ITEM_ACTION)
                {
                    RunAction(it, now);
                    return true;
                }
                if (it.type == ITEM_LIST || it.type == ITEM_LINKED_LIST)
                {
                    OpenListbox(0, it.label, it.options, (int)it.optionCount,
                                OV_HOTKEY_ITEM, idx, it.value, now);
                    return true;
                }
                if (it.type == ITEM_VALUE || it.type == ITEM_LINKED_VALUE)
                {
                    OpenNumeric(it, true, now);
                    return true;
                }
                if (it.type == ITEM_SLIDER)
                {
                    OpenSlider(it, true, now);
                    return true;
                }
                return false;
            }

            static void ReleaseInactiveHotkeys(void)
            {
                for (int i = 0; i < g_itemCount; i++)
                    if (g_hotActive[i]
                        && !(g_items[i].appliedHotkey != 0 && g_items[i].appliedHotkey == g_held))
                        g_hotActive[i] = false;
            }

            static bool TriggerAppliedHotkeys(u32 now)
            {
                struct Fn
                {
                    u32  now;
                    bool triggered;
                    void operator()(Item &it)
                    {
                        const int idx = ItemIndex(it);

                        if (it.type == ITEM_FOLDER || g_hotActive[idx]
                            || it.appliedHotkey == 0 || it.appliedHotkey != g_held)
                            return;
                        if (!ActivateHotkeyItem(it, now))
                            return;
                        g_hotActive[idx] = true;
                        triggered = true;
                    }
                } fn = { now, false };

                WalkItems(fn);
                return fn.triggered;
            }

            // ================================================================
            // ホットキー入力待ち（openHotkeyPicker / handleHotkeyCapture）
            // ================================================================
            static void OpenHotkeyPicker(u32 now)
            {
                Item &it = Sel();

                if (it.type == ITEM_FOLDER || it.disabled)
                    return;
                CloseOtherOverlays();
                g_capture.active = true;
                g_capture.phase = CAP_ARMING;
                g_capture.item = ItemIndex(it);
                g_capture.captured = 0;
                AnimOpen(g_capture.anim, now, kBottomAnimMs);
            }

            static void HandleHotkeyCapture(int bit, bool pressed, bool repeated, u32 now)
            {
                if (!g_capture.active || g_capture.anim.closing)
                    return;
                if (g_capture.phase == CAP_ARMING)
                {
                    if (g_held == 0)
                        g_capture.phase = CAP_WAITING;
                    return;
                }
                if (pressed && !repeated)
                {
                    g_capture.phase = CAP_RECORDING;
                    g_capture.captured = (u16)(g_capture.captured | (u16)(1u << bit));
                    return;
                }
                if (!pressed && g_capture.phase == CAP_RECORDING && g_held == 0)
                {
                    g_items[g_capture.item].hotkey = g_capture.captured;
                    AnimClose(g_capture.anim, now, kBottomAnimMs);
                }
            }

            // ================================================================
            // 下画面 UI への入力（handleOverlay）
            // ================================================================
            static void TakeKeyboardResult(u32 now)
            {
                GuiKeyboard::Result result;

                if (!GuiKeyboard::TakeResult(result))
                    return;
                if (result.kind == GuiKeyboard::NUMBER && result.item >= 0
                    && result.item < g_itemCount)
                {
                    Item &it = g_items[result.item];

                    if (result.apply)
                        CommitHotkeyValue(it, result.value);
                    else
                        it.value = result.value;
                }
                else if (result.kind == GuiKeyboard::TEXT)
                    AddNotice("ACTION", u8"名前変更を呼び出しました", now);
            }

            static void KeyboardKey(int bit, const Input &in, u32 now)
            {
                const int dx = bit == HB_RIGHT ? 1 : bit == HB_LEFT ? -1 : 0;
                const int dy = bit == HB_DOWN ? 1 : bit == HB_UP ? -1 : 0;

                if (dx == 0 && dy == 0 && bit != HB_A && bit != HB_B)
                    return;
                GuiKeyboard::Handle(dx, dy, bit == HB_A, bit == HB_B,
                                    in.touch, in.tx, in.ty, now);
                g_kbHandled = true;
                TakeKeyboardResult(now);
            }

            static void CloseSlider(u32 now)
            {
                AnimClose(g_slider.anim, now, kBottomAnimMs);
            }

            static void HandleOverlay(int bit, u32 now, bool repeated, const Input &in)
            {
                if (g_overlay.active)
                {
                    if (g_overlay.anim.closing)
                        return;
                    if (bit == HB_UP)
                        MoveList(g_overlay, -1, now, kScreenRows, !repeated);
                    else if (bit == HB_DOWN)
                        MoveList(g_overlay, 1, now, kScreenRows, !repeated);
                    else if (bit == HB_B)
                        CloseList(g_overlay, now);
                    else if (bit == HB_A)
                    {
                        if (g_overlay.mode == OV_CHAT_KANJI)
                            CloseList(g_overlay, now);
                        else
                        {
                            if (g_overlay.mode == OV_HOTKEY_ITEM && g_overlay.item >= 0)
                                CommitHotkeyValue(g_items[g_overlay.item], g_overlay.index);
                            std::snprintf(g_msg, sizeof(g_msg), u8"%sを選択しました",
                                          g_overlay.options[g_overlay.index]);
                            AddNotice("SELECTED", g_msg, now);
                            CloseList(g_overlay, now);
                        }
                    }
                    return;
                }
                if (g_slider.active)
                {
                    if (g_slider.anim.closing)
                        return;

                    const Item &it = g_items[g_slider.item];

                    if (bit == HB_LEFT)
                        g_slider.value = ClampI(g_slider.value - it.step, it.minimum, it.maximum);
                    else if (bit == HB_RIGHT)
                        g_slider.value = ClampI(g_slider.value + it.step, it.minimum, it.maximum);
                    else if (bit == HB_UP)
                        g_slider.value = ClampI(g_slider.value + it.step * 5, it.minimum, it.maximum);
                    else if (bit == HB_DOWN)
                        g_slider.value = ClampI(g_slider.value - it.step * 5, it.minimum, it.maximum);
                    else if (bit == HB_A)
                    {
                        if (g_slider.applyOnConfirm)
                            CommitHotkeyValue(g_items[g_slider.item], g_slider.value);
                        else
                            g_items[g_slider.item].value = g_slider.value;
                        CloseSlider(now);
                    }
                    else if (bit == HB_B)
                        CloseSlider(now);
                    return;
                }
                if (GuiKeyboard::Active())
                    KeyboardKey(bit, in, now);
            }

            static void HandleInlineList(int bit, u32 now, bool repeated)
            {
                if (g_inline.anim.closing)
                    return;
                if (bit == HB_UP)
                    MoveList(g_inline, -1, now, kInlineRows, !repeated);
                else if (bit == HB_DOWN)
                    MoveList(g_inline, 1, now, kInlineRows, !repeated);
                else if (bit == HB_A)
                {
                    g_items[g_inline.item].value = g_inline.index;
                    CloseList(g_inline, now);
                }
                else if (bit == HB_B)
                    CloseList(g_inline, now);
            }

            static void ChangeValue(Item &it, int dir, u32 now)
            {
                if (it.disabled)
                    return;
                it.value = ClampI(it.value + dir * it.step, it.minimum, it.maximum);
                g_valueDir = dir;
                g_valueAt = now;
                g_valueOn = true;
            }

            // ================================================================
            // handle(key, now, pressed, repeated)
            // ================================================================
            static void HandleKey(int bit, u32 now, bool pressed, bool repeated, const Input &in)
            {
                // 退場アニメーション中のメニューは閉じたものとして扱う（gohan issue #1）
                const bool  menuOpen = g_visible && g_openTarget != 0.0f;

                if (g_capture.active)
                {
                    HandleHotkeyCapture(bit, pressed, repeated, now);
                    return;
                }
                ReleaseInactiveHotkeys();
                if (g_dialog.type != DLG_NONE)
                {
                    if (!pressed || g_dialog.anim.closing)
                        return;
                    if (bit == HB_UP)
                        g_dialog.index = (g_dialog.index + 1) % 2;
                    else if (bit == HB_DOWN)
                        g_dialog.index = (g_dialog.index + 1) % 2;
                    else if (bit == HB_A)
                        ChooseDialog(g_dialog.index == 0, now);
                    else if (bit == HB_B)
                        ChooseDialog(false, now);
                    return;
                }
                if (!menuOpen && !OverlayActive() && !g_inline.active && pressed && !repeated
                    && TriggerAppliedHotkeys(now))
                    return;
                if (!pressed)
                {
                    if ((bit == HB_X || bit == HB_L) && FinishHoldAction(bit, now))
                        return;
                    if (bit == HB_UP || bit == HB_DOWN)
                        g_edgeBlocked = 0;
                    return;
                }
                if (OverlayActive())
                {
                    if (repeated && !g_overlay.active)
                        return;
                    HandleOverlay(bit, now, repeated, in);
                    return;
                }
                if (!menuOpen)
                    return;
                if (g_inline.active)
                {
                    HandleInlineList(bit, now, repeated);
                    return;
                }
                if (bit == HB_UP)
                    MoveSelection(-1, now, !repeated);
                else if (bit == HB_DOWN)
                    MoveSelection(1, now, !repeated);
                else if (bit == HB_A)
                    ActivateSelected(now);
                else if (bit == HB_B)
                {
                    if (g_depth > 1)
                    {
                        g_depth--;
                        ResetSelectionAnimation(now);
                    }
                    else
                        CloseMenu(now);
                }
                else if ((bit == HB_X || bit == HB_L) && !repeated)
                    BeginHoldAction(bit, now);
                else if (bit == HB_Y)
                    OpenHotkeyPicker(now);
                else if ((bit == HB_LEFT || bit == HB_RIGHT) && IsNumericItem(Sel()))
                    ChangeValue(Sel(), bit == HB_RIGHT ? 1 : -1, now);
            }

            // 下画面のタッチ（app.js の bottomCanvas pointerdown。押した瞬間だけ）
            static void HandleTouchDown(const Input &in, u32 now)
            {
                if (g_capture.active && !g_capture.anim.closing)
                {
                    for (int which = 0; which < 2; which++)
                    {
                        int x, y, w, h;

                        CaptureButtonRect(which, x, y, w, h);
                        if (in.tx < x || in.tx > x + w || in.ty < y || in.ty > y + h)
                            continue;
                        if (which == 0)
                            g_items[g_capture.item].hotkey = 0;
                        AnimClose(g_capture.anim, now, kBottomAnimMs);
                        return;
                    }
                    return;
                }
                if (g_slider.active && !g_slider.anim.closing)
                {
                    const int   x = SliderTrackX(), w = SliderTrackW();
                    const int   y = SliderTrackY() - 12;
                    const Item &it = g_items[g_slider.item];

                    if (in.tx < x || in.tx > x + w || in.ty < y || in.ty > y + 30)
                        return;

                    const float raw = (float)it.minimum
                                      + Clamp01((float)(in.tx - x) / (float)w)
                                        * (float)(it.maximum - it.minimum);

                    g_slider.value = (s32)std::floor(raw / (float)it.step + 0.5f) * it.step;
                }
            }

            // ================================================================
            // open / close / beginClose / syncLinkedItems
            // ================================================================
            static void SyncLinkedItems(void)
            {
                for (int i = 0; i < g_itemCount; i++)
                {
                    Item &it = g_items[i];

                    if (!IsLinked(it))
                        continue;

                    s32  v = 0;
                    const bool ok = g_behavior[i].LinkedRead != nullptr
                                    && g_behavior[i].LinkedRead(i, &v);

                    // 読取失敗時は誤値を編集させないため選択不可にする
                    if (!ok || (it.type == ITEM_LINKED_LIST && (v < 0 || v >= (s32)it.optionCount)))
                    {
                        it.disabled = true;
                        continue;
                    }
                    it.disabled = false;
                    it.value = v;
                    it.applied = v;
                }
            }

            void    OpenMenu(u32 now)
            {
                // ホットキーで開いたトグル型アクションの確認はメニューを開いても残す
                if (g_dialog.type != DLG_TOGGLE_ACTION)
                    g_dialog.type = DLG_NONE;
                g_hold.active = false;
                SyncLinkedItems();
                ResetSelectionAnimation(now);
                AnimateTo(1.0f, now);
            }

            static void BeginClose(u32 now)
            {
                if (g_dialog.type != DLG_TOGGLE_ACTION)
                    g_dialog.type = DLG_NONE;
                g_hold.active = false;
                AnimateTo(0.0f, now);
            }

            void    CloseMenu(u32 now)
            {
                CloseList(g_inline, now);
                if (g_overlay.active)
                {
                    // 上画面リストはメニュー本体と独立した操作 UI として残す
                    if (g_overlay.screen == 1)
                        CloseList(g_overlay, now);
                }
                else if (GuiKeyboard::Active())
                    GuiKeyboard::Cancel(now);
                else if (g_capture.active)
                    AnimClose(g_capture.anim, now, kBottomAnimMs);
                else if (g_slider.active)
                    CloseSlider(now);
                if (DirtyCount() > 0)
                {
                    OpenDialog(DLG_CLOSE, now);
                    return;
                }
                BeginClose(now);
            }

            // ================================================================
            // update(now)
            // ================================================================
            static void Update(u32 now)
            {
                UpdateHoldAction(now);
                if (g_openTarget == 0.0f && OpenAmount(now) <= 0.001f && g_visible)
                {
                    g_visible = false;
                    g_needFinal = true;     // 閉じ切ったら 1 回だけ空で描く
                }
                if (g_inline.active && AnimExitComplete(g_inline.anim, now))
                    g_inline.active = false;
                if (g_overlay.active && AnimExitComplete(g_overlay.anim, now))
                {
                    g_overlay.active = false;
                    g_needFinal = true;
                }
                if (g_capture.active && AnimExitComplete(g_capture.anim, now))
                {
                    g_capture.active = false;
                    g_needFinal = true;
                }
                if (g_slider.active && AnimExitComplete(g_slider.anim, now))
                {
                    g_slider.active = false;
                    g_needFinal = true;
                }
                {
                    const bool was = GuiKeyboard::Active();

                    GuiKeyboard::Update(now);
                    if (was && !GuiKeyboard::Active())
                        g_needFinal = true;
                }
                if (g_dialog.type != DLG_NONE && AnimExitComplete(g_dialog.anim, now))
                {
                    const u8    type = g_dialog.type;
                    const int   item = g_dialog.item;
                    const bool  confirmed = g_dialog.confirmed;

                    g_dialog.type = DLG_NONE;
                    g_needFinal = true;
                    if (confirmed)
                    {
                        if (type == DLG_CLOSE)
                        {
                            ApplyAll(now);
                            BeginClose(now);
                        }
                        else if (type == DLG_APPLY_ALL)
                            ApplyAll(now);
                        else if (type == DLG_REVERT_ALL)
                            DiscardAll();
                        else if (type == DLG_TOGGLE_ACTION && item >= 0)
                            ExecuteToggleAction(g_items[item], now);
                    }
                }
                // 「実行した」表示の終わり
                for (int i = 0; i < g_itemCount; i++)
                {
                    Item &it = g_items[i];

                    if (it.feedbackOn && (s32)(it.feedbackUntil - now) <= 0)
                    {
                        it.feedbackOn = false;
                        g_needFinal = true;
                    }
                }
                // 通知の寿命。最後の 1 件が消えたら空で 1 回描く。
                {
                    bool alive = false;

                    for (int i = 0; i < kNoticeMax; i++)
                    {
                        if (NoticeAlive(g_notices[i], now))
                            alive = true;
                        else
                            g_notices[i].active = false;
                    }
                    if (g_noticesAlive && !alive)
                        g_needFinal = true;
                    g_noticesAlive = alive;
                }
            }

            // ================================================================
            // ★通知は関数内定義（効果）の ON/OFF から出す（2026-09-17 利用者指示）
            //   効果は項目 ON の適用（束縛なし）、OFF の適用（必ず解除）、ホットキーの反転、
            //   関数側の書き込みのどれでも変わる。どこで変わったかを問わず、観測した状態が
            //   前回と違うときだけ通知する。束縛ありの ON 適用（アームのみ）は効果が変わらないので出ない。
            //   並びは walkItems の順（Simulator の適用順と同じ）。
            // ================================================================
            static void PollEffects(u32 now)
            {
                struct Fn
                {
                    u32 now;
                    void operator()(Item &it)
                    {
                        const int idx = ItemIndex(it);

                        if (it.type != ITEM_CHECKBOX || !HasEffect(idx))
                            return;

                        const bool active = EffectActive(idx);

                        if (active == g_effectSeen[idx])
                            return;
                        g_effectSeen[idx] = active;
                        // 題 = チート名、本文 = 〈チート名〉を有効／無効にしました（無効は赤）
                        std::snprintf(g_msg, sizeof(g_msg), active ? u8"%sを有効にしました" : u8"%sを無効にしました",
                                      it.label);
                        AddNotice(it.label, g_msg, now, !active);
                    }
                } fn = { now };

                WalkItems(fn);
            }

            // 登録した時点の状態を「観測済み」にする（登録しただけで通知しない）
            void    PrimeEffect(int index)
            {
                if (index >= 0 && index < kMaxItems)
                    g_effectSeen[index] = EffectActive(index);
            }

            // ToggleHandlers の駆動（プラグイン側の拡張。Simulator の executeActiveCheckboxes）
            static void DriveToggleHandlers(void)
            {
                const ToggleHandlers *h = &g_toggleHdl;
                // 開いている間の held は 0（メニュー操作のボタンを効果へ漏らさない）
                const u16   held = (g_visible || OverlayActive() || g_dialog.type != DLG_NONE)
                                   ? 0 : g_held;

                for (int i = 0; i < g_itemCount; i++)
                {
                    if (g_items[i].type != ITEM_CHECKBOX)
                        continue;

                    const bool on = g_items[i].applied != 0;

                    if (on != g_toggleState[i])
                    {
                        g_toggleState[i] = on;
                        if (on && h != nullptr && h->OnEnable != nullptr)
                            h->OnEnable(i);
                        else if (!on && h != nullptr && h->OnDisable != nullptr)
                            h->OnDisable(i);
                    }
                    if (on && h != nullptr && h->OnTick != nullptr)
                        h->OnTick(i, held);
                }
            }

            static void PollChatKanji(u32 now)
            {
                if (ChatKanji::Poll() && g_overlay.active && !g_overlay.anim.closing
                    && g_overlay.mode == OV_CHAT_KANJI)
                {
                    if (ChatKanji::Error()[0])
                    {
                        AddNotice(u8"チャット漢字候補", ChatKanji::Error(), now, true);
                        CloseList(g_overlay, now);
                    }
                    else
                        OpenList(g_overlay, 1, ChatKanji::Title(), ChatKanji::Rows(),
                                 ChatKanji::RowCount(), OV_CHAT_KANJI, g_overlay.item, 0,
                                 kScreenRows, now);
                }
                if (g_overlay.active && g_overlay.mode == OV_CHAT_KANJI && !ChatKanji::FontReady())
                {
                    // 解放済みのフォント表で退場フレームを描かない
                    g_overlay.active = false;
                    g_needFinal = true;
                }
            }

            // ================================================================
            // 1 フレーム。app.js の frame と同じく「入力 → update」の順。
            // ================================================================
            void    Step(u32 now, const Input &in)
            {
                const u16   released = (u16)(g_prevHeld & ~in.held);
                const u16   pressed = (u16)(in.held & ~g_prevHeld);
                const bool  touchDown = in.touch && !g_prevTouch;

                g_kbHandled = false;
                PollChatKanji(now);
                for (int bit = 0; bit < HB_COUNT; bit++)
                {
                    if ((released & (1u << bit)) == 0)
                        continue;
                    g_held = (u16)(g_held & ~(1u << bit));
                    HandleKey(bit, now, false, false, in);
                }
                for (int bit = 0; bit < HB_COUNT; bit++)
                {
                    if ((pressed & (1u << bit)) == 0)
                        continue;
                    g_held = (u16)(g_held | (1u << bit));
                    if (IsDir(bit))
                        g_repAt[bit] = now + (u32)kRepeatDelay;
                    HandleKey(bit, now, true, false, in);
                }
                // ControlRepeater.update: 十字だけ、予定時刻ちょうどに連続入力を送る
                for (int bit = 0; bit < HB_COUNT; bit++)
                {
                    if (!IsDir(bit) || (g_held & (1u << bit)) == 0 || (pressed & (1u << bit)) != 0)
                        continue;
                    while ((s32)(now - g_repAt[bit]) >= 0)
                    {
                        HandleKey(bit, g_repAt[bit], true, true, in);
                        g_repAt[bit] += (u32)kRepeatEvery;
                    }
                }
                g_prevHeld = in.held;
                g_held = in.held;

                // タッチ（キーボードは自分で立ち上がりを作るので水準を渡す）
                if (GuiKeyboard::Active() && !g_kbHandled)
                {
                    GuiKeyboard::Handle(0, 0, false, false, in.touch, in.tx, in.ty, now);
                    TakeKeyboardResult(now);
                }
                else if (touchDown)
                    HandleTouchDown(in, now);
                g_prevTouch = in.touch;

                Update(now);
                DriveToggleHandlers();
                PollEffects(now);
            }

            // gameInputCaptureState
            bool    ButtonBlock(void)
            {
                return g_visible || g_dialog.type != DLG_NONE || OverlayActive() || g_inline.active;
            }

            // gameInputCaptureState の blockGameTouch（下画面の overlay。退場中も含む）
            bool    BottomUiPresent(void)
            {
                return (g_overlay.active && g_overlay.screen == 1)
                       || g_capture.active || g_slider.active || GuiKeyboard::Active();
            }

            // app.js drawBottomOverlay の暗幕（色 x 出現量）
            bool    BottomDim(u32 now, u32 &color, float &amount)
            {
                if (g_overlay.active && g_overlay.screen == 1)
                {
                    color = kColDim;
                    amount = AnimAmount(g_overlay.anim, now);
                }
                else if (g_capture.active)
                {
                    color = kColDim;
                    amount = AnimAmount(g_capture.anim, now);
                }
                else if (g_slider.active)
                {
                    color = kColDim;
                    amount = AnimAmount(g_slider.anim, now);
                }
                else if (GuiKeyboard::Active())
                {
                    color = GuiKeyboard::DimColor();
                    amount = GuiKeyboard::VisibleAmount(now);
                }
                else
                    return false;
                return amount > 0.0f;
            }

            void    ResetState(void)
            {
                std::memset(&g_inline, 0, sizeof(g_inline));
                std::memset(&g_overlay, 0, sizeof(g_overlay));
                std::memset(&g_dialog, 0, sizeof(g_dialog));
                std::memset(&g_capture, 0, sizeof(g_capture));
                std::memset(&g_slider, 0, sizeof(g_slider));
                std::memset(&g_hold, 0, sizeof(g_hold));
                std::memset(g_notices, 0, sizeof(g_notices));
                std::memset(g_hotActive, 0, sizeof(g_hotActive));
                std::memset(g_repAt, 0, sizeof(g_repAt));
                std::memset(&g_toggleHdl, 0, sizeof(g_toggleHdl));
                for (int i = 0; i < kMaxItems; i++)
                    g_effectSeen[i] = EffectActive(i);
                g_noticeNextId = 1;
                g_noticeEver = false;
                g_noticeLastAt = 0;
                g_noticesAlive = false;
                g_visible = false;
                g_openFrom = 0.0f;
                g_openTarget = 0.0f;
                g_openStart = 0;
                g_needFinal = false;
                g_valueOn = g_actOn = g_edgeOn = false;
                g_edgeBlocked = 0;
                g_held = 0;
                g_prevHeld = 0;
                g_prevTouch = false;
                for (int i = 0; i < kMaxItems; i++)
                    g_toggleState[i] = i < g_itemCount && g_items[i].type == ITEM_CHECKBOX
                                       && g_items[i].applied != 0;
                g_depth = 1;
                g_frames[0].title = "ROOT";
                g_frames[0].first = g_rootFirst;
                g_frames[0].count = g_rootCount;
                g_frames[0].selection = 0;
                ResetSelectionAnimation(0);
            }

            // 押している水準を前回として覚える（開いた瞬間の Select を拾わない）
            void    PrimeInput(u16 held)
            {
                g_held = held;
                g_prevHeld = held;
            }
        }
    }
}
