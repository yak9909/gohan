#include "GameLabel.hpp"
#include "FrameTrace.hpp"
#include "GameList.hpp"
#include "GridCursor.hpp"

#include <3ds.h>
#include <cstring>

// ゲームの所持ベルの箱を上画面に借りる（IDA-opus-5.5-F036、docs/topics/game_list.md）。
// 手本は BsTimeBelWindow の組み立て sub_2939C8: 書体 0/1/3 を保持体へ登録 → time_bel_win.bclyt を 0x5510 で組む
// → 優先度 0x80 → time_bel_win_in/out を G_clock / G_bell_00 のグループに結ぶ（vc_ACSYSTEM_DATA1）。
// 保持体（arc）は全部の箱で 1 つ、レイアウトとアニメは箱ごと。

namespace GameLabel {

namespace {

const u32 kLayoutMgrPtr = 0x0096FC38;
const u32 kFontMgrPtr = 0x0094C9C8;

typedef void *(*CtorFn)(void *self);
typedef int (*ArcLoadStepFn)(void *holder, const char *path);
typedef int (*LayoutBuildFn)(void *layout, const char *name, void *holder, u32 cmdBytes);
typedef void (*LayoutFn)(void *layout);
typedef void *(*FindFn)(void *layout, const char *name);
typedef void *(*FindGroupFn)(void *layout, const char *name, u32 recursive);
typedef int (*AnimLoadFn)(void *anim, const char *name, void *holder);
typedef void (*GroupAnimFn)(void *layout, void *anim, void *group, u32 zero);
typedef void (*AnimSetFrameFn)(void *anim, float frame);
typedef void (*AnimFn)(void *anim);
typedef int (*AnimFinishedFn)(void *anim);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef void *(*FontSlotFn)(void *fontMgr, u32 kind);
typedef void (*RegisterFontFn)(void *accessor, const char *name, void *font);
typedef int (*RegisterTexFn)(void *holder);
typedef int (*SetStringFn)(void *textBox, const u16 *str, u32 dst, u32 len);
typedef void (*AllocBufFn)(void *textBox, u32 chars, u32 flags);

const CtorFn         ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);
const CtorFn         ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const ArcLoadStepFn  ArcLoadStep    = reinterpret_cast<ArcLoadStepFn>(0x00567244);
const CtorFn         LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);
const CtorFn         LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutBuildFn  LayoutBuild    = reinterpret_cast<LayoutBuildFn>(0x005685A4);
const LayoutFn       LayoutFinalize = reinterpret_cast<LayoutFn>(0x00133A5C);
const LayoutFn       LayoutCalc     = reinterpret_cast<LayoutFn>(0x00568030);       // vc_ACSYSTEM_DATA2
const FindFn         FindPane       = reinterpret_cast<FindFn>(0x00567BAC);
const FindFn         FindTextBox    = reinterpret_cast<FindFn>(0x00567EA0);         // 型を確かめる版
const FindGroupFn    FindGroup      = reinterpret_cast<FindGroupFn>(0x004B4328);
const CtorFn         AnimCtor       = reinterpret_cast<CtorFn>(0x001261EC);
const CtorFn         AnimDtor       = reinterpret_cast<CtorFn>(0x00568CAC);
const AnimLoadFn     AnimLoad       = reinterpret_cast<AnimLoadFn>(0x00568A04);
const GroupAnimFn    GroupBind      = reinterpret_cast<GroupAnimFn>(0x00567A50);    // vc_ACSYSTEM_DATA1
const GroupAnimFn    GroupUnbind    = reinterpret_cast<GroupAnimFn>(0x00567DA4);    // vc_DATA3
const AnimSetFrameFn AnimSetFrame   = reinterpret_cast<AnimSetFrameFn>(0x00568C00);
const AnimFn         AnimStep       = reinterpret_cast<AnimFn>(0x00568964);
const AnimFinishedFn AnimFinished   = reinterpret_cast<AnimFinishedFn>(0x0074F58C);
const AddLayoutFn    AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);
const FontSlotFn     FontName       = reinterpret_cast<FontSlotFn>(0x00747528);
const FontSlotFn     FontGet        = reinterpret_cast<FontSlotFn>(0x0052D6A8);
const RegisterFontFn RegisterFont   = reinterpret_cast<RegisterFontFn>(0x004B3FD4);
const RegisterTexFn  RegisterTex    = reinterpret_cast<RegisterTexFn>(0x00568CFC);
const SetStringFn    SetString      = reinterpret_cast<SetStringFn>(0x004BACBC);    // nwlyt_TextBox_SetString
const u32 kTextBoxAllocSlot = 28;           // TextBox vt[28] 0x13B9A8 = 器の確保 (箱, 文字数, 旗)。旧い器は vt[29] で返す

const char kArcPath[] = "Layout/Menu/time_bel_win.arc";
const u32 kCmdBytes = 0x5510;               // BsTimeBelWindow と同じ
const u8 kPriority = 0x80;                  // 同上（上画面）
const u32 kHolderAccessor = 12;
const u32 kLayoutPriority = 12, kLayoutHolder = 236;
const u32 kPaneFlags = 183;                 // bit0 = 見える、bit4-5 = 行列の更新済み（0 にすると計算し直す）
const u32 kPaneX = 40, kPaneY = 44, kPaneScaleX = 64, kPaneWidth = 72;
const u32 kPaneBase = 0xB6;                 // 下位 4 ビット = 基準点（横 + 縦×3。nwlyt_Pane_GetAnchorOffset 0x73B5C4）
const u32 kTextFont = 224;                  // TextBox の書体
const u32 kTextDraw = 260;                  // TextBox の描画用の器（+9 の byte が器の確保の旗）
const u32 kTextPosition = 252, kTextDirty = 254;   // 文字の配置（横 + 縦×3）/ bit0 = 作り直し
const u32 kAnimTotal = 4, kAnimCur = 8;
const u32 kPicMaterial = 0x13C, kMatColor0 = 0x10, kMatColor1 = 0x14, kMatFlags = 0x4D;

// ---- 見た目（利用者の指示）----
// 箱: 上画面（400x240、中心が原点・上が +y）の左上の端から**横に並べる**（利用者指示。最初は縦に並べていた）。
//   左端 x = -192、中心 y = 96、箱と箱の間は kGap。出ていない箱は詰める。
//   ★時計は左上ではなく左下にある（最初の版は時計の下のつもりで y=36 に置き「下すぎる」と言われた）。
// 箱の元の幅は 98 を横 1.3 倍（127px）。文字の幅（全角 15px・半角 9px の見積もり。「配置モード」5 文字 ≒ 75px を実機で確認）
//   に左右 16px を足した幅がこれより広ければ横に伸ばす。
const float kLeft = -192.0f, kTop = 96.0f, kGap = 6.0f, kRowPitch = 36.0f;
// 余白は左右合わせて 60px（利用者: 32px では窮屈。一回り大きく）
const float kBaseW = 98.0f, kMinScale = 1.3f, kPad = 60.0f, kWide = 15.0f, kNarrow = 9.0f;
// 箱（P_bell_base、N_bell の子で (-59,-6)）と影（P_bell_sh、(-57,-8)）。文字は N_bell_00（(-16,-18)）の子。
const float kBoxOffX = -59.0f, kBoxOffY = -6.0f, kTextParentX = -16.0f, kTextParentY = -18.0f;
// 色（利用者: 水色）。マテリアル色 [0]/[1] を LA4 の明るさで混ぜる。素は e1b90f / fffabe（山吹・クリーム）。
const u8 kBlueDark[3] = { 0x5A, 0xAA, 0xE6 }, kBlueLight[3] = { 0xD2, 0xF0, 0xFF };
// 文字（利用者: 白）。TextBox の文字色 2 つ（+216 上 / +220 下、nwlyt_TextBox_Ctor がリソース +92.. から写す）と、
//   TextBox のマテリアル（+256）の色 [1]（素は 8c3c14 = 茶。文字はこの色で塗られる）を白にする。
const u32 kTextColorTop = 216, kTextColorBottom = 220, kTextMaterial = 256;
// 文字色（利用者: 背景より濃い水色。白から変更）。byte の並びは R, G, B, A（ctor がリソースの 4 byte をそのまま組む）
// 利用者: もう少し濃く（1E6EB4 → 0F4C8A）
const u8 kTextRgba[4] = { 0x0F, 0x4C, 0x8A, 0xFF };
// 警告色（利用者: 上限に届いた・足りないときは箱と文字を赤く）
const u8 kRedDark[3] = { 0xE0, 0x48, 0x48 }, kRedLight[3] = { 0xFF, 0xD4, 0xD4 };
const u8 kRedTextRgba[4] = { 0xA0, 0x18, 0x18, 0xFF };
// ゲームの時計: BsTimeBelWindow の実体 dword_94A720（BsTimeBelWindow_Init 0x2939C8 の最後で入る）、+1760 = N_time ペイン。
//   左下の箱と重なるので、出している間は毎フレーム見える旗（ペイン+183 bit0）を落とす（ゲームの処理の後・記録の前）。
const u32 kTimeBelWindowPtr = 0x0094A720, kTimeBelClockPane = 1760;
const u32 kTeardownWaitFrames = 3;

enum class Dir : u8 { None, In, Out };

struct Label {
    alignas(8) u8 layout[332];
    alignas(8) u8 in[40];
    alignas(8) u8 out[40];
    void *group, *text, *base, *shadow, *all;
    bool made, built, anims, entered, live;
    Dir dir;
    u32 textDone;
    float bellX, bellY;                     // 登場し終えたときの N_bell の位置
    float width;                            // 箱の幅（ApplyText が決める）
    float placedX;                          // いま置いている箱の左端（並べ直しが要るかの判定）
    // メニューのスレッドが書く
    volatile bool want;
    volatile bool alert;
    bool alertShown;                        // いま塗っている色（組み立て直後は通常色）
    volatile u32 row;
    volatile bool bottom;
    u16 pend[kMaxChars + 1];
    volatile u32 textSeq;
};

alignas(8) u8 s_holder[584];
bool s_holderMade, s_holderReady, s_hookReady;
Label s_labels[kSlots];
const char *volatile s_error = "";
u32 s_waitFrames;
volatile bool s_hideClock;
u32 s_clockPane;                // 隠しているペイン（0 = 隠していない）
u8 s_clockFlag;                 // 隠す前の旗

void StepClock(void) {
    const u32 win = *reinterpret_cast<const volatile u32 *>(kTimeBelWindowPtr);
    const u32 pane = (win >= 0x30000000u && win < 0x40000000u) ? *reinterpret_cast<const volatile u32 *>(win + kTimeBelClockPane) : 0u;
    if (s_hideClock && pane >= 0x30000000u && pane < 0x40000000u) {
        if (s_clockPane != pane) {
            s_clockPane = pane;
            s_clockFlag = *reinterpret_cast<volatile u8 *>(pane + kPaneFlags);
        }
        *reinterpret_cast<volatile u8 *>(pane + kPaneFlags) &= ~1u;
    } else if (s_clockPane != 0) {
        if (pane == s_clockPane)
            *reinterpret_cast<volatile u8 *>(pane + kPaneFlags) =
                (u8)((*reinterpret_cast<volatile u8 *>(pane + kPaneFlags) & ~1u) | (s_clockFlag & 1u));
        s_clockPane = 0;
    }
}

inline u8 *P(void *p, u32 off) { return reinterpret_cast<u8 *>(p) + off; }
inline float &F(void *p, u32 off) { return *reinterpret_cast<float *>(P(p, off)); }
inline u8 &B(void *p, u32 off) { return *P(p, off); }
inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(P(p, off)); }

void HidePane(void *pane) {
    if (pane != nullptr)
        B(pane, kPaneFlags) &= ~1u;
}

void Touch(void *pane) {
    B(pane, kPaneFlags) &= 0xCFu;
}

void MovePane(void *pane, float x, float y) {
    F(pane, kPaneX) = x;
    F(pane, kPaneY) = y;
    Touch(pane);
}

float Progress(void *anim) {
    const float total = F(anim, kAnimTotal) - 1.0f;
    if (total <= 0.0f)
        return 1.0f;
    const float p = F(anim, kAnimCur) / total;
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

// 箱と文字の色を塗る（通常 = 水色 / 警告 = 赤）
void Paint(Label &l, bool alert) {
    const u8 *text = alert ? kRedTextRgba : kTextRgba;
    for (u32 k = 0; k < 4; ++k) {
        P(l.text, kTextColorTop)[k] = text[k];
        P(l.text, kTextColorBottom)[k] = text[k];
    }
    B(l.text, kTextDirty) |= 1u;
    if (W(l.base, kPicMaterial) != 0) {
        u8 *mat = reinterpret_cast<u8 *>(W(l.base, kPicMaterial));
        const u8 *dark = alert ? kRedDark : kBlueDark;
        const u8 *light = alert ? kRedLight : kBlueLight;
        for (u32 k = 0; k < 3; ++k) {
            mat[kMatColor0 + k] = dark[k];
            mat[kMatColor1 + k] = light[k];
        }
        mat[kMatFlags] &= ~4u;              // GPU へ送り直させる
    }
    l.alertShown = alert;
}

void Switch(Label &l, void *from, void *to) {
    float start = 0.0f;
    if (l.dir != Dir::None) {
        start = (1.0f - Progress(from)) * (F(to, kAnimTotal) - 1.0f);
        GroupUnbind(l.layout, from, l.group, 0);
    }
    GroupBind(l.layout, to, l.group, 0);
    AnimSetFrame(to, start);
}

void DestroyLabel(Label &l) {
    FrameTrace::Mark(FrameTrace::LabelDestroy, reinterpret_cast<u32>(l.layout), l.made ? 1 : 0);
    if (l.made) {
        if (l.built)
            LayoutFinalize(l.layout);
        LayoutDtor(l.layout);
    }
    if (l.anims) {
        AnimDtor(l.in);
        AnimDtor(l.out);
    }
    l.made = l.built = l.anims = l.entered = l.live = false;
    l.group = l.text = l.base = l.shadow = l.all = nullptr;
    l.dir = Dir::None;
}

void DestroyHolder(void) {
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = s_holderReady = false;
}

// arc を読み、書体（BsTimeBelWindow と同じ 0/1/3）とテクスチャを登録する。真 = 使える
bool HolderStep(void) {
    if (s_holderReady)
        return true;
    if (!s_holderMade) {
        ArcCtor(s_holder);
        s_holderMade = true;
    }
    if (ArcLoadStep(s_holder, kArcPath) == 0)
        return false;
    void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
    if (fontMgr == nullptr || FontGet(fontMgr, 0) == nullptr) {
        s_error = "ゲームの書体が取れない";
        return false;
    }
    static const u32 kKinds[] = { 0, 1, 3 };
    for (u32 i = 0; i < 3; ++i) {
        void *font = FontGet(fontMgr, kKinds[i]);
        if (font == nullptr)
            continue;
        u32 *name = reinterpret_cast<u32 *>(FontName(fontMgr, kKinds[i]));
        reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(name[0])[2])(name);
        RegisterFont(P(s_holder, kHolderAccessor), reinterpret_cast<const char *>(name[1]), font);
    }
    RegisterTex(s_holder);
    s_holderReady = true;
    FrameTrace::Mark(FrameTrace::LabelHolderDone);
    return true;
}

bool BuildLabel(Label &l) {
    void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
    LayoutCtor(l.layout);
    l.made = true;
    W(l.layout, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
    if (LayoutBuild(l.layout, "time_bel_win.bclyt", nullptr, kCmdBytes) == 0) {
        s_error = "time_bel_win.bclyt を組めない";
        return false;
    }
    l.built = true;
    B(l.layout, kLayoutPriority) = kPriority;
    AnimCtor(l.in);
    AnimCtor(l.out);
    l.anims = true;
    AnimLoad(l.in, "time_bel_win_in.bclan", s_holder);
    AnimLoad(l.out, "time_bel_win_out.bclan", s_holder);
    l.group = FindGroup(l.layout, "G_bell_00", 1);
    l.text = FindTextBox(l.layout, "T_bell_00");
    l.base = FindPane(l.layout, "P_bell_base");
    l.shadow = FindPane(l.layout, "P_bell_sh");
    l.all = FindPane(l.layout, "N_all");
    void *bell = FindPane(l.layout, "N_bell");
    if (l.group == nullptr || l.text == nullptr || l.base == nullptr || l.shadow == nullptr || l.all == nullptr
        || bell == nullptr) {
        s_error = "time_bel_win の部品が見つからない";
        return false;
    }
    // 時計・アイコンを隠す
    HidePane(FindPane(l.layout, "N_time"));
    static const char *const kIcons[] = { "P_bell_icn_sh", "P_bell_icon", "P_mld_icn_sh", "P_mdl_icon",
                                          "P_cin_icn_sh", "P_cin_icon", "P_ticket_ico_sh", "P_ticket_icon" };
    for (u32 i = 0; i < sizeof(kIcons) / sizeof(kIcons[0]); ++i)
        HidePane(FindPane(l.layout, kIcons[i]));
    // 文字: 通常の書体、器を広げる、基準点と配置を中央に
    W(l.text, kTextFont) = reinterpret_cast<u32>(FontGet(fontMgr, 0));
    const u32 draw = W(l.text, kTextDraw);
    const u32 flags = draw != 0 ? *reinterpret_cast<const u8 *>(draw + 9) : 0;
    u32 *tvt = *reinterpret_cast<u32 **>(l.text);
    reinterpret_cast<AllocBufFn>(tvt[kTextBoxAllocSlot])(l.text, kMaxChars, flags);
    B(l.text, kPaneBase) = (u8)((B(l.text, kPaneBase) & 0xF0u) | 4u);
    B(l.text, kTextPosition) = 4;
    B(l.text, kTextDirty) |= 1u;
    MovePane(l.text, kBoxOffX - kTextParentX, kBoxOffY - kTextParentY);
    // 文字の色（マテリアルの色 [1] を白にして、文字色 2 つで決める）
    Paint(l, false);
    if (W(l.text, kTextMaterial) != 0) {
        u8 *tm = reinterpret_cast<u8 *>(W(l.text, kTextMaterial));
        tm[kMatColor1] = tm[kMatColor1 + 1] = tm[kMatColor1 + 2] = 0xFF;
        tm[kMatFlags] &= ~4u;
    }

    // 登場し終えたときの N_bell の位置（箱を置くときの基準）
    GroupBind(l.layout, l.in, l.group, 0);
    AnimSetFrame(l.in, F(l.in, kAnimTotal) - 1.0f);
    LayoutCalc(l.layout);
    l.bellX = F(bell, kPaneX);
    l.bellY = F(bell, kPaneY);
    GroupUnbind(l.layout, l.in, l.group, 0);
    FrameTrace::Mark(FrameTrace::LabelBuild, reinterpret_cast<u32>(l.layout));
    l.textDone = l.textSeq - 1;             // 文字を必ず一度書く
    l.width = 0.0f;
    l.placedX = -10000.0f;
    l.entered = false;
    l.live = true;
    return true;
}

// 箱を左端 left に置く（上下は kTop）。N_all をずらして合わせる
void Place(Label &l, float left) {
    const float cx = left + l.width * 0.5f;
    const float cy = l.bottom ? -kTop + (float)l.row * kRowPitch : kTop - (float)l.row * kRowPitch;
    MovePane(l.all, cx - (l.bellX + kBoxOffX), cy - (l.bellY + kBoxOffY));
    l.placedX = left;
}

// 文字を書き、幅を合わせる（置く場所は FrameStep が並べて決める）
void ApplyText(Label &l) {
    FrameTrace::Mark(FrameTrace::LabelText, reinterpret_cast<u32>(l.layout));
    u32 len = 0;
    float width = 0.0f;
    while (len < kMaxChars && l.pend[len] != 0) {
        width += l.pend[len] < 0x100 ? kNarrow : kWide;
        ++len;
    }
    SetString(l.text, l.pend, 0, len);
    float scale = (width + kPad) / kBaseW;
    if (scale < kMinScale)
        scale = kMinScale;
    const float w = kBaseW * scale;
    F(l.base, kPaneScaleX) = scale;
    F(l.shadow, kPaneScaleX) = scale;
    F(l.text, kPaneWidth) = w - 16.0f;
    Touch(l.base);
    Touch(l.shadow);
    Touch(l.text);
    B(l.text, kTextDirty) |= 1u;
    l.width = w;
    l.placedX = -10000.0f;                  // 置き直させる
}

// 1 つの箱の 1 フレーム。戻り値: 描いた（出ている・アニメ中）
bool StepLabel(Label &l, float left, void *mgr) {
    const bool want = l.want;
    if (!l.live) {
        // ★元の下画面 UI が出入りしている間は新しく組まない（地図の arc の取り直しと重ねない）
        if (!want || !s_holderReady || s_error[0] != 0 || l.made || GameList::FieldTransition())
            return false;                   // l.made: 退場し終えて壊すのを待っている
        if (!BuildLabel(l)) {
            DestroyLabel(l);
            return false;
        }
    }
    if (l.textDone != l.textSeq) {
        l.textDone = l.textSeq;
        ApplyText(l);
    }
    if (l.placedX != left)
        Place(l, left);
    if (l.alertShown != l.alert)
        Paint(l, l.alert);
    if (want) {
        if (!l.entered) {
            Switch(l, l.out, l.in);         // dir が None なので 0 から
            l.dir = Dir::In;
            l.entered = true;
        } else if (l.dir == Dir::Out) {
            Switch(l, l.out, l.in);
            l.dir = Dir::In;
        }
    } else {
        if (!l.entered) {
            l.live = false;                 // 一度も出していない: そのまま片付けへ
            return false;
        }
        if (l.dir != Dir::Out) {
            Switch(l, l.in, l.out);
            l.dir = Dir::Out;
        }
    }
    void *anim = l.dir == Dir::In ? l.in : l.dir == Dir::Out ? l.out : nullptr;
    if (anim != nullptr) {
        if (AnimFinished(anim)) {
            GroupUnbind(l.layout, anim, l.group, 0);
            const Dir done = l.dir;
            l.dir = Dir::None;
            if (done == Dir::Out) {
                // 退場し終えた: このフレームから描かない。壊すのは数フレーム後
                l.live = false;
                l.entered = false;
                return false;
            }
        } else {
            AnimStep(anim);
        }
    }
    LayoutCalc(l.layout);
    if (mgr != nullptr)
        AddLayout(mgr, l.layout, 0);
    return true;
}

}  // namespace

void SetText(u32 slot, const char *utf8) {
    if (slot >= kSlots)
        return;
    Label &l = s_labels[slot];
    u32 n = 0;
    const u8 *s = reinterpret_cast<const u8 *>(utf8 != nullptr ? utf8 : "");
    while (*s != 0 && n < kMaxChars) {
        u32 c = *s++;
        if (c >= 0xE0) {
            const u32 c1 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            const u32 c2 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            c = ((c & 0x0F) << 12) | (c1 << 6) | c2;
        } else if (c >= 0xC0) {
            const u32 c1 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            c = ((c & 0x1F) << 6) | c1;
        } else if (c >= 0x80) {
            c = '?';
        }
        l.pend[n++] = (u16)c;
    }
    l.pend[n] = 0;
    l.textSeq = l.textSeq + 1;
}

void SetRow(u32 slot, u32 row) {
    if (slot < kSlots)
        s_labels[slot].row = row;
}

void SetBottom(u32 slot, bool bottom) {
    if (slot < kSlots)
        s_labels[slot].bottom = bottom;
}

void SetAlert(u32 slot, bool alert) {
    if (slot < kSlots)
        s_labels[slot].alert = alert;
}

void HideGameClock(bool hide) {
    s_hideClock = hide;
}

void Show(u32 slot) {
    if (slot >= kSlots)
        return;
    if (!s_hookReady) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep)) {
            s_error = "フレームフックを入れられない";
            return;
        }
        s_hookReady = true;
    }
    s_error = "";
    s_labels[slot].want = true;
}

void Hide(u32 slot) {
    if (slot < kSlots)
        s_labels[slot].want = false;
}

bool Present(void) {
    return s_holderMade;
}

const char *LastError(void) {
    return s_error;
}

void FrameStep(void) {
    StepClock();
    bool anyWant = false;
    for (u32 i = 0; i < kSlots; ++i)
        anyWant = anyWant || s_labels[i].want;
    if (anyWant && s_error[0] == 0 && (s_holderReady || !GameList::FieldTransition()) && !HolderStep()) {
        if (s_error[0] != 0)
            DestroyHolder();
        return;
    }
    void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
    // 段ごとに左から順に並べる。出したい箱か、まだ描いている（退場中の）箱だけが場所を取る
    float left[2][kSlots];                  // [上/下][段]
    for (u32 r = 0; r < kSlots; ++r)
        left[0][r] = left[1][r] = kLeft;
    for (u32 i = 0; i < kSlots; ++i) {
        Label &l = s_labels[i];
        const u32 row = l.row < kSlots ? l.row : kSlots - 1;
        float &x = left[l.bottom ? 1 : 0][row];
        const bool takesRoom = l.want || l.live;
        StepLabel(l, x, mgr);
        if (takesRoom && l.width > 0.0f)
            x += l.width + kGap;
    }
    // 退場し終えた箱を壊す（描くのをやめてから数フレーム後。GPU がまだ読んでいるかもしれない）
    bool pending = false;
    for (u32 i = 0; i < kSlots; ++i)
        pending = pending || (!s_labels[i].live && s_labels[i].made);
    if (!pending) {
        s_waitFrames = 0;
    } else if (++s_waitFrames >= kTeardownWaitFrames) {
        for (u32 i = 0; i < kSlots; ++i)
            if (!s_labels[i].live && s_labels[i].made)
                DestroyLabel(s_labels[i]);
        s_waitFrames = 0;
    }
    // どの箱も出しておらず組んでもいなければ arc も返す
    bool made = false;
    for (u32 i = 0; i < kSlots; ++i)
        made = made || s_labels[i].made;
    if (!anyWant && !made && s_holderMade)
        DestroyHolder();
}

}  // namespace GameLabel
