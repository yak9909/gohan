#include "GridCursor.hpp"

#include "BuildingHighlight.hpp"
#include "PublicWorks.hpp"

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
static u8 s_rotAnims[kMaxCursors][kMaterialAnimBytes] GC_ALIGNED;

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
// 向きを決める 4 キーのアニメ。frame 0 = 45度 / 1 = 135 / 2 = 225 / 3 = 315
// （`UnitCursor.bcres` 0x161C、メンバは Materials["m_UnitCursor"].TextureCoordinators[1].Rotate の 1 つだけ）。
static const u32 kRotateCanmOffset = 0x161C;
static const float kRotateFrame45 = 0.0f;
// 1 体あたりの予約量。実測 5,090 B（IDA-opus-5-F023）に、向きアニメをもう一つ
// 組む分と余裕を乗せてある。★足りないまま建てるとゲーム側が落ちる（F034）ので多めに取る。
// ★親ヒープ（*0x94CC48）の空きは実機で 484 KB しかなく（2026-09-24）、設置プレビューと並ぶと足りなかった。
//   実測 5,090 B（F023）＋向きアニメの分として 6 KB。1 体ごとに建てる前に残りを見る（kHeapExhausted）ので、
//   足りなければ建てずに止まる。最後の 1 体は kInstanceHeapSlack が受ける。
static const u32 kInstanceBytesPerCursor = 6144;
// ゲーム自身が使う分として、親ヒープにこれだけは必ず残す（足りなければ作らない）。
// ★128 KB では、UnitCursor 40 体と交番のプレビューが並ばず「足りない」になった（2026-09-24 実機、空き 208 KB）。
static const u32 kParentReserve = 0xC000;
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
static bool s_diagonal;             // 縞模様を 45 度傾けるか

static volatile Stage s_stage = Stage::Off;
static volatile Request s_request = Request::None;
static volatile u32 s_failReason = Fail::kNone;

static void* s_sceneOwner;
static void* s_sceneResource;
static void* s_resource;
static void* s_model;
static void* s_nodes[kMaxCursors];
static u32 s_cursorCount;
static bool s_rotBuilt[kMaxCursors];   // 向きアニメを組んだ体だけ解放する
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

// ★マス指定の形（建物エディター）。プレイヤーの足元ではなく、村のマス (x, y) の並びへ 1 体ずつ置く。
//   マス (x, y) の中心は world (32x+16, 地面, 32y+16)（建物の実体の位置と同じ規則。PublicWorks）。
//   メニュースレッドが pend に書いて番号を進め、描画スレッドが Reposition で写す。
static volatile bool s_tileMode;
static u8 s_tileX[kMaxCursors];
static u8 s_tileY[kMaxCursors];
static u32 s_tileCount;
static u8 s_pendX[kMaxCursors];
static u8 s_pendY[kMaxCursors];
static volatile u32 s_pendCount;
static volatile u32 s_pendSeq;
static u32 s_takenSeq;
// ★高さはマスごとの地面ではなく、全部を 1 つの高さ（PublicWorks::CursorHeight: 建物は基点の地面、橋は橋の高さ）に
//   揃える（利用者指示）。s_pendHeightId < 0 のときだけマスごとの地面。
static volatile s32 s_pendHeightId = -1;
static volatile u8 s_pendAnchorX;
static volatile u8 s_pendAnchorY;
typedef float (*GroundHeightFn)(const float* pos, u32 zero);    // 0x006C69C0（S0 で返る）
static const GroundHeightFn GroundHeight = reinterpret_cast<GroundHeightFn>(0x006C69C0);

// ★モードごとの色（建物エディター: 配置 = そのまま、移動 = 青、削除 = 赤）。
//   UnitCursor の資源は自前で読んだものなので、TEV の最終段をその場で「前段と Constant5 を混ぜる」に
//   組み替え（BuildingHighlight::PlanTev、F011〜F013 と同じ）、インスタンスごとの色の写し（mask 0x834 の 0x800）の
//   Constant5 に色と強さを書く。強さ 0 なら元の見た目。
static volatile u32 s_tintColor;
static volatile u8 s_tintStrength;
static bool s_tintPrepared;
static bool s_tintPremultiplied;

static bool s_hookInstalled;
// スタブから毎フレーム呼ぶ相乗り先。フックを 2 つは置けないのでここで配る。
static const u32 kMaxExtraSteps = 4;
static void (*volatile s_extraSteps[kMaxExtraSteps])(void);
static bool s_wantShown;   // 利用者が「出す」と言っている間だけ真。組み直しの可否はこれで決める
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

// 大きさや見た目を変える要求。**必ず解放を挾む**。
// instance ヒープは体数ぴったりで作っているので、数が変われば作り直す以外にない。
// ★以前は Ready のときだけ見ていたので、**組み立て中に数を変えると**新しい数で
// 古いヒープへ建て続けて落ちていた（IDA-opus-5-F034）。
static void RequestRebuild() {
    // 止めると言われたあとに勝手に出し直さないための関門。
    if (!s_wantShown || s_stage == Stage::Off || s_stage == Stage::Failed)
        return;                       // 次に出すとき新しい値で建つ
    s_rebuild = true;
    s_request = Request::Teardown;
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

static void PoseTiles() {
    const u32 seq = s_pendSeq;
    const s32 heightId = s_pendHeightId;
    const u32 anchorX = s_pendAnchorX;
    const u32 anchorY = s_pendAnchorY;
    u32 count = s_pendCount;
    if (count > kMaxCursors)
        count = kMaxCursors;
    for (u32 i = 0; i < count; ++i) {
        s_tileX[i] = s_pendX[i];
        s_tileY[i] = s_pendY[i];
    }
    s_tileCount = count;
    s_takenSeq = seq;                    // 写している間に書き換わっていれば次のフレームでもう一度
    const float shared = heightId >= 0 ? PublicWorks::CursorHeight((u16)heightId, anchorX, anchorY) : 0.0f;
    for (u32 i = 0; i < s_cursorCount && i < s_tileCount; ++i) {
        float pos[3] = { (float)(32 * s_tileX[i] + 16), 0.0f, (float)(32 * s_tileY[i] + 16) };
        pos[1] = heightId >= 0 ? shared : GroundHeight(pos, 0);
        PoseCursor(i, pos[0], pos[1], pos[2]);
    }
}

static void PoseAll() {
    if (s_tileMode) {
        PoseTiles();
        return;
    }
    const float ox = s_originX;
    const float oz = s_originZ;
    u32 index = 0;
    // ★実機で見ると 2 つの枚数が逆だった（利用者報告、2026-09-22）。
    //   軸の写像自体は移動で合っているので、**どちらの数がどちらの辺か**だけを入れ替える。
    //   位置のずれ（s_row / s_col）はそれぞれの軸に残してあるので、十字キーは変わらない。
    for (u32 j = 0; j < s_footprintH; ++j) {          // 縦の枚数 = -Z 方向
        for (u32 i = 0; i < s_footprintW; ++i) {      // 横の枚数 = +X 方向
            if (index >= s_cursorCount)
                return;
            const float x = ox + (float)(s_row + (s16)i) * s_spacing;
            const float z = oz - (float)(s_col + (s16)j) * s_spacing;
            PoseCursor(index, x, s_playerY, z);
            ++index;
        }
    }
}

// ---------------------------------------------------------------------------------------
// Build and teardown, both on the game's draw thread.
// ---------------------------------------------------------------------------------------

static bool BuildOneCursor(u32 index) {
    // ★これを戻り値で済ませてはいけない。instance ヒープが尽きると
    //   `nwgfx_SkeletalModel_Create 0x0049693C` は失敗した確保の null をそのまま辿り、
    //   `ldr r0,[r5]` で落ちる（IDA-opus-5-F034、クラッシュダンプ）。呼ぶ前に残りを見る。
    void* heap = *reinterpret_cast<void**>(Word(s_instanceAllocator, 4));
    if (!IsHeapPointer(heap) || HeapGetFreeSize(heap) < kInstanceBytesPerCursor) {
        Stop(Fail::kHeapExhausted);
        return false;
    }
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

    // 向きのアニメ。ゲーム自身と同じ slot 2（IDA-opus-5-F029）。
    // slot 0 の 150 フレームは Translate と MaterialColor しか書かないので、Rotate は衝突しない。
    s_rotBuilt[index] = false;
    // マス指定の形（公共事業エディター）は常に斜め。メニューの設定は変えない（変えると通知が出る。利用者報告）
    if (s_diagonal || s_tileMode) {
        void* rot = s_rotAnims[index];
        void* rotCanm = reinterpret_cast<u8*>(s_resource) + kRotateCanmOffset;
        if (std::memcmp(rotCanm, "CANM", 4) != 0) {
            Stop(Fail::kAnimBuildFailed);
            return false;
        }
        MaterialAnimCtor(rot);
        if (MaterialAnimBuildFromRes(rot, holder, rotCanm, s_instanceAllocator, 0) != 1) {
            Stop(Fail::kAnimBuildFailed);
            return false;
        }
        s_rotBuilt[index] = true;
        BindAnimSlot(holder, rot, 2);
        // 1 フレームで止める。キーは 4 つで 45/135/225/315 度。
        AnimSetFrame(rot, kRotateFrame45);
    }
    return true;
}

static void DestroyCursors() {
    for (u32 i = 0; i < kMaxCursors; ++i) {
        if (*Word(s_holders[i], 4) != 0u) {
            ModelInstanceDestroy(s_holders[i]);
            // vtable slot 5, not the destructor: the destructor would leave the animation
            // object and its child heap allocated (IDA-opus-5-F023).
            MaterialAnimReleaseBuilt(s_anims[i]);
            if (s_rotBuilt[i])
                MaterialAnimReleaseBuilt(s_rotAnims[i]);
        }
        s_rotBuilt[i] = false;
        s_nodes[i] = nullptr;
    }
    s_cursorCount = 0;
}

static void TeardownAll() {
    s_tintPrepared = false;
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
        // ★マス指定の形（建物エディター）では、村の屋外でない・シーンが取れない一瞬があっても
        //   失敗にせず次のフレームでもう一度見る（実機で組み直しの途中にこれで止まり、UnitCursor が消えた）。
        void* owner = *kSceneOwner;
        if (*kRoomId != 0 || !IsHeapPointer(owner)) {
            if (!s_tileMode)
                Stop(Fail::kNotInVillage);
            return;
        }
        void* parent = *kParentHeap;
        if (!IsHeapPointer(parent)) {
            Stop(Fail::kNoParentHeap);
            return;
        }
        {
            const u32 want = kResourceHeapBytes + (u32)s_footprintW * (u32)s_footprintH * kInstanceBytesPerCursor +
                             kInstanceHeapSlack + kParentReserve;
            if (HeapGetFreeSize(parent) < want) {
                Stop(Fail::kInstanceHeap);
                return;
            }
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

        // ★生の footprint ではなく、**ヒープを作ったときの体数**で建てる。
        //   途中で数を変えられてもここが増えないので、ヒープを超えようがない。
        //   数の変更は SetFootprint が組み直しを頼む形で反映される。
        const u32 want = s_heapCursors;
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

// ---------------------------------------------------------------------------------------
// Tint (building editor). Runs on the draw thread once the cursors are built.
// ---------------------------------------------------------------------------------------

static const u32 kModelMaterials = 0x164;
static const u32 kMatColour = 48;
static const u32 kMatTev = 72;
static const u32 kResTevRel = 648;
static const u32 kResTevKey = 712;
static const u32 kResConst5 = 36 + 4 * 54;
static const u32 kTevBytes = 244;
static const u32 kMaxTintMaterials = 8;

static inline u32 Rd32(u32 a) { return *reinterpret_cast<volatile u32*>(a); }

// 資源の TEV（全インスタンスで共有）を 1 回だけ組み替える。資源は自前なので戻さなくてよい。
static void PrepareTint() {
    s_tintPrepared = true;
    s_tintPremultiplied = false;
    if (s_cursorCount == 0)
        return;
    const u32 node = *Word(s_holders[0], 4);
    const u32 arr = BuildingHighlight::SafeReadable(node + kModelMaterials, 4) ? Rd32(node + kModelMaterials) : 0;
    if (!BuildingHighlight::SafeReadable(arr, 4 * kMaxTintMaterials))
        return;
    for (u32 k = 0; k < kMaxTintMaterials; ++k) {
        const u32 m = Rd32(arr + 4 * k);
        if (!BuildingHighlight::LooksLikeMaterial(m))
            break;
        const u32 colour = Rd32(m + kMatColour);
        const u32 tevres = Rd32(m + kMatTev);
        if (!BuildingHighlight::SafeReadable(tevres, kResTevKey + 4) || !BuildingHighlight::SafeReadable(colour, 256))
            continue;
        const u32 rel = Rd32(tevres + kResTevRel);
        if (rel == 0)
            continue;
        const u32 tev = tevres + kResTevRel + rel;
        if (!BuildingHighlight::SafeReadable(tev, kTevBytes))
            continue;
        static u8 work[kTevBytes] __attribute__((aligned(4)));
        std::memcpy(work, reinterpret_cast<const void*>(tev), kTevBytes);
        const int plan = BuildingHighlight::PlanTev(work, colour);
        if (plan == 0)
            continue;
        std::memcpy(reinterpret_cast<void*>(tev), work, kTevBytes);
        *reinterpret_cast<volatile u32*>(tevres + kResTevKey) = 0;     // 毎回書き出させる
        if (plan == 3)
            s_tintPremultiplied = true;
    }
}

static void ApplyTint() {
    const u32 value = BuildingHighlight::TintConstant(s_tintColor, s_tintStrength, s_tintPremultiplied);
    for (u32 i = 0; i < s_cursorCount; ++i) {
        const u32 node = *Word(s_holders[i], 4);
        if (node == 0u || !BuildingHighlight::SafeReadable(node + kModelMaterials, 4))
            continue;
        const u32 arr = Rd32(node + kModelMaterials);
        if (!BuildingHighlight::SafeReadable(arr, 4 * kMaxTintMaterials))
            continue;
        for (u32 k = 0; k < kMaxTintMaterials; ++k) {
            const u32 m = Rd32(arr + 4 * k);
            if (!BuildingHighlight::LooksLikeMaterial(m))
                break;
            const u32 colour = Rd32(m + kMatColour);
            if (BuildingHighlight::SafeReadable(colour + kResConst5, 4))
                *reinterpret_cast<volatile u32*>(colour + kResConst5) = value;
        }
    }
}

// Grow or shrink to the requested footprint without rebuilding what is already there.
// ---------------------------------------------------------------------------------------
// The frame callback. Runs on the game's draw thread, once per frame, from the stub.
// ---------------------------------------------------------------------------------------

extern "C" void FrameCallback(void) {
    ++s_frames;

    // 相乗り先が先。こちらが何をしていても、あちらは毎フレーム走る。
    for (u32 i = 0; i < kMaxExtraSteps; ++i) {
        void (*step)(void) = s_extraSteps[i];
        if (step != nullptr)
            step();
    }

    // Teardown is allowed whatever the scene is doing: nothing it calls needs one.
    if (s_request == Request::Teardown) {
        if (s_stage == Stage::Off) {
            // 既に止まっているなら解放するものは無い。組み直しだけ頼まれているなら通す。
            s_request = (s_rebuild && s_wantShown) ? Request::Setup : Request::None;
            s_rebuild = false;
            return;
        }
        // ★組み立ての途中でもその場で折り返す。TeardownAll は半端な状態を片付ける。
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
        // 大きさや見た目が変わって作り直す必要があったときは、そのまま組み直す。
        s_request = (s_rebuild && s_wantShown) ? Request::Setup : Request::None;
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
    } else if (s_tileMode && s_takenSeq != s_pendSeq) {
        PoseAll();
    }

    if (s_tileMode) {
        if (!s_tintPrepared)
            PrepareTint();
        ApplyTint();
    }

    for (u32 i = 0; i < s_cursorCount; ++i) {
        if (s_tileMode && i >= s_tileCount)
            continue;                     // マス指定の形で使っていない体は出さない
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
    // ★片付けの途中で出し直しを頼まれたら、片付け終わってから組み直す。以前は段が Ready のままなので
    //   何もせずに真を返し、片付けだけが進んで消えたままになった（建物エディターの画面遷移後の再開）。
    if (s_request == Request::Teardown || s_stage == Stage::Teardown) {
        if (!InstallHook())
            return false;
        s_wantShown = true;
        s_rebuild = true;
        return true;
    }
    if (s_stage == Stage::Ready || s_request == Request::Setup) {
        s_wantShown = true;
        return true;
    }
    s_failReason = Fail::kNone;
    if (!InstallHook())
        return false;
    s_wantShown = true;
    s_request = Request::Setup;
    return true;
}

bool ShowTiles(void) {
    if (!s_tileMode && s_wantShown)
        return false;                     // 足元の形で出ている。混ぜない
    s_tileMode = true;
    // 最初から 8x3 = 24 体で組む（公共事業の大半が入る）。大きい形のときだけ増やす。
    if ((u32)s_footprintW * (u32)s_footprintH < 24)
        SetFootprint(kMaxSide, 3);
    return Show();
}

void SetTiles(const u8* xs, const u8* ys, u32 count, s32 heightId, u8 anchorX, u8 anchorY) {
    if (count > kMaxCursors)
        count = kMaxCursors;
    s_pendHeightId = heightId;
    s_pendAnchorX = anchorX;
    s_pendAnchorY = anchorY;
    for (u32 i = 0; i < count; ++i) {
        s_pendX[i] = xs[i];
        s_pendY[i] = ys[i];
    }
    s_pendCount = count;
    ++s_pendSeq;
    // 組んである体数で足りなければ 8 の倍数で組み直す（ヒープは体数ぴったりで取るので）
    // 体数は形に合わせて増やし、小さい形に戻ったら減らす（親ヒープを空けて設置プレビューに回す）。
    // 行 = 8 体。24 体より下には減らさず、2 行以上余ったときだけ減らす（切り替えのたびに組み直さないため）。
    if (s_tileMode || !s_wantShown) {
        const u32 have = (u32)s_footprintH;
        u32 rows = (count + kMaxSide - 1) / kMaxSide;
        if (rows < 3)
            rows = 3;
        if (rows > have || rows + 2 <= have)
            SetFootprint(kMaxSide, rows);
    }
}

void SetTint(u32 color, u8 strength) {
    s_tintColor = color;
    s_tintStrength = strength;
}

void Hide(void) {
    s_tileMode = false;
    s_wantShown = false;
    s_haveOrigin = false;   // 次に出すときはプレイヤーの足元から
    s_rebuild = false;
    if (s_stage == Stage::Off)
        return;
    s_request = Request::Teardown;
}

bool IsShown(void) {
    // 組み直しの間は一瞬 Off を通るので、段ではなく利用者の意思を返す。
    // そうしないと大きさを変えるたびにチェックが外れる。
    // ★マス指定の形（公共事業エディターが使う）はメニュー項目の効果ではない。数えると、エディターが
    //   出し入れするたびにメニューが「グリッドカーソル ON/OFF」を通知していた（利用者報告）。
    return s_wantShown && !s_tileMode && s_stage != Stage::Failed;
}

void Shutdown(void) {
    if (!s_hookInstalled)
        return;
    for (u32 i = 0; i < kMaxExtraSteps; ++i)
        s_extraSteps[i] = nullptr;
    // Make the callback do nothing, take the branch out, and only then clear the stub --
    // the draw thread could be inside it at this moment.
    s_wantShown = false;
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

bool InstallFrameHook(void) {
    return InstallHook();
}

// 同じ関数は 1 回だけ載る。空きが無ければ偽。
// ★以前は 1 語しかなく、後から登録したチートが前のものを黙って消していた。
bool AddExtraFrameStep(void (*fn)(void)) {
    if (fn == nullptr)
        return false;
    for (u32 i = 0; i < kMaxExtraSteps; ++i)
        if (s_extraSteps[i] == fn)
            return true;
    for (u32 i = 0; i < kMaxExtraSteps; ++i) {
        if (s_extraSteps[i] == nullptr) {
            s_extraSteps[i] = fn;
            return true;
        }
    }
    return false;
}

u32 LastFailReason(void) {
    return s_failReason;
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
    RequestRebuild();
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

void SetDiagonalStripes(bool on) {
    if (s_diagonal == on)
        return;
    s_diagonal = on;
    // 外しても Rotate は最後に書かれた値のまま残るので、組み直す。
    RequestRebuild();
}

bool DiagonalStripes(void) { return s_diagonal; }

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
                GridCursor::Show();     // 失敗しても通知は出さない（利用者指示）。理由は「状態を見る」で出る
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

            bool    DiagIsActive(int index)
            {
                (void)index;
                return GridCursor::DiagonalStripes();
            }

            void    DiagSetActive(int index, bool active)
            {
                (void)index;
                GridCursor::SetDiagonalStripes(active);
            }

            const GuiMenu::ToggleEffectFuncs kDiagFuncs = { DiagIsActive, DiagSetActive };

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
            const int diagIndex = GuiMenu::FindItem(kGridCursorDiag);

            if (statIndex >= 0)
                GuiMenu::RegisterExecute(statIndex, StatusExecute);
            if (snapIndex >= 0)
            {
                GuiMenu::RegisterToggleEffect(snapIndex, &kSnapFuncs);
                // ★チェックの表示は項目の value が持っていて、RegisterToggleEffect が呼ぶ
                //   PrimeEffect は通知用の g_effectSeen しか更新しない。効果が既定 ON の
                //   チェックボックスは、ここで揃えないと**表示だけ OFF のまま ON の振る舞い**になる
                //   （利用者報告、2026-09-22）。
                GuiMenu::SetItemApplied(snapIndex, GridCursor::Snap() ? 1 : 0);
            }
            if (diagIndex >= 0)
            {
                GuiMenu::RegisterToggleEffect(diagIndex, &kDiagFuncs);
                GuiMenu::SetItemApplied(diagIndex, GridCursor::DiagonalStripes() ? 1 : 0);
            }

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
