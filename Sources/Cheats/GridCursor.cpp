#include "GridCursor.hpp"

#include "Cheats.hpp"
#include "GridCursorGameApi.hpp"
#include "GridCursorStub.h"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace GridCursor {

using namespace Game;

// ---------------------------------------------------------------------------------------
// Storage
//
// The game reads and writes all of this, which is fine: a 3gx plugin lives in the game's
// own address space. Only the heaps have to come out of the game's allocator, because the
// game has to own the memory it allocates instances into.
//
// Everything is 32-byte aligned because that is the alignment the game uses for its own
// allocations of these structures, and nothing here is big enough for that to cost.
// ---------------------------------------------------------------------------------------

#define GC_ALIGNED __attribute__((aligned(32)))

static u8 s_resourceHolder[kResourceHolderBytes] GC_ALIGNED;
static u8 s_resourceAllocator[16] GC_ALIGNED;
static u8 s_instanceAllocator[16] GC_ALIGNED;
static u8 s_holders[kMaxCursors][kNodeHolderBytes] GC_ALIGNED;
static u8 s_anims[kMaxCursors][kMaterialAnimBytes] GC_ALIGNED;

static const char kHeapNameText[] = "GridCursor";
static const char kResourcePath[] = "Ftr/Chip/UnitCursor.bcres";
static const char kModelName[] = "UnitCursor";

static SafeString s_heapName;
static SafeString s_path;

// The looping 150-frame material animation, at its offset inside the file. The loader
// leaves a bcres at its file image, so a CANM is at resource + its file offset -- checked
// on UnitCursor itself, whose first animation is at 0x1148 (IDA-opus-5-F012).
static const u32 kCanmOffset = 0x1148;
static const float kCanmFrames = 150.0f;

// Heap sizes. The game gives its own UnitCursor resource a 20,480 byte heap for a 16,256
// byte file; 32 KiB is the same shape with room to spare. Instances measured 5,090 bytes
// each including their animation (IDA-opus-5-F023), so 64 KiB covers all sixteen.
static const u32 kResourceHeapBytes = 0x8000;
// instance ヒープは体数から決める。1 体 5,090 B の実測（IDA-opus-5-F023）に 25% ほど足した。
// ★以前の固定 0x10000 は 12 体分しかなく、「16 体入る」と書いてあったのは誤り。
static const u32 kInstanceBytesPerCursor = 6400;
static const u32 kInstanceHeapSlack = 0x4000;
// 1 フレームに作る上限。これはゲームの描画パスの中なので、
// 64 体を一気に作るとそのフレームだけ長く止まる。
static const u32 kBuildPerFrame = 8;
static const u32 kLoadAttempts = 120;   // the load lands in one or two frames in practice

// 間隔と拡大率は**別々に持つ**。以前は scale = spacing / 12 と連動させていたので、
// 片方だけ動かせず実機で合わせ込めなかった（利用者報告、2026-09-22）。モデルは寸法を
// 持たず、ゲームもこのモデルを拡大しないので、1 マスが world で何単位かはデータに無い。
// 実機で 1x1 の見た目を小道のタイルに合わせ（拡大率）、1 マス移動が隣にぴったり乗る
// ところまで詰める（間隔）。合った 2 つの値からモデルの素寸が決まる。
// 実測済みの既定値。利用者が実機で 32 を出し、`UnitCursor.bcres` の
// ボックスが size (32, 32, 0) で独立に一致した（IDA-opus-5-F033）。
static float s_spacing = 32.0f;     // マスの間隔＝1 回の移動量。world 単位
static float s_scale = 1.0f;        // カーソル自身の倍率。100% = 1.0
static bool s_snap = true;          // 基点を間隔の格子へ丸めるか

static volatile Stage s_stage = Stage::Off;
static volatile Request s_request = Request::None;
static volatile u32 s_failReason = Fail::kNone;

static void* s_sceneOwner;
static void* s_sceneResource;
static void* s_resource;
static void* s_model;
static void* s_nodes[kMaxCursors];
static u32 s_cursorCount;
static u32 s_loadAttempts;
static u32 s_frames;
static u32 s_submits;
static float s_animFrame;

static u8 s_footprintW = 1;
static u8 s_footprintH = 1;
static s16 s_col;         // 画面の右が +
static s16 s_row;         // 画面の下が +
static float s_playerX;   // カーソルを出したときのプレイヤーの足元。丸める前の生の値
static float s_playerY;
static float s_playerZ;
static float s_originX;   // 実際に使う基点。出したときに 1 度だけ決める
static float s_originZ;
static bool s_haveOrigin; // 一度掴んだら大きさを変えても掴み直さない
static u32 s_heapCursors; // instance ヒープを何体ぶんで作ったか
static bool s_rebuild;    // 解放のあと自動でもう一度組み立てる

static bool s_hookInstalled;
static bool s_sceneOk;
static u32 s_quietFrames;     // frames since we stopped submitting, before we free
static u32 s_resourceFree;    // sampled on the draw thread; the plugin thread only reads
static u32 s_instanceFree;

// ---------------------------------------------------------------------------------------
// Small helpers. None of these call the game.
// ---------------------------------------------------------------------------------------

static inline u32* Word(void* base, u32 offset) {
    return reinterpret_cast<u32*>(reinterpret_cast<u8*>(base) + offset);
}

static inline bool IsHeapPointer(const void* p) {
    const u32 v = reinterpret_cast<u32>(p);
    return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u;
}

// Named Stop, not Fail: Fail is the namespace the reason codes live in.
static void Stop(u32 reason) {
    s_failReason = reason;
    s_stage = Stage::Failed;
}

static bool RoomIsCurved() {
    const u32 room = *kRoomId;
    return (kRoomFlags[room] & kRoomFlagCurved) != 0u;
}

// ---------------------------------------------------------------------------------------
// Posing. Outdoors the world is bent into a cylinder, so a plain world position has to go
// through the game's own conversion and the angle it returns folded into the matrix. This
// is the same sequence the tree and the beetle were placed with.
// ---------------------------------------------------------------------------------------

static void PoseCursor(u32 index, float x, float y, float z) {
    float matrix[12];
    float in[3] = {x, y, z};
    float out[3] = {x, y, z};
    u16 angle = 0;
    if (RoomIsCurved())
        angle = FieldPositionToRenderSpace(out, in);

    for (u32 i = 0; i < 12; ++i)
        matrix[i] = 0.0f;
    matrix[0] = s_scale;
    matrix[5] = s_scale;
    matrix[10] = s_scale;
    matrix[3] = out[0];
    matrix[7] = out[1];
    matrix[11] = out[2];
    if (angle != 0)
        AppendRotationX16(matrix, angle);

    SetMatrix3x4(s_holders[index], matrix);
    // Not optional: this is what fills the skeleton's per-bone arrays, and without it the
    // instance is submitted every frame and never drawn (IDA-opus-5-F020).
    UpdateWorldAndSkeleton(s_holders[index]);
}

// 画面と world の対応。**実機で観測した向き**（IDA-opus-5-F032）:
//   十字右を押す（それまでの +X）と画面では下へ、十字上（それまでの -Z）で右へ動いた。
//   つまり world +X が画面の下、world -Z が画面の右。並べる向きも同じ回転をしていた。

// 基点を決める。**出したときと、丸めを切り替えたときだけ**呼ぶ。
// 間隔を変えるたびに丸め直していた頃は floor の落ち先がその都度変わるので、
// カーソルが飛んで見えていた（利用者報告、2026-09-22）。
//
// ★丸め先はマスの**中心**。`UnitCursor.bcres` の SOBJ 0xDB0 のボックスは
//   centre (0,0,0) / size (32,32,0)。**モデルの原点が quad の中心**なので、
//   マスの角へ乗せると必ず半マスずれる（IDA-opus-5-F033）。
static void ComputeOrigin() {
    if (s_snap) {
        s_originX = std::floor(s_playerX / s_spacing) * s_spacing + s_spacing * 0.5f;
        s_originZ = std::floor(s_playerZ / s_spacing) * s_spacing + s_spacing * 0.5f;
    } else {
        s_originX = s_playerX;
        s_originZ = s_playerZ;
    }
}

static void PoseAll() {
    const float ox = s_originX;
    const float oz = s_originZ;
    u32 index = 0;
    for (u32 r = 0; r < s_footprintH; ++r) {          // 画面の縦
        for (u32 c = 0; c < s_footprintW; ++c) {      // 画面の横
            if (index >= s_cursorCount)
                return;
            const float x = ox + (float)(s_row + (s16)r) * s_spacing;
            const float z = oz - (float)(s_col + (s16)c) * s_spacing;
            PoseCursor(index, x, s_playerY, z);
            ++index;
        }
    }
}

// ---------------------------------------------------------------------------------------
// Build and teardown, both on the game's draw thread.
// ---------------------------------------------------------------------------------------

static bool BuildOneCursor(u32 index) {
    void* holder = s_holders[index];
    NodeHolderCtor(holder);
    // (bufferOption, isAnimationEnabled, maxAnimObjectsPerGroup). The last two matter: with
    // animation disabled the instance gets no binding at all, and the game gives its own
    // UnitCursor three slots (IDA-opus-5-F015, F029).
    const int created = ModelInstanceCreate(holder, s_model, s_instanceAllocator,
                                            s_instanceAllocator, 0x834u, 1u, 3u);
    void* node = *reinterpret_cast<void**>(Word(holder, 4));
    if (created != 1 || !IsHeapPointer(node)) {
        Stop(Fail::kCreateFailed);
        return false;
    }
    if (*Word(node, kNodeVtable) != kSkeletalModelVtable) {
        Stop(Fail::kNodeWrongVtable);
        return false;
    }
    // A short heap leaves this null and the first draw dereferences it (V2-F025).
    if (!IsHeapPointer(*reinterpret_cast<void**>(Word(node, kNodeMaterialActivator)))) {
        Stop(Fail::kActivatorNull);
        return false;
    }
    // If the instance has no mesh array of its own, destroying it later writes into the
    // shared resource (IDA-opus-5-F023). Refuse now rather than corrupt on the way out.
    if (*Word(node, kNodeMeshArrayBegin) == *Word(node, kNodeMeshArrayEnd)) {
        Stop(Fail::kSharedMeshArray);
        return false;
    }
    s_nodes[index] = node;

    void* anim = s_anims[index];
    MaterialAnimCtor(anim);
    void* canm = reinterpret_cast<u8*>(s_resource) + kCanmOffset;
    if (std::memcmp(canm, "CANM", 4) != 0) {
        Stop(Fail::kAnimBuildFailed);
        return false;
    }
    if (MaterialAnimBuildFromRes(anim, holder, canm, s_instanceAllocator, 0) != 1) {
        Stop(Fail::kAnimBuildFailed);
        return false;
    }
    // Slot 0 is where the game keeps the always-on animation.
    BindAnimSlot(holder, anim, 0);
    return true;
}

static void DestroyCursors() {
    for (u32 i = 0; i < kMaxCursors; ++i) {
        if (*Word(s_holders[i], 4) != 0u) {
            ModelInstanceDestroy(s_holders[i]);
            // vtable slot 5, not the destructor: the destructor would leave the animation
            // object and its child heap allocated (IDA-opus-5-F023).
            MaterialAnimReleaseBuilt(s_anims[i]);
        }
        s_nodes[i] = nullptr;
    }
    s_cursorCount = 0;
}

static void TeardownAll() {
    DestroyCursors();
    if (*Word(s_resourceHolder, 0) == kResourceLoaderVtable) {
        // Before the heap, always. The other order walks a pointer into freed memory and
        // the thread executes into the symbol table (IDA-opus-5-F027).
        ResHolderDtor(s_resourceHolder);
    }
    if (*Word(s_instanceAllocator, 4) != 0u)
        HeapAllocatorDestroyHeap(s_instanceAllocator);
    if (*Word(s_resourceAllocator, 4) != 0u)
        HeapAllocatorDestroyHeap(s_resourceAllocator);
    s_resource = nullptr;
    s_model = nullptr;
    s_sceneOwner = nullptr;
    s_sceneResource = nullptr;
    s_stage = Stage::Off;
    s_failReason = Fail::kNone;
}

// ---------------------------------------------------------------------------------------
// One step of the build per frame. Spreading it out costs nothing and keeps any single
// frame callback short, which matters because this runs inside the game's draw pass.
// ---------------------------------------------------------------------------------------

static void StepBuild() {
    switch (s_stage) {
    case Stage::AllocHeaps: {
        if (*kRoomId != 0) {                      // the village outdoors
            Stop(Fail::kNotInVillage);
            return;
        }
        void* parent = *kParentHeap;
        if (!IsHeapPointer(parent)) {
            Stop(Fail::kNoParentHeap);
            return;
        }
        void* owner = *kSceneOwner;
        if (!IsHeapPointer(owner)) {
            Stop(Fail::kNotInVillage);
            return;
        }
        s_sceneOwner = owner;
        s_sceneResource = *reinterpret_cast<void**>(Word(owner, kSceneOwnerResourceOffset));

        s_heapName.vtable = kSafeStringVtable;
        s_heapName.text = kHeapNameText;
        s_path.vtable = kSafeStringVtable;
        s_path.text = kResourcePath;

        HeapAllocatorCtor(s_resourceAllocator);
        HeapAllocatorCtor(s_instanceAllocator);
        if (HeapCreateNamed(s_resourceAllocator, kResourceHeapBytes, parent, &s_heapName,
                            1, 0u) != 1 ||
            !IsHeapPointer(*reinterpret_cast<void**>(Word(s_resourceAllocator, 4)))) {
            Stop(Fail::kResourceHeap);
            return;
        }
        // 体数ぶんだけ取る。大きさを増やして入り切らなくなったときは SetFootprint が
        // 組み直しを頼むので、ここは「いま要る量」でよい。
        s_heapCursors = (u32)s_footprintW * (u32)s_footprintH;
        const u32 instanceBytes = s_heapCursors * kInstanceBytesPerCursor + kInstanceHeapSlack;
        if (HeapCreateNamed(s_instanceAllocator, instanceBytes, parent, &s_heapName,
                            1, 0u) != 1 ||
            !IsHeapPointer(*reinterpret_cast<void**>(Word(s_instanceAllocator, 4)))) {
            Stop(Fail::kInstanceHeap);
            return;
        }
        ResHolderCtor(s_resourceHolder);
        s_loadAttempts = 0;
        s_stage = Stage::LoadResource;
        return;
    }
    case Stage::LoadResource: {
        void* heap = *reinterpret_cast<void**>(Word(s_resourceAllocator, 4));
        if (ResHolderRequestLoad(s_resourceHolder, &s_path, heap, 128) == 1) {
            s_stage = Stage::SetupResource;
            return;
        }
        if (++s_loadAttempts >= kLoadAttempts)
            Stop(Fail::kLoadGaveUp);
        return;
    }
    case Stage::SetupResource: {
        void* resource = *reinterpret_cast<void**>(Word(s_resourceHolder, 8));
        if (!IsHeapPointer(resource) || std::memcmp(resource, "CGFX", 4) != 0) {
            Stop(Fail::kNotCgfx);
            return;
        }
        ResHolderSetup(s_resourceHolder, s_resourceAllocator, 0, 1);
        s_resource = resource;
        s_stage = Stage::FindModel;
        return;
    }
    case Stage::FindModel: {
        void* model = FindModelByName(Word(s_resourceHolder, 4), kModelName);
        if (!IsHeapPointer(model)) {
            Stop(Fail::kModelMissing);
            return;
        }
        if (*Word(model, 0) != kSkeletalModelTypeInfo) {
            Stop(Fail::kModelWrongType);
            return;
        }
        s_model = model;
        s_stage = Stage::BuildCursors;
        return;
    }
    case Stage::BuildCursors: {
        void* player = *kPlayer;
        if (!IsHeapPointer(player)) {
            Stop(Fail::kNoPlayer);
            return;
        }
        // 大きさを変えて組み直したときにカーソルが足元へ戻らないよう、基点は一度だけ掴む。
        if (!s_haveOrigin) {
            const float* p = reinterpret_cast<const float*>(
                reinterpret_cast<u8*>(player) + kPlayerPositionOffset);
            s_playerX = p[0];
            s_playerY = p[1];
            s_playerZ = p[2];
            s_col = 0;
            s_row = 0;
            s_haveOrigin = true;
            ComputeOrigin();
        }

        const u32 want = (u32)s_footprintW * (u32)s_footprintH;
        u32 made = 0;
        while (s_cursorCount < want && made < kBuildPerFrame) {
            if (!BuildOneCursor(s_cursorCount))
                return;
            ++s_cursorCount;
            ++made;
        }
        if (s_cursorCount < want)
            return;                    // この段に居たまま次のフレームへ
        PoseAll();
        s_animFrame = 0.0f;
        s_stage = Stage::Ready;
        return;
    }
    default:
        return;
    }
}

// Grow or shrink to the requested footprint without rebuilding what is already there.
static void ApplyResize() {
    const u32 want = (u32)s_footprintW * (u32)s_footprintH;
    while (s_cursorCount > want) {
        const u32 i = s_cursorCount - 1;
        if (*Word(s_holders[i], 4) != 0u) {
            ModelInstanceDestroy(s_holders[i]);
            MaterialAnimReleaseBuilt(s_anims[i]);
        }
        s_nodes[i] = nullptr;
        s_cursorCount = i;
    }
    u32 made = 0;
    while (s_cursorCount < want && made < kBuildPerFrame) {
        if (!BuildOneCursor(s_cursorCount))
            return;
        ++s_cursorCount;
        ++made;
    }
    if (s_cursorCount < want) {
        s_request = Request::Resize;   // 残りは次のフレームで
        return;
    }
    PoseAll();
}

// ---------------------------------------------------------------------------------------
// The frame callback. Runs on the game's draw thread, once per frame, from the stub.
// ---------------------------------------------------------------------------------------

extern "C" void FrameCallback(void) {
    ++s_frames;

    // Teardown is allowed whatever the scene is doing: nothing it calls needs one.
    if (s_request == Request::Teardown) {
        if (s_stage == Stage::Off) {
            // 既に止まっているなら解放するものは無い。組み直しだけ頼まれているなら通す。
            s_request = s_rebuild ? Request::Setup : Request::None;
            s_rebuild = false;
            return;
        }
        // Stop submitting first and let a few frames pass. F025 showed freeing a drawn
        // instance is harmless, but the picture is only right if nothing is mid-flight.
        if (s_stage != Stage::Teardown) {
            s_stage = Stage::Teardown;
            s_quietFrames = 0;
            return;
        }
        if (++s_quietFrames < 4)
            return;
        TeardownAll();
        // 大きさが変わって instance ヒープを作り直す必要があったときは、そのまま組み直す。
        s_request = s_rebuild ? Request::Setup : Request::None;
        s_rebuild = false;
        return;
    }

    if (s_request == Request::Setup) {
        s_request = Request::None;
        if (s_stage == Stage::Off || s_stage == Stage::Failed) {
            s_failReason = Fail::kNone;
            s_stage = Stage::AllocHeaps;
        }
    }

    if (s_stage == Stage::Off || s_stage == Stage::Failed || s_stage == Stage::Teardown)
        return;

    // The scene-owner check. At a room change the owner goes null and then comes back at a
    // different address, so this stops drawing by itself and stays stopped -- which is what
    // the game's own cursor does (IDA-opus-5-F024). Our objects survive untouched.
    //
    // ★この検査は「控えた所有者と同じか」なので、控える前にかけてはいけない。
    //   控えるのは AllocHeaps の中なので、そこへ行く前にここで弾くと stage が
    //   AllocHeaps のまま一生進まない（失敗にもならないので通知も出ない）。
    //   AllocHeaps は自分で room と所有者を検査するから、素通しでよい。
    if (s_sceneOwner == nullptr) {
        s_sceneOk = false;
        if (s_stage == Stage::AllocHeaps)
            StepBuild();
        return;
    }

    void* owner = *kSceneOwner;
    s_sceneOk = IsHeapPointer(owner) && owner == s_sceneOwner &&
                *reinterpret_cast<void**>(Word(owner, kSceneOwnerResourceOffset)) ==
                    s_sceneResource;
    if (!s_sceneOk)
        return;

    if (s_stage != Stage::Ready) {
        StepBuild();
        return;
    }

    if (s_request == Request::Reposition) {
        s_request = Request::None;
        PoseAll();
    } else if (s_request == Request::Resize) {
        s_request = Request::None;
        ApplyResize();
    }

    for (u32 i = 0; i < s_cursorCount; ++i) {
        void* holder = s_holders[i];
        if (*Word(holder, 4) == 0u)
            continue;
        // Drive the loop ourselves. The resource says 150 frames and loop, and the game's
        // own wrap folds anything we pass into that range.
        AnimSetFrame(s_anims[i], s_animFrame);
        // The game's own call: every animation object gets its vtable[5], then both
        // evaluate-and-apply phases (IDA-opus-5-F029).
        EvaluateAndApplyAnims(holder);
        Submit(holder, 0);
        ++s_submits;
    }
    s_animFrame += 1.0f;
    if (s_animFrame >= kCanmFrames)
        s_animFrame -= kCanmFrames;

    // Once a second, and from this thread only: walking a heap's free list is cheap but it
    // is the game's heap, so it happens here rather than from the plugin thread.
    if ((s_frames % 60u) == 0u) {
        void* resourceHeap = *reinterpret_cast<void**>(Word(s_resourceAllocator, 4));
        void* instanceHeap = *reinterpret_cast<void**>(Word(s_instanceAllocator, 4));
        s_resourceFree = IsHeapPointer(resourceHeap) ? HeapGetFreeSize(resourceHeap) : 0u;
        s_instanceFree = IsHeapPointer(instanceHeap) ? HeapGetFreeSize(instanceHeap) : 0u;
    }
}

// ---------------------------------------------------------------------------------------
// Menu-thread side. Sets requests, reads status, and owns the hook.
//
// The hook goes in once and stays until the process exits. Hide() only takes the state back
// to Off, and the stub then costs a counter and two compares a frame. Taking the branch out
// from the menu thread while the draw thread might be inside the stub is the one race worth
// refusing to have, so removal waits for Shutdown.
// ---------------------------------------------------------------------------------------

static void Flush(u32 address, u32 size) {
    CTRPluginFramework::GuiMenu::FlushMemory(address, size);
}

static bool InstallHook(void) {
    if (s_hookInstalled)
        return true;
    u8* stub = reinterpret_cast<u8*>(Stub::kAddress);
    // Never write over something already there. gohan keeps its own caves in this tail and
    // so did the debugger sessions. tools/gridcursor/verify_gridcursor.py checks the same
    // thing offline against gohan's cave headers; this is the runtime half of that.
    for (u32 i = 0; i < Stub::kSize; ++i) {
        if (stub[i] != 0) {
            Stop(Fail::kCaveOccupied);
            return false;
        }
    }
    if (*reinterpret_cast<u32*>(Stub::kHookAddress) != Stub::kHookOriginal) {
        Stop(Fail::kHookNotNop);
        return false;
    }

    std::memcpy(stub, Stub::kBytes, Stub::kSize);
    *reinterpret_cast<u32*>(stub + Stub::kCallbackOffset) =
        reinterpret_cast<u32>(&FrameCallback);
    // The stub is still data until the hook points at it, so flush it first.
    Flush(Stub::kAddress, Stub::kSize);
    *reinterpret_cast<u32*>(Stub::kHookAddress) = Stub::kHookWord;
    Flush(Stub::kHookAddress, 4);
    s_hookInstalled = true;
    return true;
}

bool Show(void) {
    if (s_stage == Stage::Ready || s_request == Request::Setup)
        return true;
    s_failReason = Fail::kNone;
    if (!InstallHook())
        return false;
    s_request = Request::Setup;
    return true;
}

void Hide(void) {
    s_haveOrigin = false;   // 次に出すときはプレイヤーの足元から
    s_rebuild = false;
    if (s_stage == Stage::Off)
        return;
    s_request = Request::Teardown;
}

bool IsShown(void) {
    return s_stage != Stage::Off && s_stage != Stage::Failed;
}

void Shutdown(void) {
    if (!s_hookInstalled)
        return;
    // Make the callback do nothing, take the branch out, and only then clear the stub --
    // the draw thread could be inside it at this moment.
    s_stage = Stage::Off;
    s_request = Request::None;
    *reinterpret_cast<u32*>(Stub::kAddress + Stub::kCallbackOffset) = 0;
    Flush(Stub::kAddress + Stub::kCallbackOffset, 4);
    *reinterpret_cast<u32*>(Stub::kHookAddress) = Stub::kHookOriginal;
    Flush(Stub::kHookAddress, 4);
    svcSleepThread(100000000ull);   // three frames at 30fps, with room to spare
    std::memset(reinterpret_cast<void*>(Stub::kAddress), 0, Stub::kSize);
    Flush(Stub::kAddress, Stub::kSize);
    s_hookInstalled = false;
}

void Move(int dCol, int dRow) {
    s_col = (s16)(s_col + dCol);
    s_row = (s16)(s_row + dRow);
    if (s_stage == Stage::Ready)
        s_request = Request::Reposition;
}

void SetFootprint(u32 width, u32 height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (width > kMaxSide) width = kMaxSide;
    if (height > kMaxSide) height = kMaxSide;
    if (width * height > kMaxCursors)
        return;
    if (width == s_footprintW && height == s_footprintH)
        return;
    s_footprintW = (u8)width;
    s_footprintH = (u8)height;
    if (s_stage != Stage::Ready)
        return;
    if (width * height > s_heapCursors) {
        // ヒープはいまの体数ぶんしか無い。足りないまま建てると activator が null のまま
        // 半分だけできて、描いた瞬間に落ちる（V2-F025）。作り直す。
        s_rebuild = true;
        s_request = Request::Teardown;
    } else {
        s_request = Request::Resize;
    }
}

void SetSpacing(s32 worldUnits) {
    if (worldUnits < 1) worldUnits = 1;
    if (worldUnits > 400) worldUnits = 400;
    s_spacing = (float)worldUnits;
    if (s_stage == Stage::Ready)
        s_request = Request::Reposition;
}

s32 Spacing(void) { return (s32)s_spacing; }

void SetScalePercent(s32 percent) {
    if (percent < 5) percent = 5;
    if (percent > 1000) percent = 1000;
    s_scale = (float)percent / 100.0f;
    // 倍率は行列の中なので、姿勢を付け直さないと反映されない。
    if (s_stage == Stage::Ready)
        s_request = Request::Reposition;
}

s32 ScalePercent(void) { return (s32)(s_scale * 100.0f + 0.5f); }

void SetSnap(bool on) {
    if (s_snap == on)
        return;
    s_snap = on;
    // 切り替えたときだけ、握んだ生の位置から基点を作り直す。
    if (s_haveOrigin)
        ComputeOrigin();
    if (s_stage == Stage::Ready)
        s_request = Request::Reposition;
}

bool Snap(void) { return s_snap; }

Status Read(void) {
    Status out;
    out.stage = s_stage;
    out.failReason = s_failReason;
    out.frames = s_frames;
    out.submits = s_submits;
    out.cursors = s_cursorCount;
    out.footprintW = s_footprintW;
    out.footprintH = s_footprintH;
    out.col = s_col;
    out.row = s_row;
    out.resourceFree = s_resourceFree;
    out.instanceFree = s_instanceFree;
    return out;
}

const char* StageName(Stage stage) {
    switch (stage) {
    case Stage::Off: return u8"止まっている";
    case Stage::AllocHeaps: return u8"ヒープ確保";
    case Stage::LoadResource: return u8"読み込み中";
    case Stage::SetupResource: return u8"資源の準備";
    case Stage::FindModel: return u8"モデル検索";
    case Stage::BuildCursors: return u8"生成中";
    case Stage::Ready: return u8"描画中";
    case Stage::Teardown: return u8"解放中";
    case Stage::Failed: return u8"停止（失敗）";
    }
    return u8"?";
}

const char* FailName(u32 reason) {
    switch (reason) {
    case Fail::kNone: return u8"-";
    case Fail::kNotInVillage: return u8"村の屋外ではありません";
    case Fail::kNoParentHeap: return u8"親ヒープが取れません";
    case Fail::kResourceHeap: return u8"資源ヒープが作れません";
    case Fail::kInstanceHeap: return u8"instance ヒープが作れません";
    case Fail::kLoadGaveUp: return u8"読み込みが終わりません";
    case Fail::kNotCgfx: return u8"読めたものが CGFX ではありません";
    case Fail::kModelMissing: return u8"その名前のモデルがありません";
    case Fail::kModelWrongType: return u8"モデルが SkeletalModel ではありません";
    case Fail::kCreateFailed: return u8"instance の生成に失敗しました";
    case Fail::kActivatorNull: return u8"instance が半分しかできていません（ヒープ不足）";
    case Fail::kNodeWrongVtable: return u8"node の vtable が違います";
    case Fail::kNoPlayer: return u8"プレイヤーが取れません";
    case Fail::kAnimBuildFailed: return u8"アニメの構築に失敗しました";
    case Fail::kSharedMeshArray: return u8"instance が資源の mesh 配列を共有しています";
    case Fail::kCaveOccupied: return u8"ケーブの置き場が空いていません";
    case Fail::kHookNotNop: return u8"フック先が期待の NOP ではありません";
    }
    return u8"?";
}

}  // namespace GridCursor

// ---------------------------------------------------------------------------------------
// gohan のメニューとの結び付け（gohan.md「テスト」フォルダ）
// ---------------------------------------------------------------------------------------

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            int     g_showIndex = -1;
            int     g_colsIndex = -1;
            int     g_rowsIndex = -1;
            int     g_tileIndex = -1;
            int     g_scaleIndex = -1;
            int     g_moveIndex = -1;
            u32     g_gcPreviousKeys = 0;

            void    ApplyFootprint(void)
            {
                const int w = g_colsIndex >= 0 ? GuiMenu::ItemApplied(g_colsIndex) : 1;
                const int h = g_rowsIndex >= 0 ? GuiMenu::ItemApplied(g_rowsIndex) : 1;

                if (w >= 1 && h >= 1)
                    GridCursor::SetFootprint((u32)w, (u32)h);
            }

            bool    ShowIsActive(int index)
            {
                (void)index;
                return GridCursor::IsShown();
            }

            void    ShowSetActive(int index, bool active)
            {
                (void)index;
                if (!active)
                {
                    GridCursor::Hide();
                    return;
                }
                // 数値項目の適用値が正本。出す前に反映しておく。
                ApplyFootprint();
                if (g_tileIndex >= 0)
                    GridCursor::SetSpacing(GuiMenu::ItemApplied(g_tileIndex));
                if (g_scaleIndex >= 0)
                    GridCursor::SetScalePercent(GuiMenu::ItemApplied(g_scaleIndex));
                if (!GridCursor::Show())
                {
                    const GridCursor::Status s = GridCursor::Read();

                    GuiNotification::NotifyRed(kGridCursor, GridCursor::FailName(s.failReason));
                }
            }

            const GuiMenu::ToggleEffectFuncs kShowFuncs = { ShowIsActive, ShowSetActive };

            void    FootprintApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                ApplyFootprint();
            }

            void    TileApplied(int index, s32 value)
            {
                (void)index;
                GridCursor::SetSpacing(value);
            }

            void    ScaleApplied(int index, s32 value)
            {
                (void)index;
                GridCursor::SetScalePercent(value);
            }

            bool    SnapIsActive(int index)
            {
                (void)index;
                return GridCursor::Snap();
            }

            void    SnapSetActive(int index, bool active)
            {
                (void)index;
                GridCursor::SetSnap(active);
            }

            const GuiMenu::ToggleEffectFuncs kSnapFuncs = { SnapIsActive, SnapSetActive };

            // 「十字キーで動かす」が有効な間だけ。ゲーム側の十字キーを毎フレーム塞ぐので、
            // カーソルを動かしてもプレイヤーは歩かない。held はメニュー表示中は 0 になる。
            void    GridCursorMoveTick(u16 held)
            {
                GuiMenu::BlockGameDpad();

                const u32 keys = held;
                const u32 pressed = keys & ~g_gcPreviousKeys;

                g_gcPreviousKeys = keys;
                // 画面基準。world への写像は GridCursor 側が持つ（IDA-opus-5-F032）。
                if (pressed & (u32)Key::DPadLeft)  GridCursor::Move(-1, 0);
                if (pressed & (u32)Key::DPadRight) GridCursor::Move(+1, 0);
                if (pressed & (u32)Key::DPadUp)    GridCursor::Move(0, -1);
                if (pressed & (u32)Key::DPadDown)  GridCursor::Move(0, +1);
            }

            // 止まっている理由を利用者が見られるようにする。失敗でない停止（段が進まない、
            // シーン所有者が合わない）は通知を出さないので、これが無いと外からは何も分からない。
            void    StatusExecute(int index)
            {
                (void)index;
                const GridCursor::Status s = GridCursor::Read();
                static char message[96];

                // 通知は 1 行で幅 130px までしか出ないので、いちばん効く 2 つ
                // （どの段で止まっているか／止めた理由）を先に置く。
                if (s.failReason != GridCursor::Fail::kNone)
                    std::snprintf(message, sizeof(message), u8"%s",
                                  GridCursor::FailName(s.failReason));
                else
                    std::snprintf(message, sizeof(message), u8"%s %ux%u=%u体 (%d,%d) F%lu",
                                  GridCursor::StageName(s.stage),
                                  (unsigned)s.footprintW, (unsigned)s.footprintH,
                                  (unsigned)s.cursors,
                                  (int)s.col, (int)s.row,
                                  (unsigned long)s.frames);
                GuiNotification::Notify(kGridCursor, message);
            }
        }

        bool    GridCursorTick(int index, u16 held)
        {
            if (g_moveIndex < 0 || index != g_moveIndex)
                return false;
            GridCursorMoveTick(held);
            return true;
        }

        bool    GridCursorDisable(int index)
        {
            if (g_moveIndex < 0 || index != g_moveIndex)
                return false;
            g_gcPreviousKeys = 0;
            return true;
        }

        void    WireGridCursor(void)
        {
            g_showIndex = GuiMenu::FindItem(kGridCursor);
            g_colsIndex = GuiMenu::FindItem(kGridCursorCols);
            g_rowsIndex = GuiMenu::FindItem(kGridCursorRows);
            g_tileIndex = GuiMenu::FindItem(kGridCursorTile);
            g_scaleIndex = GuiMenu::FindItem(kGridCursorScale);
            g_moveIndex = GuiMenu::FindItem(kGridCursorMove);

            const int statIndex = GuiMenu::FindItem(kGridCursorStat);
            const int snapIndex = GuiMenu::FindItem(kGridCursorSnap);

            if (statIndex >= 0)
                GuiMenu::RegisterExecute(statIndex, StatusExecute);
            if (snapIndex >= 0)
                GuiMenu::RegisterToggleEffect(snapIndex, &kSnapFuncs);

            if (g_showIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_showIndex, &kShowFuncs);
            if (g_colsIndex >= 0)
                GuiMenu::RegisterApply(g_colsIndex, FootprintApplied);
            if (g_rowsIndex >= 0)
                GuiMenu::RegisterApply(g_rowsIndex, FootprintApplied);
            ApplyFootprint();
            if (g_tileIndex >= 0)
            {
                GuiMenu::RegisterApply(g_tileIndex, TileApplied);
                GridCursor::SetSpacing(GuiMenu::ItemApplied(g_tileIndex));
            }
            if (g_scaleIndex >= 0)
            {
                GuiMenu::RegisterApply(g_scaleIndex, ScaleApplied);
                GridCursor::SetScalePercent(GuiMenu::ItemApplied(g_scaleIndex));
            }
        }
    }
}
