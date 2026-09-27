#include "InstancedDraw.hpp"

#include "BuildingHighlight.hpp"
#include "GridCursorGameApi.hpp"

#include <cstring>

// 根拠: acnl_disassemble work/FINDINGS.md IDA-opus-5.5-F059、docs/topics/map_editor_3d.md §3。
//
// ★1 か所ごとに描画コンテキストの「最後に行列を送った体」ctx[2]+28 を 0 にする。sub_48D51C はそれが同じ体なら
//   RenderContext_SendModelMatrices（c0-c2 = 体 +140 世界行列、c3-c5 = 体 +444）を省くので、同じ体を続けて描くと
//   全部 1 か所目の位置に重なる。行列は値でコマンドへ写るので、描いたあとに書き換えてよい。
// ★体 +444 はシーンの更新が作る欄。描画リストへ出さない体は更新されないので、fgobj と同じく
//   fgobj_Object_SetModelView 0x58F984 に偽の物体（+264 = 体、+268 = 0）を渡して作る。視点は fgobj_DrawList と同じ 0xABD380 + 164。

namespace InstancedDraw {

using namespace GridCursor::Game;

namespace {

typedef void (*FuncNodeCreateFn)(void *holder, void *allocator);
typedef void (*FuncNodeSetCallbacksFn)(void *holder, void (*cb0)(u32, u32), void (*cb1)(u32, u32), u32 userdata);
typedef void (*HolderDestroyFn)(void *holder);
typedef void (*ModelViewFn)(const u8 *obj, const float *view);
typedef void (*DrawMeshFn)(u32 ctx, u32 mesh, u32 node);

const FuncNodeCreateFn       FuncNodeCreate       = reinterpret_cast<FuncNodeCreateFn>(0x004EFFB4);       // g3d_FuncNode_Create
const FuncNodeSetCallbacksFn FuncNodeSetCallbacks = reinterpret_cast<FuncNodeSetCallbacksFn>(0x004EFF58); // g3d_FuncNode_SetCallbacks
const HolderDestroyFn        HolderDestroy        = reinterpret_cast<HolderDestroyFn>(0x004ED668);        // SceneNodeHolder_Destroy
const ModelViewFn            ModelView            = reinterpret_cast<ModelViewFn>(0x0058F984);            // fgobj_Object_SetModelView
const DrawMeshFn             DrawMesh             = reinterpret_cast<DrawMeshFn>(0x0048D26C);             // gfx_DrawMesh

const u32 kCamera = 0x00ABD380, kCameraView = 164;      // sub_711A8C(&unk_ABD380, 1024) = +164
const u32 kGpuCmdPtr = 0x0096EA94, kGpuCmdEnd = 0x0096EA98;
const u32 kCmdRoom = 0x1000;                            // 1 か所を描く前に残っていてほしいコマンドの量
const u32 kCtxInner = 8, kInnerLastModel = 28;
const u32 kFakeObjBytes = 272, kFakeObjNode = 264, kFakeObjAnim = 268;
// 体（nw::gfx::Model）と資源の欄（Scene_DrawLayers 0x4EB840 と同じ）
const u32 kNodeRes = 8, kNodeMaterials = 0x164, kNodeMeshBegin = 0x170, kNodeMeshEnd = 0x174;
const u32 kNodeVisBegin = 0x17C, kNodeVisEnd = 0x180;
const u32 kResMeshCount = 180, kResMeshes = 184, kResVisDict = 208, kResLayer = 32;
const u32 kMeshMaterial = 28, kMeshVisible = 36, kMeshVisIndex = 38;
const u32 kAllLayers = 0xFFFFFFFFu;
// 村の物体の描画ノード（fgobj_Proc_Setup 0x59CDD0 が proc + 0x45B0 に作り、g3d_FuncNode_SetCallbacks で cb0 = 0x59A900 / cb1 = 0x59BFEC）
const u32 kFgobjProcPtr = 0x00948E70, kFgobjFuncHolder = 0x45B0, kFuncNodeCb0 = 0x148, kFgobjDrawCb0 = 0x0059A900;
const u32 kFuncNodeVtable = 0x008FCF10;     // vtbl_g3d_FuncNode
// 材質のフラグメント部分（汎用の書き出し 0x49CA24〜: a3[20] = M+0x50、+280 の bit1 = 深度書き込み、+720 = 鍵）
const u32 kMatFrag = 0x50, kFragOpFlags = 280, kFragKey = 720, kMaxDepthMats = 16;

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline void W32(u32 a, u32 v) { *reinterpret_cast<volatile u32 *>(a) = v; }
inline u32 Rel(u32 a) { const u32 v = R32(a); return v != 0u ? a + v : 0u; }

// Scene_DrawLayers と同じ可視の判定
bool MeshVisible(u32 node, u32 mesh) {
    const s16 vi = *reinterpret_cast<const volatile s16 *>(mesh + kMeshVisIndex);
    const bool meshOn = *reinterpret_cast<const volatile u8 *>(mesh + kMeshVisible) != 0u;
    if (vi < 0)
        return meshOn;
    u32 entry;
    if (R32(node + kNodeVisBegin) == R32(node + kNodeVisEnd)) {
        const u32 dict = Rel(R32(node + kNodeRes) + kResVisDict);
        entry = dict != 0u ? Rel(dict + 16u * (u32)vi + 40u) : 0u;
        if (entry == 0u)
            return false;
    } else {
        entry = R32(node + kNodeVisBegin) + 8u * (u32)vi;
    }
    return *reinterpret_cast<const volatile u8 *>(entry + 4) != 0u && meshOn;
}

void DrawNodeMeshes(u32 ctx, u32 node, u32 layer) {
    u32 it = R32(node + kNodeMeshBegin), end = R32(node + kNodeMeshEnd);
    if (it == end) {
        const u32 res = R32(node + kNodeRes);
        it = Rel(res + kResMeshes);
        end = it + 4u * R32(res + kResMeshCount);
    }
    for (; it != end; it += 4u) {
        const u32 mesh = Rel(it);
        if (mesh == 0u || !MeshVisible(node, mesh))
            continue;
        const u32 material = R32(R32(node + kNodeMaterials) + 4u * R32(mesh + kMeshMaterial));
        if (layer != kAllLayers && (R32(R32(material + 8) + kResLayer) & 0xFFu) != layer)
            continue;
        DrawMesh(ctx, mesh, node);
    }
}

void DrawBatches(u32 ctx, Drawer &d, u32 layer) {
    if (d.batches == nullptr || d.batchCount == 0u)
        return;
    float view[12];
    std::memcpy(view, reinterpret_cast<const void *>(kCamera + kCameraView), sizeof(view));
    alignas(8) u8 fake[kFakeObjBytes];
    std::memset(fake, 0, sizeof(fake));
    const u32 inner = R32(ctx + kCtxInner);
    for (u32 b = 0; b < d.batchCount; ++b) {
        const Batch &batch = d.batches[b];
        const u32 node = R32(reinterpret_cast<u32>(batch.holder) + 4);
        if (node == 0u)
            continue;
        std::memcpy(fake + kFakeObjNode, &node, 4);
        std::memset(fake + kFakeObjAnim, 0, 4);
        for (u32 i = 0; i < batch.count; ++i) {
            if (R32(kGpuCmdEnd) - R32(kGpuCmdPtr) < kCmdRoom) {
                d.skipped += batch.count - i;
                break;
            }
            SetMatrix3x4(batch.holder, batch.matrices[i]);
            UpdateWorldAndSkeleton(batch.holder);
            ModelView(fake, view);
            W32(inner + kInnerLastModel, 0u);           // 同じ体でも行列を送り直させる
            DrawNodeMeshes(ctx, node, layer);
            if (layer != 0u)
                ++d.drawn;
        }
    }
    W32(inner + kInnerLastModel, 0u);
}

void DrawLayer0(u32 ctx, u32 userdata) { DrawBatches(ctx, *reinterpret_cast<Drawer *>(userdata), 0u); }
void DrawLayer1(u32 ctx, u32 userdata) { DrawBatches(ctx, *reinterpret_cast<Drawer *>(userdata), 1u); }

// ---- 村の物体の下に描く ----
typedef void (*FuncNodeCbFn)(u32 ctx, u32 userdata);
const u32 kMaxUnder = 4;
Drawer *s_under[kMaxUnder];
u32 s_underProc, s_underNode;               // 包んだ描画ノードとその持ち主（場面が変わったら触らない）

void UnderCb0(u32 ctx, u32 userdata) {
    for (u32 i = 0; i < kMaxUnder; ++i) {
        Drawer *d = s_under[i];
        if (d == nullptr || !d->underArmed)
            continue;
        d->underArmed = false;
        DrawBatches(ctx, *d, kAllLayers);
    }
    reinterpret_cast<FuncNodeCbFn>(kFgobjDrawCb0)(ctx, userdata);   // 村の物体の層 0（fgobj_DrawCallbackLayer0 0x59A900 → fgobj_DrawList(proc, ctx, proc+13740)。先頭でキャッシュを捨てる）
}

// いまの場面の村の物体の描画ノード（無ければ 0）
u32 FgobjFuncNode(u32 &proc) {
    proc = R32(kFgobjProcPtr);
    if (proc < 0x08000000u || proc >= 0x40000000u || (proc & 3u) != 0u)
        return 0u;
    const u32 node = R32(proc + kFgobjFuncHolder + 4);
    if (node < 0x08000000u || node >= 0x40000000u || (node & 3u) != 0u || R32(node) != kFuncNodeVtable)
        return 0u;
    return node;
}

void Unhook(void) {
    u32 proc = 0;
    const u32 node = FgobjFuncNode(proc);
    if (node != 0u && node == s_underNode && proc == s_underProc && R32(node + kFuncNodeCb0) == reinterpret_cast<u32>(&UnderCb0))
        W32(node + kFuncNodeCb0, kFgobjDrawCb0);
    s_underNode = s_underProc = 0u;
}

}  // namespace

bool Create(Drawer &d, void *allocator) {
    std::memset(d.holder, 0, sizeof(d.holder));
    d.batches = nullptr;
    d.batchCount = 0;
    d.underArmed = false;
    FuncNodeCreate(d.holder, allocator);
    if (R32(reinterpret_cast<u32>(d.holder) + 4) == 0u)
        return false;
    FuncNodeSetCallbacks(d.holder, DrawLayer0, DrawLayer1, reinterpret_cast<u32>(&d));
    return true;
}

bool Created(const Drawer &d) {
    return R32(reinterpret_cast<u32>(d.holder) + 4) != 0u;
}

void Destroy(Drawer &d) {
    bool any = false;
    for (u32 i = 0; i < kMaxUnder; ++i) {
        if (s_under[i] == &d)
            s_under[i] = nullptr;
        any = any || s_under[i] != nullptr;
    }
    if (!any)
        Unhook();
    d.underArmed = false;
    d.batches = nullptr;
    d.batchCount = 0;
    if (Created(d))
        HolderDestroy(d.holder);
    std::memset(d.holder, 0, sizeof(d.holder));
}

bool SubmitUnder(Drawer &d, const Batch *batches, u32 count) {
    u32 proc = 0;
    const u32 node = FgobjFuncNode(proc);
    if (node == 0u)
        return false;
    const u32 cb = R32(node + kFuncNodeCb0);
    if (cb == kFgobjDrawCb0) {
        W32(node + kFuncNodeCb0, reinterpret_cast<u32>(&UnderCb0));
        s_underNode = node;
        s_underProc = proc;
    } else if (cb != reinterpret_cast<u32>(&UnderCb0)) {
        return false;                       // 誰かが別の物に替えている。触らない
    }
    u32 slot = kMaxUnder;
    for (u32 i = 0; i < kMaxUnder && slot == kMaxUnder; ++i)
        if (s_under[i] == &d)
            slot = i;
    for (u32 i = 0; i < kMaxUnder && slot == kMaxUnder; ++i)
        if (s_under[i] == nullptr)
            slot = i;
    if (slot == kMaxUnder)
        return false;
    s_under[slot] = &d;
    d.batches = batches;
    d.batchCount = count;
    d.drawn = 0;
    d.underArmed = count != 0u;
    return true;
}

void DisableDepthWrite(void *holder) {
    const u32 node = R32(reinterpret_cast<u32>(holder) + 4);
    if (node == 0u)
        return;
    const u32 arr = R32(node + kNodeMaterials);
    if (!BuildingHighlight::SafeReadable(arr, 4 * kMaxDepthMats))
        return;
    for (u32 k = 0; k < kMaxDepthMats; ++k) {
        const u32 m = R32(arr + 4 * k);
        if (!BuildingHighlight::LooksLikeMaterial(m))
            break;
        const u32 frag = R32(m + kMatFrag);
        if (!BuildingHighlight::SafeReadable(frag, kFragKey + 4))
            continue;
        W32(frag + kFragOpFlags, R32(frag + kFragOpFlags) & ~2u);
        W32(frag + kFragKey, 0u);
    }
}

void Submit(Drawer &d, const Batch *batches, u32 count) {
    d.batches = batches;
    d.batchCount = count;
    d.drawn = 0;
    if (Created(d) && count != 0u)
        GridCursor::Game::Submit(d.holder, 0);
}

}  // namespace InstancedDraw
