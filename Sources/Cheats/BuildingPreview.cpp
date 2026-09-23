#include "BuildingPreview.hpp"

#include "BuildingHighlight.hpp"
#include "GridCursorGameApi.hpp"
#include "PublicWorks.hpp"
#include "RomfsIndex.hpp"

#include <3ds.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace BuildingPreview {

using namespace GridCursor::Game;

namespace {

#define BP_ALIGNED __attribute__((aligned(32)))

enum class Stage : u32 { Off, AllocHeaps, Load, Setup, Build, Ready, Teardown, Failed };

// 3 つの資源: 0 = LUT、1 = テクスチャ、2 = モデル（PwpPreview_LoadSlot と同じ組）
const u32 kLut = 0, kTex = 1, kModel = 2, kResCount = 3;

u8 s_holders[kResCount][kResourceHolderBytes] BP_ALIGNED;
u8 s_resourceAllocator[16] BP_ALIGNED;
u8 s_instanceAllocator[16] BP_ALIGNED;
u8 s_node[kNodeHolderBytes] BP_ALIGNED;

const char kHeapNameText[] = "BuildingPreview";
const u32 kInstanceHeapBytes = 0x8000;      // モデルビューアと同じ（実測 5,090 B の 1 体に余裕）
const u32 kParentReserve = 0x20000;         // ゲーム自身の分として親ヒープに必ず残す
const u32 kInstanceReserve = 8192;
const u32 kMaxMaterials = 32;

// フラグメント設定（M+80 の先。IDA-gpt-6-astra-F002 / F010）
const u32 kMatColour = 48;
const u32 kMatFrag = 80;
const u32 kFragDepth = 280;
const u32 kFragFbRead = 300;
const u32 kFragCheckA = 320;
const u32 kFragCheckB = 324;
const u32 kFragBlend = 328;
const u32 kFragBlendColor = 336;
const u32 kResFragKey = 720;
const u32 kBlendConstAlpha = (12u << 16) | (13u << 20) | (1u << 24) | (13u << 28);
const u32 kModelMaterials = 0x164;
const char kMaterialName[] = "N2nw3gfx8MaterialE";

SafeString s_heapName;
SafeString s_paths[kResCount];
char s_pathText[kResCount][96];
u32 s_sizes[kResCount];
char s_modelName[64];

// メニュースレッドが書く「次に出すもの」。s_pendSeq が奇数の間は書きかけ（描画スレッドは写さない）。
char s_pendPath[kResCount][96];
u32 s_pendSize[kResCount];
char s_pendName[64];
volatile s32 s_pendId = -1;         // -1 = 出せる種類ではない
volatile u32 s_pendSeq;
s32 s_shownId = -1;                 // メニュー側が最後に頼んだ id
u32 s_stableFrames;                 // 頼まれた id が変わらずに続いたフレーム
s32 s_lastSeen = -2;
const u32 kDebounceFrames = 8;      // 十字キーを押し続けている間は読み込みを始めない

volatile Stage s_stage = Stage::Off;
volatile u32 s_failReason;
volatile bool s_want;
volatile s32 s_tx, s_ty;
s32 s_builtId = -1;
u32 s_loadAttempts;
u32 s_loading;                  // いま読んでいる資源の番号
u32 s_quietFrames;
void *s_sceneOwner;
void *s_sceneResource;
void *s_model;
u32 s_frags[kMaxMaterials];
u32 s_fragCount;
u32 s_phase;
Wave s_wave = { 0xA0, 0x40, 11 };

inline u32 *Word(void *base, u32 offset) {
    return reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(base) + offset);
}
inline u32 R32(u32 a) { return *reinterpret_cast<volatile u32 *>(a); }
inline bool IsHeapPointer(const void *p) {
    const u32 v = reinterpret_cast<u32>(p);
    return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u;
}

void Stop(u32 reason) {
    s_failReason = reason;
    s_stage = Stage::Failed;
}

// ★範囲だけで番地と決めない（配列の先のゴミ 0x3F248A94 を読んで SIGSEGV、2026-09-24 実機）。
//   読めるかは OS に問い合わせる（BuildingHighlight::SafeReadable）。
bool IsMaterial(u32 obj) {
    return BuildingHighlight::LooksLikeMaterial(obj);
}

void TeardownAll(void) {
    if (*Word(s_node, 4) != 0u)
        ModelInstanceDestroy(s_node);
    // ★ヒープより先に holder を壊す（逆にすると解放済みを辿る。IDA-opus-5-F027）
    for (u32 i = kResCount; i-- > 0;)
        if (*Word(s_holders[i], 0) == kResourceLoaderVtable)
            ResHolderDtor(s_holders[i]);
    if (*Word(s_instanceAllocator, 4) != 0u)
        HeapAllocatorDestroyHeap(s_instanceAllocator);
    if (*Word(s_resourceAllocator, 4) != 0u)
        HeapAllocatorDestroyHeap(s_resourceAllocator);
    s_model = nullptr;
    s_sceneOwner = nullptr;
    s_sceneResource = nullptr;
    s_fragCount = 0;
    s_builtId = -1;
    // 次の組み立てで「前の holder がまだ生きている」と見誤らないよう、置き場ごと消す
    std::memset(s_holders, 0, sizeof(s_holders));
    std::memset(s_node, 0, sizeof(s_node));
    std::memset(s_resourceAllocator, 0, sizeof(s_resourceAllocator));
    std::memset(s_instanceAllocator, 0, sizeof(s_instanceAllocator));
    s_stage = Stage::Off;
}

// 自前インスタンスの材質（本体はインスタンスごとの写し）を定数アルファのブレンドにする。
void MakeTranslucent(void) {
    s_fragCount = 0;
    void *node = *reinterpret_cast<void **>(Word(s_node, 4));
    const u32 arr = *Word(node, kModelMaterials);
    if (!BuildingHighlight::SafeReadable(arr, 4 * kMaxMaterials))
        return;
    for (u32 k = 0; k < kMaxMaterials; ++k) {
        const u32 m = R32(arr + 4 * k);
        if (!IsMaterial(m) || !BuildingHighlight::SafeReadable(m + kMatColour, kMatFrag + 4 - kMatColour))
            break;
        const u32 colour = R32(m + kMatColour);
        const u32 frag = R32(m + kMatFrag);
        if (!BuildingHighlight::SafeReadable(frag, kResFragKey + 4) ||
            !BuildingHighlight::SafeReadable(colour, kResFragKey + 4))
            continue;
        if (R32(frag + kFragCheckA) != 0x00E40100u || R32(frag + kFragCheckB) != 0x803F0100u)
            continue;
        *reinterpret_cast<volatile u32 *>(frag + kFragDepth) = R32(frag + kFragDepth) & ~2u;
        *reinterpret_cast<volatile u32 *>(frag + kFragFbRead) = 0;
        *reinterpret_cast<volatile u32 *>(frag + kFragBlend) = kBlendConstAlpha;
        *reinterpret_cast<volatile u32 *>(frag + kFragBlendColor) = (u32)s_wave.alpha << 24;
        *reinterpret_cast<volatile u32 *>(colour + kResFragKey) = 0;
        if (frag != colour)
            *reinterpret_cast<volatile u32 *>(frag + kResFragKey) = 0;
        s_frags[s_fragCount++] = frag;
    }
}

void Pose(void) {
    const u32 x = (u32)s_tx;
    const u32 y = (u32)s_ty;
    float in[3] = { (float)(32 * x + 16), PublicWorks::SpawnHeight((u16)s_builtId, x, y), (float)(32 * y + 16) };
    float out[3] = { in[0], in[1], in[2] };
    u16 angle = 0;
    if ((kRoomFlags[*kRoomId] & kRoomFlagCurved) != 0u)
        angle = FieldPositionToRenderSpace(out, in);
    float matrix[12];
    for (u32 i = 0; i < 12; ++i)
        matrix[i] = 0.0f;
    matrix[0] = matrix[5] = matrix[10] = 1.0f;
    matrix[3] = out[0];
    matrix[7] = out[1];
    matrix[11] = out[2];
    if (angle != 0)
        AppendRotationX16(matrix, angle);
    SetMatrix3x4(s_node, matrix);
    UpdateWorldAndSkeleton(s_node);
}

void Animate(void) {
    s_phase += s_wave.speed;
    const float t = (float)(s_phase % 1000u) * (6.2831853f / 1000.0f);
    s32 a = (s32)s_wave.alpha + (s32)(std::sin(t) * (float)s_wave.wave);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    for (u32 i = 0; i < s_fragCount; ++i)
        *reinterpret_cast<volatile u32 *>(s_frags[i] + kFragBlendColor) = (u32)a << 24;
}

void StepBuild(void) {
    switch (s_stage) {
    case Stage::AllocHeaps: {
        void *parent = *kParentHeap;
        void *owner = *kSceneOwner;
        if (!IsHeapPointer(parent) || !IsHeapPointer(owner)) {
            Stop(1);
            return;
        }
        s_sceneOwner = owner;
        s_sceneResource = *reinterpret_cast<void **>(Word(owner, kSceneOwnerResourceOffset));
        s_heapName.vtable = kSafeStringVtable;
        s_heapName.text = kHeapNameText;
        u32 bytes = 0x4000;
        for (u32 i = 0; i < kResCount; ++i) {
            s_paths[i].vtable = kSafeStringVtable;
            s_paths[i].text = s_pathText[i];
            bytes += s_sizes[i] * 2;
        }
        // ★親ヒープ（空き 484 KB を実測）はゲームも使う。残りが足りなければ作らない
        if (HeapGetFreeSize(parent) < bytes + kInstanceHeapBytes + kParentReserve) {
            Stop(10);
            return;
        }
        HeapAllocatorCtor(s_resourceAllocator);
        HeapAllocatorCtor(s_instanceAllocator);
        if (HeapCreateNamed(s_resourceAllocator, bytes, parent, &s_heapName, 1, 0u) != 1 ||
            !IsHeapPointer(*reinterpret_cast<void **>(Word(s_resourceAllocator, 4)))) {
            Stop(2);
            return;
        }
        if (HeapCreateNamed(s_instanceAllocator, kInstanceHeapBytes, parent, &s_heapName, 1, 0u) != 1 ||
            !IsHeapPointer(*reinterpret_cast<void **>(Word(s_instanceAllocator, 4)))) {
            Stop(3);
            return;
        }
        for (u32 i = 0; i < kResCount; ++i)
            ResHolderCtor(s_holders[i]);
        s_loadAttempts = 0;
        s_loading = 0;
        s_stage = Stage::Load;
        return;
    }
    case Stage::Load: {
        // 1 本ずつ順に（同時に頼んでよいかは確かめていない）。1 が返ったら読み終わり。
        void *heap = *reinterpret_cast<void **>(Word(s_resourceAllocator, 4));
        if (ResHolderRequestLoad(s_holders[s_loading], &s_paths[s_loading], heap, 128) == 1) {
            s_loadAttempts = 0;
            ++s_loading;
            // ★読み込みの途中では絶対に片付けない。1 本読み終わった切れ目でだけ止まる。
            //   途中でヒープを返すと、ゲームの読み込み係が返したヒープへ書き続け、以後の読み込みが
            //   終わらなくなる（利用者報告「無限ロード。画面遷移でリロードすると表示される」）。
            if (!s_want || s_pendId != s_builtId) {
                s_stage = Stage::Teardown;
                s_quietFrames = 0;
                return;
            }
            if (s_loading >= kResCount)
                s_stage = Stage::Setup;
            return;
        }
        // 読み終わらないまま諦めると同じ問題になるので、失敗にはせず待ち続ける（数えるだけ）
        ++s_loadAttempts;
        return;
    }
    case Stage::Setup: {
        for (u32 i = 0; i < kResCount; ++i) {
            void *res = *reinterpret_cast<void **>(Word(s_holders[i], 8));
            if (!IsHeapPointer(res) || std::memcmp(res, "CGFX", 4) != 0) {
                Stop(5);
                return;
            }
        }
        // PwpPreview_LoadSlot と同じ順: モデル単体 → LUT と結ぶ → テクスチャ単体 → テクスチャと結ぶ
        ResHolderSetup(s_holders[kLut], s_resourceAllocator, 0, 1);
        ResHolderSetup(s_holders[kModel], s_resourceAllocator, 0, 1);
        ResHolderSetup(s_holders[kModel], s_resourceAllocator, reinterpret_cast<int>(s_holders[kLut]), 1);
        ResHolderSetup(s_holders[kTex], s_resourceAllocator, 0, 1);
        ResHolderSetup(s_holders[kModel], s_resourceAllocator, reinterpret_cast<int>(s_holders[kTex]), 1);
        void *model = FindModelByName(Word(s_holders[kModel], 4), s_modelName);
        if (!IsHeapPointer(model)) {
            Stop(6);
            return;
        }
        s_model = model;
        s_stage = Stage::Build;
        return;
    }
    case Stage::Build: {
        void *heap = *reinterpret_cast<void **>(Word(s_instanceAllocator, 4));
        if (!IsHeapPointer(heap) || HeapGetFreeSize(heap) < kInstanceReserve) {
            Stop(7);
            return;
        }
        NodeHolderCtor(s_node);
        const int created = ModelInstanceCreate(s_node, s_model, s_instanceAllocator, s_instanceAllocator,
                                                0x834u, 1u, 3u);
        void *node = *reinterpret_cast<void **>(Word(s_node, 4));
        if (created != 1 || !IsHeapPointer(node) ||
            !IsHeapPointer(*reinterpret_cast<void **>(Word(node, kNodeMaterialActivator)))) {
            Stop(8);
            return;
        }
        if (*Word(node, kNodeMeshArrayBegin) == *Word(node, kNodeMeshArrayEnd)) {
            Stop(9);                                // 自前の mesh 配列が無い形は壊さずに降りる（IDA-opus-5-F023）
            return;
        }
        MakeTranslucent();
        Pose();
        s_stage = Stage::Ready;
        return;
    }
    default:
        return;
    }
}

// 名前からパスと大きさを決める（メニュースレッド。書く先は pend）。モデルかテクスチャが無い種類は false。
// 役場 0x50・駅 0x54・0x4F は PwpPreview_LoadSlot 0x227CFC と同じ特別な名前（村の今の見た目の番号を庭データから読む:
// 庭 *(0x955F8C) +401848 の u16。役場 = 下位 2 ビット（sub_6CA35C）、駅 = ビット 8〜9（sub_6CA378））。
bool Resolve(u16 id) {
    char name[48];
    const char *base = PublicWorks::NameOf(id);
    const u32 garden = *reinterpret_cast<const volatile u32 *>(0x00955F8C);
    const u32 look = (garden >= 0x08000000u && BuildingHighlight::SafeReadable(garden + 401848, 2))
                         ? *reinterpret_cast<const volatile u16 *>(garden + 401848) : 0u;
    if (id == 0x50)
        std::snprintf(name, sizeof(name), "sobj_officeA%02u", (unsigned)(look & 3u));
    else if (id == 0x54)
        std::snprintf(name, sizeof(name), "sobj_stationA%02u", (unsigned)((look >> 8) & 3u));
    else if (id == 0x4F)
        std::snprintf(name, sizeof(name), "sobj_reset_cls");
    else
        std::snprintf(name, sizeof(name), "%s", base);
    if (name[0] == '\0')
        return false;
    const bool fobj = std::strncmp(name, "fobj_", 5) == 0;
    const bool sobj = std::strncmp(name, "sobj_", 5) == 0;
    if (!fobj && !sobj)
        return false;
    std::snprintf(s_pendPath[kLut], sizeof(s_pendPath[kLut]), fobj ? "Strc/fobj/lut/Lut_fieldobj.bcres"
                                                                   : "Strc/sobj/lut/Lut_sobj.bcres");
    std::snprintf(s_pendPath[kTex], sizeof(s_pendPath[kTex]),
                  fobj ? "Strc/fobj/%s/Textures/season00.bcres" : "Strc/sobj/%s/Textures/season00/season00.bcres",
                  name);
    std::snprintf(s_pendPath[kModel], sizeof(s_pendPath[kModel]), fobj ? "Strc/fobj/%s/%s.bcres" : "Strc/sobj/%s/%s.bcres",
                  name, name);
    std::snprintf(s_pendName, sizeof(s_pendName), "%s", name);
    for (u32 i = 0; i < kResCount; ++i) {
        s_pendSize[i] = RomfsIndex::FileSize(s_pendPath[i]);
        if (s_pendSize[i] == 0)
            return false;
    }
    return true;
}

}  // namespace

void FrameStep(void) {
    const s32 target = s_want ? s_pendId : -1;
    if (target != s_lastSeen) {
        s_lastSeen = target;
        s_stableFrames = 0;
    } else if (s_stableFrames < 1000) {
        ++s_stableFrames;
    }

    if (s_stage == Stage::Load) {                   // 読み込み中は切れ目まで進める（StepBuild が判断する）
        StepBuild();
        return;
    }
    // 止める・種類が変わった: 描くのをやめて 4 フレーム待ってから返す
    if (s_stage != Stage::Off && (target != s_builtId || s_stage == Stage::Teardown)) {
        if (s_stage != Stage::Teardown) {
            s_stage = Stage::Teardown;
            s_quietFrames = 0;
            return;
        }
        if (++s_quietFrames < 4)
            return;
        TeardownAll();
        return;
    }
    if (s_stage == Stage::Off) {
        if (target < 0 || s_stableFrames < kDebounceFrames)
            return;
        // 頼まれたものを写す（書きかけなら次のフレーム）
        const u32 seq = s_pendSeq;
        if ((seq & 1u) != 0)
            return;
        for (u32 i = 0; i < kResCount; ++i) {
            std::memcpy(s_pathText[i], s_pendPath[i], sizeof(s_pathText[i]));
            s_sizes[i] = s_pendSize[i];
        }
        std::memcpy(s_modelName, s_pendName, sizeof(s_modelName));
        if (s_pendSeq != seq || s_pendId != target)
            return;
        s_builtId = target;
        s_failReason = 0;
        s_stage = Stage::AllocHeaps;
    }
    if (s_stage == Stage::Failed)
        return;                                     // 同じ種類で失敗したまま。種類を変えるまで待つ
    if (s_sceneOwner != nullptr) {
        void *owner = *kSceneOwner;
        if (!IsHeapPointer(owner) || owner != s_sceneOwner ||
            *reinterpret_cast<void **>(Word(owner, kSceneOwnerResourceOffset)) != s_sceneResource)
            return;                                 // 場面が変わった。エディターが止める
    }
    if (s_stage != Stage::Ready) {
        StepBuild();
        return;
    }
    Pose();
    Animate();
    if (*Word(s_node, 4) != 0u)
        Submit(s_node, 0);
}

// ---- メニュースレッド ----

void Show(u16 id, s32 x, s32 y) {
    s_tx = x;
    s_ty = y;
    s_want = true;
    if (s_shownId == (s32)id)
        return;
    s_shownId = id;
    // 次に出すものを書く。描画スレッドは待たない（切り替えは描画スレッドが切れ目で行う）。
    s_pendSeq = s_pendSeq + 1;                      // 奇数 = 書きかけ
    const bool ok = Resolve(id);
    s_pendSeq = s_pendSeq + 1;
    s_pendId = ok ? (s32)id : -1;
}

void Hide(void) {
    s_want = false;
    s_shownId = -1;
    s_pendId = -1;
}

void SetWave(const Wave &wave) {
    s_wave = wave;
}

const Wave &GetWave(void) { return s_wave; }

Status GetStatus(void) {
    Status out;
    out.shownId = s_shownId;
    out.available = s_pendId >= 0 || s_shownId < 0;
    out.failed = s_stage == Stage::Failed;
    out.failReason = s_failReason;
    out.ready = s_stage == Stage::Ready;
    return out;
}

const char *StateName(void) {
    switch (s_stage) {
    case Stage::Off: return u8"止まっている";
    case Stage::AllocHeaps: return u8"ヒープ確保";
    case Stage::Load: return u8"読み込み中";
    case Stage::Setup: return u8"資源の準備";
    case Stage::Build: return u8"生成中";
    case Stage::Ready: return u8"表示中";
    case Stage::Teardown: return u8"解放中";
    case Stage::Failed: return u8"失敗";
    }
    return u8"?";
}

}  // namespace BuildingPreview
