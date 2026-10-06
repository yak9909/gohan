// DecorTrash — 家の模様替えに HHD のゴミ箱を足す（T022、2026-10-07。未実機）
//
// 根拠（解析リポジトリ project_v2。work/FINDINGS.md IDA-opus-5.5-F104 / F105、work/hhd/FINDINGS.md HHD-F015 / F017、
//      設計 docs/topics/t022_decorate_port_requirements.md §5.5）:
//   模様替え UI は CRO ModuleIndoor（チップ）と ModuleFtr（家具の掴み）。番地・照合語・当たりは自動生成の DecorTrashTable.h
//     （tools/hhd/export_decor_trash.py）。CRO の実行時の先頭 = 本体の取り込み口の語 − オフセット。照合語が全部合うときだけ触る。
//   チップ（エディター + 1176 + 1120 × i、112 個）: +1117 使用中、+12/+16 = 状態の calc（PMF）、+1032 = 家具の記録の番号（s16）、
//     +1068/+1072 = 押し始めの点、+1084/+1088 = いまの指の点（Chip_Update が**触れている間だけ**書く）、+1036/+1044 = 一緒に動くチップの数と配列。
//   離したフレームの Drop（FtrMgr_DropSingle / Group）は最後に書かれた +1084 を使う。ゴミ箱の上にいる間、毎フレーム +1084 を押し始めの点にしておけば、
//     離したときだけ「押し始めで離した」= ゲーム自身が元の位置へ戻し、掴みの状態も片付ける。そのあと家具を消す（AcFtr_ChangeState(家具, 6)。
//     F104 で実機確認済み。記録は AcFtr_DestroyBegin が外す）。チップは開いた時しか作られないので、消した家具のチップは自前で Out にする。
//   音: 元へ戻す音（SE_SYS_FUR_OFF / PUT / BACK）は鳴らさない（利用者 2026-10-07）。サウンド管理 [0x947080] +3165 が 0 でないと
//     新しい効果音を鳴らさない（Game_PlaySound → sub_6B40B8 の判定）ので、ゴミ箱の上にいる間だけ立てて、終わったら元の値へ戻す。
//   HHD のゴミ箱: 部品の状態 Wait / OnTarget（"on" + 音）/ OffTarget（"off"）/ DroppedTarget（"ok" + 音）。当たり = B_Base_00 の矩形に点が入る。
//   資源 hhd_decorate.arc（tools/hhd/make_decorate_arc.py。SD の gohan/common/）。

#include "DecorTrash.hpp"
#include "DecorTrashTable.h"
#include "Cheats.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"
#include "GohanFiles.hpp"

#include <CTRPluginFramework.hpp>
#include <cstring>

namespace DecorTrash {

namespace {

using namespace CTRPluginFramework;
using namespace DecorTrashTable;
using Cheats::kDecorTrash;

// ---- レイアウトの関数（HhdScreen.cpp と同じ番地・同じ手順。実機確認済みの経路）----
typedef void *(*CtorFn)(void *self);
typedef int (*AttachFn)(void *accessor, void *arc, const char *root);
typedef int (*LayoutBuildFn)(void *layout, const char *name, void *holder, u32 cmdBytes);
typedef void (*LayoutFn)(void *layout);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef int (*RegisterTexFn)(void *holder);
typedef void (*FlushFn)(void *p, u32 size);
typedef void *(*HeapAllocFn)(void *heap, u32 size, u32 align);
typedef void (*HeapFreeFn)(void *heap, void *p);
typedef void *(*FontSlotFn)(void *fontMgr, u32 kind);
typedef void (*RegisterFontFn)(void *accessor, const char *name, void *font);
typedef void *(*FindGroupFn)(void *layout, const char *name, u32 recursive);
typedef int (*AnimLoadFn)(void *anim, const char *name, void *holder);
typedef void (*GroupAnimFn)(void *layout, void *anim, void *group, u32 zero);
typedef void (*AnimSetFrameFn)(void *anim, float frame);
typedef int (*AnimFinishedFn)(void *anim);

const CtorFn         ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);
const CtorFn         ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const AttachFn       Attach         = reinterpret_cast<AttachFn>(0x004B5900);
const RegisterTexFn  RegisterTex    = reinterpret_cast<RegisterTexFn>(0x00568CFC);
const FlushFn        FlushRange     = reinterpret_cast<FlushFn>(0x0012D294);
const CtorFn         LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);
const CtorFn         LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutBuildFn  LayoutBuild    = reinterpret_cast<LayoutBuildFn>(0x005685A4);
const LayoutFn       LayoutFinalize = reinterpret_cast<LayoutFn>(0x00133A5C);
const LayoutFn       LayoutCalc     = reinterpret_cast<LayoutFn>(0x00568030);
const AddLayoutFn    AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);
const FontSlotFn     FontName       = reinterpret_cast<FontSlotFn>(0x00747528);
const FontSlotFn     FontGet        = reinterpret_cast<FontSlotFn>(0x0052D6A8);
const RegisterFontFn RegisterFont   = reinterpret_cast<RegisterFontFn>(0x004B3FD4);
const CtorFn         AnimCtor       = reinterpret_cast<CtorFn>(0x001261EC);
const CtorFn         AnimDtor       = reinterpret_cast<CtorFn>(0x00568CAC);
const AnimLoadFn     AnimLoad       = reinterpret_cast<AnimLoadFn>(0x00568A04);
const FindGroupFn    FindGroup      = reinterpret_cast<FindGroupFn>(0x004B4328);
const GroupAnimFn    GroupBind      = reinterpret_cast<GroupAnimFn>(0x00567A50);
const GroupAnimFn    GroupUnbind    = reinterpret_cast<GroupAnimFn>(0x00567DA4);
const AnimSetFrameFn AnimSetFrame   = reinterpret_cast<AnimSetFrameFn>(0x00568C00);
const LayoutFn       AnimStep       = reinterpret_cast<LayoutFn>(0x00568964);
const AnimFinishedFn AnimFinished   = reinterpret_cast<AnimFinishedFn>(0x0074F58C);

const u32 kLayoutMgrPtr = 0x0096FC38;
const u32 kFontMgrPtr = 0x0094C9C8;
const u32 kLoaderPtr = 0x0096FC40, kLoaderHeap = 4;
const u32 kLoaderAlignPtr = 0x0096FC2C;
const u32 kHeapAllocSlot = 24 / 4, kHeapFreeSlot = 28 / 4;
const u32 kHolderArc = 8, kHolderAccessor = 0xC, kHolderArcLoaded = 0x158;
const u32 kLayoutHolder = 236;
const u32 kScreenLower = 1;
const u32 kTeardownWaitFrames = 3;          // GameLabel / HhdScreen と同じ（描くのをやめてから壊すまで）
const u32 kCmdBytes = 0x2000;               // ペイン 11・絵 5 枚。HhdScreen の地（784 B 実測）に余裕を足した値（未実測）

const char kArcName[] = "hhd_decorate.arc";
const char kLayoutName[] = "hhd_trash.bclyt";
const u32 kMaxArcBytes = 0x10000;

// ---- 家具を消す（F104）----
typedef void *(*FtrFromIndexFn)(u32 *index);          // sub_4E8650: 失敗すると *index = -1 にする（写しを渡す）
typedef int (*FtrChangeStateFn)(void *ftr, u32 state);
typedef int (*FtrReadyFn)(void *ftr);
const FtrFromIndexFn   FtrFromIndex   = reinterpret_cast<FtrFromIndexFn>(0x004E8650);
const FtrChangeStateFn FtrChangeState = reinterpret_cast<FtrChangeStateFn>(0x0056FD10);   // AcFtr_ChangeState
const u32 kFtrReadySlot = 144 / 4;          // vtable +144（偽なら +1892 |= 0x800 で後回し。ゲーム自身の消去 sub_4E4FD0 と同じ）
const u32 kFtrLaterFlags = 1892;
const u32 kFtrStateRemove = 6;

// ---- 模様替え（F105）----
const u32 kEditorChips = 1176, kChipStride = 1120, kChipCount = 112;
const u32 kEditorCalc = 52;                 // エディター +40 の状態 → +52 = 今の calc
const u32 kChipInUse = 1117, kChipCalc = 12, kChipCalcAdj = 16, kChipRecord = 1032;
const u32 kChipStartX = 1068, kChipStartY = 1072, kChipNowX = 1084, kChipNowY = 1088;
const u32 kChipFollowCount = 1036, kChipFollowArray = 1044;
const u32 kRecordFtrIndex = 4;
typedef void *(*RecGetFn)(const s16 *index);
typedef int (*RecAliveFn)(const s16 *index);
typedef void (*ChangeByCalcFn)(void *obj, u32 calc, u32 adj);

// ---- 音 ----
typedef void (*PlaySoundFn)(u32 id);
const PlaySoundFn PlaySound = reinterpret_cast<PlaySoundFn>(0x0058C7D4);   // Game_PlaySound
// HHD の音名 onTarget / dropped の代わり（SOUND/index/sounds.csv）。SE_FUR_TRASH は GROUP_HOUSE（家の中で読み込み済み）
const u32 kSndOnTarget = 0x0100080B;        // SE_FUR_TRASH
const u32 kSndDropped = 0x01000962;         // SE_FUR_TRASH_THROW（GROUP_STATIC）
const u32 kSoundMgrPtr = 0x00947080, kSoundBlockNew = 3165;   // +3165 != 0 なら新しい効果音を鳴らさない（sub_6B40B8 ほか）

// ---- 状態 ----
enum class Stage : u8 { Idle, Draw, Teardown, Failed };
enum class Trash : u8 { Wait, OnTarget, OffTarget, Dropped };
const u32 kMaxHeld = 16;

volatile bool s_enabled;
bool s_hooked, s_toldFail;
int s_index = -1;
u8 *s_file;
u32 s_fileSize;

Stage s_stage = Stage::Idle;
u32 s_wait;
bool s_leaving;
alignas(8) u8 s_holder[584];                // GameLabel / HhdScreen と同じ大きさ
alignas(8) u8 s_layout[332];
alignas(8) u8 s_anim[6][40];
enum { kAnimIn, kAnimOut, kAnimOn, kAnimOff, kAnimOk, kAnimLoop, kAnims };
const char *const kAnimName[kAnims] = { "hhd_trash_in.bclan", "hhd_trash_out.bclan", "hhd_trash_on.bclan",
                                        "hhd_trash_off.bclan", "hhd_trash_ok.bclan", "hhd_trash_loop.bclan" };
void *s_group, *s_loopGroup, *s_bound;
bool s_holderMade, s_layoutMade, s_built, s_animMade, s_loopBound;
void *s_heap, *s_arc;

Trash s_trash = Trash::Wait;
u32 s_dragChip;                             // ゴミ箱の上で掴んでいるチップ（OnTarget の間）
u32 s_heldChip[kMaxHeld], s_heldFtr[kMaxHeld];
s16 s_heldRec[kMaxHeld];
u32 s_heldCount;
bool s_blocking;
u8 s_blockSaved;

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline void W32(u32 a, u32 v) { *reinterpret_cast<volatile u32 *>(a) = v; }

inline bool IsHeapPointer(const void *p) {
    const u32 v = reinterpret_cast<u32>(p);
    return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u;
}

// ---- CRO の先頭（取り込み口から。照合語が全部合うときだけ）----
struct Mods {
    u32 indoor, ftr;                        // .text の先頭（0 = 使えない）
    u32 editor, ftrMgr;
};
u32 s_indoorSlotSeen = 0xFFFFFFFFu, s_ftrSlotSeen = 0xFFFFFFFFu, s_indoor, s_ftr;

bool WordsMatch(u32 base, const u32 (*words)[2], u32 n) {
    for (u32 i = 0; i < n; ++i) {
        const u32 a = base + words[i][0];
        if (!Process::CheckAddress(a, MEMPERM_READ) || R32(a) != words[i][1])
            return false;
    }
    return true;
}

u32 Resolve(u32 slot, u32 offset, u32 &seen, u32 &cached, const u32 (*words)[2], u32 n) {
    const u32 w = R32(slot);
    if (w != seen) {                        // 読み込み・入れ替えのときだけ照合し直す
        seen = w;
        cached = 0;
        if (w != kUnloadedSlotWord && w > offset && WordsMatch(w - offset, words, n))
            cached = w - offset;
    }
    return cached;
}

Mods Modules(void) {
    static const u32 indoorWords[][2] = {
        { kIndoorChipDragCalc, kIndoorChipDragCalcWord }, { kIndoorChipDropCalc, kIndoorChipDropCalcWord },
        { kIndoorChipOutCalc, kIndoorChipOutCalcWord }, { kIndoorStateChange, kIndoorStateChangeWord },
        { kIndoorEditorInCalc, kIndoorEditorInCalcWord }, { kIndoorEditorNeutralCalc, kIndoorEditorNeutralCalcWord },
        { kIndoorEditorOutCalc, kIndoorEditorOutCalcWord }, { kIndoorSlotTarget, kIndoorSlotTargetWord },
    };
    static const u32 ftrWords[][2] = {
        { kFtrRecGet, kFtrRecGetWord }, { kFtrRecIsAlive, kFtrRecIsAliveWord }, { kFtrSlotTarget, kFtrSlotTargetWord },
    };
    Mods m = { 0, 0, 0, 0 };
    m.indoor = Resolve(kIndoorSlot, kIndoorSlotOffset, s_indoorSlotSeen, s_indoor, indoorWords, sizeof(indoorWords) / sizeof(indoorWords[0]));
    m.ftr = Resolve(kFtrSlot, kFtrSlotOffset, s_ftrSlotSeen, s_ftr, ftrWords, sizeof(ftrWords) / sizeof(ftrWords[0]));
    if (m.indoor == 0 || m.ftr == 0)
        return m;
    const u32 editorAt = R32(m.indoor + kIndoorEditorPtrLiteral);
    const u32 ftrMgrAt = R32(m.indoor + kIndoorFtrMgrPtrLiteral);
    if (!Process::CheckAddress(editorAt, MEMPERM_READ) || !Process::CheckAddress(ftrMgrAt, MEMPERM_READ))
        return m;
    const u32 editor = R32(editorAt), ftrMgr = R32(ftrMgrAt);
    if (IsHeapPointer(reinterpret_cast<void *>(editor)))
        m.editor = editor;
    if (IsHeapPointer(reinterpret_cast<void *>(ftrMgr)))
        m.ftrMgr = ftrMgr;
    return m;
}

// 模様替えが開いていて、ゴミ箱を出してよい段（エディターの In / Neutral）
bool EditorLive(const Mods &m) {
    if (m.editor == 0)
        return false;
    const u32 calc = R32(m.editor + kEditorCalc);
    return calc == m.indoor + kIndoorEditorInCalc || calc == m.indoor + kIndoorEditorNeutralCalc;
}

u32 FindDragChip(const Mods &m) {
    for (u32 i = 0; i < kChipCount; ++i) {
        const u32 chip = m.editor + kEditorChips + kChipStride * i;
        if (*reinterpret_cast<const volatile u8 *>(chip + kChipInUse) == 0)
            continue;
        if (R32(chip + kChipCalc) == m.indoor + kIndoorChipDragCalc && R32(chip + kChipCalcAdj) == 0)
            return chip;
    }
    return 0;
}

// ---- 音を止める（ゴミ箱の上にいる間だけ）----
void Block(bool on) {
    const u32 mgr = R32(kSoundMgrPtr);
    if (!IsHeapPointer(reinterpret_cast<void *>(mgr)))
        return;
    volatile u8 *b = reinterpret_cast<volatile u8 *>(mgr + kSoundBlockNew);
    if (on && !s_blocking) {
        s_blockSaved = *b;
        *b = 1;
        s_blocking = true;
    } else if (!on && s_blocking) {
        *b = s_blockSaved;
        s_blocking = false;
    }
}

// ---- 掴んでいる家具（チップ + 一緒に動くチップ）を控える ----
void Capture(const Mods &m, u32 chip) {
    const RecGetFn recGet = reinterpret_cast<RecGetFn>(m.ftr + kFtrRecGet);
    const RecAliveFn recAlive = reinterpret_cast<RecAliveFn>(m.ftr + kFtrRecIsAlive);
    u32 chips[kMaxHeld];
    u32 n = 0;
    chips[n++] = chip;
    const u32 follow = R32(chip + kChipFollowCount), array = R32(chip + kChipFollowArray);
    if (IsHeapPointer(reinterpret_cast<void *>(array)))
        for (u32 i = 0; i < follow && n < kMaxHeld; ++i)
            chips[n++] = R32(array + 4 * i);
    s_heldCount = 0;
    for (u32 i = 0; i < n; ++i) {
        const u32 c = chips[i];
        if (!IsHeapPointer(reinterpret_cast<void *>(c)))
            continue;
        const s16 rec = *reinterpret_cast<const volatile s16 *>(c + kChipRecord);
        if (rec < 0 || !recAlive(&rec))
            continue;
        const u32 record = reinterpret_cast<u32>(recGet(&rec));
        u32 index = R32(record + kRecordFtrIndex);
        void *ftr = FtrFromIndex(&index);
        if (ftr == nullptr)
            continue;                       // 家具のオブジェクトでない物（記録 +8 の側）は消さない
        s_heldChip[s_heldCount] = c;
        s_heldRec[s_heldCount] = rec;
        s_heldFtr[s_heldCount] = reinterpret_cast<u32>(ftr);
        ++s_heldCount;
    }
}

// 離したあと（ゲームが元の位置へ戻したあと）に消す
void RemoveHeld(const Mods &m) {
    const RecGetFn recGet = reinterpret_cast<RecGetFn>(m.ftr + kFtrRecGet);
    const RecAliveFn recAlive = reinterpret_cast<RecAliveFn>(m.ftr + kFtrRecIsAlive);
    const ChangeByCalcFn change = reinterpret_cast<ChangeByCalcFn>(m.indoor + kIndoorStateChange);
    for (u32 i = 0; i < s_heldCount; ++i) {
        const s16 rec = s_heldRec[i];
        if (!recAlive(&rec))
            continue;
        u32 index = R32(reinterpret_cast<u32>(recGet(&rec)) + kRecordFtrIndex);
        void *ftr = FtrFromIndex(&index);
        if (ftr == nullptr || reinterpret_cast<u32>(ftr) != s_heldFtr[i])
            continue;                       // 控えた後に別の家具へ替わっていたら触らない
        const FtrReadyFn ready = reinterpret_cast<FtrReadyFn>((*reinterpret_cast<u32 **>(ftr))[kFtrReadySlot]);
        if (ready(ftr))
            FtrChangeState(ftr, kFtrStateRemove);
        else
            *reinterpret_cast<volatile u16 *>(reinterpret_cast<u32>(ftr) + kFtrLaterFlags) |= 0x800u;
        const u32 chip = s_heldChip[i];
        if (*reinterpret_cast<const volatile u8 *>(chip + kChipInUse) != 0)
            change(reinterpret_cast<void *>(chip), m.indoor + kIndoorChipOutCalc, 0);
    }
    s_heldCount = 0;
}

// ---- レイアウト ----
void BindAnim(u32 which) {
    if (s_bound != nullptr)
        GroupUnbind(s_layout, s_bound, s_group, 0);
    GroupBind(s_layout, s_anim[which], s_group, 0);
    AnimSetFrame(s_anim[which], 0.0f);
    s_bound = s_anim[which];
}

bool StepAnim(void) {
    if (s_bound == nullptr)
        return false;
    if (AnimFinished(s_bound)) {
        GroupUnbind(s_layout, s_bound, s_group, 0);
        s_bound = nullptr;
        return false;
    }
    AnimStep(s_bound);
    return true;
}

void Release(void) {
    if (s_layoutMade) {
        if (s_built && s_loopBound)
            GroupUnbind(s_layout, s_anim[kAnimLoop], s_loopGroup, 0);
        if (s_built && s_bound != nullptr)
            GroupUnbind(s_layout, s_bound, s_group, 0);
        if (s_built)
            LayoutFinalize(s_layout);
        LayoutDtor(s_layout);
    }
    if (s_animMade)
        for (u32 i = 0; i < kAnims; ++i)
            AnimDtor(s_anim[i]);
    if (s_holderMade)
        ArcDtor(s_holder);
    if (s_arc != nullptr && s_heap != nullptr)
        reinterpret_cast<HeapFreeFn>((*reinterpret_cast<u32 **>(s_heap))[kHeapFreeSlot])(s_heap, s_arc);
    s_arc = s_heap = nullptr;
    s_group = s_loopGroup = s_bound = nullptr;
    s_holderMade = s_layoutMade = s_built = s_animMade = s_loopBound = false;
}

bool Fail(void) {
    s_stage = Stage::Failed;
    return false;
}

bool Build(void) {
    const u32 loader = R32(kLoaderPtr);
    if (!IsHeapPointer(reinterpret_cast<void *>(loader)))
        return Fail();
    void *heap = *reinterpret_cast<void **>(loader + kLoaderHeap);
    void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
    if (!IsHeapPointer(heap) || fontMgr == nullptr || FontGet(fontMgr, 0) == nullptr || s_file == nullptr)
        return Fail();
    const u32 align = R32(kLoaderAlignPtr);
    void *arc = reinterpret_cast<HeapAllocFn>((*reinterpret_cast<u32 **>(heap))[kHeapAllocSlot])(heap, s_fileSize, align);
    if (!IsHeapPointer(arc))
        return Fail();
    s_heap = heap;
    s_arc = arc;
    std::memcpy(arc, s_file, s_fileSize);
    FlushRange(arc, s_fileSize);
    ArcCtor(s_holder);
    s_holderMade = true;
    W32(reinterpret_cast<u32>(s_holder) + kHolderArcLoaded, reinterpret_cast<u32>(arc));
    W32(reinterpret_cast<u32>(s_holder) + kHolderArc, reinterpret_cast<u32>(arc));
    if (Attach(s_holder + kHolderAccessor, arc, ".") == 0)
        return Fail();
    u32 *name = reinterpret_cast<u32 *>(FontName(fontMgr, 0));
    reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(name[0])[2])(name);
    RegisterFont(s_holder + kHolderAccessor, reinterpret_cast<const char *>(name[1]), FontGet(fontMgr, 0));
    RegisterTex(s_holder);
    LayoutCtor(s_layout);
    s_layoutMade = true;
    W32(reinterpret_cast<u32>(s_layout) + kLayoutHolder, reinterpret_cast<u32>(s_holder));
    if (LayoutBuild(s_layout, kLayoutName, nullptr, kCmdBytes) == 0)
        return Fail();
    s_built = true;                         // 優先度は既定（0x80。sub_12D2BC）のまま
    for (u32 i = 0; i < kAnims; ++i)
        AnimCtor(s_anim[i]);
    s_animMade = true;
    for (u32 i = 0; i < kAnims; ++i)
        if (AnimLoad(s_anim[i], kAnimName[i], s_holder) == 0)
            return Fail();
    s_group = FindGroup(s_layout, "G_InOut", 1);
    s_loopGroup = FindGroup(s_layout, "G_Loop", 1);
    if (s_group == nullptr || s_loopGroup == nullptr)
        return Fail();
    GroupBind(s_layout, s_anim[kAnimLoop], s_loopGroup, 0);   // 縞の流れ（HHD は G_Loop を常に再生。HHD-F009）
    AnimSetFrame(s_anim[kAnimLoop], 0.0f);
    s_loopBound = true;
    BindAnim(kAnimIn);
    s_trash = Trash::Wait;
    s_leaving = false;
    s_stage = Stage::Draw;
    return true;
}

// 掴みを放す（ゴミ箱を使わずに終わる）: 音を戻し、控えを捨てる
void Disarm(void) {
    Block(false);
    s_dragChip = 0;
    s_heldCount = 0;
}

bool InRect(u32 x, u32 y) {
    const float fx = (float)x, fy = (float)y;
    return fx >= kHitLeft && fx < kHitRight && fy >= kHitTop && fy < kHitBottom;
}

void TrashStep(const Mods &m) {
    const bool touching = Touch::IsDown();
    const UIntVector pos = Touch::GetPosition();
    switch (s_trash) {
    case Trash::Wait:
    case Trash::OffTarget: {
        if (s_trash == Trash::OffTarget && s_bound == nullptr)
            s_trash = Trash::Wait;
        const u32 chip = FindDragChip(m);
        if (chip != 0 && touching && InRect(pos.x, pos.y)) {
            BindAnim(kAnimOn);
            PlaySound(kSndOnTarget);        // 鳴らしてから止める（止めるのは新しく鳴らす分だけ）
            s_dragChip = chip;
            Capture(m, chip);
            Block(true);
            s_trash = Trash::OnTarget;
        }
        break;
    }
    case Trash::OnTarget: {
        const u32 chip = s_dragChip;
        const u32 calc = R32(chip + kChipCalc);
        if (calc == m.indoor + kIndoorChipDragCalc) {
            if (touching && !InRect(pos.x, pos.y)) {
                Disarm();                   // ゴミ箱から外れた: この フレームの Chip_Update が本当の指の点を書いている
                BindAnim(kAnimOff);
                s_trash = Trash::OffTarget;
                break;
            }
            if (touching)
                Capture(m, chip);
            // 押し始めの点にしておく（触れている間は次の Chip_Update が上書きする。離したときだけ Drop がこれを使う）
            W32(chip + kChipNowX, R32(chip + kChipStartX));
            W32(chip + kChipNowY, R32(chip + kChipStartY));
            break;
        }
        if (calc == m.indoor + kIndoorChipDropCalc) {
            Block(false);                   // ゲームが元の位置へ戻し終えた（音は止めていた）
            RemoveHeld(m);
            BindAnim(kAnimOk);
            PlaySound(kSndDropped);
            s_dragChip = 0;
            s_trash = Trash::Dropped;
            break;
        }
        Disarm();                           // Drop 以外へ抜けた（エディターが閉じた等）: 何も消さない
        BindAnim(kAnimOff);
        s_trash = Trash::OffTarget;
        break;
    }
    case Trash::Dropped:
        if (s_bound == nullptr)
            s_trash = Trash::Wait;
        break;
    }
}

void FrameStep(void) {
    switch (s_stage) {
    case Stage::Idle: {
        if (!s_enabled || s_file == nullptr)
            return;
        const Mods m = Modules();
        if (!EditorLive(m))
            return;
        if (!Build())
            return;
        [[fallthrough]];                    // 組めたフレームから描く
    }
    case Stage::Draw: {
        const Mods m = Modules();
        const bool want = s_enabled && EditorLive(m);
        if (!want && !s_leaving) {
            Disarm();
            BindAnim(kAnimOut);
            s_leaving = true;
        }
        const bool moving = StepAnim();
        if (s_loopBound)
            AnimStep(s_anim[kAnimLoop]);
        if (s_leaving) {
            if (!moving) {
                s_stage = Stage::Teardown;  // このフレームから描かない。壊すのは数フレーム後
                s_wait = 0;
                return;
            }
        } else {
            TrashStep(m);
        }
        void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
        LayoutCalc(s_layout);
        if (mgr != nullptr)
            AddLayout(mgr, s_layout, kScreenLower);
        return;
    }
    case Stage::Teardown:
        if (++s_wait < kTeardownWaitFrames)
            return;
        Release();
        s_stage = Stage::Idle;
        return;
    case Stage::Failed:
        Disarm();
        Release();
        s_enabled = false;
        s_stage = Stage::Idle;
        return;
    }
}

bool Load(void) {
    if (s_file != nullptr)
        return true;
    char path[96];
    if (!GohanFiles::CommonPath(path, sizeof(path), kArcName))
        return false;
    u32 size = 0;
    u8 *data = GohanFiles::ReadAll(path, kMaxArcBytes, size);
    if (data == nullptr)
        return false;
    s_fileSize = size;
    s_file = data;
    return true;
}

}  // namespace

void Wire(void) {
    s_index = GuiMenu::FindItem(kDecorTrash);
}

bool Tick(int index, unsigned short) {
    if (index != s_index || index < 0)
        return false;
    if (s_enabled)
        return true;
    if (!Load()) {
        if (!s_toldFail)
            GuiNotification::NotifyRed(kDecorTrash, u8"SD の gohan/common/hhd_decorate.arc を読めません。");
        s_toldFail = true;
        return true;
    }
    if (!s_hooked)
        s_hooked = GridCursor::InstallFrameHook() && GridCursor::AddExtraFrameStep(FrameStep);
    if (!s_hooked) {
        if (!s_toldFail)
            GuiNotification::NotifyRed(kDecorTrash, u8"フレームの相乗りを入れられません。");
        s_toldFail = true;
        return true;
    }
    s_toldFail = false;
    s_enabled = true;
    return true;
}

bool Disable(int index) {
    if (index != s_index || index < 0)
        return false;
    s_enabled = false;                      // ゲームのスレッドが退場させて片付ける
    return true;
}

}  // namespace DecorTrash
