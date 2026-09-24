#include "GameLabel.hpp"
#include "GridCursor.hpp"

#include <3ds.h>
#include <cstring>

// ゲームの所持ベルの箱を上画面に借りる（IDA-opus-5.5-F036、docs/topics/game_list.md）。
// 手本は BsTimeBelWindow の組み立て sub_2939C8: 書体 0/1/3 を保持体へ登録 → time_bel_win.bclyt を 0x5510 で組む
// → 優先度 0x80 → time_bel_win_in/out を G_clock / G_bell_00 のグループに結ぶ（vc_ACSYSTEM_DATA1）。
// 部品の関数は GameList と同じもの（番地は GameList.cpp と docs を参照）。

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

const char kArcPath[] = "Layout/Menu/time_bel_win.arc";
const u32 kCmdBytes = 0x5510;               // BsTimeBelWindow と同じ
const u8 kPriority = 0x80;                  // 同上（上画面）
const u32 kHolderAccessor = 12;
const u32 kLayoutPriority = 12, kLayoutHolder = 236;
const u32 kPaneFlags = 183;                 // bit0 = 見える、bit4-5 = 行列の更新済み（0 にすると計算し直す）
const u32 kPaneX = 40, kPaneY = 44;
const u32 kTextFont = 224;                  // TextBox の書体
const u32 kAnimTotal = 4, kAnimCur = 8;
// 箱（P_bell_base、N_bell の子で (-59,-6)、98x32 を横 1.3 倍）の中心を置く位置（上画面 400x240、中心が原点・上が +y）。
// 上画面の左上の端（左右とも 8px 空ける）。★時計は左上ではなく左下にある（実機の画面。最初の版は時計の下のつもりで
//   y=36 に置き「下すぎる」と言われた）。
const float kBoxX = -128.0f, kBoxY = 96.0f;
// 文字: T_bell_00 は基準点 8（右下）・文字の配置 5（右・上下中央）で右寄せだった。基準点 4・配置 4（中央）にして、
//   箱の中心に置く。子の位置は親（N_bell_00、N_bell の子で (-16,-18)）の平行移動から測る。
const u32 kPaneBase = 0xB6;                 // 下位 4 ビット = 基準点（横 + 縦×3。nwlyt_Pane_GetAnchorOffset 0x73B5C4）
const u32 kTextPosition = 252, kTextDirty = 254;   // TextBox: 文字の配置（横 + 縦×3）/ bit0 = 作り直し
const float kTextX = -59.0f - (-16.0f), kTextY = -6.0f - (-18.0f);
// 箱の色（利用者: 水色）。マテリアル（Picture+0x13C、80 B）の色 [0] と [1]（+0x10 / +0x14、bclyt の res+20 の写し）を
//   テクスチャ（LA4）の明るさで混ぜて塗る。素の値は e1b90f00 / fffabeff（山吹・クリーム）。アルファの byte はそのまま。
const u32 kPicMaterial = 0x13C, kMatColor0 = 0x10, kMatColor1 = 0x14, kMatFlags = 0x4D;
const u8 kBlueDark[3] = { 0x5A, 0xAA, 0xE6 }, kBlueLight[3] = { 0xD2, 0xF0, 0xFF };
const float kBoxOffX = -59.0f, kBoxOffY = -6.0f;
const u32 kTeardownWaitFrames = 3;

alignas(8) u8 s_holder[584];
alignas(8) u8 s_layout[332];
alignas(8) u8 s_in[40];
alignas(8) u8 s_out[40];
void *s_group;
void *s_text;

volatile bool s_want;
u16 s_pend[kMaxChars + 1];
volatile u32 s_textSeq;
u32 s_textDone;
const char *volatile s_error = "";

enum class Stage : u8 { Off, Loading, Live, Waiting };
volatile Stage s_stage = Stage::Off;
bool s_holderMade, s_layoutMade, s_layoutBuilt, s_animsMade, s_hookReady;
bool s_entered;            // 組み立ててから一度でも登場させた
enum class Dir : u8 { None, In, Out };
Dir s_dir = Dir::None;
u32 s_waitFrames;

inline u8 *P(void *p, u32 off) { return reinterpret_cast<u8 *>(p) + off; }
inline float &F(void *p, u32 off) { return *reinterpret_cast<float *>(P(p, off)); }
inline u8 &B(void *p, u32 off) { return *P(p, off); }
inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(P(p, off)); }

void Hide(void *pane) {
    if (pane != nullptr)
        B(pane, kPaneFlags) &= ~1u;
}

void MovePane(void *pane, float x, float y) {
    F(pane, kPaneX) = x;
    F(pane, kPaneY) = y;
    B(pane, kPaneFlags) &= 0xCFu;
}

float Progress(void *anim) {
    const float total = F(anim, kAnimTotal) - 1.0f;
    if (total <= 0.0f)
        return 1.0f;
    const float p = F(anim, kAnimCur) / total;
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

void Switch(void *from, void *to) {
    float start = 0.0f;
    if (s_dir != Dir::None) {
        start = (1.0f - Progress(from)) * (F(to, kAnimTotal) - 1.0f);
        GroupUnbind(s_layout, from, s_group, 0);
    }
    GroupBind(s_layout, to, s_group, 0);
    AnimSetFrame(to, start);
}

void DestroyAll(void) {
    if (s_layoutMade) {
        if (s_layoutBuilt)
            LayoutFinalize(s_layout);
        LayoutDtor(s_layout);
    }
    s_layoutMade = s_layoutBuilt = false;
    if (s_animsMade) {
        AnimDtor(s_in);
        AnimDtor(s_out);
    }
    s_animsMade = false;
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = false;
    s_group = s_text = nullptr;
    s_dir = Dir::None;
    s_entered = false;
    s_stage = Stage::Off;
}

void RegisterFonts(void) {
    void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
    static const u32 kKinds[] = { 0, 1, 3 };        // BsTimeBelWindow と同じ 3 つ
    for (u32 i = 0; i < 3; ++i) {
        void *font = FontGet(fontMgr, kKinds[i]);
        if (font == nullptr)
            continue;
        u32 *name = reinterpret_cast<u32 *>(FontName(fontMgr, kKinds[i]));
        reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(name[0])[2])(name);
        RegisterFont(P(s_holder, kHolderAccessor), reinterpret_cast<const char *>(name[1]), font);
    }
}

// 1 フレームに 1 段。真 = 組み上がった
bool Build(void) {
    if (!s_holderMade) {
        ArcCtor(s_holder);
        s_holderMade = true;
    }
    if (!s_layoutBuilt) {
        if (ArcLoadStep(s_holder, kArcPath) == 0)
            return false;
        void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
        if (fontMgr == nullptr || FontGet(fontMgr, 0) == nullptr) {
            s_error = "ゲームの書体が取れない";
            return false;
        }
        RegisterFonts();
        RegisterTex(s_holder);
        LayoutCtor(s_layout);
        s_layoutMade = true;
        W(s_layout, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
        if (LayoutBuild(s_layout, "time_bel_win.bclyt", nullptr, kCmdBytes) == 0) {
            s_error = "time_bel_win.bclyt を組めない";
            return false;
        }
        s_layoutBuilt = true;
        B(s_layout, kLayoutPriority) = kPriority;
        AnimCtor(s_in);
        AnimCtor(s_out);
        s_animsMade = true;
        AnimLoad(s_in, "time_bel_win_in.bclan", s_holder);
        AnimLoad(s_out, "time_bel_win_out.bclan", s_holder);
        s_group = FindGroup(s_layout, "G_bell_00", 1);
        s_text = FindTextBox(s_layout, "T_bell_00");
        void *bell = FindPane(s_layout, "N_bell");
        void *all = FindPane(s_layout, "N_all");
        if (s_group == nullptr || s_text == nullptr || bell == nullptr || all == nullptr) {
            s_error = "time_bel_win の部品が見つからない";
            return false;
        }
        // 時計と、ベル以外のアイコン・ベルのアイコンを隠す。文字は通常の書体にする
        Hide(FindPane(s_layout, "N_time"));
        static const char *const kIcons[] = { "P_bell_icn_sh", "P_bell_icon", "P_mld_icn_sh", "P_mdl_icon",
                                              "P_cin_icn_sh", "P_cin_icon", "P_ticket_ico_sh", "P_ticket_icon" };
        for (u32 i = 0; i < sizeof(kIcons) / sizeof(kIcons[0]); ++i)
            Hide(FindPane(s_layout, kIcons[i]));
        W(s_text, kTextFont) = reinterpret_cast<u32>(FontGet(fontMgr, 0));
        B(s_text, kPaneBase) = (u8)((B(s_text, kPaneBase) & 0xF0u) | 4u);
        B(s_text, kTextPosition) = 4;
        B(s_text, kTextDirty) |= 1u;
        MovePane(s_text, kTextX, kTextY);
        void *base = FindPane(s_layout, "P_bell_base");
        if (base != nullptr && W(base, kPicMaterial) != 0) {
            u8 *mat = reinterpret_cast<u8 *>(W(base, kPicMaterial));
            for (u32 k = 0; k < 3; ++k) {
                mat[kMatColor0 + k] = kBlueDark[k];
                mat[kMatColor1 + k] = kBlueLight[k];
            }
            mat[kMatFlags] &= ~4u;          // GPU へ送り直させる
        }
        // 登場し終えたときの N_bell の位置から、箱の中心が kBox に来るよう N_all をずらす
        GroupBind(s_layout, s_in, s_group, 0);
        AnimSetFrame(s_in, F(s_in, kAnimTotal) - 1.0f);
        LayoutCalc(s_layout);
        const float bx = F(bell, kPaneX), by = F(bell, kPaneY);
        GroupUnbind(s_layout, s_in, s_group, 0);
        MovePane(all, kBoxX - (bx + kBoxOffX), kBoxY - (by + kBoxOffY));
        s_textDone = s_textSeq - 1;         // 文字を必ず一度書く
        s_entered = false;
        return true;
    }
    return true;
}

}  // namespace

void SetText(const char *utf8) {
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
        s_pend[n++] = (u16)c;
    }
    s_pend[n] = 0;
    s_textSeq = s_textSeq + 1;
}

void Show(void) {
    if (!s_hookReady) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep)) {
            s_error = "フレームフックを入れられない";
            return;
        }
        s_hookReady = true;
    }
    s_error = "";
    s_want = true;
}

void Hide(void) {
    s_want = false;
}

bool Present(void) {
    return s_stage != Stage::Off;
}

const char *LastError(void) {
    return s_error;
}

void FrameStep(void) {
    const bool want = s_want;
    switch (s_stage) {
    case Stage::Off:
        if (!want || s_error[0] != 0)
            return;
        s_stage = Stage::Loading;
        // 続けて組み立てへ
    case Stage::Loading:
        if (!Build()) {
            if (s_error[0] != 0)
                DestroyAll();
            return;
        }
        s_stage = Stage::Live;
        break;
    case Stage::Live:
        break;
    case Stage::Waiting:
        if (++s_waitFrames >= kTeardownWaitFrames)
            DestroyAll();
        return;
    }

    // 文字（出ている間でも差し替えられる）
    if (s_textDone != s_textSeq) {
        s_textDone = s_textSeq;
        u32 len = 0;
        while (len < kMaxChars && s_pend[len] != 0)
            ++len;
        SetString(s_text, s_pend, 0, len);
    }
    // 登場・退場（途中で逆向きにできる。Switch は今のアニメの進みから逆側の位置を決める）
    if (want) {
        if (!s_entered) {
            Switch(s_out, s_in);            // s_dir が None なので 0 から
            s_dir = Dir::In;
            s_entered = true;
        } else if (s_dir == Dir::Out) {
            Switch(s_out, s_in);
            s_dir = Dir::In;
        }
    } else {
        if (!s_entered) {                   // 一度も出していない: そのまま片付け
            s_stage = Stage::Waiting;
            s_waitFrames = 0;
            return;
        }
        if (s_dir != Dir::Out) {
            Switch(s_in, s_out);
            s_dir = Dir::Out;
        }
    }
    void *anim = s_dir == Dir::In ? s_in : s_dir == Dir::Out ? s_out : nullptr;
    if (anim != nullptr) {
        if (AnimFinished(anim)) {
            GroupUnbind(s_layout, anim, s_group, 0);
            const Dir done = s_dir;
            s_dir = Dir::None;
            if (done == Dir::Out) {
                s_stage = Stage::Waiting;   // 退場し終えた: この フレームから描かない
                s_waitFrames = 0;
                return;
            }
        } else {
            AnimStep(anim);
        }
    }
    LayoutCalc(s_layout);
    void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
    if (mgr != nullptr)
        AddLayout(mgr, s_layout, 0);
}

}  // namespace GameLabel
