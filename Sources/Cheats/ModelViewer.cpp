#include "ModelViewer.hpp"

#include "Cheats.hpp"
#include "GridCursor.hpp"
#include "GridCursorGameApi.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"
#include "RomfsIndex.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ModelViewer {

using namespace GridCursor::Game;

#define MV_ALIGNED __attribute__((aligned(32)))

// ---------------------------------------------------------------------------------------
// Storage. One instance, so all of this is a single copy of what GridCursor keeps arrays of.
// ---------------------------------------------------------------------------------------

static u8 s_resourceHolder[kResourceHolderBytes] MV_ALIGNED;
static u8 s_resourceAllocator[16] MV_ALIGNED;
static u8 s_instanceAllocator[16] MV_ALIGNED;
static u8 s_holder[kNodeHolderBytes] MV_ALIGNED;

static const char kHeapNameText[] = "ModelViewer";
static const u32 kLoadAttempts = 240;
static const u32 kMaxModelName = 64;
static const u32 kInstanceHeapBytes = 0x8000;   // 1 体。実測 5,090 B + 余裕（F023 / F034）
static const u32 kInstanceReserve = 8192;       // 生成前にこれだけ空いているか見る

static SafeString s_heapName;
static SafeString s_path;
static char s_pathText[192];
static char s_modelName[kMaxModelName];

static volatile Stage s_stage = Stage::Off;
static volatile u32 s_failReason = Fail::kNone;
static volatile bool s_wantShown;
static volatile bool s_rebuild;

static void* s_sceneOwner;
static void* s_sceneResource;
static void* s_resource;
static void* s_model;
static void* s_node;
static u32 s_loadAttempts;
static u32 s_frames;
static u32 s_submits;
static u32 s_quietFrames;
static u32 s_resourceFree;
static u32 s_instanceFree;
static u32 s_modelCount;
static s32 s_modelIndex;
static u32 s_resourceHeapBytes;

static float s_scale = 1.0f;
static float s_offsetX;
static float s_offsetY;
static float s_offsetZ;

// ---------------------------------------------------------------------------------------
// Helpers. Same shape as GridCursor's; kept separate so that fixing one cannot silently
// change the other.
// ---------------------------------------------------------------------------------------

static inline u32* Word(void* base, u32 offset) {
    return reinterpret_cast<u32*>(reinterpret_cast<u8*>(base) + offset);
}

static inline bool IsHeapPointer(const void* p) {
    const u32 v = reinterpret_cast<u32>(p);
    return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u;
}

static void Stop(u32 reason) {
    s_failReason = reason;
    s_stage = Stage::Failed;
}

static bool RoomIsCurved() {
    return (kRoomFlags[*kRoomId] & kRoomFlagCurved) != 0u;
}

// CGFX の「相対オフセット」。値を持つ語の位置からの相対。
static void* Rel(void* base, u32 at) {
    const s32 value = *reinterpret_cast<s32*>(Word(base, at));
    if (value == 0)
        return nullptr;
    return reinterpret_cast<u8*>(base) + at + value;
}

// 読み込んだ直後の**素のファイル像**からモデルの名前を読む。
// この段階のオフセットは確実に「その語からの相対」なので、Setup を呼ぶ**前**に読む。
// Setup が何を書き換えるかは確かめていないが、この順ならどちらでも正しい。
static u32 ReadModelNames(void* resource, s32 wanted, char* nameOut, u32 cap) {
    if (std::memcmp(resource, "CGFX", 4) != 0)
        return 0;
    const u32 count = *Word(resource, 0x1C);
    if (count == 0 || count > 4096)
        return 0;
    void* table = Rel(resource, 0x20);
    if (table == nullptr || std::memcmp(table, "DICT", 4) != 0)
        return 0;
    if (*Word(table, 8) != count)
        return 0;
    if (wanted >= 0 && (u32)wanted < count) {
        void* entry = reinterpret_cast<u8*>(table) + 0x1C + 16 * (u32)wanted;
        const char* name = reinterpret_cast<const char*>(Rel(entry, 8));
        if (name == nullptr)
            return 0;
        u32 i = 0;
        while (i + 1 < cap && name[i] != '\0') {
            nameOut[i] = name[i];
            ++i;
        }
        nameOut[i] = '\0';
    }
    return count;
}

// ---------------------------------------------------------------------------------------
// Posing. Same sequence the grid cursor is placed with, minus the tiling.
// ---------------------------------------------------------------------------------------

static void Pose() {
    void* player = *kPlayer;
    if (!IsHeapPointer(player))
        return;
    const float* p = reinterpret_cast<const float*>(
        reinterpret_cast<u8*>(player) + kPlayerPositionOffset);

    float matrix[12];
    float in[3] = {p[0] + s_offsetX, p[1] + s_offsetY, p[2] + s_offsetZ};
    float out[3] = {in[0], in[1], in[2]};
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

    SetMatrix3x4(s_holder, matrix);
    // 骨の配列を埋めるのはこれ。書かないと submit しても 1 体も描かれない（F020）。
    UpdateWorldAndSkeleton(s_holder);
}

// ---------------------------------------------------------------------------------------
// Build and teardown, on the draw thread.
// ---------------------------------------------------------------------------------------

static void TeardownAll() {
    if (*Word(s_holder, 4) != 0u)
        ModelInstanceDestroy(s_holder);
    s_node = nullptr;
    if (*Word(s_resourceHolder, 0) == kResourceLoaderVtable) {
        // ヒープより先。逆にすると解放済みへポインタを辿る（IDA-opus-5-F027）。
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

static void StepBuild() {
    switch (s_stage) {
    case Stage::AllocHeaps: {
        if (s_pathText[0] == '\0') {
            Stop(Fail::kNoPath);
            return;
        }
        void* parent = *kParentHeap;
        if (!IsHeapPointer(parent)) {
            Stop(Fail::kNoParentHeap);
            return;
        }
        void* owner = *kSceneOwner;
        if (!IsHeapPointer(owner)) {
            Stop(Fail::kNoScene);
            return;
        }
        s_sceneOwner = owner;
        s_sceneResource = *reinterpret_cast<void**>(Word(owner, kSceneOwnerResourceOffset));

        s_heapName.vtable = kSafeStringVtable;
        s_heapName.text = kHeapNameText;
        s_path.vtable = kSafeStringVtable;
        s_path.text = s_pathText;

        HeapAllocatorCtor(s_resourceAllocator);
        HeapAllocatorCtor(s_instanceAllocator);
        // ★大きさはファイルから決める。ファイルは 16 KB から 5.5 MB まであり、親ヒープの
        //   空きは 500 KB ほどしかないので、固定値では小さすぎるか取れないかのどちらかになる。
        if (HeapCreateNamed(s_resourceAllocator, s_resourceHeapBytes, parent, &s_heapName,
                            1, 0u) != 1 ||
            !IsHeapPointer(*reinterpret_cast<void**>(Word(s_resourceAllocator, 4)))) {
            Stop(Fail::kResourceHeap);
            return;
        }
        if (HeapCreateNamed(s_instanceAllocator, kInstanceHeapBytes, parent, &s_heapName,
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
        // ★Setup より先に読む（ReadModelNames の注釈）。
        //   前に出していたモデルの名前を引きずらないよう、先に空にする。
        s_modelName[0] = '\0';
        s_modelCount = ReadModelNames(resource, s_modelIndex, s_modelName, sizeof(s_modelName));
        if (s_modelCount == 0) {
            Stop(Fail::kNoModels);
            return;
        }
        if (s_modelName[0] == '\0') {
            // 番号が範囲外だった。先頭へ丸めて読み直す。
            s_modelIndex = 0;
            if (ReadModelNames(resource, 0, s_modelName, sizeof(s_modelName)) == 0 ||
                s_modelName[0] == '\0') {
                Stop(Fail::kNoModels);
                return;
            }
        }
        ResHolderSetup(s_resourceHolder, s_resourceAllocator, 0, 1);
        s_resource = resource;
        s_stage = Stage::FindModel;
        return;
    }
    case Stage::FindModel: {
        void* model = FindModelByName(Word(s_resourceHolder, 4), s_modelName);
        if (!IsHeapPointer(model)) {
            Stop(Fail::kModelMissing);
            return;
        }
        s_model = model;
        s_stage = Stage::BuildInstance;
        return;
    }
    case Stage::BuildInstance: {
        void* heap = *reinterpret_cast<void**>(Word(s_instanceAllocator, 4));
        if (!IsHeapPointer(heap) || HeapGetFreeSize(heap) < kInstanceReserve) {
            // ゲームの生成器は確保の失敗を検査しない（IDA-opus-5-F034）。
            Stop(Fail::kHeapExhausted);
            return;
        }
        NodeHolderCtor(s_holder);
        const int created = ModelInstanceCreate(s_holder, s_model, s_instanceAllocator,
                                                s_instanceAllocator, 0x834u, 1u, 3u);
        void* node = *reinterpret_cast<void**>(Word(s_holder, 4));
        if (created != 1 || !IsHeapPointer(node)) {
            Stop(Fail::kCreateFailed);
            return;
        }
        // ★ここは型を見るだけで弾かない。Model と SkeletalModel のどちらも来る。
        if (!IsHeapPointer(*reinterpret_cast<void**>(Word(node, kNodeMaterialActivator)))) {
            Stop(Fail::kActivatorNull);
            return;
        }
        if (*Word(node, kNodeMeshArrayBegin) == *Word(node, kNodeMeshArrayEnd)) {
            // 自前の mesh 配列を持たない。壊さずに降りる（IDA-opus-5-F023）。
            Stop(Fail::kSharedMeshArray);
            return;
        }
        s_node = node;
        if (!IsHeapPointer(*kPlayer)) {
            Stop(Fail::kNoPlayer);
            return;
        }
        Pose();
        s_stage = Stage::Ready;
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------------------------------
// Called every frame from the grid cursor's stub, whatever that cheat is doing.
// ---------------------------------------------------------------------------------------

void FrameStep(void) {
    ++s_frames;

    if (s_rebuild && s_stage != Stage::Off && s_stage != Stage::Failed) {
        if (s_stage != Stage::Teardown) {
            s_stage = Stage::Teardown;
            s_quietFrames = 0;
            return;
        }
        if (++s_quietFrames < 4)
            return;
        TeardownAll();
        s_rebuild = false;
        if (s_wantShown)
            s_stage = Stage::AllocHeaps;
        return;
    }
    if (s_rebuild) {
        s_rebuild = false;
        if (s_wantShown && s_stage == Stage::Off)
            s_stage = Stage::AllocHeaps;
        return;
    }

    if (s_stage == Stage::Off || s_stage == Stage::Failed || s_stage == Stage::Teardown)
        return;

    // 部屋が変わったら自分で止まる。所有者を控える前に掛けない（IDA-opus-5-F031）。
    if (s_sceneOwner != nullptr) {
        void* owner = *kSceneOwner;
        const bool same = IsHeapPointer(owner) && owner == s_sceneOwner &&
                          *reinterpret_cast<void**>(Word(owner, kSceneOwnerResourceOffset)) ==
                              s_sceneResource;
        if (!same)
            return;
    }

    if (s_stage != Stage::Ready) {
        StepBuild();
        return;
    }

    Pose();
    if (*Word(s_holder, 4) != 0u) {
        Submit(s_holder, 0);
        ++s_submits;
    }

    if ((s_frames % 60u) == 0u) {
        void* resourceHeap = *reinterpret_cast<void**>(Word(s_resourceAllocator, 4));
        void* instanceHeap = *reinterpret_cast<void**>(Word(s_instanceAllocator, 4));
        s_resourceFree = IsHeapPointer(resourceHeap) ? HeapGetFreeSize(resourceHeap) : 0u;
        s_instanceFree = IsHeapPointer(instanceHeap) ? HeapGetFreeSize(instanceHeap) : 0u;
    }
}

// ---------------------------------------------------------------------------------------
// Menu-thread side.
// ---------------------------------------------------------------------------------------

static void RequestRebuild() {
    if (!s_wantShown)
        return;
    s_rebuild = true;
}

void SetPath(const char* romfsPath) {
    if (romfsPath == nullptr)
        return;
    std::snprintf(s_pathText, sizeof(s_pathText), "%s", romfsPath);
    s_modelIndex = 0;
    RequestRebuild();
}

const char* Path(void) { return s_pathText; }

void SetModelIndex(s32 index) {
    if (index < 0)
        index = 0;
    if (s_modelIndex == index)
        return;
    s_modelIndex = index;
    RequestRebuild();
}

void SetScalePercent(s32 percent) {
    if (percent < 1) percent = 1;
    if (percent > 2000) percent = 2000;
    s_scale = (float)percent / 100.0f;
}

void SetOffset(s32 x, s32 y, s32 z) {
    s_offsetX = (float)x;
    s_offsetY = (float)y;
    s_offsetZ = (float)z;
}

bool Show(void) {
    if (s_pathText[0] == '\0') {
        s_failReason = Fail::kNoPath;
        return false;
    }
    if (!GridCursor::InstallFrameHook()) {
        s_failReason = Fail::kHookFailed;
        return false;
    }
    if (!GridCursor::AddExtraFrameStep(FrameStep)) {
        s_failReason = Fail::kHookFailed;
        return false;
    }
    s_failReason = Fail::kNone;
    s_wantShown = true;
    if (s_stage == Stage::Off || s_stage == Stage::Failed)
        s_stage = Stage::AllocHeaps;
    return true;
}

void Hide(void) {
    s_wantShown = false;
    s_rebuild = false;
    if (s_stage == Stage::Off)
        return;
    s_stage = Stage::Teardown;
    s_quietFrames = 0;
    s_rebuild = true;      // 解放だけして、出し直さない
}

bool IsShown(void) {
    return s_wantShown && s_stage != Stage::Failed;
}

void Shutdown(void) {
    s_wantShown = false;
    s_stage = Stage::Off;
}

Status Read(void) {
    Status out;
    out.stage = s_stage;
    out.failReason = s_failReason;
    out.modelCount = s_modelCount;
    out.modelIndex = (u32)(s_modelIndex < 0 ? 0 : s_modelIndex);
    out.frames = s_frames;
    out.submits = s_submits;
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
    case Stage::BuildInstance: return u8"生成中";
    case Stage::Ready: return u8"描画中";
    case Stage::Teardown: return u8"解放中";
    case Stage::Failed: return u8"停止（失敗）";
    }
    return u8"?";
}

const char* FailName(u32 reason) {
    switch (reason) {
    case Fail::kNone: return u8"-";
    case Fail::kNoIndex: return u8"RomFS の索引がありません";
    case Fail::kNoPath: return u8"モデルが選ばれていません";
    case Fail::kNoParentHeap: return u8"親ヒープが取れません";
    case Fail::kResourceHeap: return u8"資源ヒープが作れません（大きすぎます）";
    case Fail::kInstanceHeap: return u8"instance ヒープが作れません";
    case Fail::kLoadGaveUp: return u8"読み込みが終わりません";
    case Fail::kNotCgfx: return u8"CGFX ではありません";
    case Fail::kNoModels: return u8"中にモデルがありません";
    case Fail::kModelMissing: return u8"その名前のモデルが引けません";
    case Fail::kCreateFailed: return u8"instance の生成に失敗しました";
    case Fail::kActivatorNull: return u8"instance が半分しかできていません";
    case Fail::kSharedMeshArray: return u8"mesh 配列を資源と共有しています";
    case Fail::kNoPlayer: return u8"プレイヤーが取れません";
    case Fail::kNoScene: return u8"シーンが取れません";
    case Fail::kHookFailed: return u8"フックが入れられません";
    case Fail::kHeapExhausted: return u8"instance ヒープの残りが足りません";
    }
    return u8"?";
}

// 資源ヒープの大きさはファイルから決める。ここだけメニュー側から触る。
void SetResourceHeapFromFileSize(u32 fileBytes) {
    // 実績のある比率: 16,256 B のファイルに 32,768 B（グリッドカーソル）。
    s_resourceHeapBytes = fileBytes * 2 + 0x4000;
}

}  // namespace ModelViewer

// ---------------------------------------------------------------------------------------
// gohan のメニューとの結び付け（gohan.md「テスト」フォルダ）
// ---------------------------------------------------------------------------------------

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            // メニュー項目は 255 件までしか options を持てない（optionCount が u8）ので、
            // 一覧は窓をずらして見せる。
            const u32 kWindow = 200;

            int             g_buildIndex = -1;
            int             g_scrollIndex = -1;
            int             g_pickIndex = -1;
            int             g_showIndex = -1;
            int             g_modelIndex = -1;
            int             g_scaleIndex = -1;
            int             g_xIndex = -1;
            int             g_yIndex = -1;
            int             g_zIndex = -1;

            const char     *g_names[kWindow];
            u32             g_windowStart;
            u32             g_windowCount;

            void    RefillWindow(void)
            {
                if (g_pickIndex < 0)
                    return;
                const int at = g_scrollIndex >= 0 ? GuiMenu::ItemApplied(g_scrollIndex) : 0;

                g_windowStart = at < 0 ? 0u : (u32)at;
                g_windowCount = RomfsIndex::Window(g_windowStart, g_names, kWindow);
                if (g_windowCount == 0)
                {
                    g_names[0] = u8"（ここには何もありません）";
                    g_windowCount = 1;
                }
                GuiMenu::SetItemOptions(g_pickIndex, g_names, (int)g_windowCount);
                GuiMenu::SetItemApplied(g_pickIndex, 0);
            }

            void    ApplyPick(void)
            {
                if (!RomfsIndex::Ready() || g_pickIndex < 0)
                    return;
                const int slot = GuiMenu::ItemApplied(g_pickIndex);

                if (slot < 0 || (u32)slot >= g_windowCount)
                    return;
                const u32 absolute = g_windowStart + (u32)slot;

                ModelViewer::SetResourceHeapFromFileSize(RomfsIndex::SizeAt(absolute));
                ModelViewer::SetPath(RomfsIndex::PathAt(absolute));
            }

            void    BuildExecute(int index)
            {
                (void)index;
                static char message[96];

                if (!RomfsIndex::Build())
                {
                    std::snprintf(message, sizeof(message), u8"%s (0x%08lX)",
                                  RomfsIndex::ReasonName(RomfsIndex::Reason()),
                                  (unsigned long)RomfsIndex::ErrorCode());
                    GuiNotification::NotifyRed(kMvBuild, message);
                    return;
                }
                RefillWindow();
                // 本体と更新を分けて出す。更新側が 0 なら開けていないということ。
                const unsigned long total = (unsigned long)RomfsIndex::Count();
                const unsigned long fromUpdate = (unsigned long)RomfsIndex::UpdateCount();

                std::snprintf(message, sizeof(message), u8".bcres %lu 件 (本体%lu+更新%lu)",
                              total, total - fromUpdate, fromUpdate);
                GuiNotification::Notify(kMvBuild, message);
            }

            void    ScrollApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                RefillWindow();
            }

            void    PickApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                ApplyPick();
            }

            void    ModelIndexApplied(int index, s32 value)
            {
                (void)index;
                ModelViewer::SetModelIndex(value);
            }

            void    ScaleApplied(int index, s32 value)
            {
                (void)index;
                ModelViewer::SetScalePercent(value);
            }

            void    OffsetApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                ModelViewer::SetOffset(g_xIndex >= 0 ? GuiMenu::ItemApplied(g_xIndex) : 0,
                                       g_yIndex >= 0 ? GuiMenu::ItemApplied(g_yIndex) : 0,
                                       g_zIndex >= 0 ? GuiMenu::ItemApplied(g_zIndex) : 0);
            }

            bool    ShowIsActive(int index)
            {
                (void)index;
                return ModelViewer::IsShown();
            }

            void    ShowSetActive(int index, bool active)
            {
                (void)index;
                if (!active)
                {
                    ModelViewer::Hide();
                    return;
                }
                ApplyPick();
                if (g_scaleIndex >= 0)
                    ModelViewer::SetScalePercent(GuiMenu::ItemApplied(g_scaleIndex));
                OffsetApplied(0, 0);
                if (!ModelViewer::Show())
                {
                    const ModelViewer::Status s = ModelViewer::Read();

                    GuiNotification::NotifyRed(kMvShow, ModelViewer::FailName(s.failReason));
                }
            }

            const GuiMenu::ToggleEffectFuncs kShowFuncs = { ShowIsActive, ShowSetActive };

            void    StatusExecute(int index)
            {
                (void)index;
                const ModelViewer::Status s = ModelViewer::Read();
                static char message[96];

                if (s.failReason != ModelViewer::Fail::kNone)
                    std::snprintf(message, sizeof(message), u8"%s",
                                  ModelViewer::FailName(s.failReason));
                else
                    std::snprintf(message, sizeof(message), u8"%s %lu/%lu 提%lu",
                                  ModelViewer::StageName(s.stage),
                                  (unsigned long)s.modelIndex + 1,
                                  (unsigned long)s.modelCount,
                                  (unsigned long)s.submits);
                GuiNotification::Notify(kMvStat, message);
            }
        }

        void    WireModelViewer(void)
        {
            g_buildIndex = GuiMenu::FindItem(kMvBuild);
            g_scrollIndex = GuiMenu::FindItem(kMvScroll);
            g_pickIndex = GuiMenu::FindItem(kMvPick);
            g_showIndex = GuiMenu::FindItem(kMvShow);
            g_modelIndex = GuiMenu::FindItem(kMvIndex);
            g_scaleIndex = GuiMenu::FindItem(kMvScale);
            g_xIndex = GuiMenu::FindItem(kMvX);
            g_yIndex = GuiMenu::FindItem(kMvY);
            g_zIndex = GuiMenu::FindItem(kMvZ);

            const int statIndex = GuiMenu::FindItem(kMvStat);

            if (g_buildIndex >= 0)
                GuiMenu::RegisterExecute(g_buildIndex, BuildExecute);
            if (statIndex >= 0)
                GuiMenu::RegisterExecute(statIndex, StatusExecute);
            if (g_showIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_showIndex, &kShowFuncs);
            if (g_scrollIndex >= 0)
                GuiMenu::RegisterApply(g_scrollIndex, ScrollApplied);
            if (g_pickIndex >= 0)
                GuiMenu::RegisterApply(g_pickIndex, PickApplied);
            if (g_modelIndex >= 0)
                GuiMenu::RegisterApply(g_modelIndex, ModelIndexApplied);
            if (g_scaleIndex >= 0)
                GuiMenu::RegisterApply(g_scaleIndex, ScaleApplied);
            if (g_xIndex >= 0)
                GuiMenu::RegisterApply(g_xIndex, OffsetApplied);
            if (g_yIndex >= 0)
                GuiMenu::RegisterApply(g_yIndex, OffsetApplied);
            if (g_zIndex >= 0)
                GuiMenu::RegisterApply(g_zIndex, OffsetApplied);
        }
    }
}

