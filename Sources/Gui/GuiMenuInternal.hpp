// ============================================================================
// GuiMenu の内部共有（GuiMenuModel / GuiMenuDraw / GuiMenuItems / GuiMenu）
// ============================================================================
//
// 正本は gohan-gui-simulator の `ui-model.js`（模型）と `app.js`（描画）。
// **寸法・色・時間・座標はあちらから 1 対 1 で写す。**
// `tools/patches/verify_plugin_port_v2.py` の群 5 が両者を突き合わせ、
// `tools/patches/verify_menu_port.py` が状態遷移を Simulator と差分試験する。
//
// このヘッダと GuiMenuModel / GuiMenuDraw / GuiMenuItems は CTRPF と libctru に
// 依存しない（ホストでそのままコンパイルして試験するため）。
// スレッド・コントローラ・タッチ・svc は GuiMenu.cpp だけが触る。

#ifndef GUIMENU_INTERNAL_HPP
#define GUIMENU_INTERNAL_HPP

#include "GuiMenu.hpp"
#include "GuiRenderer.hpp"

namespace CTRPluginFramework
{
    namespace GuiMenu
    {
        namespace detail
        {
            // ================================================================
            // Simulator の定数（ui-model.js の MENU / CONTROL_REPEAT / LISTBOX /
            // DIALOG / NOTICE / TOGGLE_ACTION / BOTTOM_OVERLAY）
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
            const int   kHoldMs      = 600;     // MENU.holdDuration
            const int   kRepeatDelay = 200;     // CONTROL_REPEAT.delay
            const int   kRepeatEvery = 60;      // CONTROL_REPEAT.interval
            const int   kFeedbackMs  = 800;     // TOGGLE_ACTION.feedbackDuration
            const int   kListAnimMs  = 180;     // LISTBOX.animationDuration
            const int   kListScrollMs = 100;    // LISTBOX.scrollDuration
            const int   kInlineRows  = 6;       // LISTBOX.inlineVisibleRows
            const int   kScreenRows  = 8;       // LISTBOX.screenVisibleRows
            const int   kBottomAnimMs = 180;    // BOTTOM_OVERLAY.animationDuration
            const int   kDialogMs    = 160;     // DIALOG.animationDuration
            const int   kNoticeMax   = 7;       // NOTICE.maximum
            const int   kNoticeW     = 144;     // NOTICE.width
            const int   kNoticeH     = 27;      // NOTICE.height
            const int   kNoticeGap   = 4;       // NOTICE.gap
            const int   kNoticeMargin = 8;      // NOTICE.margin
            const int   kNoticeEnterMs = 360;   // NOTICE.enterDuration
            const int   kNoticeHoldMs = 2600;   // NOTICE.holdDuration
            const int   kNoticeExitMs = 360;    // NOTICE.exitDuration
            const int   kNoticeSpawnMs = 40;    // NOTICE.spawnInterval
            const int   kDescLines   = 6;       // drawDescriptionPanel の slice(0, 6)

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
            const u32   kColDisabled  = 0xFF545B52; // #525b54
            const u32   kColDescOff   = 0xFF646B62; // #626b64
            const u32   kColLinked    = 0xFFFFC85C; // #5cc8ff
            const u32   kColCheckMark = 0xFF6B6BFF; // #ff6b6b
            const u32   kColFootTxt   = 0xFF879082; // #829087
            const u32   kColFootOff   = 0xFF5B6358; // #58635b
            const u32   kColPanelEdge = 0xFF5B6C4F; // #4f6c5b
            const u32   kColBarTrack  = 0xA82C3426; // rgba(38,52,44,.66)
            const u32   kColBarThumb  = 0xE0A4E463; // rgba(99,228,164,.88)
            const u32   kColListSel   = 0xA3374529; // rgba(41,69,55,.64)
            const u32   kColBoxBg     = 0xD10B0D0A; // rgba(10,13,11,.82)
            const u32   kColDlgBg     = 0xE6080907; // rgba(7,9,8,.9)
            const u32   kColDlgSel    = 0xB81F495B; // rgba(91,73,31,.72)
            const u32   kColHoldBg    = 0xF0080907; // rgba(7,9,8,.94)
            const u32   kColHoldTrack = 0xD9224855; // rgba(85,72,34,.85)
            const u32   kColKeyBg     = 0xC21E231C; // rgba(28,35,30,.76)
            const u32   kColKeyLine   = 0xFF3D453A; // #3a453d
            const u32   kColKeyTxt    = 0xFFB9C1B7; // #b7c1b9
            const u32   kColHint      = 0xFF929A8F; // #8f9a92
            const u32   kColBound     = 0xFF81897D; // #7d8981
            const u32   kColSliderTrack = 0xB82A3027; // rgba(39,48,42,.72)
            const u32   kColSliderFill = 0xE0A4E463; // rgba(99,228,164,.88)
            const u32   kColSliderKnob = 0xE6FFFFFF; // rgba(255,255,255,.9)
            const u32   kColNoticeBg  = 0xC70B0D0A; // rgba(10,13,11,.78)
            const u32   kColNoticeMsg = 0xFFC7CEC4; // #c4cec7
            const u32   kColDanger    = 0xFF4D48E5; // #e5484d
            const u32   kColDangerEdge = 0xFF5B4F6C; // #6c4f5b
            const u32   kColDim       = 0xB8111410; // rgba(16,20,17,.72)

            // ================================================================
            // 項目（ui-model.js の createMenuTree の item(type, ...)）
            // ================================================================
            enum ItemType
            {
                ITEM_FOLDER = 0,
                ITEM_CHECKBOX,          // "checkbox"
                ITEM_ACTION,            // "action"
                ITEM_TOGGLE_ACTION,     // "toggle-action"
                ITEM_LIST,              // "list"
                ITEM_LINKED_LIST,       // "linked-list"
                ITEM_VALUE,             // "value"
                ITEM_LINKED_VALUE,      // "linked-value"
                ITEM_SLIDER             // "slider"
            };
            enum Format { FMT_NONE = 0, FMT_DEC, FMT_HEX, FMT_FLOAT };
            enum ActionId
            {
                ACT_NONE = 0, ACT_SAVE, ACT_TOP_LIST, ACT_BOTTOM_LIST,
                ACT_TEXT, ACT_COMPACT_TEXT, ACT_CHAT_KANJI
            };

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
                bool        disabled;       // 連動型の読取失敗（カーソルは置けるが編集できない）
                const char *label;
                const char *desc;
                const char *const *options;
                // ★float は 1/10 単位の整数で持つ（浮動小数の等値比較を避ける）
                s32         value;
                s32         applied;
                s32         minimum;
                s32         maximum;
                s32         step;
                // ホットキー（ui-model.js の hotkey / appliedHotkey）。0 = "なし"。
                u16         hotkey;
                u16         appliedHotkey;
                // トグル型アクションの「実行した」表示（executionFeedbackUntil）
                bool        feedbackOn;
                u32         feedbackUntil;
            };

            struct Frame
            {
                const char *title;
                int         first;
                int         count;
                int         selection;
            };

            // 開閉のアニメーション（createListboxAnimation 等の共通部）
            struct Anim
            {
                float   from, target;
                u32     start;
                int     ms;
                bool    closing;
            };

            struct Listbox          // inlineList / overlay[type=listbox]
            {
                bool        active;
                u8          screen;         // 0=上 / 1=下
                u8          mode;           // OV_*
                int         item;           // 対象項目（-1=なし）
                const char *title;
                const char *const *options;
                int         optionCount;
                int         index;
                Anim        anim;
                float       scrollFrom, scrollTarget;
                u32         scrollStart;
            };
            enum ListMode { OV_GENERIC = 0, OV_HOTKEY_ITEM, OV_CHAT_KANJI };

            enum DialogType
            {
                DLG_NONE = 0, DLG_CLOSE, DLG_APPLY_ALL, DLG_REVERT_ALL, DLG_TOGGLE_ACTION
            };
            struct Dialog
            {
                u8      type;
                int     item;           // DLG_TOGGLE_ACTION の対象
                int     index;
                bool    confirmed;
                Anim    anim;
            };

            enum CapturePhase { CAP_ARMING = 0, CAP_WAITING, CAP_RECORDING };
            struct Capture          // overlay[type=hotkey-capture]
            {
                bool    active;
                u8      phase;
                int     item;
                u16     captured;
                Anim    anim;
            };

            struct Slider           // overlay[type=slider]
            {
                bool    active;
                int     item;
                s32     value;
                bool    applyOnConfirm;
                Anim    anim;
            };

            struct Hold             // holdAction
            {
                bool    active;
                int     bit;            // HB_X / HB_L
                u32     start;
            };

            struct Notice
            {
                bool    active;
                int     id;
                char    title[96];      // ★題はチート名（UTF-8 で 45 B を超える名前がある）
                char    msg[96];
                bool    red;
                u32     createdAt;
                float   moveFromY, targetY;
                u32     moveStartedAt;
            };

            // 関数側の登録（振る舞い。データではない）
            struct Behavior
            {
                bool    (*IsActive)(int index);
                void    (*SetActive)(int index, bool active);
                bool    (*LinkedRead)(int index, s32 *value);
                void    (*LinkedWrite)(int index, s32 value);
                void    (*Execute)(int index);
                void    (*Apply)(int index, s32 value);
            };

            // 1 フレームぶんの入力（水準）。GuiMenu.cpp が CTRPF から読んで渡す。
            struct Input
            {
                u16     held;           // HotkeyBit マスク
                bool    touch;
                int     tx, ty;
            };

            // ================================================================
            // 状態（GuiMenuModel.cpp が持つ）
            // ================================================================
            extern Item         g_items[kMaxItems];
            extern int          g_itemCount;
            extern int          g_rootFirst;
            extern int          g_rootCount;
            extern Frame        g_frames[kMaxDepth];
            extern int          g_depth;
            extern Behavior     g_behavior[kMaxItems];
            extern Listbox      g_inline;
            extern Listbox      g_overlay;
            extern Dialog       g_dialog;
            extern Capture      g_capture;
            extern Slider       g_slider;
            extern Hold         g_hold;
            extern Notice       g_notices[kNoticeMax];
            extern bool         g_visible;
            extern float        g_openFrom, g_openTarget;
            extern u32          g_openStart;
            extern bool         g_needFinal;
            extern const char  *g_longOpts[kLongList];
            // 通知が積まれたときに呼ぶ（試験用の観測口。未設定なら呼ばない）
            extern void       (*g_noticeHook)(const char *title, const char *msg);
            extern ToggleHandlers g_toggleHdl;

            // ---- 小物 ----
            float   Clamp01(float v);
            int     ClampI(int v, int lo, int hi);
            float   EaseOutCubic(float t);
            float   Mix(float a, float b, float t);
            float   Progress(u32 start, u32 now, int ms);
            float   AnimAmount(const Anim &a, u32 now);
            float   ScrollTarget(int index, int count, int rows);
            const char *FormatHotkey(u16 mask, char *buf, unsigned int cap);

            // ---- 模型（GuiMenuModel.cpp）----
            void    ResetState(void);
            int     ItemIndex(const Item &it);
            bool    IsDirtyItem(const Item &it);
            int     DirtyCount(void);
            bool    IsLinked(const Item &it);
            bool    IsNumericItem(const Item &it);
            bool    HasValue(const Item &it);
            bool    EffectActive(int index);
            Frame  &Cur(void);
            Item   &Sel(void);
            float   OpenAmount(u32 now);
            float   SelectionPosition(u32 now);
            float   ViewportStart(u32 now);
            float   ValueBounce(u32 now);
            float   ActBounce(u32 now);
            float   EdgeBounce(u32 now);
            float   ListScroll(const Listbox &l, u32 now);
            bool    OverlayActive(void);
            bool    Animating(u32 now);
            float   NoticeX(const Notice &nt, u32 now);
            float   NoticeY(const Notice &nt, u32 now);
            bool    NoticeAlive(const Notice &nt, u32 now);
            void    AddNotice(const char *title, const char *msg, u32 now, bool red = false);
            void    OpenMenu(u32 now);
            void    CloseMenu(u32 now);
            void    Step(u32 now, const Input &in);
            void    PrimeInput(u16 held);
            void    PrimeEffect(int index);
            bool    HoldProgress(u32 now, float &progress, const char *&label);
            bool    ButtonBlock(void);
            bool    BottomUiPresent(void);         // 下画面に操作 UI がある（退場中も含む）
            bool    BottomDim(u32 now, u32 &color, float &amount);

            // ---- 描画（GuiMenuDraw.cpp）----
            void    BuildTop(u32 now);
            void    BuildBottom(u32 now);
            int     SliderTrackX(void);
            int     SliderTrackY(void);
            int     SliderTrackW(void);
            void    CaptureButtonRect(int which, int &x, int &y, int &w, int &h);

            // ---- 項目の木（GuiMenuItems.cpp）----
            void    BuildTree(void);
            void    WireBehaviors(void);            // 項目名で振る舞いを登録する
            // ---- 差分試験の見本の観測口（tools/patches/menu_sample_items.cpp だけが定義する）----
            bool    SampleEffectState(int which);       // 0=無敵モード / 1=壁抜け
            s32     SampleLinkedValue(int which);       // 0=連動型数値 / 1=連動型リスト
            void    SetSampleLinkedAvailable(bool ok);  // 連動型の読取失敗を再現する
            int     SampleExecuteCount(void);           // トグル型アクションの実行回数
        }
    }
}

#endif
