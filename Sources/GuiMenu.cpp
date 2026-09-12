// ============================================================================
// GuiMenu — CTRPF-GUI-Simulator の MENU を本格移植（F-321）
// ============================================================================
//
// 正本は `Plugin/CTRPF-GUI-Simulator/ui-model.js`（模型）と `app.js`（描画）。
// **寸法・色・時間・座標はあちらから 1 対 1 で写す。**
// `PATCHES/verify_plugin_port_v2.py` の群 5 が両者を突き合わせるので、
// 片方だけ直すと落ちる。
//
// CTRPF の既存メニューはゲームを止める。こちらはゲームの nw::lyt 経路へ
// 自前の Layout ノードを差し込む方式なので、**開いている間もゲームが進行する**。
//
// 2026-09-07: 文字/数値キーボードの保留を解除し GuiKeyboard へ移植。
//   文字入力3項目を復帰。value/slider の A は数値キーボードを開く。
//   slider の専用バー描画は別作業。既存の左右操作も残す。
//
// ★Simulator との既知の差（意図したもの）
//   1. 切り抜き（clip）が無い。nw::lyt の経路に矩形クリップが無いので、
//      窓からはみ出す行は**描かない**（Simulator は途中で切って見せる）。
//      100ms のスクロール中に端の行が出入りする形になる。
//   2. 説明文は 3 行まで（Simulator は 6 行）。文字スロットの予算による。
//      同梱の説明文は最長 2 行なので、この木では見た目が変わらない。
//
// ホットキー
//   Select 単独        … 自前メニュー
//   Select + 十字上    … 従来の CTRPF メニュー

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "GuiMenu.hpp"
#include "GuiKeyboard.hpp"
#include <CTRPluginFramework/System/Touch.hpp>
#include "GuiV2.hpp"
#include "csvc.h"   // svcInvalidateEntireInstructionCache

namespace CTRPluginFramework
{
    namespace GuiMenu
    {
        namespace
        {
            const GuiV2::Screen TOP = GuiV2::SCREEN_TOP;
            const GuiV2::Screen BOT = GuiV2::SCREEN_BOTTOM;
            const GuiV2::Font   FNUM = GuiV2::FONT_NUM;

            // ================================================================
            // Simulator の定数（ui-model.js の MENU / CONTROL_REPEAT / LISTBOX /
            // DIALOG。★名前も向こうに合わせる）
            // ================================================================
            const int   kMenuW       = 160;
            const int   kItemH       = 18;
            const int   kVisibleRows = 10;
            const int   kEnterMs     = 320;     // MENU.enterDuration
            const int   kSelMoveMs   = 100;     // MENU.selectionMoveDuration
            const int   kScrollMs    = 100;     // MENU.scrollDuration
            const int   kValueBounceMs = 100;   // MENU.valueBounceDuration
            const int   kActBounceMs = 80;      // MENU.activationBounceDuration
            const int   kPulseMs     = 1800;    // MENU.pulseDuration
            const int   kRepeatDelay = 200;     // CONTROL_REPEAT.delay
            const int   kRepeatEvery = 60;      // CONTROL_REPEAT.interval
            const int   kListAnimMs  = 180;     // LISTBOX.animationDuration
            const int   kListScrollMs = 100;    // LISTBOX.scrollDuration
            const int   kInlineRows  = 6;       // LISTBOX.inlineVisibleRows
            const int   kScreenRows  = 8;       // LISTBOX.screenVisibleRows
            const int   kDialogMs    = 160;     // DIALOG.animationDuration
            const int   kNoticeMax   = 5;       // ★Simulator(NOTICE.maximum) は 7 だが 5 にする（F-344）。
                                               //   F-325 では予算のため 2 にしていたが、F-342 で
                                               //   コマンド語の器を詰めて 207,296 B 空いたので戻した。
                                               //   1 件の実費は 文字スロット 2 本（題 24 字 + 本文 16 字）
                                               //   + 矩形 6 枚（Frame1px 5 + 縁の帯 1）= 領域 10,144 B /
                                               //   記録 4,470 B。
                                               //   ★F-344: 手紙を開くと nw::lyt の確保に失敗して
                                               //     落ちたので、借用を 0x50000 へ下げた。その枠に
                                               //     収めるため 7 -> 5 にしている。
                                               //   6 件目以降は一番古いものを押し出す（queue は保つ）。
            const int   kNoticeW     = 144;     // NOTICE.width
            const int   kNoticeH     = 27;      // NOTICE.height
            const int   kNoticeGap   = 4;       // NOTICE.gap
            const int   kNoticeMargin = 8;      // NOTICE.margin
            const int   kNoticeEnterMs = 360;   // NOTICE.enterDuration
            const int   kNoticeHoldMs = 2600;   // NOTICE.holdDuration
            const int   kNoticeExitMs = 360;    // NOTICE.exitDuration
            const int   kNoticeSpawnMs = 40;    // NOTICE.spawnInterval

            // ---- 色（rgba(R,G,B,A) -> 0xAABBGGRR。メモリ上は R,G,B,A の順）----
            const u32   kColPanel     = 0xC70D100C; // rgba(12,16,13,.78)
            const u32   kColBorder    = 0xE6A4E463; // rgba(99,228,164,.9)
            const u32   kColHeader    = 0xBD222B1C; // rgba(28,43,34,.74)
            const u32   kColFoot      = 0xC2171B15; // rgba(21,27,23,.76)
            const u32   kColSelFill   = 0x00374529; // rgba(41,69,55,α) ★α は脈動
            const u32   kColSelLine   = 0x00A4E463; // rgba(99,228,164,α) ★α は脈動
            const u32   kColAccent    = 0xFFA4E463; // #63e4a4
            const u32   kColWhite     = 0xFFFFFFFF; // #ffffff
            const u32   kColText      = 0xFFB1B9AE; // #aeb9b1
            const u32   kColBody      = 0xFFC7CEC4; // #c4cec7
            const u32   kColCrumb     = 0xFFA7D979; // #79d9a7
            const u32   kColDirty     = 0xFF66D1FF; // #ffd166
            const u32   kColHotkey    = 0xFFFFA978; // #78a9ff
            const u32   kColFootTxt   = 0xFF879082; // #829087
            const u32   kColFootOff   = 0xFF5B6358; // #58635b
            const u32   kColPanelEdge = 0xFF5B6C4F; // #4f6c5b
            const u32   kColBarTrack  = 0xA82C3426; // rgba(38,52,44,.66)
            const u32   kColBarThumb  = 0xE0A4E463; // rgba(99,228,164,.88)
            const u32   kColListSel   = 0xA3374529; // rgba(41,69,55,.64)
            const u32   kColBoxBg     = 0xD10B0D0A; // rgba(10,13,11,.82)
            const u32   kColDlgBg     = 0xE6080907; // rgba(7,9,8,.9)
            const u32   kColDlgSel    = 0xB81F495B; // rgba(91,73,31,.72)
            const u32   kColKeyBg     = 0xC21E231C; // rgba(28,35,30,.76)
            const u32   kColKeyLine   = 0xFF3D453A; // #3a453d
            const u32   kColKeyTxt    = 0xFFB9C1B7; // #b7c1b9
            const u32   kColHint      = 0xFF929A8F; // #8f9a92
            const u32   kColNoticeBg  = 0xC70B0D0A; // rgba(10,13,11,.78)
            const u32   kColDanger    = 0xFF4D48E5; // #e5484d
            const u32   kColDangerEdge = 0xFF5B4F6C; // #6c4f5b
            const u32   kColDim       = 0xB8111410; // rgba(16,20,17,.72)

            // 通知文の語尾（要望 R5。変えたければここだけ直す）。
            const char *kMsgEnabledSuffix = u8"を有効にしました";
            const char *kMsgDisabledSuffix = u8"を無効にしました";

            // ================================================================
            // 項目（ui-model.js の createMenuTree を平たくしたもの）
            // ================================================================
            enum ItemType
            {
                ITEM_FOLDER = 0, ITEM_CHECKBOX, ITEM_ACTION,
                ITEM_LIST, ITEM_VALUE, ITEM_SLIDER
            };
            enum Format { FMT_NONE = 0, FMT_DEC, FMT_HEX, FMT_FLOAT };
            enum ActionId { ACT_NONE = 0, ACT_SAVE, ACT_TOP_LIST, ACT_BOTTOM_LIST, ACT_TEXT, ACT_COMPACT_TEXT };

            const int   kMaxItems   = 96;
            const int   kLongList   = 30;
            const int   kMaxDepth   = 4;
            const int   kNameBytes  = 28;
            const int   kWrapBytes  = 96;
            const int   kTextBytes  = 96;

            struct Item
            {
                u8          type;
                u8          fmt;
                u8          action;
                u8          childFirst;     // 子の先頭（ITEM_FOLDER のみ）
                u8          childCount;
                u8          optionCount;
                const char *label;
                const char *desc;
                const char *const *options;
                // ★float は 1/10 単位の整数で持つ（浮動小数の等値比較を避ける）
                s32         value;
                s32         applied;
                s32         minimum;
                s32         maximum;
                s32         step;
                // ★ホットキー（ui-model.js の hotkey / appliedHotkey）。
                //   文字列ではなくボタンの集合で持つ（表示の時だけ "L+UP" に組む）。
                //   0 = "なし"。AddItem の memset で 0 になる。
                u16         hotkey;
                u16         appliedHotkey;
            };

            Item        g_items[kMaxItems];
            int         g_itemCount = 0;

            // 生成する名前（"スクロール項目01" など）の置き場。std::string を使わない。
            char        g_names[70][kNameBytes];
            int         g_nameCount = 0;
            const char *g_longOpts[kLongList];
            const char *kWeather[] = { u8"晴れ", u8"雨", u8"雪" };

            struct Frame
            {
                const char *title;
                int         first;
                int         count;
                int         selection;
            };
            Frame       g_frames[kMaxDepth];
            int         g_depth = 1;

            // ---- インラインリスト ----
            struct Listbox
            {
                bool    active;
                int     item;
                int     index;
                bool    closing;
                float   animFrom, animTarget;
                u32     animStart;
                int     animMs;
                float   scrollFrom, scrollTarget;
                u32     scrollStart;
            };
            Listbox     g_inline;

            // ---- ダイアログ ----
            enum DialogType { DLG_NONE = 0, DLG_CLOSE, DLG_REVERT };
            struct Dialog
            {
                u8      type;
                int     index;
                bool    closing;
                bool    confirmed;
                float   animFrom, animTarget;
                u32     animStart;
                int     animMs;
            };
            Dialog      g_dialog;

            const char *kDlgCloseTitle   = u8"変更を適用しますか？";
            const char *kDlgRevertTitle  = u8"変更を戻しますか？";
            const char *kDlgCloseOpts[]  = { u8"適用", u8"キャンセル" };
            const char *kDlgRevertOpts[] = { u8"戻す", u8"キャンセル" };

            // ---- ホットキー（ui-model.js の HOTKEY_BUTTON_ORDER / LABELS の写し）----
            //   順序は表示順そのもの。マスクの bit 位置は公開 enum HotkeyBit。
            //   ★公開側と二重定義しない。こちらは 3DS キー対応表と表示名だけ持つ。
            const u32   kHotkeyKeys[HB_COUNT] = {
                (u32)Key::ZL, (u32)Key::L, (u32)Key::R, (u32)Key::ZR,
                (u32)Key::DPadUp, (u32)Key::DPadDown,
                (u32)Key::DPadLeft, (u32)Key::DPadRight,
                (u32)Key::A, (u32)Key::B, (u32)Key::X, (u32)Key::Y,
                (u32)Key::Select, (u32)Key::Start
            };
            const char *const kHotkeyLabels[HB_COUNT] = {
                "ZL", "L", "R", "ZR",
                "UP", "DOWN", "LEFT", "RIGHT",
                "A", "B", "X", "Y", "SELECT", "START"
            };
            // ★入力待ちでは A/B/X を記録しない（A=決定 / B=取消 / X=無効）。
            //   Simulator はマウスのボタンを押すが、3DS は十字キー操作なので
            //   決定・取消・無効の手段を残す。A/B/X 単体や含む組み合わせは
            //   設定できない（「なし」・L+UP・R+DOWN・ZL+ZR・SELECT 型は可）。
            const u16   kHotkeyRecordable =
                (u16)((1u << HB_ZL) | (1u << HB_L) | (1u << HB_R) | (1u << HB_ZR)
                      | (1u << HB_UP) | (1u << HB_DOWN)
                      | (1u << HB_LEFT) | (1u << HB_RIGHT)
                      | (1u << HB_Y) | (1u << HB_SELECT) | (1u << HB_START));

            // ---- 画面リストボックス（ui-model.js の overlay[type=listbox]）----
            //   上下どちらか 1 つだけ開く。inlineList / dialog とは独立。
            enum OverlayMode { OV_GENERIC = 0, OV_HOTKEY_ITEM };
            struct Overlay
            {
                bool        active;
                u8          screen;           // 0=上 / 1=下
                u8          mode;             // OV_GENERIC=選ぶだけ / OV_HOTKEY_ITEM=項目へ反映
                int         item;             // OV_HOTKEY_ITEM の対象（-1=なし）
                const char *title;
                const char *const *options;
                int         optionCount;
                int         index;
                bool        closing;
                float       animFrom, animTarget;
                u32         animStart;
                int         animMs;
                float       scrollFrom, scrollTarget;
                u32         scrollStart;
            };
            Overlay     g_overlay;

            // ---- ホットキー入力待ち（ui-model.js の overlay[type=hotkey-capture]）----
            struct Capture
            {
                bool    active;
                u8      phase;          // 0=arming（全離し待ち）/ 1=waiting / 2=recording
                int     item;
                u16     captured;
            };
            Capture     g_capture;

            // ---- 通知（ui-model.js の NotificationTimeline の写し。段 7）----
            struct Notice
            {
                bool    active;
                int     id;
                char    title[32];
                char    msg[96];
                u32     accent;           // 要望 R4/R6a。帯と縁の色
                u32     border;
                u32     createdAt;
                float   moveFromY, targetY;
                u32     moveStartedAt;
            };
            Notice      g_notices[kNoticeMax];
            int         g_noticeNextId = 1;
            u32         g_noticeLastAt = 0;
            bool        g_noticeEver = false;
            bool        g_noticesAlive = false;
            // ★別スレッド（CTRPF のメニュー側）からの通知依頼は 1 件だけ預かる。
            //   中身を写し終えてから旗を立てる。メニュースレッドが Update() で
            //   引き取って timeline へ積む（g_open 等と同じ単語受け渡し）。
            char        g_pendTitle[32];
            char        g_pendMsg[96];
            bool        g_pendRed = false;
            volatile bool g_pendHave = false;

            // ---- 状態 ----
            bool        g_open    = false;
            bool        g_visible = false;
            bool        g_run     = false;
            Thread      g_thread  = nullptr;

            float       g_openFrom = 0.0f, g_openTarget = 0.0f;
            u32         g_openStart = 0;

            float       g_selFrom = 0.0f, g_selTo = 0.0f;
            u32         g_selStart = 0;
            float       g_viewFrom = 0.0f, g_viewTarget = 0.0f;
            u32         g_viewStart = 0;

            int         g_valueDir = 0;
            u32         g_valueAt = 0;
            bool        g_valueOn = false;
            u32         g_actAt = 0;
            bool        g_actOn = false;
            int         g_edgeDir = 0;
            u32         g_edgeAt = 0;
            bool        g_edgeOn = false;
            int         g_edgeBlocked = 0;

            bool        g_needFinal = false;    // 閉じ切った後に 1 回だけ空で描く
            // ★入力は「水準」だけを見て、立ち上がりは自分で作る。
            //   CTRPF の `_keysDown`（IsKeyPressed）は Update() の周期に
            //   縛られていて、別スレッドから見ると取りこぼしと二重入力を起こす。
            u32         g_keysNow = 0;      // いま押されている
            u32         g_keysPrev = 0;
            u32         g_keysHit = 0;      // 立ち上がり（このフレームで押された）
            u32         g_keysOff = 0;      // 立ち下がり

            // 連続入力は**キーごとに**持つ（1 本だと同時押しで取り合いになる）
            enum RepeatKey { REP_UP = 0, REP_DOWN, REP_LEFT, REP_RIGHT, REP_COUNT };
            const u32   kRepeatKeys[REP_COUNT] = {
                (u32)Key::DPadUp, (u32)Key::DPadDown,
                (u32)Key::DPadLeft, (u32)Key::DPadRight
            };
            bool        g_repHeld[REP_COUNT];
            u32         g_repAt[REP_COUNT];

            // ホットキー発動済みの記憶（ui-model.js の activeHotkeyItems）。
            // 押し続けている間は再発動しない。全部離して押し直すとまた動く。
            bool        g_hotActive[kMaxItems];

            // 関数側の発火ハンドラ（未登録なら内蔵動作）。メニュースレッドが呼ぶ。
            HotkeyHandler g_hotHandler = nullptr;

            // トグル実行エンジンの登録（振る舞い。データではない）。
            // アドレス・有効値・既定値は関数側が持つ。
            struct FxDesc
            {
                bool    used;
                bool    (*IsActive)(int index);
                void    (*SetActive)(int index, bool active);
            };
            FxDesc      g_fx[kMaxItems];
            // メニューON/OFF で判定を送るか（項目ごと。既定 false）。
            bool        g_fxMenuJudge[kMaxItems];

            // トグル実行ハンドラの写し（未登録は呼ばない）。
            ToggleHandlers g_toggleHdl;
            // 直前の有効状態（チェック項目のみ見る）。遷移検出用。
            bool        g_toggleState[kMaxItems];

            // ★試験用の効果配線（無敵モード・壁抜け）。ゲームメモリには触らず、
            //   項目ごとの試験変数を反転するだけ。通知経路の確認と実チートの
            //   雛形用。不要になれば WireTestEffects() ごと消す。
            //   実チートはこの形を写す： IsActive＝読取判定、SetActive＝書込。
            //   メモリ型は PatchListIsActive/PatchListSetActive が使える。
            static int  s_testIdx[2];
            static bool s_testFx[2];
            static int  TestSlot(int index)
            {
                if (index == s_testIdx[0] || index == s_testIdx[1])
                    return index == s_testIdx[0] ? 0 : 1;
                return -1;
            }
            static bool TestIsActive(int index)
            {
                const int s = TestSlot(index);

                return s >= 0 ? s_testFx[s] : false;
            }
            static void TestSetActive(int index, bool active)
            {
                const int s = TestSlot(index);

                if (s >= 0)
                    s_testFx[s] = active;
            }
            void    WireTestEffects(void)
            {
                static const ToggleEffectFuncs kTestFx =
                    { TestIsActive, TestSetActive };
                int i = 0;

                s_testIdx[0] = -1;
                s_testIdx[1] = -1;
                s_testFx[0] = false;
                s_testFx[1] = false;
                while (i < g_itemCount)
                {
                    if (std::strcmp(g_items[i].label, u8"無敵モード") == 0)
                        s_testIdx[0] = i;
                    else if (std::strcmp(g_items[i].label, u8"壁抜け") == 0)
                        s_testIdx[1] = i;
                    i++;
                }
                i = 0;
                while (i < 2)
                {
                    if (s_testIdx[i] >= 0)
                        GuiMenu::RegisterToggleEffect(s_testIdx[i], &kTestFx);
                    i++;
                }
            }

            // ================================================================
            // 小物
            // ================================================================
            u32     NowMs(void)
            {
                return (u32)(svcGetSystemTick() / (u64)(SYSCLOCK_ARM11 / 1000));
            }

            float   Clamp01(float v)
            {
                return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            }

            int     ClampI(int v, int lo, int hi)
            {
                return v < lo ? lo : (v > hi ? hi : v);
            }

            float   EaseOutCubic(float t)
            {
                const float u = 1.0f - Clamp01(t);

                return 1.0f - u * u * u;
            }

            float   Mix(float a, float b, float t)
            {
                return a + (b - a) * t;
            }

            float   Progress(u32 start, u32 now, int ms)
            {
                if (ms <= 0)
                    return 1.0f;
                if (now <= start)
                    return 0.0f;
                return Clamp01((float)(now - start) / (float)ms);
            }

            // Simulator の globalAlpha 相当（元の α に係数を掛ける）
            u32     Fade(u32 col, float a)
            {
                const u32   src = (col >> 24) & 0xFF;
                const u32   out = (u32)((float)src * Clamp01(a) + 0.5f);

                return (col & 0x00FFFFFF) | (out << 24);
            }

            u32     WithAlpha(u32 col, float a)
            {
                const u32   v = (u32)(Clamp01(a) * 255.0f + 0.5f);

                return (col & 0x00FFFFFF) | (v << 24);
            }

            float   SelectionPulse(u32 now)
            {
                const float phase = 6.2831853f * (float)(now % (u32)kPulseMs)
                                  / (float)kPulseMs;

                return 0.48f + 0.07f * sinf(phase);
            }

            float   SelectionOutlinePulse(u32 now)
            {
                const float phase = 6.2831853f * (float)(now % (u32)kPulseMs)
                                  / (float)kPulseMs;
                const float v = 0.7f + 0.2f * sinf(phase);

                return v < 0.5f ? 0.5f : (v > 0.9f ? 0.9f : v);
            }

            float   ScrollTarget(int index, int count, int rows)
            {
                const int   hi = count - rows > 0 ? count - rows : 0;

                return (float)ClampI(index - rows / 2, 0, hi);
            }

            // いま押されている 3DS ボタンをホットキー集合（14 bit）にする。
            // ★SampleInput() で読んだ水準の写しだけ見る（F-321）。
            u16     HeldHotkeyMask(void)
            {
                const u32   k = g_keysNow;
                u16         m = 0;
                int         i = 0;

                while (i < HB_COUNT)
                {
                    if ((k & kHotkeyKeys[i]) != 0)
                        m = (u16)(m | (u16)(1u << i));
                    i++;
                }
                return m;
            }

            // formatHotkeyButtons の写し。0 なら "なし"。
            const char *FormatHotkey(u16 mask, char *buf, size_t cap)
            {
                size_t  out = 0;
                int     i = 0;
                bool    first = true;

                if (mask == 0)
                {
                    std::snprintf(buf, cap, "%s", u8"なし");
                    return buf;
                }
                buf[0] = '\0';
                while (i < HB_COUNT)
                {
                    if ((mask & (u16)(1u << i)) != 0)
                    {
                        const char *s = kHotkeyLabels[i];
                        size_t      n = std::strlen(s);

                        if (!first)
                        {
                            if (out + 1 < cap)
                                buf[out++] = '+';
                        }
                        if (out + n < cap)
                        {
                            std::memcpy(buf + out, s, n);
                            out += n;
                        }
                        first = false;
                    }
                    i++;
                }
                buf[out < cap ? out : cap - 1] = '\0';
                return buf;
            }

            // ================================================================
            // 木を作る（createMenuTree の写し）
            // ================================================================
            const char *MakeName(const char *prefix, int index)
            {
                char *p = g_names[g_nameCount++];

                std::snprintf(p, kNameBytes, "%s%02d", prefix, index + 1);
                return p;
            }

            int     AddItem(u8 type, const char *label, const char *desc)
            {
                Item &it = g_items[g_itemCount];

                std::memset(&it, 0, sizeof(it));
                it.type = type;
                it.label = label;
                it.desc = desc;
                return g_itemCount++;
            }

            void    BuildTree(void)
            {
                int i;

                g_itemCount = 0;
                g_nameCount = 0;
                i = 0;
                while (i < kLongList)
                {
                    g_longOpts[i] = MakeName(u8"リスト項目", i);
                    i++;
                }

                // ---- 子を先に並べる（平たい配列なので連続させる）----
                const int playerFirst = g_itemCount;
                AddItem(ITEM_CHECKBOX, u8"無敵モード", u8"ダメージを受けなくなります。");
                AddItem(ITEM_CHECKBOX, u8"壁抜け", u8"当たり判定を無効にします。");
                i = AddItem(ITEM_ACTION, u8"名前を変更", u8"下画面に五十音順キーボードを開きます。");
                g_items[i].action = ACT_TEXT;
                const int playerCount = g_itemCount - playerFirst;

                const int numFirst = g_itemCount;
                i = AddItem(ITEM_VALUE, u8"所持ベル",
                            u8"10進数で入力します。左右で100ずつ変更します。");
                g_items[i].fmt = FMT_DEC;
                g_items[i].value = 1000; g_items[i].applied = 1000;
                g_items[i].minimum = 0; g_items[i].maximum = 99999; g_items[i].step = 100;
                i = AddItem(ITEM_VALUE, u8"アイテムID",
                            u8"16進数で入力します。左右で1ずつ変更します。");
                g_items[i].fmt = FMT_HEX;
                g_items[i].value = 0x2001; g_items[i].applied = 0x2001;
                g_items[i].minimum = 0; g_items[i].maximum = 0xFFFF; g_items[i].step = 1;
                i = AddItem(ITEM_VALUE, u8"移動速度",
                            u8"float値です。左右で0.1ずつ変更します。");
                g_items[i].fmt = FMT_FLOAT;
                g_items[i].value = 10; g_items[i].applied = 10;
                g_items[i].minimum = 1; g_items[i].maximum = 50; g_items[i].step = 1;
                i = AddItem(ITEM_SLIDER, u8"通知時間",
                            u8"下画面の横向きスライダーで変更します。");
                g_items[i].fmt = FMT_FLOAT;
                g_items[i].value = 30; g_items[i].applied = 30;
                g_items[i].minimum = 10; g_items[i].maximum = 80; g_items[i].step = 5;
                const int numCount = g_itemCount - numFirst;

                const int uiFirst = g_itemCount;
                i = AddItem(ITEM_ACTION, u8"上画面リスト",
                            u8"上画面へ汎用リストボックスを開きます。");
                g_items[i].action = ACT_TOP_LIST;
                i = AddItem(ITEM_ACTION, u8"下画面リスト",
                            u8"下画面へ汎用リストボックスを開きます。");
                g_items[i].action = ACT_BOTTOM_LIST;
                i = AddItem(ITEM_ACTION, u8"文字キーボード", u8"五十音順とQWERTYを切り替えられます。");
                g_items[i].action = ACT_TEXT;
                i = AddItem(ITEM_ACTION, u8"小型文字キーボード", u8"小さい文字キーボードを開きます。");
                g_items[i].action = ACT_COMPACT_TEXT;
                const int uiCount = g_itemCount - uiFirst;

                const int scrollFirst = g_itemCount;
                i = 0;
                while (i < 24)
                {
                    const int k = AddItem(ITEM_CHECKBOX, MakeName(u8"スクロール項目", i),
                                          u8"多数の項目を移動した時のスクロールを確認します。");

                    g_items[k].value = (i % 3 == 0) ? 1 : 0;
                    g_items[k].applied = g_items[k].value;
                    i++;
                }
                const int scrollCount = g_itemCount - scrollFirst;

                // ---- 根 ----
                const int rootFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"プレイヤー", u8"プレイヤー関連のチートを開きます。");
                g_items[i].childFirst = (u8)playerFirst;
                g_items[i].childCount = (u8)playerCount;
                AddItem(ITEM_CHECKBOX, u8"歩行速度アップ", u8"移動速度の変更を有効にします。");
                i = AddItem(ITEM_ACTION, u8"セーブ実行",
                            u8"UIを変更せずセーブ関数だけを呼び出します。");
                g_items[i].action = ACT_SAVE;
                i = AddItem(ITEM_LIST, u8"天候",
                            u8"項目の位置にリストを展開して天候を選択します。");
                g_items[i].options = kWeather;
                g_items[i].optionCount = 3;
                i = AddItem(ITEM_FOLDER, u8"数値設定", u8"数値入力とスライダーのサンプルです。");
                g_items[i].childFirst = (u8)numFirst;
                g_items[i].childCount = (u8)numCount;
                i = AddItem(ITEM_FOLDER, u8"UIテスト", u8"自前UI関数の表示サンプルです。");
                g_items[i].childFirst = (u8)uiFirst;
                g_items[i].childCount = (u8)uiCount;
                i = AddItem(ITEM_LIST, u8"大量リスト",
                            u8"多数のリスト項目をインライン表示してスクロールを確認します。");
                g_items[i].options = g_longOpts;
                g_items[i].optionCount = (u8)kLongList;
                i = AddItem(ITEM_FOLDER, u8"スクロールテスト",
                            u8"多数のチート項目を表示してスクロールを確認します。");
                g_items[i].childFirst = (u8)scrollFirst;
                g_items[i].childCount = (u8)scrollCount;
                AddItem(ITEM_CHECKBOX, u8"しずえスキップ",
                        u8"しずえの会話を飛ばして村へ出ます。起動時の一括処理を先に実行します。");
                i = 0;
                while (i < 12)
                {
                    AddItem(ITEM_CHECKBOX, MakeName(u8"追加チート", i),
                            u8"ルートメニューのスクロール確認用項目です。");
                    i++;
                }

                g_depth = 1;
                g_frames[0].title = "ROOT";
                g_frames[0].first = rootFirst;
                g_frames[0].count = g_itemCount - rootFirst;
                g_frames[0].selection = 0;
            }

            Frame  &Cur(void)   { return g_frames[g_depth - 1]; }
            Item   &Sel(void)   { return g_items[Cur().first + Cur().selection]; }

            bool    IsDirtyItem(const Item &it)
            {
                // ★ACTION もホットキー差分では適用待ちになる（F-326）。
                //   値を持たないので value 側は常に一致する。
                if (it.type == ITEM_FOLDER)
                    return false;
                return it.value != it.applied
                       || it.hotkey != it.appliedHotkey;
            }

            int     DirtyCount(void)
            {
                int n = 0;
                int i = 0;

                while (i < g_itemCount)
                {
                    if (IsDirtyItem(g_items[i]))
                        n++;
                    i++;
                }
                return n;
            }

            // ================================================================
            // 文字の整形
            // ================================================================
            const char *ItemIcon(const Item &it)
            {
                if (it.type == ITEM_FOLDER)
                    return ">";
                if (it.type == ITEM_CHECKBOX)
                    return it.value ? "[X]" : "[ ]";
                if (it.type == ITEM_ACTION)
                    return "A";
                if (it.type == ITEM_LIST)
                    return "L";
                return "V";
            }

            const char *FormatValue(const Item &it, char *buf, size_t cap)
            {
                buf[0] = '\0';
                if (it.type == ITEM_CHECKBOX)
                    std::snprintf(buf, cap, "%s", it.value ? "ON" : "OFF");
                else if (it.type == ITEM_LIST)
                {
                    if (it.options != nullptr && it.value < (int)it.optionCount)
                        std::snprintf(buf, cap, "%s", it.options[it.value]);
                }
                else if (it.fmt == FMT_HEX)
                    std::snprintf(buf, cap, "0x%04X", (unsigned int)it.value);
                else if (it.fmt == FMT_FLOAT)
                    std::snprintf(buf, cap, "%d.%d", (int)(it.value / 10),
                                  (int)(it.value % 10));
                else if (it.type == ITEM_VALUE || it.type == ITEM_SLIDER)
                    std::snprintf(buf, cap, "%d", (int)it.value);
                return buf;
            }

            bool    IsNumericItem(const Item &it)
            {
                return it.type == ITEM_VALUE || it.type == ITEM_SLIDER;
            }

            // trimBitmapText の写し。収まらなければ "..." を付けて切る。
            const char *Trim(const char *src, int maxWidth, char *buf, size_t cap)
            {
                const int   dots = GuiV2::MeasureText("...");
                int         i = 0;
                int         w = 0;
                size_t      out = 0;
                size_t      fit = 0;

                buf[0] = '\0';
                while (src[i] != '\0')
                {
                    const int    start = i;
                    const int    cw = GuiV2::NextCharWidth(src, i);
                    const size_t len = (size_t)(i - start);

                    if (out + len + 4 >= cap)
                        break;
                    if (w + cw > maxWidth)
                    {
                        std::memcpy(buf, src, fit);
                        std::snprintf(buf + fit, cap - fit, "...");
                        return buf;
                    }
                    std::memcpy(buf + out, src + start, len);
                    out += len;
                    w += cw;
                    buf[out] = '\0';
                    if (w + dots <= maxWidth)
                        fit = out;
                }
                return buf;
            }

            // wrapBitmapText の写し。行数を返す。
            int     Wrap(const char *src, int maxWidth, int maxLines,
                         char lines[][kWrapBytes])
            {
                int     i = 0;
                int     n = 0;
                int     w = 0;
                size_t  out = 0;

                if (maxLines <= 0)
                    return 0;
                lines[0][0] = '\0';
                while (src[i] != '\0')
                {
                    const int    start = i;
                    const int    cw = GuiV2::NextCharWidth(src, i);
                    const size_t len = (size_t)(i - start);

                    if (out > 0 && w + cw > maxWidth)
                    {
                        lines[n][out] = '\0';
                        n++;
                        if (n >= maxLines)
                            return n;
                        out = 0;
                        w = 0;
                        lines[n][0] = '\0';
                    }
                    if (out + len + 1 >= (size_t)kWrapBytes)
                        continue;
                    std::memcpy(lines[n] + out, src + start, len);
                    out += len;
                    w += cw;
                    lines[n][out] = '\0';
                }
                return out > 0 ? n + 1 : n;
            }

            // ================================================================
            // 描画の小物
            // ================================================================
            // 1px の枠（A案 F-323: 縁と地を重ねない）。
            //   旧版は「外枠フル + 内地フル」の 2 枚で、外枠の内側が内地に
            //   覆われて二重ブレンドになり、登場・退場のフェード途中で
            //   縁だけ濃く残った。縁 4 辺と内地 1 枚の 5 枚・重なりゼロにし、
            //   各画素を単一層にする（一枚絵をフェードするのと等価）。
            //   不透明時も同じ絵になる。
            void    Frame1px(GuiV2::Screen sc, int x, int y, int w, int h,
                             u32 line, u32 fill)
            {
                if (w <= 2 || h <= 2)
                {
                    GuiV2::FillRect(sc, x, y, w, h, line);
                    if (w > 2 && h > 2)
                        GuiV2::FillRect(sc, x + 1, y + 1, w - 2, h - 2, fill);
                    return;
                }
                GuiV2::FillRect(sc, x, y, w, 1, line);
                GuiV2::FillRect(sc, x, y + h - 1, w, 1, line);
                GuiV2::FillRect(sc, x, y + 1, 1, h - 2, line);
                GuiV2::FillRect(sc, x + w - 1, y + 1, 1, h - 2, line);
                GuiV2::FillRect(sc, x + 1, y + 1, w - 2, h - 2, fill);
            }

            // 半透明の枠は 4 本に分けないと中身に縁が重なる
            void    Outline(GuiV2::Screen sc, int x, int y, int w, int h, u32 col)
            {
                GuiV2::FillRect(sc, x, y, w, 1, col);
                GuiV2::FillRect(sc, x, y + h - 1, w, 1, col);
                GuiV2::FillRect(sc, x, y + 1, 1, h - 2, col);
                GuiV2::FillRect(sc, x + w - 1, y + 1, 1, h - 2, col);
            }

            // drawScrollBar の写し
            void    ScrollBar(GuiV2::Screen sc, int x, int y, int h,
                              int visibleRows, int count, float scroll, float alpha)
            {
                if (count <= visibleRows)
                    return;

                int         thumbH = h * visibleRows / count;
                const int   maxScroll = count - visibleRows;
                int         thumbY;

                if (thumbH < 10)
                    thumbH = 10;
                thumbY = y + (int)((float)(h - thumbH)
                                   * Clamp01(scroll / (float)maxScroll) + 0.5f);
                GuiV2::FillRect(sc, x, y, 2, h, Fade(kColBarTrack, alpha));
                GuiV2::FillRect(sc, x, thumbY, 2, thumbH, Fade(kColBarThumb, alpha));
            }

            // ★後方で定義する通知・オーバーレイ関数の前方宣言
            //   （Animating() や描画が先に使うため）
            float   NoticeX(const Notice &nt, u32 now);
            float   NoticeY(const Notice &nt, u32 now);
            bool    NoticeAlive(const Notice &nt, u32 now);
            bool    NoticesAlive(u32 now);
            void    AddNotice(const char *title, const char *msg, u32 now,
                              bool red = false);
            float   OverlayAmount(u32 now);
            float   OverlayScroll(u32 now);
            void    ToggleEvaluate(int index, u32 now);

            // ================================================================
            // アニメーション（ui-model.js の CheatMenuModel と同じ式）
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

            // selectionBounceOffset（値の増減で入力方向へ跳ねる）
            float   ValueBounce(u32 now)
            {
                if (!g_valueOn || now < g_valueAt)
                    return 0.0f;
                if (now - g_valueAt >= (u32)kValueBounceMs)
                    return 0.0f;

                const float p = (float)(now - g_valueAt) / (float)kValueBounceMs;
                const float a = p < 0.28f ? EaseOutCubic(p / 0.28f)
                                          : 1.0f - EaseOutCubic((p - 0.28f) / 0.72f);

                return (float)g_valueDir * 4.0f * a;
            }

            // activationBounceOffset（A で選んだとき右へ跳ねる）
            float   ActBounce(u32 now)
            {
                if (!g_actOn || now < g_actAt)
                    return 0.0f;
                if (now - g_actAt >= (u32)kActBounceMs)
                    return 0.0f;

                const float p = (float)(now - g_actAt) / (float)kActBounceMs;
                const float a = p < 0.30f ? EaseOutCubic(p / 0.30f)
                                          : 1.0f - EaseOutCubic((p - 0.30f) / 0.70f);

                return 3.0f * a;
            }

            // selectionBoundaryBounceOffset（端で止まったとき縦に跳ねる）
            float   EdgeBounce(u32 now)
            {
                if (!g_edgeOn || now < g_edgeAt)
                    return 0.0f;
                if (now - g_edgeAt >= (u32)kSelMoveMs)
                    return 0.0f;

                const float p = (float)(now - g_edgeAt) / (float)kSelMoveMs;
                const float a = p < 0.28f ? EaseOutCubic(p / 0.28f)
                                          : 1.0f - EaseOutCubic((p - 0.28f) / 0.72f);

                return (float)g_edgeDir * 4.0f * a;
            }

            float   InlineAmount(u32 now)
            {
                if (!g_inline.active)
                    return 0.0f;
                return Mix(g_inline.animFrom, g_inline.animTarget,
                           EaseOutCubic(Progress(g_inline.animStart, now,
                                                 g_inline.animMs)));
            }

            float   InlineScroll(u32 now)
            {
                return Mix(g_inline.scrollFrom, g_inline.scrollTarget,
                           EaseOutCubic(Progress(g_inline.scrollStart, now,
                                                 kListScrollMs)));
            }

            float   DialogAmount(u32 now)
            {
                if (g_dialog.type == DLG_NONE)
                    return 0.0f;
                return Mix(g_dialog.animFrom, g_dialog.animTarget,
                           EaseOutCubic(Progress(g_dialog.animStart, now,
                                                 g_dialog.animMs)));
            }

            bool    Animating(u32 now)
            {
                if (g_openTarget != OpenAmount(now))
                    return true;
                if (SelectionPosition(now) != g_selTo)
                    return true;
                if (ViewportStart(now) != g_viewTarget)
                    return true;
                if (ValueBounce(now) != 0.0f || ActBounce(now) != 0.0f
                    || EdgeBounce(now) != 0.0f)
                    return true;
                if (g_inline.active || g_dialog.type != DLG_NONE)
                    return true;
                if (g_overlay.active || g_capture.active || GuiKeyboard::Active())
                    return true;
                if (NoticesAlive(now))
                    return true;
                return false;
            }

            // ================================================================
            // 描画（app.js の drawMenu / drawInlineList / drawDescriptionPanel /
            // drawDialog の写し）
            // ================================================================
            char        g_buf[kTextBytes];
            char        g_buf2[kTextBytes];
            char        g_lines[3][kWrapBytes];

            void    DrawDescription(float amount)
            {
                const Item &it = Sel();
                const int   x = 168, y = 8, w = 224;
                const int   n = Wrap(it.desc, w - 16, 3, g_lines);
                const int   h = 35 + n * 11;

                Frame1px(TOP, x, y, w, h, Fade(kColPanelEdge, amount),
                         Fade(kColBoxBg, amount));
                GuiV2::DrawText(TOP, x + 8, y + 7,
                                Trim(it.label, w - 16, g_buf, sizeof(g_buf)),
                                Fade(kColAccent, amount));
                std::snprintf(g_buf2, sizeof(g_buf2), "HOTKEY:%s",
                              FormatHotkey(it.hotkey, g_buf, sizeof(g_buf)));
                GuiV2::DrawText(TOP, x + 8, y + 18, g_buf2, Fade(kColHotkey, amount));

                int i = 0;
                while (i < n)
                {
                    GuiV2::DrawText(TOP, x + 8, y + 31 + i * 11, g_lines[i],
                                    Fade(kColBody, amount));
                    i++;
                }
            }

            void    DrawInlineList(int menuX, float start, u32 now)
            {
                const Item &it = g_items[g_inline.item];
                const float amount = InlineAmount(now);

                if (amount <= 0.0f)
                    return;

                const int   row = (int)((float)Cur().selection - start);
                const int   rows = it.optionCount < kInlineRows
                                 ? (int)it.optionCount : kInlineRows;
                const int   h = rows * 14 + 6;
                int         y = 28 + row * kItemH + 14;
                const int   x = (int)(Mix((float)(menuX + kMenuW),
                                          (float)(menuX + 21), amount) + 0.5f);
                const float scroll = InlineScroll(now);

                if (y > 212 - h)
                    y = 212 - h;
                Frame1px(TOP, x, y, 132, h, Fade(kColBorder, amount),
                         Fade(kColBoxBg, amount));

                int first = (int)scroll;
                int last = first + rows;

                if (first < 0)
                    first = 0;
                if (last > (int)it.optionCount - 1)
                    last = (int)it.optionCount - 1;
                int i = first;
                while (i <= last)
                {
                    const int rowY = (int)(0.5f + (float)(y + 3)
                                           + ((float)i - scroll) * 14.0f);

                    if (rowY >= y + 2 && rowY + 12 <= y + h - 2)
                    {
                        if (i == g_inline.index)
                            GuiV2::FillRect(TOP, x + 3, rowY, 122, 12,
                                            Fade(kColListSel, amount));
                        GuiV2::DrawText(TOP, x + 8, rowY + 2,
                                        Trim(it.options[i], 114, g_buf, sizeof(g_buf)),
                                        Fade(i == g_inline.index ? kColWhite : kColText,
                                             amount));
                    }
                    i++;
                }
                ScrollBar(TOP, x + 128, y + 3, h - 6, rows, (int)it.optionCount,
                          scroll, amount);
            }

            void    DrawDialog(u32 now)
            {
                const float amount = DialogAmount(now);

                if (amount <= 0.0f)
                    return;

                const int   w = 224, h = 90;
                const int   x = (int)(Mix(192.0f, 168.0f, amount) + 0.5f);
                const int   y = 75;
                const char *title = g_dialog.type == DLG_CLOSE ? kDlgCloseTitle
                                                               : kDlgRevertTitle;
                const char *const *opts = g_dialog.type == DLG_CLOSE ? kDlgCloseOpts
                                                                     : kDlgRevertOpts;

                Frame1px(TOP, x, y, w, h, Fade(kColDirty, amount),
                         Fade(kColDlgBg, amount));
                GuiV2::DrawText(TOP, x + 10, y + 10, title, Fade(kColWhite, amount));

                int i = 0;
                while (i < 2)
                {
                    const int rowY = y + 33 + i * 23;

                    if (i == g_dialog.index)
                        Frame1px(TOP, x + 8, rowY - 5, w - 16, 19,
                                 Fade(kColDirty, amount), Fade(kColDlgSel, amount));
                    GuiV2::DrawText(TOP, x + 18, rowY, opts[i],
                                    Fade(i == g_dialog.index ? kColWhite : kColText,
                                         amount));
                    i++;
                }
            }

            // drawListbox の写し。上下共用（段 6）。
            // 上: baseX はメニュー開閉に追従（閉じているときは中央）。
            // 下: 呼ぶ側が dim を敷いてから呼ぶ。
            void    DrawScreenListbox(GuiV2::Screen sc, int baseX, int width,
                                      int offscreenX, u32 now)
            {
                const float amount = OverlayAmount(now);

                if (amount <= 0.0f)
                    return;
                const int   x = (int)(Mix((float)offscreenX, (float)baseX,
                                          amount) + 0.5f);
                const int   count = g_overlay.optionCount;
                const int   rows = count < kScreenRows ? count : kScreenRows;
                const int   h = 25 + rows * 20;
                const int   y = (240 - h + 1) / 2;
                const float scroll = OverlayScroll(now);
                int         first = (int)scroll;
                int         last = first + rows;
                int         i;

                if (first < 0)
                    first = 0;
                if (last > count - 1)
                    last = count - 1;
                Frame1px(sc, x, y, width, h, Fade(kColAccent, amount),
                         Fade(kColBoxBg, amount));
                GuiV2::DrawText(sc, x + 8, y + 7,
                                Trim(g_overlay.title, width - 20,
                                     g_buf, sizeof(g_buf)),
                                Fade(kColAccent, amount));
                i = first;
                while (i <= last)
                {
                    const int rowY = (int)(0.5f + (float)(y + 23)
                                           + ((float)i - scroll) * 20.0f);

                    // ★切り抜きが無いので窓に入る行だけ描く（行描画と同じ流儀）
                    if (rowY >= y + 20 && rowY + 8 <= y + h - 2)
                    {
                        if (i == g_overlay.index)
                            GuiV2::FillRect(sc, x + 5, rowY - 4, width - 14, 16,
                                            Fade(kColListSel, amount));
                        GuiV2::DrawText(sc, x + 12, rowY,
                                        Trim(g_overlay.options[i], width - 24,
                                             g_buf, sizeof(g_buf)),
                                        Fade(i == g_overlay.index ? kColWhite
                                                                  : kColText,
                                             amount));
                    }
                    i++;
                }
                ScrollBar(sc, x + width - 6, y + 24, h - 29, rows, count,
                          scroll, amount);
            }

            // drawHotkeyCapture の写し（段 6）。下画面の中央に出す。
            // 丸角は無いので角は四角い（見た目だけの差）。
            void    DrawHotkeyCapture(void)
            {
                const int   w = 224, h = 86;
                const int   x = (320 - w) / 2;
                const int   y = (240 - h) / 2;
                const char *status = g_capture.phase == 0 ? u8"ボタンを全て離す"
                                   : g_capture.phase == 1 ? u8"ボタン入力待ち"
                                   : u8"入力中";
                int         bx;

                Frame1px(BOT, x, y, w, h, kColAccent, kColBoxBg);
                GuiV2::DrawText(BOT, x + 8, y + 8, "HOTKEY INPUT", kColAccent);
                GuiV2::DrawText(BOT, x + 8, y + 24, status, kColWhite);
                if (g_capture.captured != 0)
                    GuiV2::DrawText(BOT, x + 8, y + 37,
                                    FormatHotkey(g_capture.captured,
                                                 g_buf, sizeof(g_buf)),
                                    kColHotkey);
                else
                    GuiV2::DrawText(BOT, x + 8, y + 37, "--", kColHotkey);
                GuiV2::DrawText(BOT, x + 96, y + 37, u8"全て離すと決定", kColHint);
                bx = 0;
                while (bx < 2)
                {
                    const int   kx = x + 8 + bx * 108;
                    const char *label = bx == 0 ? u8"無効" : u8"取消";
                    const int   tw = GuiV2::MeasureText(label);

                    Frame1px(BOT, kx, y + 56, 100, 21, kColKeyLine, kColKeyBg);
                    GuiV2::DrawText(BOT, kx + (100 - tw) / 2, y + 61, label,
                                    kColKeyTxt);
                    bx++;
                }
            }

            // drawNotice の写し（段 7）。上画面の右下へ積む。
            void    DrawNotices(u32 now)
            {
                int i = 0;

                while (i < kNoticeMax)
                {
                    const Notice &nt = g_notices[i];

                    if (NoticeAlive(nt, now) && (s32)(now - nt.createdAt) >= 0)
                    {
                        const int x = (int)(NoticeX(nt, now) + 0.5f);
                        const int y = (int)(NoticeY(nt, now) + 0.5f);

                        Frame1px(TOP, x, y, kNoticeW, kNoticeH,
                                 nt.border, kColNoticeBg);
                        GuiV2::FillRect(TOP, x + 1, y + 1, 2, kNoticeH - 2,
                                        nt.accent);
                        GuiV2::DrawText(TOP, x + 7, y + 4,
                                        Trim(nt.title, kNoticeW - 20,
                                             g_buf, sizeof(g_buf)),
                                        kColWhite);
                        GuiV2::DrawText(TOP, x + 7, y + 14,
                                        Trim(nt.msg, kNoticeW - 14,
                                             g_buf, sizeof(g_buf)),
                                        kColBody);
                    }
                    i++;
                }
            }

            void    BuildTop(u32 now)
            {
                GuiV2::Begin(TOP);
                if (!g_visible)
                {
                    // ★閉じているときは上画面リストボックスと通知だけ出す
                    //   （ホットキー発動でメニューなしに開くことがある）。
                    if (g_overlay.active && g_overlay.screen == 0)
                        DrawScreenListbox(TOP, 98, 204, 400, now);
                    DrawNotices(now);
                    return;
                }

                const float amount = OpenAmount(now);
                const int   menuX = (int)(-kMenuW + kMenuW * amount + 0.5f);
                const Frame &fr = Cur();
                const float start = ViewportStart(now);
                const float actOff = ActBounce(now);
                const int   changed = DirtyCount();

                // ---- 地・縁・帯 ----
                GuiV2::FillRect(TOP, menuX, 0, kMenuW, 240, kColPanel);
                GuiV2::FillRect(TOP, menuX + kMenuW - 2, 0, 2, 240, kColBorder);
                GuiV2::FillRect(TOP, menuX, 0, kMenuW - 2, 23, kColHeader);
                GuiV2::DrawText(TOP, menuX + 7, 7, "CHEAT MENU", kColWhite);
                GuiV2::DrawText(TOP, menuX + 91, 7,
                                Trim(fr.title, 62, g_buf, sizeof(g_buf)), kColCrumb);

                // ---- 選択帯 ----
                {
                    const float animRow = Clamp01((SelectionPosition(now) - start)
                                                  / (float)(kVisibleRows - 1))
                                        * (float)(kVisibleRows - 1);
                    const int   hy = (int)(28.0f + animRow * (float)kItemH
                                           + EdgeBounce(now) + 0.5f);
                    const int   hx = menuX + 4 + (int)(ValueBounce(now) + actOff + 0.5f);

                    GuiV2::FillRect(TOP, hx, hy - 3, kMenuW - 10, 15,
                                    WithAlpha(kColSelFill, SelectionPulse(now)));
                    Outline(TOP, hx, hy - 3, kMenuW - 10, 15,
                            WithAlpha(kColSelLine, SelectionOutlinePulse(now)));
                }

                // ---- 行（★切り抜きが無いので窓に入る行だけ描く）----
                {
                    int first = (int)start;
                    int last = first + kVisibleRows;
                    int i;

                    if (first < 0)
                        first = 0;
                    if (last > fr.count - 1)
                        last = fr.count - 1;
                    i = first;
                    while (i <= last)
                    {
                        const Item &it = g_items[fr.first + i];
                        const int   y = (int)(28.0f + ((float)i - start)
                                              * (float)kItemH + 0.5f);
                        const bool  selected = (i == fr.selection);
                        const int   off = selected ? (int)(actOff + 0.5f) : 0;
                        const u32   col = IsDirtyItem(it) ? kColDirty
                                        : (selected ? kColWhite : kColText);

                        if (y < 25 || y > 204)
                        {
                            i++;
                            continue;
                        }
                        GuiV2::DrawText(TOP, menuX + 8 + off, y, ItemIcon(it),
                                        it.type == ITEM_FOLDER ? kColAccent : col);

                        FormatValue(it, g_buf2, sizeof(g_buf2));

                        const GuiV2::Font vf = IsNumericItem(it) ? FNUM
                                                                 : GuiV2::FONT_MAIN;
                        const int   vw = g_buf2[0] != '\0'
                                       ? GuiV2::MeasureText(g_buf2, 1, vf) : 0;

                        GuiV2::DrawText(TOP, menuX + 27 + off, y,
                                        Trim(it.label, kMenuW - 33 - vw,
                                             g_buf, sizeof(g_buf)), col);
                        if (g_buf2[0] != '\0')
                            GuiV2::DrawText(TOP, menuX + kMenuW - 8 - vw + off, y,
                                            g_buf2, col, 1, vf);
                        if (it.type != ITEM_FOLDER && it.hotkey != 0)
                            GuiV2::DrawText(TOP, menuX + 147 + off, y + 8,
                                            "H", kColHotkey);
                        i++;
                    }
                }
                ScrollBar(TOP, menuX + 154, 28, 176, kVisibleRows, fr.count, start, 1.0f);

                // ---- 下帯 ----
                GuiV2::FillRect(TOP, menuX, 216, kMenuW - 2, 24, kColFoot);
                GuiV2::DrawText(TOP, menuX + 6, 220, u8"A決定 X適用 Y HOTKEY", kColFootTxt);
                std::snprintf(g_buf, sizeof(g_buf), u8"%d変更", changed);
                GuiV2::DrawText(TOP, menuX + 6, 230, g_buf,
                                changed ? kColDirty : kColFootOff);
                GuiV2::DrawText(TOP, menuX + 72, 230, u8"B戻る L変更戻し", kColFootTxt);

                if (g_inline.active)
                    DrawInlineList(menuX, start, now);
                DrawDescription(amount);
                if (g_overlay.active && g_overlay.screen == 0)
                    DrawScreenListbox(TOP,
                                      (int)(Mix(98.0f, 178.0f, amount) + 0.5f),
                                      204, 400, now);
                if (g_dialog.type != DLG_NONE)
                    DrawDialog(now);
                DrawNotices(now);
            }

            void    BuildBottom(u32 now)
            {
                GuiV2::Begin(BOT);

                // ★暗幕は GuiV2 の「下画面ロック」が出す（F-350）。
                //   ここで自前に描かない。ロックの ON/OFF は開閉のときに切り替える。
                //   ロックが立っている間はゲーム側のタッチも遮断される。
                GuiV2::DrawBottomDim(now);

                if (g_overlay.active && g_overlay.screen == 1)
                    DrawScreenListbox(BOT, 52, 216, 320, now);
                else if (g_capture.active)
                    DrawHotkeyCapture();
                else if (GuiKeyboard::Active())
                    GuiKeyboard::Draw(now);
            }

            // ================================================================
            // 入力（ui-model.js の handle / moveSelection の写し）
            // ================================================================
            void    ResetSelectionAnimation(u32 now)
            {
                const Frame &fr = Cur();

                g_selFrom = (float)fr.selection;
                g_selTo = (float)fr.selection;
                g_selStart = now;
                g_viewFrom = ScrollTarget(fr.selection, fr.count, kVisibleRows);
                g_viewTarget = g_viewFrom;
                g_viewStart = now;
                g_edgeBlocked = 0;
            }

            bool    MoveSelection(int dir, u32 now, bool allowWrap)
            {
                Frame      &fr = Cur();
                const int   candidate = fr.selection + dir;

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
                g_edgeBlocked = 0;

                const int next = (candidate + fr.count) % fr.count;

                g_viewFrom = ViewportStart(now);
                g_viewTarget = ScrollTarget(next, fr.count, kVisibleRows);
                g_viewStart = now;
                g_selFrom = SelectionPosition(now);
                g_selTo = (float)next;
                g_selStart = now;
                fr.selection = next;
                return true;
            }

            void    AnimateOpen(float target, u32 now)
            {
                g_openFrom = OpenAmount(now);
                g_openTarget = target;
                g_openStart = now;
                if (target == 1.0f)
                    g_visible = true;
            }

            void    OpenInlineList(Item &it, int index, u32 now)
            {
                const float s = ScrollTarget(index, (int)it.optionCount, kInlineRows);

                g_inline.active = true;
                g_inline.item = (int)(&it - &g_items[0]);
                g_inline.index = index;
                g_inline.closing = false;
                g_inline.animFrom = 0.0f;
                g_inline.animTarget = 1.0f;
                g_inline.animStart = now;
                g_inline.animMs = kListAnimMs;
                g_inline.scrollFrom = s;
                g_inline.scrollTarget = s;
                g_inline.scrollStart = now;
            }

            void    CloseInlineList(u32 now)
            {
                if (!g_inline.active || g_inline.closing)
                    return;

                const float cur = InlineAmount(now);

                g_inline.animFrom = cur;
                g_inline.animTarget = 0.0f;
                g_inline.animStart = now;
                g_inline.animMs = (int)((float)kListAnimMs * cur);
                g_inline.closing = true;
            }

            void    MoveInline(int dir, u32 now, bool allowWrap)
            {
                const Item &it = g_items[g_inline.item];
                const int   count = (int)it.optionCount;
                const int   next = g_inline.index + dir;

                if (!allowWrap && (next < 0 || next >= count))
                    return;
                g_inline.scrollFrom = InlineScroll(now);
                g_inline.index = (next + count) % count;
                g_inline.scrollTarget = ScrollTarget(g_inline.index, count, kInlineRows);
                g_inline.scrollStart = now;
            }

            // ================================================================
            // 画面リストボックス（段 6。ui-model.js の overlay[type=listbox]）
            // ================================================================
            float   OverlayAmount(u32 now)
            {
                if (!g_overlay.active)
                    return 0.0f;
                return Mix(g_overlay.animFrom, g_overlay.animTarget,
                           EaseOutCubic(Progress(g_overlay.animStart, now,
                                                 g_overlay.animMs)));
            }

            float   OverlayScroll(u32 now)
            {
                return Mix(g_overlay.scrollFrom, g_overlay.scrollTarget,
                           EaseOutCubic(Progress(g_overlay.scrollStart, now,
                                                 kListScrollMs)));
            }

            void    OpenOverlay(int screen, const char *title,
                                const char *const *options, int count,
                                int mode, int item, int selected, u32 now)
            {
                const float s = ScrollTarget(selected, count, kScreenRows);

                g_overlay.active = true;
                g_overlay.screen = (u8)screen;
                g_overlay.mode = (u8)mode;
                g_overlay.item = item;
                g_overlay.title = title;
                g_overlay.options = options;
                g_overlay.optionCount = count;
                g_overlay.index = ClampI(selected, 0, count - 1);
                g_overlay.closing = false;
                g_overlay.animFrom = 0.0f;
                g_overlay.animTarget = 1.0f;
                g_overlay.animStart = now;
                g_overlay.animMs = kListAnimMs;
                g_overlay.scrollFrom = s;
                g_overlay.scrollTarget = s;
                g_overlay.scrollStart = now;
            }

            void    CloseOverlay(u32 now)
            {
                if (!g_overlay.active || g_overlay.closing)
                    return;

                const float cur = OverlayAmount(now);

                g_overlay.animFrom = cur;
                g_overlay.animTarget = 0.0f;
                g_overlay.animStart = now;
                g_overlay.animMs = (int)((float)kListAnimMs * cur);
                g_overlay.closing = true;
            }

            void    MoveOverlay(int dir, u32 now, bool allowWrap)
            {
                const int   count = g_overlay.optionCount;
                const int   next = g_overlay.index + dir;

                if (!allowWrap && (next < 0 || next >= count))
                    return;
                g_overlay.scrollFrom = OverlayScroll(now);
                g_overlay.index = (next + count) % count;
                g_overlay.scrollTarget = ScrollTarget(g_overlay.index, count,
                                                      kScreenRows);
                g_overlay.scrollStart = now;
            }

            // handleOverlay の listbox 分岐の写し。
            // ★通知の題は発火させたチート名（親項目の label）に統一する。
            void    ConfirmOverlay(u32 now)
            {
                const char *title = g_overlay.item >= 0
                                    ? g_items[g_overlay.item].label
                                    : g_overlay.title;

                if (g_overlay.mode == OV_HOTKEY_ITEM && g_overlay.item >= 0)
                {
                    Item &it = g_items[g_overlay.item];

                    it.value = g_overlay.index;
                    it.applied = it.value;
                }
                std::snprintf(g_buf, sizeof(g_buf), "%s%s",
                              g_overlay.options[g_overlay.index],
                              u8"を選択しました");
                AddNotice(title, g_buf, now);
                CloseOverlay(now);
            }

            // ================================================================
            // 通知（段 7。ui-model.js の NotificationTimeline の写し）
            // ================================================================
            float   NoticeX(const Notice &nt, u32 now)
            {
                const float targetX = (float)(400 - kNoticeMargin - kNoticeW);
                const s32   age = (s32)(now - nt.createdAt);

                if (age < 0)
                    return 400.0f;
                if (age < kNoticeEnterMs)
                    return Mix(400.0f, targetX,
                               EaseOutCubic((float)age / (float)kNoticeEnterMs));
                if (age < kNoticeEnterMs + kNoticeHoldMs)
                    return targetX;
                return Mix(targetX, 400.0f,
                           EaseOutCubic((float)(age - kNoticeEnterMs - kNoticeHoldMs)
                                        / (float)kNoticeExitMs));
            }

            float   NoticeY(const Notice &nt, u32 now)
            {
                return Mix(nt.moveFromY, nt.targetY,
                           EaseOutCubic(Progress(nt.moveStartedAt, now,
                                                 kNoticeEnterMs)));
            }

            bool    NoticeAlive(const Notice &nt, u32 now)
            {
                return nt.active
                       && (s32)(now - nt.createdAt)
                          < kNoticeEnterMs + kNoticeHoldMs + kNoticeExitMs;
            }

            bool    NoticesAlive(u32 now)
            {
                int i = 0;

                while (i < kNoticeMax)
                {
                    if (NoticeAlive(g_notices[i], now))
                        return true;
                    i++;
                }
                return false;
            }

            void    AddNotice(const char *title, const char *msg, u32 now,
                              bool red)
            {
                int     i = 0;
                u32     at = now;

                // 死んだ分を掃く
                while (i < kNoticeMax)
                {
                    if (!NoticeAlive(g_notices[i], now))
                        g_notices[i].active = false;
                    i++;
                }
                if (g_noticeEver && at < g_noticeLastAt + (u32)kNoticeSpawnMs)
                    at = g_noticeLastAt + (u32)kNoticeSpawnMs;
                g_noticeEver = true;
                g_noticeLastAt = at;
                // 満杯なら一番古いものを押し出す
                {
                    bool full = true;

                    i = 0;
                    while (i < kNoticeMax)
                    {
                        if (!g_notices[i].active)
                        {
                            full = false;
                            break;
                        }
                        i++;
                    }
                    if (full)
                    {
                        int k = 1;

                        while (k < kNoticeMax)
                        {
                            g_notices[k - 1] = g_notices[k];
                            k++;
                        }
                        g_notices[kNoticeMax - 1].active = false;
                    }
                }
                // 既存を押し上げる
                i = 0;
                while (i < kNoticeMax)
                {
                    Notice &ex = g_notices[i];

                    if (ex.active)
                    {
                        ex.moveFromY = NoticeY(ex, at);
                        ex.targetY -= (float)(kNoticeH + kNoticeGap);
                        ex.moveStartedAt = at;
                    }
                    i++;
                }
                // 空きへ積む
                i = 0;
                while (i < kNoticeMax)
                {
                    Notice &nt = g_notices[i];

                    if (!nt.active)
                    {
                        nt.active = true;
                        nt.id = g_noticeNextId++;
                        std::snprintf(nt.title, sizeof(nt.title), "%s", title);
                        std::snprintf(nt.msg, sizeof(nt.msg), "%s", msg);
                        nt.accent = red ? kColDanger : kColAccent;
                        nt.border = red ? kColDangerEdge : kColPanelEdge;
                        nt.createdAt = at;
                        nt.moveFromY = (float)(240 - kNoticeMargin - kNoticeH);
                        nt.targetY = nt.moveFromY;
                        nt.moveStartedAt = at;
                        break;
                    }
                    i++;
                }
                if (g_noticeNextId > 999)
                    g_noticeNextId = 1;
            }

            void    OpenDialog(u8 type, u32 now)
            {
                if (g_dialog.type != DLG_NONE)
                    return;
                g_dialog.type = type;
                g_dialog.index = 0;
                g_dialog.closing = false;
                g_dialog.confirmed = false;
                g_dialog.animFrom = 0.0f;
                g_dialog.animTarget = 1.0f;
                g_dialog.animStart = now;
                g_dialog.animMs = kDialogMs;
            }

            void    ChooseDialog(bool confirm, u32 now)
            {
                if (g_dialog.type == DLG_NONE || g_dialog.closing)
                    return;

                const float cur = DialogAmount(now);

                g_dialog.confirmed = confirm;
                g_dialog.animFrom = cur;
                g_dialog.animTarget = 0.0f;
                g_dialog.animStart = now;
                g_dialog.animMs = (int)((float)kDialogMs * cur);
                g_dialog.closing = true;
            }

            void    ApplyItem(Item &it, u32 now)
            {
                if (!IsDirtyItem(it))
                    return;
                const int   oldApplied = it.applied;

                it.applied = it.value;
                it.appliedHotkey = it.hotkey;
                // ★要望 R6b。チェックの適用では通知しない（有効・無効とも）。
                //   オンオフとその通知は関数側（ホットキー実行側）が持つ。
                //   語尾定数（kMsgEnabledSuffix 等）は関数側が使うために残す。
                // ★メニュー判定ONの項目だけ、適用値の反転をホットキー判定と
                //   同じ反転評価へ送る（値だけの変更・ホットキーだけの変更では送らない）。
                if (oldApplied != it.applied)
                {
                    const int idx = (int)(&it - &g_items[0]);

                    if (idx >= 0 && idx < kMaxItems && g_fxMenuJudge[idx])
                        ToggleEvaluate(idx, now);
                }
                (void)now;
            }

            void    ApplyAll(u32 now)
            {
                int i = 0;

                while (i < g_itemCount)
                {
                    ApplyItem(g_items[i], now);
                    i++;
                }
            }

            void    DiscardAll(void)
            {
                int i = 0;

                while (i < g_itemCount)
                {
                    if (IsDirtyItem(g_items[i]))
                    {
                        g_items[i].value = g_items[i].applied;
                        g_items[i].hotkey = g_items[i].appliedHotkey;
                    }
                    i++;
                }
            }

            void    ChangeValue(Item &it, int dir, u32 now)
            {
                it.value = ClampI(it.value + dir * it.step, it.minimum, it.maximum);
                g_valueDir = dir;
                g_valueAt = now;
                g_valueOn = true;
            }

            // runAction の写し。文字キーボード系は移植しないので何もしない。
            // ★通知の題は発火させたチート名に統一する（項目の label）。
            void    OpenNumber(Item &it, bool apply, u32 now)
            {
                GuiKeyboard::Format fmt = it.fmt == FMT_FLOAT ? GuiKeyboard::FLOAT_TENTHS
                    : it.fmt == FMT_HEX ? GuiKeyboard::HEXADECIMAL : GuiKeyboard::DECIMAL;
                GuiKeyboard::OpenNumber((int)(&it - g_items), it.value,
                    it.minimum, it.maximum, fmt, apply, now);
            }

            void    RunAction(Item &it, u32 now)
            {
                const int   self = (int)(&it - &g_items[0]);

                if (it.action == ACT_TEXT || it.action == ACT_COMPACT_TEXT)
                    GuiKeyboard::OpenText(it.action == ACT_COMPACT_TEXT, now);
                else if (it.action == ACT_SAVE)
                    AddNotice(it.label, u8"セーブ関数を呼び出しました", now);
                else if (it.action == ACT_TOP_LIST)
                    OpenOverlay(0, u8"上画面リスト", g_longOpts, kLongList,
                                OV_GENERIC, self, 0, now);
                else if (it.action == ACT_BOTTOM_LIST)
                    OpenOverlay(1, u8"下画面リスト", g_longOpts, kLongList,
                                OV_GENERIC, self, 0, now);
            }

            void    OpenCapture(int item, u32 now)
            {
                g_capture.active = true;
                g_capture.phase = 0;
                g_capture.item = item;
                g_capture.captured = 0;
                (void)now;
            }

            // 入力待ちのボタン処理。A=決定 / B=取消 / X=無効。
            // A/B/X 自体は記録しない（Simulator との差。手段を残すため）。
            void    HandleCapture(bool a, bool b, bool x, u32 now)
            {
                (void)now;
                if (g_capture.phase == 0)
                    return;
                if (b)
                {
                    g_capture.active = false;       // 取消（古いまま）
                    g_needFinal = true;
                    return;
                }
                if (x)
                {
                    g_items[g_capture.item].hotkey = 0; // 無効＝なしにする
                    g_capture.active = false;
                    g_needFinal = true;
                    return;
                }
                if (g_capture.phase == 1)
                {
                    const u16 held = HeldHotkeyMask();
                    int i = 0;

                    while (i < HB_COUNT)
                    {
                        if ((held & (u16)(1u << i)) != 0
                            && ((kHotkeyRecordable & (u16)(1u << i)) != 0))
                        {
                            g_capture.captured = (u16)(g_capture.captured
                                                       | (u16)(1u << i));
                            g_capture.phase = 2;
                        }
                        i++;
                    }
                    return;
                }
                // phase == 2
                {
                    const u16 held = HeldHotkeyMask() & kHotkeyRecordable;

                    g_capture.captured = (u16)(g_capture.captured | held);
                    if (a && g_capture.captured != 0)
                    {
                        g_items[g_capture.item].hotkey = g_capture.captured;
                        g_capture.active = false;
                        g_needFinal = true;
                    }
                }
            }

            // activateHotkeyItem の写し。メニューを閉じているときの発動用。
            // value/slider は下画面 UI を移植しないので +1 して即適用する。
            // 読取検証で反転する。残っていたら外して無効通知、
            // そうでなければ付けて有効通知（題はチート名）。
            // ★既定値の自動保持はしない（禁止）。外す値は常に関数側の
            //   明示値（SetActive 内で書くもの）である。
            // ホットキー押下とメニューON/OFF（判定ONの項目だけ）の共通路。
            void    ToggleEvaluate(int index, u32 now)
            {
                if (index < 0 || index >= g_itemCount)
                    return;
                if (!g_fx[index].used)
                    return;
                Item &it = g_items[index];

                if (it.type != ITEM_CHECKBOX)
                    return;
                const FxDesc &fx = g_fx[index];
                const bool  active = fx.IsActive(index);

                fx.SetActive(index, !active);
                if (active)
                {
                    std::snprintf(g_buf2, sizeof(g_buf2), "%s%s", it.label,
                                  kMsgDisabledSuffix);
                    AddNotice(it.label, g_buf2, now, true);
                }
                else
                {
                    std::snprintf(g_buf2, sizeof(g_buf2), "%s%s", it.label,
                                  kMsgEnabledSuffix);
                    AddNotice(it.label, g_buf2, now, false);
                }
            }

            void    FireHotkeyItem(Item &it, u32 now)
            {
                if (it.type == ITEM_CHECKBOX)
                {
                    // ★メニューは有効無効を切り替えない。
                    //   有効中の押下だけエンジン（またはハンドラ）へ渡す。
                    //   無効中の押下は何もしない。未登録の効果も何もしない。
                    //   メニュー開閉中の押下は TriggerHotkeys 自体が走らない。
                    if (it.applied == 0)
                        return;
                    const int idx = (int)(&it - &g_items[0]);

                    if (g_hotHandler != nullptr)
                    {
                        g_hotHandler(idx);
                        return;
                    }
                    ToggleEvaluate(idx, now);
                }
                else if (it.type == ITEM_ACTION)
                    RunAction(it, now);
                else if (it.type == ITEM_LIST)
                {
                    OpenOverlay(0, it.label, it.options, (int)it.optionCount,
                                OV_HOTKEY_ITEM, (int)(&it - &g_items[0]),
                                it.value, now);
                }
                else if (it.type == ITEM_VALUE || it.type == ITEM_SLIDER)
                {
                    OpenNumber(it, true, now);
                }
            }

            void    TriggerHotkeys(u32 now)
            {
                const u16   held = HeldHotkeyMask();
                int         i;

                if (held == 0)
                    return;
                // 先に離れたものを外す（押し直すとまた動く）
                i = 0;
                while (i < g_itemCount)
                {
                    if (g_hotActive[i] && g_items[i].appliedHotkey != held)
                        g_hotActive[i] = false;
                    i++;
                }
                i = 0;
                while (i < g_itemCount)
                {
                    Item &it = g_items[i];

                    if (it.type != ITEM_FOLDER && !g_hotActive[i]
                        && it.appliedHotkey != 0 && it.appliedHotkey == held)
                    {
                        g_hotActive[i] = true;
                        FireHotkeyItem(it, now);
                    }
                    i++;
                }
            }

            void    ReleaseHotkeys(void)
            {
                const u16   held = HeldHotkeyMask();
                int         i = 0;

                while (i < g_itemCount)
                {
                    if (g_hotActive[i] && g_items[i].appliedHotkey != held)
                        g_hotActive[i] = false;
                    i++;
                }
            }

            void    ActivateSelected(u32 now)
            {
                Item &it = Sel();

                g_actAt = now;
                g_actOn = true;
                if (it.type == ITEM_FOLDER)
                {
                    if (g_depth >= kMaxDepth)
                        return;
                    g_frames[g_depth].title = it.label;
                    g_frames[g_depth].first = (int)it.childFirst;
                    g_frames[g_depth].count = (int)it.childCount;
                    g_frames[g_depth].selection = 0;
                    g_depth++;
                    ResetSelectionAnimation(now);
                }
                else if (it.type == ITEM_CHECKBOX)
                    it.value = it.value ? 0 : 1;
                else if (it.type == ITEM_ACTION)
                    RunAction(it, now);
                else if (it.type == ITEM_LIST)
                    OpenInlineList(it, it.value, now);
                else if (it.type == ITEM_VALUE || it.type == ITEM_SLIDER)
                    OpenNumber(it, false, now);
            }

            void    BeginClose(u32 now)
            {
                g_dialog.type = DLG_NONE;
                AnimateOpen(0.0f, now);
                g_open = false;
            }

            void    RequestClose(u32 now)
            {
                CloseInlineList(now);
                CloseOverlay(now);
                GuiKeyboard::Cancel(now);
                if (DirtyCount() > 0)
                {
                    OpenDialog(DLG_CLOSE, now);
                    return;
                }
                BeginClose(now);
            }

            // ★入力を 1 回だけ読む。以降はこのフレームの写しだけを見る。
            //   `GetKeysDown(true)` = `_keysDown | _keysHeld` は**水準**なので、
            //   CTRPF の更新周期とこちらの 16ms がずれても値が壊れない。
            void    SampleInput(void)
            {
                const u32   cur = Controller::GetKeysDown(true);

                g_keysHit = cur & ~g_keysPrev;
                g_keysOff = ~cur & g_keysPrev;
                g_keysPrev = cur;
                g_keysNow = cur;
            }

            bool    Down(u32 key)       { return (g_keysNow & key) != 0; }
            bool    Hit(u32 key)        { return (g_keysHit & key) != 0; }
            bool    Off(u32 key)        { return (g_keysOff & key) != 0; }

            // 押した瞬間 + 長押しの連続入力。スライドパッドは見ない。
            //   戻り値: 0 = 反応なし / 1 = 押した瞬間 / 2 = 連続入力
            //   ★連続入力の間隔は「前回から 60ms」ではなく「いまから 60ms」。
            //     16ms 刻みで見ているので実効 64ms。二重発火より取りこぼしの無さを取る。
            int     Repeat(int which, u32 now)
            {
                const u32   key = kRepeatKeys[which];

                if (!Down(key))
                {
                    g_repHeld[which] = false;
                    return 0;
                }
                if (!g_repHeld[which])
                {
                    g_repHeld[which] = true;
                    g_repAt[which] = now + (u32)kRepeatDelay;
                    return 1;
                }
                if ((s32)(now - g_repAt[which]) >= 0)
                {
                    g_repAt[which] = now + (u32)kRepeatEvery;
                    return 2;
                }
                return 0;
            }

            void    Update(u32 now)
            {
                const bool keyboardWasActive = GuiKeyboard::Active();
                GuiKeyboard::Update(now);
                if (keyboardWasActive && !GuiKeyboard::Active()) g_needFinal = true;
                if (g_pendHave)
                {
                    g_pendHave = false;
                    AddNotice(g_pendTitle, g_pendMsg, now, g_pendRed);
                }
                // トグル実行の駆動。有効状態の遷移で両端を1回ずつ、
                // 有効な間は毎フレーム呼ぶ（メニューの開閉によらない）。
                // ★開いている間の held は 0 にする。メニュー操作のボタンが
                //   関数側のホットキー判定へ漏れないようにする。
                {
                    const u16   held = (g_visible || GuiKeyboard::Active()) ? 0 : HeldHotkeyMask();
                    int         i = 0;

                    while (i < g_itemCount)
                    {
                        if (g_items[i].type == ITEM_CHECKBOX)
                        {
                            const bool on = g_items[i].applied != 0;

                            if (on != g_toggleState[i])
                            {
                                g_toggleState[i] = on;
                                if (on)
                                {
                                    if (g_toggleHdl.OnEnable != nullptr)
                                        g_toggleHdl.OnEnable(i);
                                }
                                else if (g_toggleHdl.OnDisable != nullptr)
                                    g_toggleHdl.OnDisable(i);
                            }
                            if (on && g_toggleHdl.OnTick != nullptr)
                                g_toggleHdl.OnTick(i, held);
                        }
                        i++;
                    }
                }
                if (g_openTarget == 0.0f && OpenAmount(now) <= 0.001f && g_visible)
                {
                    g_visible = false;
                    // ★閉じ切ったら 1 回だけ空で Commit する。
                    //   これをしないと最後の列が毎フレーム再生され続ける。
                    g_needFinal = true;
                }
                if (g_inline.active && g_inline.closing
                    && now - g_inline.animStart >= (u32)g_inline.animMs)
                    g_inline.active = false;
                if (g_dialog.type != DLG_NONE && g_dialog.closing
                    && now - g_dialog.animStart >= (u32)g_dialog.animMs)
                {
                    const u8    type = g_dialog.type;
                    const bool  confirmed = g_dialog.confirmed;

                    g_dialog.type = DLG_NONE;
                    if (confirmed)
                    {
                        if (type == DLG_CLOSE)
                        {
                            ApplyAll(now);
                            BeginClose(now);
                        }
                        else
                            DiscardAll();
                    }
                    g_needFinal = true;
                }
                if (g_overlay.active && g_overlay.closing
                    && now - g_overlay.animStart >= (u32)g_overlay.animMs)
                {
                    g_overlay.active = false;
                    g_needFinal = true;
                }
                // 入力待ち：離したら次へ進む
                if (g_capture.active)
                {
                    if (g_capture.phase == 0 && HeldHotkeyMask() == 0)
                        g_capture.phase = 1;
                    else if (g_capture.phase == 2 && HeldHotkeyMask() == 0)
                    {
                        g_items[g_capture.item].hotkey = g_capture.captured;
                        g_capture.active = false;
                        g_needFinal = true;
                    }
                }
                // 通知の寿命。最後の 1 件が消えたら空で 1 回描く。
                {
                    const bool alive = NoticesAlive(now);
                    int i = 0;

                    while (i < kNoticeMax)
                    {
                        if (!NoticeAlive(g_notices[i], now))
                            g_notices[i].active = false;
                        i++;
                    }
                    if (g_noticesAlive && !alive)
                        g_needFinal = true;
                    g_noticesAlive = alive;
                }
            }

            void    HandleInput(u32 now)
            {
                const int   up = Repeat(REP_UP, now);
                const int   down = Repeat(REP_DOWN, now);
                const int   left = Repeat(REP_LEFT, now);
                const int   right = Repeat(REP_RIGHT, now);
                const bool  a = Hit((u32)Key::A);
                const bool  b = Hit((u32)Key::B);
                const bool  x = Hit((u32)Key::X);
                const bool  l = Hit((u32)Key::L);
                const bool  y = Hit((u32)Key::Y);

                // 端で止まった向きの記憶は、その向きを離したときに消す
                if (Off((u32)Key::DPadUp) || Off((u32)Key::DPadDown))
                    g_edgeBlocked = 0;

                if (GuiKeyboard::Active())
                {
                    const UIntVector pos = Touch::GetPosition();
                    GuiKeyboard::Handle(right ? 1 : left ? -1 : 0,
                        down ? 1 : up ? -1 : 0, a, b, Touch::IsDown(), pos.x, pos.y, now);
                    GuiKeyboard::Result result;
                    if (GuiKeyboard::TakeResult(result))
                    {
                        if (result.kind == GuiKeyboard::NUMBER && result.item >= 0 && result.item < g_itemCount)
                        {
                            Item &item = g_items[result.item];
                            item.value = result.value;
                            if (result.apply) item.applied = item.value;
                        }
                        else if (result.kind == GuiKeyboard::TEXT)
                            AddNotice("ACTION", u8"名前変更を呼び出しました", now);
                    }
                    g_needFinal = true;
                    return;
                }

                // ---- 入力待ち（ui-model.js の hotkey-capture）----
                if (g_capture.active)
                {
                    HandleCapture(a, b, x, now);
                    return;
                }

                // ---- ダイアログが最優先 ----
                if (g_dialog.type != DLG_NONE)
                {
                    if (g_dialog.closing)
                        return;
                    if (up == 1)
                        g_dialog.index = (g_dialog.index + 1) % 2;
                    else if (down == 1)
                        g_dialog.index = (g_dialog.index + 1) % 2;
                    else if (a)
                        ChooseDialog(g_dialog.index == 0, now);
                    else if (b)
                        ChooseDialog(false, now);
                    return;
                }

                // ---- 画面リストボックス（メニューが閉じていても動く）----
                if (g_overlay.active)
                {
                    if (g_overlay.closing)
                        return;
                    if (up)
                        MoveOverlay(-1, now, up == 1);
                    else if (down)
                        MoveOverlay(1, now, down == 1);
                    else if (a)
                        ConfirmOverlay(now);
                    else if (b)
                        CloseOverlay(now);
                    return;
                }
                if (!g_visible)
                {
                    // ★閉じているときはホットキー発動だけ見る
                    //   （ui-model.js の handle と同じ条件）。
                    //   離しただけのフレームでも記憶を外す。
                    ReleaseHotkeys();
                    if (g_keysHit != 0)
                        TriggerHotkeys(now);
                    return;
                }

                // ---- インラインリスト ----
                if (g_inline.active)
                {
                    if (g_inline.closing)
                        return;
                    if (up)
                        MoveInline(-1, now, up == 1);
                    else if (down)
                        MoveInline(1, now, down == 1);
                    else if (a)
                    {
                        g_items[g_inline.item].value = g_inline.index;
                        CloseInlineList(now);
                    }
                    else if (b)
                        CloseInlineList(now);
                    return;
                }

                // ---- メニュー本体 ----
                if (up)
                    MoveSelection(-1, now, up == 1);
                else if (down)
                    MoveSelection(1, now, down == 1);
                else if (a)
                    ActivateSelected(now);
                else if (b)
                {
                    if (g_depth > 1)
                    {
                        g_depth--;
                        ResetSelectionAnimation(now);
                    }
                    else
                        RequestClose(now);
                }
                else if (x)
                    ApplyItem(Sel(), now);
                else if (l && DirtyCount() > 0)
                    OpenDialog(DLG_REVERT, now);
                else if (y && Sel().type != ITEM_FOLDER)
                    OpenCapture((int)(&Sel() - &g_items[0]), now);
                else if ((left || right) && IsNumericItem(Sel()))
                    ChangeValue(Sel(), right ? 1 : -1, now);
            }

            void    ThreadMain(void *)
            {
                while (g_run)
                {
                    if (GuiV2::IsReady())
                    {
                        const u32 now = NowMs();

                        SampleInput();      // ★1 フレームに 1 回だけ読む
                        Update(now);
                        HandleInput(now);
                        if (g_visible || g_openTarget != 0.0f || Animating(now)
                            || g_needFinal)
                        {
                            Redraw();
                            g_needFinal = false;
                        }
                    }
                    svcSleepThread(16000000LL);     // 約 16ms
                }
            }
        }

        // ====================================================================
        // 公開部
        // ====================================================================
        // ★入力ロックは**毎フレーム条件から決める**（F-350）。
        //   開閉のたびに呼ぶ形だと、経路が増えたときに取りこぼす。
        void    SyncInputLock(void)
        {
            // ボタン遮断: 自前メニューを開いている間だけ。
            //   （スライドパッドは常に生きる。SELECT はケーブが常時落とす）
            GuiV2::SetButtonBlock(g_visible);

            // 下画面ロック: 下画面に操作対象が出ている間。
            //   ★暗幕はこのロックが出す。各所で自前に描かない。
            const bool  listbox = (g_overlay.active && g_overlay.screen == 1
                                   && !g_overlay.closing);
            const bool  capture = g_capture.active;
            const bool  keyboard = GuiKeyboard::Active();

            if (keyboard)
                GuiV2::SetBottomLock(true, GuiKeyboard::DimColor(),
                                     GuiKeyboard::DimFadeMs());
            else if (listbox || capture)
                GuiV2::SetBottomLock(true, kColDim, kListAnimMs);
            else
                GuiV2::SetBottomLock(false);
        }

        void    Redraw(void)
        {
            const u32 now = NowMs();

            if (!GuiV2::IsReady())
                return;
            SyncInputLock();
            BuildTop(now);
            BuildBottom(now);
            GuiV2::Commit();
        }

        bool    Initialize(void)
        {
            const u32 now = NowMs();

            if (g_thread != nullptr)
                return true;
            BuildTree();
            GuiKeyboard::Reset();
            WireTestEffects();
            std::memset(&g_inline, 0, sizeof(g_inline));
            std::memset(&g_dialog, 0, sizeof(g_dialog));
            std::memset(&g_overlay, 0, sizeof(g_overlay));
            std::memset(&g_capture, 0, sizeof(g_capture));
            std::memset(g_notices, 0, sizeof(g_notices));
            std::memset(g_hotActive, 0, sizeof(g_hotActive));
            std::memset(&g_toggleHdl, 0, sizeof(g_toggleHdl));
            std::memset(g_repHeld, 0, sizeof(g_repHeld));
            // ★起動時点で有効な項目で OnEnable が誤発しないよう現状態を入れる。
            {
                int i = 0;

                while (i < kMaxItems)
                {
                    g_toggleState[i] = (i < g_itemCount
                                        && g_items[i].type == ITEM_CHECKBOX
                                        && g_items[i].applied != 0);
                    i++;
                }
            }
            g_noticeNextId = 1;
            g_noticeEver = false;
            g_noticesAlive = false;
            // ★開いた瞬間に押されているボタン（Select など）を
            //   立ち上がりとして拾わないよう、いまの状態を前回として覚える。
            g_keysPrev = Controller::GetKeysDown(true);
            g_keysNow = g_keysPrev;
            g_keysHit = 0;
            g_keysOff = 0;
            g_run = true;
            g_open = true;
            g_visible = true;
            g_openFrom = 0.0f;
            g_openTarget = 1.0f;
            g_openStart = now;
            ResetSelectionAnimation(now);
            Redraw();
            g_thread = threadCreate(ThreadMain, nullptr, 0x2000, 0x30, -2, true);
            return g_thread != nullptr;
        }

        void    Shutdown(void)
        {
            if (g_thread == nullptr)
                return;
            g_run = false;
            svcSleepThread(50000000LL);
            g_thread = nullptr;
            g_open = false;
            g_visible = false;
        }

        bool    OnOpening(void)
        {
            // ★十字上を一緒に押していれば従来の CTRPF メニュー。
            if (Controller::IsKeyDown(Key::DPadUp))
                return true;
            if (!GuiV2::IsReady())
                return true;
            // ★入力待ちの最中は Select を記録用に残す。
            //   ここで閉じてしまうと SELECT を含む組み合わせが取れない。
            if (g_capture.active)
                return false;
            if (GuiKeyboard::Active())
            {
                GuiKeyboard::Cancel(NowMs());
                g_needFinal = true;
                return false;
            }

            if (g_open)
                Close();
            else
                Open();
            return false;
        }

        bool    IsOpen(void)    { return g_open; }

        // 通知を積む（段 7。別スレッドから呼んでもよい）。
        // 中身のコピーが終わってから有効化する。
        void    Notify(const char *title, const char *message)
        {
            if (g_thread == nullptr)
                return;
            std::snprintf(g_pendTitle, sizeof(g_pendTitle), "%s", title);
            std::snprintf(g_pendMsg, sizeof(g_pendMsg), "%s", message);
            g_pendRed = false;
            g_pendHave = true;
        }

        // 赤い通知を積む（関数側の無効通知など）。別スレッドから呼べる。
        void    NotifyRed(const char *title, const char *message)
        {
            if (g_thread == nullptr)
                return;
            std::snprintf(g_pendTitle, sizeof(g_pendTitle), "%s", title);
            std::snprintf(g_pendMsg, sizeof(g_pendMsg), "%s", message);
            g_pendRed = true;
            g_pendHave = true;
        }

        // ---- 関数側連携 ----
        void    SetHotkeyHandler(HotkeyHandler handler)
        {
            g_hotHandler = handler;
        }

        bool    RegisterToggleEffect(int index, const ToggleEffectFuncs *funcs)
        {
            if (index < 0 || index >= kMaxItems || funcs == nullptr)
                return false;
            if (funcs->IsActive == nullptr || funcs->SetActive == nullptr)
                return false;
            g_fx[index].used = true;
            g_fx[index].IsActive = funcs->IsActive;
            g_fx[index].SetActive = funcs->SetActive;
            return true;
        }

        void    UnregisterToggleEffect(int index)
        {
            if (index < 0 || index >= kMaxItems)
                return;
            g_fx[index].used = false;
            g_fx[index].IsActive = nullptr;
            g_fx[index].SetActive = nullptr;
        }

        void    SetEffectMenuJudgment(int index, bool send)
        {
            if (index < 0 || index >= kMaxItems)
                return;
            g_fxMenuJudge[index] = send;
        }

        bool    ToggleEffectActive(int index)
        {
            if (index < 0 || index >= kMaxItems || !g_fx[index].used)
                return false;
            if (g_fx[index].IsActive == nullptr)
                return false;
            return g_fx[index].IsActive(index);
        }

        void    FlushMemory(u32 address, u32 size)
        {
            if (address == 0 || size == 0)
                return;
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, address, size);
            svcInvalidateEntireInstructionCache();
        }

        static u32 PatchMask(int size)
        {
            return size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
        }

        static u32 PatchRead(u32 address, int size)
        {
            if (size == 1)
                return *(volatile u8 *)address;
            if (size == 2)
                return *(volatile u16 *)address;
            return *(volatile u32 *)address;
        }

        static void PatchWrite(u32 address, int size, u32 value)
        {
            if (size == 1)
                *(volatile u8 *)address = (u8)value;
            else if (size == 2)
                *(volatile u16 *)address = (u16)value;
            else
                *(volatile u32 *)address = value;
        }

        static bool PatchValid(const TogglePatch *p)
        {
            return p != nullptr && p->address != 0
                   && (p->size == 1 || p->size == 2 || p->size == 4);
        }

        bool    PatchListIsActive(const TogglePatch *patches, int count)
        {
            int i = 0;

            if (patches == nullptr || count <= 0)
                return false;
            while (i < count)
            {
                const TogglePatch *p = &patches[i];

                if (!PatchValid(p))
                    return false;
                if ((PatchRead(p->address, (int)p->size)
                     & PatchMask((int)p->size))
                    != (p->onValue & PatchMask((int)p->size)))
                    return false;
                i++;
            }
            return true;
        }

        void    PatchListSetActive(const TogglePatch *patches, int count,
                                   bool active)
        {
            int i = 0;

            if (patches == nullptr || count <= 0)
                return;
            while (i < count)
            {
                const TogglePatch *p = &patches[i];

                if (PatchValid(p))
                {
                    PatchWrite(p->address, (int)p->size,
                               active ? p->onValue : p->offValue);
                    FlushMemory(p->address, (int)p->size);
                }
                i++;
            }
        }

        bool    IsVisible(void)   { return g_visible || GuiKeyboard::Active(); }

        void    SetToggleHandlers(const ToggleHandlers *handlers)
        {
            if (handlers == nullptr)
                std::memset(&g_toggleHdl, 0, sizeof(g_toggleHdl));
            else
                g_toggleHdl = *handlers;
        }

        int     ItemCount(void)   { return g_itemCount; }

        static bool ItemOk(int index)
        {
            return index >= 0 && index < g_itemCount && g_thread != nullptr;
        }

        const char *ItemLabel(int index)
        {
            return ItemOk(index) ? g_items[index].label : "";
        }

        int     ItemValue(int index)
        {
            return ItemOk(index) ? (int)g_items[index].value : 0;
        }

        int     ItemApplied(int index)
        {
            return ItemOk(index) ? (int)g_items[index].applied : 0;
        }

        void    SetItemApplied(int index, int value)
        {
            if (!ItemOk(index))
                return;
            g_items[index].value = value;
            g_items[index].applied = value;
        }

        u16     ItemHotkey(int index)
        {
            return ItemOk(index) ? g_items[index].hotkey : 0;
        }

        u16     ItemAppliedHotkey(int index)
        {
            return ItemOk(index) ? g_items[index].appliedHotkey : 0;
        }

        const char *FormatItemHotkey(int index, char *buf, unsigned int cap)
        {
            if (buf == nullptr || cap == 0)
                return "";
            return FormatHotkey(ItemOk(index) ? g_items[index].hotkey : 0,
                                buf, (size_t)cap);
        }

        void    Open(void)
        {
            const u32 now = NowMs();

            g_open = true;
            g_dialog.type = DLG_NONE;
            ResetSelectionAnimation(now);
            AnimateOpen(1.0f, now);
            // ★描くのはメニュースレッドだけ。ここで Redraw すると
            //   2 本のスレッドが同時に Commit して壊れる。
        }

        void    Close(void)
        {
            RequestClose(NowMs());
        }
    }
}
