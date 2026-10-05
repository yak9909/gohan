#include "HhdScreen.hpp"
#include "GridCursor.hpp"
#include "Cheats.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"

#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

// 根拠（解析リポジトリ project_v2、主 IDB acnl_jpn_v15_annotated.i64。すべて静的に読んだもの。実機は未確認）:
//   ArcResAccReader_LoadArcStep 0x567244: 読み終えると holder+0x158 と holder+8 に arc の先頭を入れ、
//     nwlyt_ArcResourceAccessor_Attach(holder+0xC, arc, ".") 0x4B5900 を呼ぶ（戻り 1 = DARC を解けた）。
//   読み込みの器: ヒープ = [[0x96FC40]+4]（sub_5691E4）、アライン = [0x96FC2C]。確保 = ヒープの vtable +24 (heap, size, align)
//     （sub_56A010 0x56A0F8）、解放 = vtable +28 (heap, ptr)（sub_139574 0x139710）。読み終えたら sub_12D294(先頭, 大きさ) を呼ぶ（0x569FC0）。
//   holder+0x15C の読み込み係（FileRes）は使わないので状態は 0 のまま。ArcResAccReader_Dtor 0x567310 はそれを返そうとしない
//     （sub_139574 は状態 0 なら何もしない）→ 器はこちらで返す。
//   それ以外（テクスチャの登録・レイアウトの組み立て・LayoutMgr）は GameLabel.cpp と同じ関数・同じ順（実機確認済みの経路）。

namespace HhdScreen {

namespace {

using namespace CTRPluginFramework;

typedef void *(*CtorFn)(void *self);
typedef int (*AttachFn)(void *accessor, void *arc, const char *root);
typedef int (*LayoutBuildFn)(void *layout, const char *name, void *holder, u32 cmdBytes);
typedef void (*LayoutFn)(void *layout);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef int (*RegisterTexFn)(void *holder);
typedef void (*FlushFn)(void *p, u32 size);
typedef void *(*HeapAllocFn)(void *heap, u32 size, u32 align);
typedef void (*HeapFreeFn)(void *heap, void *p);

const CtorFn        ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);
const CtorFn        ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const AttachFn      Attach         = reinterpret_cast<AttachFn>(0x004B5900);
const RegisterTexFn RegisterTex    = reinterpret_cast<RegisterTexFn>(0x00568CFC);
const FlushFn       FlushRange     = reinterpret_cast<FlushFn>(0x0012D294);
const CtorFn        LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);
const CtorFn        LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutBuildFn LayoutBuild    = reinterpret_cast<LayoutBuildFn>(0x005685A4);
const LayoutFn      LayoutFinalize = reinterpret_cast<LayoutFn>(0x00133A5C);
const LayoutFn      LayoutCalc     = reinterpret_cast<LayoutFn>(0x00568030);
const AddLayoutFn   AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);

const u32 kLayoutMgrPtr = 0x0096FC38;
const u32 kLoaderPtr = 0x0096FC40, kLoaderHeap = 4;     // [[0x96FC40]+4] = 読み込みのヒープ
const u32 kLoaderAlignPtr = 0x0096FC2C;
const u32 kHeapAllocSlot = 24 / 4, kHeapFreeSlot = 28 / 4;
const u32 kHolderArc = 8, kHolderAccessor = 0xC, kHolderArcLoaded = 0x158;
const u32 kLayoutPriority = 12, kLayoutHolder = 236;
const u32 kCmdBytes = 0x5510;               // GameLabel（BsTimeBelWindow）と同じ。地は 4 ペインなので足りる
const u8 kPriority = 0x80;                  // GameLabel と同じ
const u32 kScreenLower = 1;                 // AddLayout の画面（GameLabel: 下画面 = 1）
const u32 kTeardownWaitFrames = 3;          // GameLabel と同じ（描くのをやめてから壊すまで）

const char kArcPath[] = "/hhd_bg_only.arc";
const char kLayoutName[] = "hhd_bg.bclyt";
const u32 kMaxArcBytes = 0x80000;

enum class Stage : u8 { Idle, Copy, Draw, Teardown, Failed };

alignas(8) u8 s_holder[584];                // GameLabel と同じ大きさ
alignas(8) u8 s_layout[332];
u8 *s_file;                                 // SD から読んだ arc（プラグインのメモリ。メニューのスレッドが作る）
u32 s_fileSize;
void *s_heap, *s_arc;                       // ゲームのヒープに写した arc
bool s_holderMade, s_layoutMade, s_layoutBuilt, s_hookReady;
volatile bool s_want;
volatile Stage s_stage = Stage::Idle;
const char *volatile s_error = "";
u32 s_wait;

inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(p) + off); }

bool IsHeapPointer(const void *p) {
    const u32 v = reinterpret_cast<u32>(p);
    return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u;
}

void Fail(const char *why) {
    s_error = why;
    s_stage = Stage::Failed;
}

void Release(void) {
    if (s_layoutMade) {
        if (s_layoutBuilt)
            LayoutFinalize(s_layout);
        LayoutDtor(s_layout);
    }
    s_layoutMade = s_layoutBuilt = false;
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = false;
    if (s_arc != nullptr && s_heap != nullptr)
        reinterpret_cast<HeapFreeFn>((*reinterpret_cast<u32 **>(s_heap))[kHeapFreeSlot])(s_heap, s_arc);
    s_arc = s_heap = nullptr;
}

// arc をゲームのヒープへ写し、ArcResAccReader に渡してテクスチャを登録し、レイアウトを組む
void Build(void) {
    const u32 loader = *reinterpret_cast<const volatile u32 *>(kLoaderPtr);
    if (!IsHeapPointer(reinterpret_cast<void *>(loader)))
        return Fail("読み込み係が無い");
    void *heap = *reinterpret_cast<void **>(loader + kLoaderHeap);
    if (!IsHeapPointer(heap))
        return Fail("読み込みのヒープが無い");
    const u32 align = *reinterpret_cast<const volatile u32 *>(kLoaderAlignPtr);
    void *arc = reinterpret_cast<HeapAllocFn>((*reinterpret_cast<u32 **>(heap))[kHeapAllocSlot])(heap, s_fileSize, align);
    if (!IsHeapPointer(arc))
        return Fail("ヒープが足りない");
    s_heap = heap;
    s_arc = arc;
    std::memcpy(arc, s_file, s_fileSize);
    FlushRange(arc, s_fileSize);

    ArcCtor(s_holder);
    s_holderMade = true;
    W(s_holder, kHolderArcLoaded) = reinterpret_cast<u32>(arc);
    W(s_holder, kHolderArc) = reinterpret_cast<u32>(arc);
    if (Attach(s_holder + kHolderAccessor, arc, ".") == 0)
        return Fail("DARC を解けない");
    RegisterTex(s_holder);

    LayoutCtor(s_layout);
    s_layoutMade = true;
    W(s_layout, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
    if (LayoutBuild(s_layout, kLayoutName, nullptr, kCmdBytes) == 0)
        return Fail("hhd_bg.bclyt を組めない");
    s_layoutBuilt = true;
    s_layout[kLayoutPriority] = kPriority;
    s_stage = Stage::Draw;
}

}  // namespace

bool Show(void) {
    if (s_stage != Stage::Idle && s_stage != Stage::Failed)
        return true;
    if (s_stage == Stage::Failed) {
        // 失敗の後始末はゲームのスレッドで済ませてから Idle に戻す（FrameStep）。ここでは待つだけ
        s_error = "前回の失敗を片付け中";
        return false;
    }
    if (s_file == nullptr) {
        File f;
        if (File::Open(f, kArcPath, File::READ) != File::SUCCESS) {
            s_error = "SD に /hhd_bg_only.arc が無い";
            return false;
        }
        const u64 size = f.GetSize();
        if (size < 0x20 || size > kMaxArcBytes) {
            f.Close();
            s_error = "arc の大きさがおかしい";
            return false;
        }
        u8 *buf = new u8[(u32)size];
        if (f.Read(buf, (u32)size) != File::SUCCESS || std::memcmp(buf, "darc", 4) != 0) {
            f.Close();
            delete[] buf;
            s_error = "arc を読めない";
            return false;
        }
        f.Close();
        s_fileSize = (u32)size;
        s_file = buf;
    }
    if (!s_hookReady) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep)) {
            s_error = "フレームフックを入れられない";
            return false;
        }
        s_hookReady = true;
    }
    s_error = "";
    s_want = true;
    s_stage = Stage::Copy;
    return true;
}

void Hide(void) {
    s_want = false;
}

bool Shown(void) {
    return s_want;
}

const char *LastError(void) {
    return s_error;
}

const char *StageName(void) {
    switch (s_stage) {
    case Stage::Idle: return u8"止まっている";
    case Stage::Copy: return u8"組み立て待ち";
    case Stage::Draw: return u8"下画面に出している";
    case Stage::Teardown: return u8"片付け中";
    case Stage::Failed: return u8"失敗";
    }
    return u8"";
}

void FrameStep(void) {
    switch (s_stage) {
    case Stage::Idle:
        return;
    case Stage::Copy:
        if (!s_want) {
            s_stage = Stage::Idle;
            return;
        }
        Build();
        if (s_stage != Stage::Draw)
            return;
        // fallthrough: 組めたフレームから描く
    case Stage::Draw: {
        if (!s_want) {
            s_stage = Stage::Teardown;
            s_wait = 0;
            return;
        }
        LayoutCalc(s_layout);
        void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
        if (mgr != nullptr)
            AddLayout(mgr, s_layout, kScreenLower);
        return;
    }
    case Stage::Teardown:
        // 描くのをやめてから数フレーム待って壊す（GPU がまだ読んでいるかもしれない。GameLabel と同じ）
        if (++s_wait < kTeardownWaitFrames)
            return;
        Release();
        s_stage = Stage::Idle;
        return;
    case Stage::Failed:
        // 途中まで作ったものを返す。描いていないので待たなくてよい
        Release();
        s_want = false;
        s_stage = Stage::Idle;
        return;
    }
}

}  // namespace HhdScreen

// ---------------------------------------------------------------------------------------
// gohan のメニューとの結び付け（root/テスト/HHD キャラクリ）
// ---------------------------------------------------------------------------------------

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            bool    HhdIsActive(int index)
            {
                (void)index;
                return HhdScreen::Shown();
            }

            void    HhdSetActive(int index, bool active)
            {
                (void)index;
                if (!active)
                {
                    HhdScreen::Hide();
                    return;
                }
                if (!HhdScreen::Show())
                    GuiNotification::NotifyRed(kHhdShow, HhdScreen::LastError());
            }

            const GuiMenu::ToggleEffectFuncs kHhdFuncs = { HhdIsActive, HhdSetActive };

            void    HhdStatus(int index)
            {
                (void)index;
                static char message[96];
                const char *err = HhdScreen::LastError();
                if (err[0] != 0)
                    std::snprintf(message, sizeof(message), u8"%s: %s", HhdScreen::StageName(), err);
                else
                    std::snprintf(message, sizeof(message), u8"%s", HhdScreen::StageName());
                GuiNotification::Notify(kHhdStat, message);
            }
        }

        void    WireHhdScreen(void)
        {
            const int show = GuiMenu::FindItem(kHhdShow);
            const int stat = GuiMenu::FindItem(kHhdStat);
            if (show >= 0)
                GuiMenu::RegisterToggleEffect(show, &kHhdFuncs);
            if (stat >= 0)
                GuiMenu::RegisterExecute(stat, HhdStatus);
        }
    }
}
