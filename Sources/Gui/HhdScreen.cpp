#include "HhdScreen.hpp"
#include "GridCursor.hpp"
#include "Cheats.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"

#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

// 根拠（解析リポジトリ project_v2、主 IDB acnl_jpn_v15_annotated.i64）:
//   メモリ上の DARC を渡す手順（IDA-opus-5.5-F073、実機で地を出せた）:
//   ArcResAccReader_LoadArcStep 0x567244 が読み終えたあとにすることを写す。holder+0x158 と holder+8 に arc の先頭、
//     nwlyt_ArcResourceAccessor_Attach(holder+0xC, arc, ".") 0x4B5900（戻り 1 = DARC を解けた）。
//   器: ヒープ = [[0x96FC40]+4]（sead::ExpHeap）、アライン = [0x96FC2C]。確保 = vtable +24 (heap, size, align)、解放 = vtable +28 (heap, ptr)。
//     写したら sub_12D294(先頭, 大きさ)。holder+0x15C の読み込み係は使わない（ArcResAccReader_Dtor はそれを返そうとしない）→ 器はこちらで返す。
//   書体: BsMenuCatalog_Init 0x21C76C と同じく種類 0 を holder+12 のアクセサへ登録（名前 = Garden_msg_size16.bcfnt。HHD のレイアウトもこれを参照）。
//   描く順: LayoutMgr_AddLayout 0x56928C は Layout+0x0C のバイトの昇順に並べ、同じ値なら後に足したものが後ろ（= 手前）。0xFF で必ず最前面（F-253）。
//   コマンドの使用量: Layout+0x100 = リスト番号、+0x118 = 確保した大きさ。管理 = dword_AD98C0[(番号 & 0x1F) + 7] から +0x40 で辿り +0 が番号のもの、
//     +0x0C = 記録した長さ（GpuCmd_SelectList 0x1216BC が切り替えのたびに保存）。

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
typedef u32 (*HeapFreeSizeFn)(void *heap);
typedef void *(*FontSlotFn)(void *fontMgr, u32 kind);
typedef void (*RegisterFontFn)(void *accessor, const char *name, void *font);

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
const HeapFreeSizeFn HeapFreeSize   = reinterpret_cast<HeapFreeSizeFn>(0x0074D744);   // sead::ExpHeap::getFreeSize（GridCursor と同じ）
const FontSlotFn     FontName       = reinterpret_cast<FontSlotFn>(0x00747528);
const FontSlotFn     FontGet        = reinterpret_cast<FontSlotFn>(0x0052D6A8);
const RegisterFontFn RegisterFont   = reinterpret_cast<RegisterFontFn>(0x004B3FD4);

const u32 kLayoutMgrPtr = 0x0096FC38;
const u32 kFontMgrPtr = 0x0094C9C8;
const u32 kLoaderPtr = 0x0096FC40, kLoaderHeap = 4;     // [[0x96FC40]+4] = 読み込みのヒープ
const u32 kLoaderAlignPtr = 0x0096FC2C;
const u32 kHeapAllocSlot = 24 / 4, kHeapFreeSlot = 28 / 4;
const u32 kHolderArc = 8, kHolderAccessor = 0xC, kHolderArcLoaded = 0x158;
const u32 kLayoutPriority = 12, kLayoutHolder = 236, kLayoutListId = 0x100, kLayoutListSize = 0x118;
const u32 kCmdListBuckets = 0x00AD98C0, kCmdMgrNext = 0x40, kCmdMgrUsed = 0x0C;
const u8 kPriority = 0xFF;                  // 最前面（ゲームの下画面の UI より手前）
const u32 kScreenLower = 1;                 // AddLayout の画面（下画面 = 1）
const u32 kTeardownWaitFrames = 3;          // GameLabel と同じ（描くのをやめてから壊すまで）

const char kArcPath[] = "/hhd_charcreate.arc";
const u32 kMaxArcBytes = 0x80000;

// 組むレイアウト。描くのは draw が真のものを、この順（後ろほど手前）に。
// コマンド領域はカタログ（0x2000〜0xB000）を目安に多めに取り、実際の使用量を Measure で測る。
struct Def { const char *name; u32 cmdBytes; bool draw; };
const Def kDefs[] = {
    { "hhd_bg.bclyt",   0x2000,  true  },
    { "hhd_face.bclyt", 0x10000, true  },
    { "hhd_eye.bclyt",  0x8000,  true  },
    { "hhd_hair.bclyt", 0x10000, false },   // 髪のモード（まだ切り替えない）
};
const u32 kLayouts = sizeof(kDefs) / sizeof(kDefs[0]);

enum class Stage : u8 { Idle, Copy, Draw, Teardown, Failed };

struct Lay {
    alignas(8) u8 obj[332];
    bool made, built;
};

alignas(8) u8 s_holder[584];                // GameLabel と同じ大きさ
Lay s_lay[kLayouts];
u8 *s_file;                                 // SD から読んだ arc（プラグインのメモリ。メニューのスレッドが作る）
u32 s_fileSize;
void *s_heap, *s_arc;                       // ゲームのヒープに写した arc
bool s_holderMade, s_hookReady;
volatile bool s_want;
volatile Stage s_stage = Stage::Idle;
const char *volatile s_error = "";
u32 s_wait;
// 測った値（ゲームのスレッドが書き、Measure が読む）
volatile u32 s_heapFreeBefore, s_heapFreeAfter, s_used[kLayouts], s_size[kLayouts];

inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(p) + off); }

bool IsHeapPointer(const void *p) {
    const u32 v = reinterpret_cast<u32>(p);
    return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u;
}

void Fail(const char *why) {
    s_error = why;
    s_stage = Stage::Failed;
}

// レイアウトのコマンドリストの記録した長さ（見つからなければ 0xFFFFFFFF）
u32 RecordedBytes(void *layout) {
    const u32 id = W(layout, kLayoutListId);
    u32 m = reinterpret_cast<const volatile u32 *>(kCmdListBuckets)[(id & 0x1Fu) + 7u];
    for (u32 guard = 0; m != 0 && guard < 256; ++guard) {
        if (!IsHeapPointer(reinterpret_cast<void *>(m)))
            break;
        if (*reinterpret_cast<const volatile u32 *>(m) == id)
            return *reinterpret_cast<const volatile u32 *>(m + kCmdMgrUsed);
        m = *reinterpret_cast<const volatile u32 *>(m + kCmdMgrNext);
    }
    return 0xFFFFFFFFu;
}

void Release(void) {
    for (u32 i = 0; i < kLayouts; ++i) {
        Lay &l = s_lay[i];
        if (l.made) {
            if (l.built)
                LayoutFinalize(l.obj);
            LayoutDtor(l.obj);
        }
        l.made = l.built = false;
    }
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = false;
    if (s_arc != nullptr && s_heap != nullptr)
        reinterpret_cast<HeapFreeFn>((*reinterpret_cast<u32 **>(s_heap))[kHeapFreeSlot])(s_heap, s_arc);
    s_arc = s_heap = nullptr;
}

// arc をゲームのヒープへ写し、ArcResAccReader に渡して書体・テクスチャを登録し、レイアウトを組む
void Build(void) {
    const u32 loader = *reinterpret_cast<const volatile u32 *>(kLoaderPtr);
    if (!IsHeapPointer(reinterpret_cast<void *>(loader)))
        return Fail("読み込み係が無い");
    void *heap = *reinterpret_cast<void **>(loader + kLoaderHeap);
    if (!IsHeapPointer(heap))
        return Fail("読み込みのヒープが無い");
    void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
    if (fontMgr == nullptr || FontGet(fontMgr, 0) == nullptr)
        return Fail("ゲームの書体が取れない");
    s_heapFreeBefore = HeapFreeSize(heap);
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
    // 書体（BsMenuCatalog_Init と同じ: 名前の器の vt[2] を呼んでから名前 = +4）
    u32 *name = reinterpret_cast<u32 *>(FontName(fontMgr, 0));
    reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(name[0])[2])(name);
    RegisterFont(s_holder + kHolderAccessor, reinterpret_cast<const char *>(name[1]), FontGet(fontMgr, 0));
    RegisterTex(s_holder);

    for (u32 i = 0; i < kLayouts; ++i) {
        Lay &l = s_lay[i];
        LayoutCtor(l.obj);
        l.made = true;
        W(l.obj, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
        if (LayoutBuild(l.obj, kDefs[i].name, nullptr, kDefs[i].cmdBytes) == 0)
            return Fail("レイアウトを組めない");
        l.built = true;
        l.obj[kLayoutPriority] = kPriority;
        s_size[i] = W(l.obj, kLayoutListSize);
        s_used[i] = 0xFFFFFFFFu;
    }
    s_heapFreeAfter = HeapFreeSize(heap);
    s_stage = Stage::Draw;
}

}  // namespace

bool Show(void) {
    if (s_stage != Stage::Idle && s_stage != Stage::Failed)
        return true;
    if (s_stage == Stage::Failed) {
        s_error = "前回の失敗を片付け中";
        return false;
    }
    if (s_file == nullptr) {
        File f;
        if (File::Open(f, kArcPath, File::READ) != File::SUCCESS) {
            s_error = "SD に /hhd_charcreate.arc が無い";
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

void Measure(char *out, u32 size) {
    // レイアウトごとに 記録した長さ/確保した大きさ（バイト）。ヒープは組む前と後の空き（KB）
    std::snprintf(out, size, u8"命令 %lu/%lu %lu/%lu %lu/%lu 空き %luK→%luK",
                  (unsigned long)s_used[0], (unsigned long)s_size[0], (unsigned long)s_used[1], (unsigned long)s_size[1],
                  (unsigned long)s_used[2], (unsigned long)s_size[2],
                  (unsigned long)(s_heapFreeBefore / 1024u), (unsigned long)(s_heapFreeAfter / 1024u));
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
        // 前のフレームで記録した長さ（このフレームの記録より前に読む）
        for (u32 i = 0; i < kLayouts; ++i)
            if (kDefs[i].draw)
                s_used[i] = RecordedBytes(s_lay[i].obj);
        void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
        for (u32 i = 0; i < kLayouts; ++i) {
            if (!kDefs[i].draw)
                continue;
            LayoutCalc(s_lay[i].obj);
            if (mgr != nullptr)
                AddLayout(mgr, s_lay[i].obj, kScreenLower);
        }
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
                static char message[192];
                static char measure[128];
                const char *err = HhdScreen::LastError();
                HhdScreen::Measure(measure, sizeof(measure));
                if (err[0] != 0)
                    std::snprintf(message, sizeof(message), u8"%s: %s", HhdScreen::StageName(), err);
                else
                    std::snprintf(message, sizeof(message), u8"%s %s", HhdScreen::StageName(), measure);
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
