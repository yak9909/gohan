#include "GameList.hpp"
#include "GridCursor.hpp"

#include <3ds.h>
#include <cstring>

// ゲームのリスト UI を下画面だけ借りる（IDA-opus-5.5-F036、docs/topics/game_list.md）。
// 番地は JPN 無印の更新版。関数の意味は静的解析（逆アセンブル）と実機の欄の読み取りで照合した。

namespace GameList {

namespace {

// ---- ゲーム側 ------------------------------------------------------------------------------
const u32 kLayoutMgrPtr = 0x0096FC38;       // u32: ssys::ma::lyt::LayoutMgr
const u32 kLytAllocatorPtr = 0x0096FC3C;    // u32: ssys::ma::Allocator（+4 = sead::ExpHeap）
const u32 kMenuOpenState = 0x00949D1E;      // u8: g_MenuOpenState。1 = アイドル / 2 = 開き要求 / 3 = 開いている（旧 menu_system.md）
const u32 kMenuFlags = 0x00949D68;          // u32: vc_MENU_FLAGS。bit 0x08 = 通常の下メニューが出ている
// ---- 元の下画面 UI（地図・タブ）をゲームの手順で退場／復帰させる（IDA-opus-5.5-F036、実機で確認）----------------
//   BsMenuTab = *(BsMenuMgr+480)。命令バイト 0x949D24 を BsMenuTab_ConsumeCommand 0x6D3F04 が毎フレーム**状態に関係なく**
//   読んで 11 に戻す: 7 = 全タブ退場（→ タブ選択）、2 = 状態 18「地図同期タブ入場」（地図を作り直し → 全タブ入場 → タブ選択）。
//   地図は 0x949D25 = 0 で退場して画面ごと破棄される（sub_6D1428 → sub_6D0B54 → sub_2B3A98）。2 = アイドル。
//   実機: 7 + 0 で地図 +484 = 0・タブのマスク 0・フラグ 0x4、2 で地図が同じ番地に戻り マスク 0x81DF・フラグ 0x2。
//   ★命令は「タブ選択」で落ち着いているときだけ書く（ほかの状態でも強制的に切り替わってしまう）。
const u32 kMenuMgrPtr = 0x00949D4C;         // u32: BsMenuMgr
const u32 kMgrTab = 480;                    // BsMenuTab*
const u32 kMgrMap = 484;                    // 地図の画面（無ければ 0）
const u32 kMgrOther = 488;                  // もう 1 つの下画面（0x949D29 で出し入れ）
const u32 kTabCalc = 32;                    // 状態の calc
const u32 kTabCalcAdj = 36;
const u32 kTabShown = 6578;                 // u16: 出ているタブのマスク
const u32 kTabMoving = 6580;                // u16: 出入り中のマスク
const u32 kTabIdleCalc = 0x006D474C;        // 「タブ選択」
const u32 kTabCommand = 0x00949D24;         // u8: 11 = 何もしない
const u32 kMapCommand = 0x00949D25;         // u8: 2 = 何もしない
const u32 kOtherCommand = 0x00949D29;       // u8: 2 = 何もしない
const u8 kCmdNone = 11, kCmdAllTabsOut = 7, kCmdRestoreField = 2, kMapIdle = 2, kMapOut = 0;
const u32 kRoomIdFn = 0x002F75CC;           // Room_GetCurrentId
// ---- 十字キーをリストにだけ渡す -------------------------------------------------------------------
//   一覧（ButtonActionControl・スクロール）は十字を BsMenuMgr の sead::ControllerWrapper（mgr+68、vtbl 0x8FF31C）から読む
//   （sub_6D33F0 = mgr+72 & マスク = wrapper の trig）。建物エディターはゲームの入力を丸ごと止めているので、
//   一覧の更新の直前だけ十字を書き、直後に元へ戻す。欄は旧 input_block.md: +4 trig / +8 release / +0xC repeat / +0x110 hold。
const u32 kMgrPad = 68;
const u32 kPadTrig = 0x04, kPadRelease = 0x08, kPadRepeat = 0x0C, kPadHold = 0x110;
const u32 kSeadUp = 0x00010000u, kSeadDown = 0x00020000u, kSeadLeft = 0x00040000u, kSeadRight = 0x00080000u;
const u32 kRepeatDelay = 15, kRepeatEvery = 4;  // フレーム（30fps）

typedef void *(*HeapAllocFn)(u32 size, void *heap, s32 align);
typedef void *(*CtorFn)(void *self);
typedef int (*ArcLoadStepFn)(void *holder, const char *path);
typedef int (*LayoutBuildFn)(void *layout, const char *name, void *holder, u32 cmdBytes);
typedef void (*LayoutFn)(void *layout);
typedef void *(*FindPaneFn)(void *layout, const char *name);
typedef int (*AnimLoadFn)(void *anim, const char *name, void *holder);
typedef void (*AnimBindFn)(void *layout, void *anim);
typedef void (*AnimSetFrameFn)(void *anim, float frame);       // 値は VFP s0（hard-float でそのまま）
typedef void (*AnimFn)(void *anim);
typedef int (*AnimFinishedFn)(void *anim);
typedef int (*SetupStepFn)(void *list, void *holder, void *heap);
typedef void (*ScrollBindFn)(void *scroll, void *pane);
typedef void (*ScrollToFn)(void *scroll, s32 top);
typedef void (*ListFn)(void *list);
typedef void (*SetSelectFn)(void *list, s32 now, s32 before);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef void *(*FontSlotFn)(void *fontMgr, u32 kind);
typedef void (*RegisterFontFn)(void *accessor, const char *name, void *font);
typedef int (*RegisterTexFn)(void *holder);

const HeapAllocFn    HeapAlloc      = reinterpret_cast<HeapAllocFn>(0x002FD0CC);    // operator new(size, heap, align)
const CtorFn         InstSelectCtor = reinterpret_cast<CtorFn>(0x007C6EB0);         // InstSelect<8>（4,812 B）
const CtorFn         ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);         // ArcResAccReader（584 B）
const CtorFn         ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const ArcLoadStepFn  ArcLoadStep    = reinterpret_cast<ArcLoadStepFn>(0x00567244);  // 終わるまで 0
const CtorFn         LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);         // ssys::ma::lyt::Layout（332 B）
const CtorFn         LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutBuildFn  LayoutBuild    = reinterpret_cast<LayoutBuildFn>(0x005685A4);
const LayoutFn       LayoutFinalize = reinterpret_cast<LayoutFn>(0x00133A5C);
const LayoutFn       LayoutCalc     = reinterpret_cast<LayoutFn>(0x00568030);       // vc_ACSYSTEM_DATA2
const FindPaneFn     FindPane       = reinterpret_cast<FindPaneFn>(0x00567BAC);
const CtorFn         AnimCtor       = reinterpret_cast<CtorFn>(0x001261EC);         // UiAnim（40 B）
const CtorFn         AnimDtor       = reinterpret_cast<CtorFn>(0x00568CAC);
const AnimLoadFn     AnimLoad       = reinterpret_cast<AnimLoadFn>(0x00568A04);
const AnimBindFn     AnimBind       = reinterpret_cast<AnimBindFn>(0x004B8ECC);
const AnimBindFn     AnimUnbind     = reinterpret_cast<AnimBindFn>(0x00568840);
const AnimSetFrameFn AnimSetFrame   = reinterpret_cast<AnimSetFrameFn>(0x00568C00);
const AnimFn         AnimStep       = reinterpret_cast<AnimFn>(0x00568964);
const AnimFinishedFn AnimFinished   = reinterpret_cast<AnimFinishedFn>(0x0074F58C);
const SetupStepFn    SetupStep      = reinterpret_cast<SetupStepFn>(0x001C513C);    // 1 で完了
const ScrollBindFn   ScrollBindPane = reinterpret_cast<ScrollBindFn>(0x00298018);
const ScrollToFn     ScrollTo       = reinterpret_cast<ScrollToFn>(0x002987A8);
const ListFn         StartEnter     = reinterpret_cast<ListFn>(0x001C58F4);         // 状態「入場処理」へ
const ListFn         StartLeave     = reinterpret_cast<ListFn>(0x001C5A34);         // 状態「退場処理」へ
const AddLayoutFn    AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);
// ★書体とテクスチャの登録（BsMenuCatalog_Init の case 0 と同じ）。無いと行の TextBox の書体（+0xE0）が 0 のままで、
//   文字箱を結ぶ vc_ACMESSAGEBOX_FUNC1 0x5E20A8 が SIGSEGV（実機 2026-09-25）。
const u32            kFontMgrPtr    = 0x0094C9C8;                                    // u32: font::Mgr
const FontSlotFn     FontName       = reinterpret_cast<FontSlotFn>(0x00747528);     // 名前（SafeString。+4 = char*）
const FontSlotFn     FontGet        = reinterpret_cast<FontSlotFn>(0x0052D6A8);     // 読み込み済みなら ResFont、まだなら 0
const RegisterFontFn RegisterFont   = reinterpret_cast<RegisterFontFn>(0x004B3FD4); // (holder+12, 名前, 書体)
const RegisterTexFn  RegisterTex    = reinterpret_cast<RegisterTexFn>(0x00568CFC);  // arc の .bclim を全部
const u32            kHolderAccessor = 12;
// ゲームのメッセージから文字列を引く（vc_NPC_2_SETUP 0x75B8A8 の中身）。WordRes = {0x904954, UTF-16*, 0x3FFFFFFF}。
//   成功で 1、+4 はメッセージデータの中の 0 終端 UTF-16（失敗なら空文字）。ロックを取るのでゲームのスレッドで呼ぶ。
// ★名前はゲームの一覧と同じく script::WordFix<38>（100 B、ctor 0x56CE48）に vc_NPC_2_SETUP 0x75B8A8 で入れ、
//   その WordFix を行の語にする（PWorkList vt[13] 0x6EDEE0 → sub_56CDEC と同じ）。以前は WordRes の文字列を
//   0 で打ち切って写しており、メッセージの制御タグ（0x000E …、引数に 0 を含む）で切れて「モダンな」だけ・空欄になった。
typedef int (*MsgSetupFn)(u32 msgData, void *word, const char *label, u32 index);
const MsgSetupFn     MsgSetup       = reinterpret_cast<MsgSetupFn>(0x0075B8A8);     // 成功で真
const CtorFn         WordFixCtor    = reinterpret_cast<CtorFn>(0x0056CE48);         // script::WordFix<38>
const u32            kWordFixBytes  = 100;
const u32            kMsgDataPtr    = 0x00957ED4;                                    // u32: vc_DATAPOINTER

const u32 kInstSelectVtbl = 0x008E54B4;     // InstSelect<8> の vtable（26 語）
const u32 kVtblWords = 26;
const u32 kWordPtrVtbl = 0x0090491C;        // script::WordPtr（vt[3] = +4 を返す）
const u32 kInstSelectBytes = 4812;

// 一覧（SelectBase / InstSelect<8>）の欄
const u32 kListLayout = 44;                 // ssys::ma::lyt::Layout
const u32 kListAnimIn = 376;                // UiAnim
const u32 kListAnimOut = 416;
const u32 kListScroll = 548;
const u32 kListTop = 2368;                  // 先頭の行番号（スクロールの +1820）
const u32 kListCount = 2500;
const u32 kListTextCap = 2504;              // 文字箱の容量
const u32 kListAnimating = 2512;            // u8
const u32 kListInputLock = 2513;            // u8: 1 = ButtonActionControl の入力を止める
const u32 kListSelected = 2792;             // s32: -1 = なし
const u32 kListRows = 8;
// Layout / UiAnim の欄
const u32 kLayoutPriority = 12;             // u8: 下画面の並び（枠と中身は 2）
const u32 kLayoutHolder = 236;              // ArcResAccReader*
const u32 kAnimTotal = 4;                   // float
const u32 kAnimCur = 8;                     // float
const u32 kAnimBound = 28;                  // u8

const char kArcPath[] = "Layout/catalog/catalogue.arc";
const u32 kFrameCmdBytes = 0x8000;          // BsMenuCatalog と同じ
// 枠の種類のアニメ（ctlg_list_kind）のフレーム。表 0x848B18[BsMenuCatalog+15264]。公共事業は 6 → 2.0（実機）。
const float kKindFrame = 2.0f;
const u32 kTeardownWaitFrames = 3;          // 描画登録をやめてから壊すまで（GPU がまだ読んでいるかもしれない）

// ---- 持ち物 --------------------------------------------------------------------------------
struct WordPtr { u32 vtbl; const u16 *text; u32 cap; };

alignas(8) u8 s_holder[584];
alignas(8) u8 s_frame[332];
alignas(8) u8 s_frameIn[40];
alignas(8) u8 s_frameOut[40];
alignas(8) u8 s_frameKind[40];
u32 s_vtblStore[1 + kVtblWords];           // [0] = TypeInfo（ゲームの vtable の 1 語手前と同じ）
u32 *const s_vtbl = s_vtblStore + 1;
u8 *s_list;                                 // ゲームのヒープ

u16 s_text[kMaxItems][kMaxChars + 1];
WordPtr s_words[kMaxItems];
alignas(8) u8 s_fix[kMaxItems][kWordFixBytes];     // ゲームの名前を入れた WordFix<38>
bool s_useFix[kMaxItems];
u32 s_count;

// プラグイン側の要求（メニューのスレッドが書き、FrameStep が読む）
u16 s_pendText[kMaxItems][kMaxChars + 1];
volatile u32 s_pendCount;
const char *volatile s_pendLabel;           // メッセージのラベル（nullptr = 使わない）
s16 s_pendMsg[kMaxItems];                   // 行ごとの番号（負 = 使わない）
volatile u32 s_itemsSeq;                    // SetItems のたびに増える
u32 s_builtSeq;
volatile bool s_want;
volatile s32 s_wantSelect = -1;
volatile u32 s_selectSeq;
u32 s_selectDone;
volatile s32 s_decided = -1;
volatile bool s_shutdown;
const char *volatile s_error = "";

enum class Stage : u8 { Off, Loading, Building, Live, Waiting };
volatile Stage s_stage = Stage::Off;
bool s_holderMade;
bool s_frameMade;       // Layout を ctor 済み
bool s_frameBuilt;      // LayoutBuild 済み
bool s_animsMade;
bool s_listSetup;       // SetupStep が 1 を返した
enum class Dir : u8 { None, In, Out };
enum class Field : u8 { Shown, Exiting, Hidden, Restoring };
volatile Field s_field = Field::Shown;
u32 s_fieldFrames;          // 今の段に入ってからのフレーム数
u32 s_fieldRoom;            // 退場させたときの部屋
volatile u32 s_dpadHeld;    // sead のビット（メニューのスレッドが書く）
u32 s_dpadPrev;
u32 s_dpadFrames;           // 押し続けているフレーム数
Dir s_frameDir = Dir::None;
Dir s_listDir = Dir::None;
bool s_hookReady;
u32 s_waitFrames;
s32 s_lastSelected = -1;

inline u8 *P(void *p, u32 off) { return reinterpret_cast<u8 *>(p) + off; }
inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(P(p, off)); }
inline s32 &S(void *p, u32 off) { return *reinterpret_cast<s32 *>(P(p, off)); }
inline float &F(void *p, u32 off) { return *reinterpret_cast<float *>(P(p, off)); }
inline u8 &B(void *p, u32 off) { return *P(p, off); }

// ---- 自前の仮想関数（ゲームが BLX で呼ぶ）-----------------------------------------------------
// vt[13]: 一覧を作る。件数を +2500 に置いて 1 を返す（0 だと組み立てが次の段へ進まない）。
int ListBuildItems(u8 *self) {
    S(self, kListCount) = (s32)s_count;
    return 1;
}

// vt[25]: i 行目の「語」。vc_ACMESSAGEBOX_FUNC2 がその vt[3] の UTF-16 を流す。
void *ListWordAt(u8 *self, s32 index) {
    (void)self;
    if (index < 0 || (u32)index >= s_count)
        index = 0;
    return s_useFix[index] ? static_cast<void *>(s_fix[index]) : static_cast<void *>(&s_words[index]);
}

// ---- UTF-8 → UTF-16（BMP だけ。範囲外は '?'）--------------------------------------------------
void Utf8To16(const char *src, u16 *dst, u32 cap) {
    u32 n = 0;
    const u8 *s = reinterpret_cast<const u8 *>(src != nullptr ? src : "");
    while (*s != 0 && n < cap) {
        u32 c = *s++;
        if (c >= 0xF0) {                    // 4 バイト（BMP 外）
            for (u32 k = 0; k < 3 && (*s & 0xC0) == 0x80; ++k)
                ++s;
            c = '?';
        } else if (c >= 0xE0) {
            u32 c1 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            u32 c2 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            c = ((c & 0x0F) << 12) | (c1 << 6) | c2;
        } else if (c >= 0xC0) {
            u32 c1 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            c = ((c & 0x1F) << 6) | c1;
        } else if (c >= 0x80) {
            c = '?';
        }
        dst[n++] = (u16)c;
    }
    dst[n] = 0;
}

// ---- アニメ（枠と中身で同じ扱い）-------------------------------------------------------------
// 今の位置の割合 [0,1]。登場の途中で退場へ切り替えるとき、見た目が飛ばないように逆側の位置へ写す。
float Progress(void *anim) {
    const float total = F(anim, kAnimTotal) - 1.0f;
    if (total <= 0.0f)
        return 1.0f;
    float p = F(anim, kAnimCur) / total;
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

// from（結合中なら）→ to へ。to のフレーム = (1 - from の進み) * (総 - 1)。from が結合されていなければ 0 から。
void SwitchAnim(void *layout, void *from, void *to) {
    float start = 0.0f;
    if (B(from, kAnimBound) != 0) {
        start = (1.0f - Progress(from)) * (F(to, kAnimTotal) - 1.0f);
        AnimUnbind(layout, from);
    }
    if (B(to, kAnimBound) == 0)
        AnimBind(layout, to);
    AnimSetFrame(to, start);
}

// ---- 片付け ---------------------------------------------------------------------------------
void DestroyAll(void) {
    if (s_list != nullptr) {
        if (s_listSetup) {
            u32 *vt = *reinterpret_cast<u32 **>(s_list);
            reinterpret_cast<ListFn>(vt[3])(s_list);    // SelectBase_Finalize
        }
        // ★組み立て途中の一覧は vt[3] が 0 の欄を辿って落ちる（+540 の文字箱）。
        //   組み立ては完了まで回してから片付ける（FrameStep がそうする）ので、ここへは来ない。
        u32 *vt = *reinterpret_cast<u32 **>(s_list);
        reinterpret_cast<ListFn>(vt[1])(s_list);        // delete 付きデストラクタ（ゲームのヒープへ返す）
        s_list = nullptr;
    }
    s_listSetup = false;
    if (s_frameMade) {
        if (s_frameBuilt)
            LayoutFinalize(s_frame);
        LayoutDtor(s_frame);
    }
    s_frameMade = s_frameBuilt = false;
    if (s_animsMade) {
        AnimDtor(s_frameIn);
        AnimDtor(s_frameOut);
        AnimDtor(s_frameKind);
    }
    s_animsMade = false;
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = false;
    s_frameDir = s_listDir = Dir::None;
    s_stage = Stage::Off;
}

// ★0 ではなく 1 がアイドル。村の屋外で 1 / フラグ 0x2（実機）。以前は「0 以外 = 開いている」と取り違え、一度も組み立てなかった。
inline u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }
inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline void W8(u32 a, u8 v) { *reinterpret_cast<volatile u8 *>(a) = v; }

u32 RoomId(void) {
    return reinterpret_cast<u32 (*)(void)>(kRoomIdFn)();
}

// タブが「タブ選択」で、命令バイトがどれも消費済み
bool FieldIdle(void) {
    const u32 mgr = R32(kMenuMgrPtr);
    const u32 tab = mgr != 0 ? R32(mgr + kMgrTab) : 0;
    if (tab == 0)
        return false;
    return R32(tab + kTabCalc) == kTabIdleCalc && R32(tab + kTabCalcAdj) == 0
        && *reinterpret_cast<const volatile u16 *>(tab + kTabMoving) == 0
        && R8(kTabCommand) == kCmdNone && R8(kMapCommand) == kMapIdle;
}

// 元の下画面 UI を hide に合わせて動かす。戻り値: 隠れきっている
bool StepField(bool hide) {
    const u32 mgr = R32(kMenuMgrPtr);
    const u32 tab = mgr != 0 ? R32(mgr + kMgrTab) : 0;
    ++s_fieldFrames;
    if (s_field != Field::Shown && RoomId() != s_fieldRoom) {
        s_field = Field::Shown;             // 場面が変わった: 下画面はゲームが作り直す。命令は出さない
        s_fieldFrames = 0;
    }
    switch (s_field) {
    case Field::Shown:
        if (hide && FieldIdle()) {
            s_fieldRoom = RoomId();
            if (R32(mgr + kMgrMap) != 0)
                W8(kMapCommand, kMapOut);
            if (R32(mgr + kMgrOther) != 0)
                W8(kOtherCommand, 0);
            W8(kTabCommand, kCmdAllTabsOut);
            s_field = Field::Exiting;
            s_fieldFrames = 0;
        }
        return false;
    case Field::Exiting:
        // 全タブが引っ込み、地図の画面が無くなった（命令は消費済み）
        if (s_fieldFrames > 1 && FieldIdle() && *reinterpret_cast<const volatile u16 *>(tab + kTabShown) == 0
            && R32(mgr + kMgrMap) == 0) {
            s_field = Field::Hidden;
            s_fieldFrames = 0;
        }
        return false;
    case Field::Hidden:
        if (!hide && FieldIdle()) {
            W8(kTabCommand, kCmdRestoreField);
            s_field = Field::Restoring;
            s_fieldFrames = 0;
            return false;
        }
        return true;
    case Field::Restoring:
        // 状態 18 → 7 → タブ選択。地図が作り直され、タブが出そろった
        if (s_fieldFrames > 1 && FieldIdle() && (R32(kMenuFlags) & 0x02u) != 0
            && *reinterpret_cast<const volatile u16 *>(tab + kTabShown) != 0) {
            s_field = Field::Shown;
            s_fieldFrames = 0;
        }
        return false;
    }
    return false;
}

bool MenuOpen(void) {
    return *reinterpret_cast<const volatile u8 *>(kMenuOpenState) != 1
        || (*reinterpret_cast<const volatile u32 *>(kMenuFlags) & 0x08u) != 0;
}

void *LytHeap(void) {
    const u32 alloc = *reinterpret_cast<const volatile u32 *>(kLytAllocatorPtr);
    return alloc != 0 ? *reinterpret_cast<void *const *>(alloc + 4) : nullptr;
}

// ---- 組み立て（1 フレームに 1 段）-------------------------------------------------------------
// 戻り値: 真 = 完了。偽 = まだ（s_error が空でなければ失敗）。
bool BuildStep(void) {
    if (!s_holderMade) {
        ArcCtor(s_holder);
        s_holderMade = true;
    }
    if (s_stage == Stage::Loading) {
        if (ArcLoadStep(s_holder, kArcPath) == 0)
            return false;
        // 書体 0 番（実機: "Garden_msg_size16.bcfnt"）とテクスチャを保持体へ登録する
        void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
        void *font = fontMgr != nullptr ? FontGet(fontMgr, 0) : nullptr;
        if (font == nullptr) {
            s_error = "ゲームの書体が取れない";
            return false;
        }
        u32 *nameObj = reinterpret_cast<u32 *>(FontName(fontMgr, 0));
        reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(nameObj[0])[2])(nameObj);   // 終端をそろえる（ゲームと同じ）
        RegisterFont(P(s_holder, kHolderAccessor), reinterpret_cast<const char *>(nameObj[1]), font);
        RegisterTex(s_holder);
        s_stage = Stage::Building;
        return false;
    }
    if (!s_frameBuilt) {
        if (!s_frameMade) {
            LayoutCtor(s_frame);
            s_frameMade = true;
        }
        W(s_frame, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
        if (LayoutBuild(s_frame, "ctlg_list.bclyt", nullptr, kFrameCmdBytes) == 0) {
            s_error = "ctlg_list.bclyt を組めない";
            return false;
        }
        s_frameBuilt = true;
        B(s_frame, kLayoutPriority) = 2;
        AnimCtor(s_frameIn);
        AnimCtor(s_frameOut);
        AnimCtor(s_frameKind);
        s_animsMade = true;
        AnimLoad(s_frameIn, "ctlg_list_in.bclan", s_holder);
        AnimLoad(s_frameOut, "ctlg_list_out.bclan", s_holder);
        AnimLoad(s_frameKind, "ctlg_list_kind.bclan", s_holder);
        // 種類の見た目を焼く（BsMenuCatalog_StartListEnter と同じ: 結合 → フレーム → 計算 → 解除）
        AnimBind(s_frame, s_frameKind);
        AnimSetFrame(s_frameKind, kKindFrame);
        LayoutCalc(s_frame);
        AnimUnbind(s_frame, s_frameKind);
        return false;
    }
    if (s_list == nullptr) {
        void *heap = LytHeap();
        if (heap == nullptr) {
            s_error = "nw::lyt のヒープが無い";
            return false;
        }
        // 一覧の文字をここで確定する（組み立て中に差し替えられても混ざらない）
        const u32 count = s_pendCount;
        s_builtSeq = s_itemsSeq;
        s_count = count;
        const char *label = s_pendLabel;
        const u32 msgData = label != nullptr ? *reinterpret_cast<const volatile u32 *>(kMsgDataPtr) : 0;
        for (u32 i = 0; i < count; ++i) {
            std::memcpy(s_text[i], s_pendText[i], sizeof(s_text[i]));
            s_useFix[i] = false;
            if (msgData != 0 && s_pendMsg[i] >= 0) {
                // ゲームの名前があればそちら（STR_Fobj_name など）。引けなければ渡された文字列のまま
                WordFixCtor(s_fix[i]);
                s_useFix[i] = MsgSetup(msgData, s_fix[i], label, (u32)s_pendMsg[i]) != 0;
            }
            s_words[i].vtbl = kWordPtrVtbl;
            s_words[i].text = s_text[i];
            s_words[i].cap = kMaxChars + 1;
        }
        void *mem = HeapAlloc(kInstSelectBytes, heap, 4);
        if (mem == nullptr) {
            s_error = "一覧の確保に失敗";
            return false;
        }
        s_list = reinterpret_cast<u8 *>(InstSelectCtor(mem));
        const u32 *base = reinterpret_cast<const u32 *>(kInstSelectVtbl);
        s_vtblStore[0] = base[-1];
        for (u32 i = 0; i < kVtblWords; ++i)
            s_vtbl[i] = base[i];
        s_vtbl[13] = reinterpret_cast<u32>(&ListBuildItems);
        s_vtbl[25] = reinterpret_cast<u32>(&ListWordAt);
        W(s_list, 0) = reinterpret_cast<u32>(s_vtbl);
        W(s_list, kListTextCap) = kMaxChars;
        return false;
    }
    if (!s_listSetup) {
        if (SetupStep(s_list, s_holder, LytHeap()) == 0)
            return false;
        s_listSetup = true;
        void *pane = FindPane(s_frame, "N_scrl_pos_00");
        if (pane != nullptr)
            ScrollBindPane(P(s_list, kListScroll), pane);
        return false;
    }
    return true;
}

void ApplySelect(s32 index) {
    if (s_list == nullptr || !s_listSetup || s_count == 0)
        return;
    if (index < 0)
        index = 0;
    if ((u32)index >= s_count)
        index = (s32)s_count - 1;
    // 見えていなければスクロールする（先頭 = index、ただし末尾で 8 行が埋まるように）
    const s32 top = S(s_list, kListTop);
    if (index < top || index >= top + (s32)kListRows) {
        s32 want = index;
        const s32 maxTop = (s32)s_count > (s32)kListRows ? (s32)s_count - (s32)kListRows : 0;
        if (want > maxTop)
            want = maxTop;
        ScrollTo(P(s_list, kListScroll), want);
    }
    u32 *vt = *reinterpret_cast<u32 **>(s_list);
    reinterpret_cast<SetSelectFn>(vt[16])(s_list, index, S(s_list, kListSelected));
    s_lastSelected = S(s_list, kListSelected);
}

void EnterBoth(void) {
    SwitchAnim(s_frame, s_frameOut, s_frameIn);
    s_frameDir = Dir::In;
    // 中身は状態機械の「入場処理」に任せる（終われば「待機処理」へ）。enter が in を 0 から結合するので、
    // 退場の途中なら out を外して in の位置を直す。
    const bool wasOut = B(P(s_list, kListAnimOut), kAnimBound) != 0;
    const float from = wasOut ? 1.0f - Progress(P(s_list, kListAnimOut)) : 0.0f;
    if (wasOut)
        AnimUnbind(P(s_list, kListLayout), P(s_list, kListAnimOut));
    StartEnter(s_list);
    AnimSetFrame(P(s_list, kListAnimIn), from * (F(P(s_list, kListAnimIn), kAnimTotal) - 1.0f));
    s_listDir = Dir::In;
}

void LeaveBoth(void) {
    SwitchAnim(s_frame, s_frameIn, s_frameOut);
    s_frameDir = Dir::Out;
    const bool wasIn = B(P(s_list, kListAnimIn), kAnimBound) != 0;
    const float from = wasIn ? 1.0f - Progress(P(s_list, kListAnimIn)) : 0.0f;
    if (wasIn)
        AnimUnbind(P(s_list, kListLayout), P(s_list, kListAnimIn));
    StartLeave(s_list);
    AnimSetFrame(P(s_list, kListAnimOut), from * (F(P(s_list, kListAnimOut), kAnimTotal) - 1.0f));
    s_listDir = Dir::Out;
}

// ★枠は毎フレーム計算する（BsMenuCatalog の一覧操作中の状態 sub_21B26C と同じ）。スクロールバーは一覧の別の
//   Layout（スクロール部品 +588）のペイン木を枠の N_scrl_pos_00 へ付け替えたもので、枠を計算しないと
//   つまみの位置が画面に反映されない（以前はアニメ中だけ計算していて、利用者報告「スクロールバーが同期していない」）。
void StepFrameAnim(void) {
    void *anim = s_frameDir == Dir::In ? s_frameIn : s_frameDir == Dir::Out ? s_frameOut : nullptr;
    if (anim != nullptr) {
        if (AnimFinished(anim)) {
            AnimUnbind(s_frame, anim);
            s_frameDir = Dir::None;
        } else {
            AnimStep(anim);
        }
    }
    LayoutCalc(s_frame);
}

}  // namespace

// ---- メニューのスレッド -----------------------------------------------------------------------
bool SetItems(const char *const *items, u32 count) {
    if (count > kMaxItems)
        count = kMaxItems;
    for (u32 i = 0; i < count; ++i)
        Utf8To16(items[i], s_pendText[i], kMaxChars);
    s_pendCount = count;
    s_pendLabel = nullptr;                  // メッセージは SetItemMessages で改めて渡す
    s_itemsSeq = s_itemsSeq + 1;
    return count > 0;
}

void FeedDpad(u32 heldKeys) {
    // CTRPF の Key: DPadRight 0x10 / DPadLeft 0x20 / DPadUp 0x40 / DPadDown 0x80（HID と同じ並び）
    u32 s = 0;
    if (heldKeys & 0x40u) s |= kSeadUp;
    if (heldKeys & 0x80u) s |= kSeadDown;
    if (heldKeys & 0x20u) s |= kSeadLeft;
    if (heldKeys & 0x10u) s |= kSeadRight;
    s_dpadHeld = s;
}

void SetItemMessages(const char *label, const s16 *indices, u32 count) {
    if (count > kMaxItems)
        count = kMaxItems;
    for (u32 i = 0; i < kMaxItems; ++i)
        s_pendMsg[i] = (indices != nullptr && i < count) ? indices[i] : (s16)-1;
    s_pendLabel = label;
    s_itemsSeq = s_itemsSeq + 1;
}

void Show(s32 selected) {
    if (!s_hookReady) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep)) {
            s_error = "フレームフックを入れられない";
            return;
        }
        s_hookReady = true;
    }
    s_wantSelect = selected;
    s_selectSeq = s_selectSeq + 1;
    s_error = "";
    s_want = true;
}

void Hide(void) {
    s_want = false;
}

bool Wanted(void) {
    return s_want;
}

bool Present(void) {
    return s_stage != Stage::Off || s_field != Field::Shown;
}

void Select(s32 index) {
    s_wantSelect = index;
    s_selectSeq = s_selectSeq + 1;
}

s32 TakeDecided(void) {
    const s32 v = s_decided;
    if (v >= 0)
        s_decided = -1;
    return v;
}

const char *LastError(void) {
    return s_error;
}

void Shutdown(void) {
    s_want = false;
    if (!s_hookReady)
        return;
    s_shutdown = true;
    for (u32 i = 0; i < 90 && (s_stage != Stage::Off || s_field != Field::Shown); ++i)
        svcSleepThread(16666667LL);
}

// ---- ゲームのスレッド -------------------------------------------------------------------------
void FrameStep(void) {
    const bool want = s_want && !s_shutdown && !MenuOpen() && s_pendCount > 0;
    void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
    // 元の下画面 UI: 出したいあいだ、またはリストが描かれているあいだは退場させておく。リストが消えてから戻す。
    const bool fieldHidden = StepField((want && s_error[0] == 0) || s_stage == Stage::Live);

    switch (s_stage) {
    case Stage::Off:
        if (!want || s_error[0] != 0)
            return;
        s_stage = Stage::Loading;
        // 続けて組み立てへ
    case Stage::Loading:
    case Stage::Building:
        if (!BuildStep()) {
            if (s_error[0] != 0) {
                // 失敗: 組み立てを最後まで回せない。一覧が組み立て途中なら壊せないので残す（漏れより落ちる方が悪い）。
                if (s_list == nullptr || s_listSetup)
                    DestroyAll();
            }
            return;
        }
        if (want && !fieldHidden)
            return;                         // 元の UI が退場しきるまで待つ（組み上がったまま描かない）
        s_stage = Stage::Live;
        s_selectDone = s_selectSeq;
        ApplySelect(s_wantSelect);
        if (want)
            EnterBoth();
        else
            LeaveBoth();                    // 組み立て中に Hide された: 出さずに片付けへ
        break;
    case Stage::Live:
        break;
    case Stage::Waiting:
        if (++s_waitFrames >= kTeardownWaitFrames)
            DestroyAll();
        return;
    }

    // ---- Live ----
    // メニューが開いた・終了: アニメを待たずに描くのをやめ、数フレーム後に壊す
    if (s_shutdown || MenuOpen()) {
        s_stage = Stage::Waiting;
        s_waitFrames = 0;
        return;
    }
    // 一覧が差し替えられた: 退場させて組み直す
    const bool stale = s_builtSeq != s_itemsSeq;
    const bool show = want && !stale && fieldHidden;
    if (show && s_frameDir != Dir::In && s_listDir != Dir::In && (s_frameDir == Dir::Out || s_listDir == Dir::Out))
        EnterBoth();
    else if (!show && s_frameDir != Dir::Out && s_listDir != Dir::Out)
        LeaveBoth();

    if (s_selectDone != s_selectSeq) {
        s_selectDone = s_selectSeq;
        ApplySelect(s_wantSelect);
    }

    // 入力はアニメ中は止める。中身の状態機械は vt[4] が進める（入場・退場・待機）。
    B(s_list, kListInputLock) = (s_frameDir != Dir::None || B(s_list, kListAnimating) != 0 || !show) ? 1 : 0;
    u32 *vt = *reinterpret_cast<u32 **>(s_list);
    // 十字キーを一覧にだけ渡す（入力を止めていない間だけ）
    const u32 menuMgr = R32(kMenuMgrPtr);
    const bool feed = menuMgr != 0 && B(s_list, kListInputLock) == 0;
    u32 saved[4] = {};
    if (feed) {
        const u32 pad = menuMgr + kMgrPad;
        const u32 hold = s_dpadHeld;
        const u32 trig = hold & ~s_dpadPrev;
        s_dpadFrames = (hold != 0 && hold == s_dpadPrev) ? s_dpadFrames + 1 : 0;
        const bool rep = trig != 0 || (s_dpadFrames >= kRepeatDelay && ((s_dpadFrames - kRepeatDelay) % kRepeatEvery) == 0);
        saved[0] = W(reinterpret_cast<void *>(pad), kPadTrig);
        saved[1] = W(reinterpret_cast<void *>(pad), kPadRelease);
        saved[2] = W(reinterpret_cast<void *>(pad), kPadRepeat);
        saved[3] = W(reinterpret_cast<void *>(pad), kPadHold);
        W(reinterpret_cast<void *>(pad), kPadTrig) = saved[0] | trig;
        W(reinterpret_cast<void *>(pad), kPadRelease) = saved[1] | (s_dpadPrev & ~hold);
        W(reinterpret_cast<void *>(pad), kPadRepeat) = saved[2] | (rep ? hold : 0);
        W(reinterpret_cast<void *>(pad), kPadHold) = saved[3] | hold;
        s_dpadPrev = hold;
    }
    reinterpret_cast<ListFn>(vt[4])(s_list);            // SelectBase_Update
    if (feed) {
        const u32 pad = menuMgr + kMgrPad;
        W(reinterpret_cast<void *>(pad), kPadTrig) = saved[0];
        W(reinterpret_cast<void *>(pad), kPadRelease) = saved[1];
        W(reinterpret_cast<void *>(pad), kPadRepeat) = saved[2];
        W(reinterpret_cast<void *>(pad), kPadHold) = saved[3];
    }
    if (s_listDir != Dir::None && B(s_list, kListAnimating) == 0)
        s_listDir = Dir::None;
    StepFrameAnim();

    // タッチで決定された行（待機処理が +2792 に置く）
    const s32 sel = S(s_list, kListSelected);
    if (sel != s_lastSelected) {
        s_lastSelected = sel;
        if (sel >= 0 && (u32)sel < s_count)
            s_decided = sel;
    }

    // 退場が両方終わったら片付けへ（この フレームから描かない）
    if (!show && s_frameDir == Dir::None && s_listDir == Dir::None) {
        s_stage = Stage::Waiting;
        s_waitFrames = 0;
        return;
    }

    // 描画登録（リストは RenderBottom のあと毎フレーム空になる）。枠 → 中身（どちらも優先度 2、入れた順）
    if (mgr != nullptr) {
        AddLayout(mgr, s_frame, 1);
        reinterpret_cast<ListFn>(vt[5])(s_list);        // SelectBase_Draw: 計算 + AddLayout(+44, 1)
    }
}

}  // namespace GameList
