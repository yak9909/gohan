#include "MapEditor3D.hpp"

#include "BuildingHighlight.hpp"
#include "GridCursorGameApi.hpp"
#include "InstancedDraw.hpp"

#include <cstring>

// 設計: acnl_disassemble docs/topics/map_editor_3d.md（IDA-opus-5.5-D004）、根拠 IDA-opus-5.5-F055 / F056。
// 色の混ぜ方は BuildingHighlight（F010〜F013）、自前の体と半透明は BuildingPreview（F002 / F035）と同じ。
//
// ★村の実体（fgobj）の材質は書き換えない（F056）。材質の物体は同じモデルの実体で共有されていて（利用者報告: 触っていない
//   同じモデルも赤くなった）、ゲームの簡易版の書き出し（0x4A17B8）が季節資源の未解決の参照表を読んで落ちた（0x4B0C54、実機）。
//   複製は実体の体 +8 の資源から作った自前の体で描く。★モデル（資源）1 種類につき体 1 つで、同じモデルの置き場は
//   描画ノード（InstancedDraw、F059）が行列を変えて何か所にも描く。ヒープはモデルの種類の数だけ。
// ★赤は体を作らない（F058）。村の実体の描画関数の表 off_948F90（2 語）を自前の包みへ差し替え、選んだマスの物体を描くときだけ
//   材質を結んだ直後に TEV 段 5 へ赤を混ぜるコマンドを積む。描いたあとはゲームのフェード（obj+104 の 0x800）と同じ手順で
//   材質・形状のキャッシュを捨てるので、同じモデルのほかの物体には漏れない。モデルの共有もヒープも関係なく何個でも赤くできる。
// ★自前の体を壊す前に、写しの材質 +648 に繋いだ自前の TEV を外す（F057）。ゲームの材質の後片付け（0x4AF288 → 0x4AE2A4）は
//   写しの +648 の TEV → +40 の参照表を辿って参照を +12 = 0 に外し（sub_4A7420）、TEV・表・参照を体のアロケータで解放する（sub_4AE958）。
//   自前の TEV の表は元の季節資源の表なので、外さずに壊すと元のモデルの参照が外れ、ゲームがそのモデルを描いた所で落ちた（実機 3 回）。

namespace MapEditor3D {

using namespace GridCursor::Game;

namespace {

// ---- ゲーム側 --------------------------------------------------------------------------------
const u32 kProcPtr = 0x00948E70;            // u32: fgobj::Proc（村の実体と季節資源の持ち主。kSceneOwner と同じ番地）
const u32 kOutdoorFlag = 0x00948E5F;        // s8: 屋外
const u32 kProcFgResource = 0x34;           // 季節資源（FieldSeasonResource_FindModel 0x5992D4 = FindModel(proc + 0x34)）
const u32 kProcItemResource = 0x13C;        // 一般のアイテムのモデル（fgobj_Object_BuildItemModel の FindModel(proc + 0x13C)）
const u32 kProcFgTable = 0x414;             // fgobj の形の表（22 B。+5 = 名前。fgobj_Object_BuildForItem 0x58FD44）
const u32 kProcObjects = 13732;             // 実体の連結の先頭（fgobj_DestroyObjectsAt 0x5A1A90 と同じ辿り方）
const u32 kObjNext = 8, kObjX = 40, kObjY = 44, kObjNode = 92;     // ノード = ホルダ（+88）の +4
const u32 kMaxWalk = 4096;
// 体（nw::gfx::TransformNode / Model。ctor 0x497F4C）
const u32 kNodeRes = 8;                     // ResModel（ctor の第 3 引数）
const u32 kNodeScale = 0x7C;                // 拡大率 x, y, z

typedef void *(*ItemPtrFn)(const void *item);
typedef int (*ItemTestFn)(const void *item);
typedef const char *(*LeafNameFn)(const u8 *type, int outdoor);
typedef int (*SandFn)(s32 x, s32 y, u32 zero);
typedef float (*GroundHeightFn)(const float *pos, u32 zero);
typedef void *(*AllocFn)(void *allocator, u32 size, s32 align);
typedef void (*FreeFn)(void *allocator, void *p);                    // ssys::ma::HeapAllocator vtable +12 = Allocator::Free 0x569B44
typedef void (*SetScaleFn)(void *holder, const float *xyz);

const ItemPtrFn      ItemRecord     = reinterpret_cast<ItemPtrFn>(0x00535188);      // ItemParam_GetRecord
const ItemPtrFn      FieldObjDef    = reinterpret_cast<ItemPtrFn>(0x002FD064);      // Item_GetFieldObjDef（ID >= 0xFE は 0）
const ItemTestFn     ItemTestA      = reinterpret_cast<ItemTestFn>(0x0076BE04);     // fgobj_Object_BuildItemModel の特例（8 / 9）
const ItemTestFn     ItemTestB      = reinterpret_cast<ItemTestFn>(0x00313654);
const LeafNameFn     LeafName       = reinterpret_cast<LeafNameFn>(0x005350C0);     // ItemModel_NameForType
const SandFn         IsSand         = reinterpret_cast<SandFn>(0x00300A84);
const GroundHeightFn GroundHeight   = reinterpret_cast<GroundHeightFn>(0x006C69C0);
const SetScaleFn     SetScale       = reinterpret_cast<SetScaleFn>(0x004ED6B4);     // TransformNodeHolder_SetScale

// 材質（BuildingHighlight / BuildingPreview と同じ欄。IDA-opus-5.5-F010）
const u32 kModelMaterials = 0x164;
const u32 kModelActivator = 0x1EC;
const u32 kMatResource = 8, kMatColour = 48, kMatTev = 72, kMatFrag = 80;
// ★汎用の書き出しは参照表をライティングの部分 M+0x44 から読む（0x49C53C: LDR R0,[R8,#0x44] → SP+0x60 → 0x49C7EC で +0x288 → +0x28）。
//   ここが資源のままだと季節資源の未解決の表を読んで落ちる（ca85896、実機 2026-09-28。戻り先 0x49C818）
const u32 kMatLighting = 0x44;
const u32 kResTevRel = 648, kResTevKey = 712, kResFragKey = 720, kResLayer = 32;
const u32 kResConst5 = 36 + 4 * 54;
const u32 kTevBytes = 244, kTevLutRel = 40;
const u32 kFragFbRead = 300, kFragCheckA = 320, kFragCheckB = 324, kFragBlend = 328, kFragBlendColor = 336;
const u32 kBlendConstAlpha = (12u << 16) | (13u << 20) | (1u << 24) | (13u << 28);
const u32 kGenericActivate = 0x0049C08C, kSimpleActivate = 0x004A17B8;
const u32 kMaxMatsPerModel = 16;
// 参照表の項目の型（sub_4A583C: 0x80000000 = そのもの、0x40000000 = +12 の相対の先。ほかは NULL）
const u32 kRefDirect = 0x80000000u, kRefRelative = 0x40000000u;

// 見た目（利用者の指示 2026-09-27: 選んだ物は赤、移動の複製は薄い白・半透明・少し上）
enum Style : u8 { kPreview, kRed };
const u32 kRedColor = 0x004040FFu;          // 0x00BBGGRR（BuildingHighlight::kRed と同じ）
const u8 kRedTint = 0xC0;
// ハイライトの種類ごとの色と濃さ（利用者指示 2026-09-29: 赤・青・白、濃さは項目で。描画スレッドが読む）
const u32 kHighlightColor[kHighlightKinds] = { kRedColor, 0x00FF6020u, 0x00FFFFFFu };   // 赤 / 青（スポイト）/ 白（配置の中心）
// 濃さは利用者の決定（2026-09-29）: 赤 150・青 80・白 40（調整用の項目は外した）
volatile u8 s_hlStrength[kHighlightKinds] = { 150, 80, 40 };
u8 s_curKind = kHighlightRed;               // いま描いている物体の種類（DrawTinted0 / 1 が決め、EmitTint が読む）
const u32 kWhite = 0x00FFFFFFu;
const u8 kWhiteTint = 0x60;
const u8 kPreviewAlpha = 0xA0;
const float kPreviewLift = 6.0f;            // 世界の長さ（1 マス = 32）

// ヒープ（親ヒープから。BuildingPreview と同じく親に必ず残す）
const u32 kHeapBytesBig = 0x40000, kHeapBytesSmall = 0x20000;
const u32 kParentReserve = 0x10000;
const u32 kHeapLow = 0x1000;
const u32 kQuietFrames = 4;                 // 描くのをやめてから壊すまで

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }
inline void W32(u32 a, u32 v) { *reinterpret_cast<volatile u32 *>(a) = v; }
inline u32 Word(void *p, u32 off) { return *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(p) + off); }
inline bool HeapPtr(u32 p) { return p >= 0x08000000u && p < 0x40000000u && (p & 3u) == 0u; }
inline bool Readable(u32 a, u32 len) { return HeapPtr(a) && BuildingHighlight::SafeReadable(a, len); }

// ---- ヒープ ----------------------------------------------------------------------------------
alignas(8) u8 s_alloc[16];
bool s_heapMade;
u32 s_heapProc, s_heapRes;                  // 作ったときの場面
const char kHeapNameText[] = "MapEditor3D";
SafeString s_heapName;

void *Heap(void) { return reinterpret_cast<void *>(Word(s_alloc, 4)); }

void *Alloc(u32 size) {
    if (!s_heapMade)
        return nullptr;
    void **vt = *reinterpret_cast<void ***>(s_alloc);
    return reinterpret_cast<AllocFn>(vt[2])(s_alloc, size, 4);
}

void FreeOwn(u32 p) {
    if (!s_heapMade || p == 0)
        return;
    void **vt = *reinterpret_cast<void ***>(s_alloc);
    reinterpret_cast<FreeFn>(vt[3])(s_alloc, reinterpret_cast<void *>(p));
}

InstancedDraw::Drawer s_drawer;             // 複製を描く描画ノード（自前のヒープに作る）
void DropHeap(void);

bool HeapRoom(u32 need) {
    return s_heapMade && HeapGetFreeSize(Heap()) >= need + kHeapLow;
}

bool EnsureHeap(void) {
    if (s_heapMade)
        return true;
    void *parent = *kParentHeap;
    const u32 proc = R32(kProcPtr);
    if (!HeapPtr(reinterpret_cast<u32>(parent)) || !HeapPtr(proc))
        return false;
    const u32 freeBytes = HeapGetFreeSize(parent);
    u32 bytes = kHeapBytesBig;
    if (freeBytes < bytes + kParentReserve)
        bytes = kHeapBytesSmall;
    if (freeBytes < bytes + kParentReserve)
        return false;
    s_heapName.vtable = kSafeStringVtable;
    s_heapName.text = kHeapNameText;
    HeapAllocatorCtor(s_alloc);
    if (HeapCreateNamed(s_alloc, bytes, parent, &s_heapName, 1, 0u) != 1 || !HeapPtr(Word(s_alloc, 4))) {
        std::memset(s_alloc, 0, sizeof(s_alloc));
        return false;
    }
    s_heapMade = true;
    s_heapProc = proc;
    s_heapRes = R32(proc + kProcFgResource);
    // 描画ノード。生成関数は確保の失敗を確かめないので残りを見てから（F059）
    if (!HeapRoom(InstancedDraw::kCreateBytes) || !InstancedDraw::Create(s_drawer, s_alloc)) {
        DropHeap();
        return false;
    }
    return true;
}

void DropHeap(void) {
    if (s_heapMade && Word(s_alloc, 4) != 0u)
        HeapAllocatorDestroyHeap(s_alloc);
    std::memset(s_alloc, 0, sizeof(s_alloc));
    s_heapMade = false;
}

// ---- 書き出し関数（自前の体だけ汎用へ）----------------------------------------------------------
//   写しの vtable は元の vtable ごとに 1 つを gohan の .bss に置き、返さない。
struct VtCopy { u32 orig; u32 words[6]; };
const u32 kMaxVt = 8;
VtCopy s_vt[kMaxVt];
u32 s_vtCount;

bool SwapToGeneric(u32 node) {
    const u32 act = R32(node + kModelActivator);
    if (!Readable(act, 4))
        return false;
    const u32 vt = R32(act);
    if (vt < 0x00100000u || vt >= 0x01000000u)
        return false;
    if (R32(vt + 12) == kGenericActivate)
        return true;
    if (R32(vt + 12) != kSimpleActivate)
        return false;
    u32 copy = 0;
    for (u32 i = 0; i < s_vtCount && copy == 0; ++i)
        if (s_vt[i].orig == vt)
            copy = reinterpret_cast<u32>(&s_vt[i].words[1]);
    if (copy == 0) {
        if (s_vtCount >= kMaxVt)
            return false;
        VtCopy &c = s_vt[s_vtCount++];
        c.orig = vt;
        for (u32 j = 0; j < 6; ++j)
            c.words[j] = R32(vt - 4 + 4 * j);
        c.words[4] = kGenericActivate;      // slot 3（words[0] は TypeInfo）
        copy = reinterpret_cast<u32>(&c.words[1]);
    }
    W32(act, copy);
    return true;
}

// ---- 参照表（フラグメントライティングの LUT）の安全確認 --------------------------------------------
//   書き出し（MaterialActivator_SendFragmentLuts 0x4B09F4）は表の 6 項目を相対で辿り、項目 +8 の相対の先を sub_4A583C で解く。
//   型が 0x80000000 / 0x40000000（先が 0 でない）以外、または +8 が 0 だと NULL を読んで落ちる（0x4B0C54、実機）。
//   項目が 0 なら飛ばす → 解けない表の代わりに「全部 0 の表」を指す。汎用の書き出しは表を必ず読む（0x49C7EC〜0x49C814）。
alignas(8) u32 s_zeroLut[8];

bool LutSafe(u32 tev) {
    const u32 rel = R32(tev + kTevLutRel);
    if (rel == 0)
        return false;
    const u32 table = tev + kTevLutRel + rel;
    if (!Readable(table, 24))
        return false;
    for (u32 i = 0; i < 6; ++i) {
        const u32 r = R32(table + 4 * i);
        if (r == 0)
            continue;
        const u32 entry = table + 4 * i + r;
        if (!Readable(entry, 12))
            return false;
        const u32 r2 = R32(entry + 8);
        if (r2 == 0)
            return false;
        const u32 ref = entry + 8 + r2;
        if (!Readable(ref, 16))
            return false;
        const u32 type = R32(ref);
        if (type == kRefDirect)
            continue;
        if (type != kRefRelative || R32(ref + 12) == 0 || !Readable(ref + 12 + R32(ref + 12), 12))
            return false;
    }
    return true;
}

// 写しの TEV の +40 を、元の表（解ければ）か全部 0 の表へ向ける
void LinkLut(u32 copyTev, u32 srcTev) {
    const u32 target = LutSafe(srcTev) ? srcTev + kTevLutRel + R32(srcTev + kTevLutRel)
                                       : reinterpret_cast<u32>(s_zeroLut);
    W32(copyTev + kTevLutRel, target - (copyTev + kTevLutRel));
}

// ---- モデルの引き方（fgobj_Object_BuildForItem 0x58FB40 と同じ）。実体が無いときだけ使う --------------
void *ResolveModel(u32 value, s32 x, s32 y) {
    const u32 proc = R32(kProcPtr);
    const s8 outdoor = *reinterpret_cast<const volatile s8 *>(kOutdoorFlag);
    if (!HeapPtr(proc) || outdoor == 0)
        return nullptr;
    u32 item = value;
    const void *ip = &item;
    const u16 id = (u16)(item & 0x7FFFu);
    const char *name = nullptr;
    u32 set = proc + kProcFgResource;
    if ((item >> 31) != 0) {                // 埋まっている（+2 の bit15。Item_IsBuriedFlag）
        const bool sand = IsSand(x, y, 0) == 1;
        name = id == 8352 ? (sand ? "fg_crack_sand_s" : "fg_crack_soil_s") : (sand ? "fg_crack_sand" : "fg_crack_soil");
    } else if (id == 158) {
        name = IsSand(x, y, 0) == 1 ? "fg_hole_sand" : "fg_hole_soil";
    } else if (id > 0xFD) {
        if ((u32)(id - 0x2000u) < 0x172Bu) {                // sub_2FCB40 → fgobj_Object_BuildItemModel
            const u32 rec = reinterpret_cast<u32>(ItemRecord(ip));
            if (!Readable(rec, 12))
                return nullptr;
            u8 type = R8(rec + 11);
            if (type >= 0xAA)
                type = 0;
            if (ItemTestA(ip) != 0)
                type = ItemTestB(ip) != 0 ? 9 : 8;
            name = LeafName(&type, outdoor);
            set = proc + kProcItemResource;
        } else if ((item & 0x6000u) == 0x4000u) {           // sub_767930
            name = outdoor != 0 ? "fg_wrapping_out" : "fg_wrapping";
            set = proc + kProcItemResource;
        } else {
            return nullptr;
        }
    } else {                                                // fgobj: 形の表
        const u32 def = reinterpret_cast<u32>(FieldObjDef(ip));
        if (!Readable(def, 16))
            return nullptr;
        u32 n = R8(def + 12);
        if (n >= 0x87)
            n = 0;
        const u32 table = R32(proc + kProcFgTable);
        if (!Readable(table + 22 * n, 22))
            return nullptr;
        name = reinterpret_cast<const char *>(table + 22 * n + 5);
    }
    if (name == nullptr)
        return nullptr;
    void *model = FindModelByName(reinterpret_cast<void *>(set), name);
    return HeapPtr(reinterpret_cast<u32>(model)) ? model : nullptr;
}

// ---- 赤（F058）: 村の実体の描画に割り込む ---------------------------------------------------------
//   sub_59A918（fgobj の FuncNode のコールバック）が描画リストの物体ごとに off_948F90[obj+285] を (obj, ctx, &視点) で呼ぶ。
//   表は 0x948E5C + 0x134（sub_59A918 の R10。全体で読むのはここだけ）、.data の初期値 {0x58F6B8, 0}, {0x58FA78, 0}。
const u32 kDrawTable = 0x00948F90;
const u32 kDrawKind0 = 0x0058F6B8;          // 材質番号のキャッシュ付き。材質・形状 → obj+120 → (0x800) → 描く
const u32 kDrawKind1 = 0x0058FA78;          // キャッシュを捨ててからメッシュごとに sub_48D26C
const u32 kLastShapeKey = 0x00948E94, kLastMatGroup = 0x00948E98, kLastShape = 0x00948E9C;
const u32 kGpuCmdPtr = 0x0096EA94;          // g_GpuCmdPtr
const u32 kObjPreDraw = 120;                // 描く直前の関数 (obj, ctx)（sub_58F6B8 0x58F864）
const u32 kObjDrawNode = 264;               // 描く体（sub_58F6B8 / sub_58FA78 の obj+0x108）
const u32 kNodeMeshBegin = 0x170, kNodeMeshEnd = 0x174;
const u32 kResMeshCount = 180, kResMeshes = 184, kResShapes = 200;
const u32 kMeshShape = 24, kMeshMaterial = 28, kMeshPrimitive = 40, kMeshTail = 104, kMeshTailBytes = 108;
const u32 kCtxInner = 8, kInnerMaterial = 32;
const u32 kTevStage5 = 5;
const u32 kTilesX = 112, kTilesY = 96;

typedef void (*DrawFn)(u32 obj, u32 ctx, const float *view);
typedef void (*PreDrawFn)(u32 obj, u32 ctx);
typedef void (*ResetCacheFn)(u32 mask);
typedef void (*ModelViewFn)(u32 obj, const float *view);
typedef void (*BindMaterialFn)(u32 inner, u32 zero);
typedef void (*BindShapeFn)(u32 inner, u32 mesh);
typedef void (*DrawMeshFn)(u32 ctx, u32 node, u32 shape, u32 primitive);
typedef void (*CopyBytesFn)(void *dst, const void *src, u32 bytes);
typedef void (*TevStageInitFn)(u8 *stage, u32 index);
typedef u32 *(*TevStageEmitFn)(const u8 *stage, u32 *out);
typedef void (*GpuPropertyFn)(u32 id, u32 *out);
typedef u32 (*GpuAdvanceFn)(u32 bytes);

const ResetCacheFn   ResetDrawCache = reinterpret_cast<ResetCacheFn>(0x004EF898);
const ModelViewFn    ModelView      = reinterpret_cast<ModelViewFn>(0x0058F984);      // 体 +444 = 視点 × 体の行列
const BindMaterialFn BindMaterial   = reinterpret_cast<BindMaterialFn>(0x00494B4C);   // gfx_RenderContext_BindMaterial
const BindShapeFn    BindShape      = reinterpret_cast<BindShapeFn>(0x004957C4);
const DrawMeshFn     DrawMesh       = reinterpret_cast<DrawMeshFn>(0x0048D51C);
const CopyBytesFn    CopyBytes      = reinterpret_cast<CopyBytesFn>(0x0012EC54);      // Mem_CopyBytes
const TevStageInitFn TevStageInit   = reinterpret_cast<TevStageInitFn>(0x0034A178);
const TevStageEmitFn TevStageEmit   = reinterpret_cast<TevStageEmitFn>(0x0072721C);
const GpuPropertyFn  GpuProperty    = reinterpret_cast<GpuPropertyFn>(0x00127EDC);    // 520 = g_GpuCmdPtr
const GpuAdvanceFn   GpuAdvance     = reinterpret_cast<GpuAdvanceFn>(0x001280B4);

u8 s_tintMap[kTilesX * kTilesY];            // マスごとのハイライトの種類（0 = 無し。描画スレッドが Frame で書き、同じスレッドの描画で読む）
bool s_tintAny;
bool s_tableHooked;
u32 s_chainPreDraw;                         // 包んでいる間だけ: 物体が元から持っていた描く直前の関数

u8 TintKind(u32 obj) {
    const s32 x = (s32)R32(obj + kObjX), y = (s32)R32(obj + kObjY);
    if (!s_tintAny || x < 0 || y < 0 || x >= (s32)kTilesX || y >= (s32)kTilesY)
        return kHighlightNone;
    const u8 k = s_tintMap[(u32)y * kTilesX + (u32)x];
    return k <= kHighlightKinds ? k : kHighlightNone;
}

// 段 5 = 定数色（赤）と前段の出力を定数アルファの割合で混ぜる（interpolate）。α は前段のまま。
//   組み立てと書き出しはゲームのフェード sub_58F0D0 と同じ関数（sub_34A178 / sub_72721C）。
void EmitTint(void) {
    alignas(4) u8 st[24];
    TevStageInit(st, kTevStage5);
    st[0] = 4;                              // RGB: interpolate = 入力0 × 入力2 + 入力1 × (1 − 入力2)
    st[1] = 0;                              // 演算子: 色 / 色 / アルファ
    st[2] = 0;
    st[3] = 2;
    st[4] = 14;                             // 入力: 定数 / 前段 / 定数
    st[5] = 15;
    st[6] = 14;
    const u8 kind = s_curKind >= 1 && s_curKind <= kHighlightKinds ? s_curKind : kHighlightRed;
    const u32 color = kHighlightColor[kind - 1];
    st[18] = (u8)(color & 0xFFu);
    st[19] = (u8)((color >> 8) & 0xFFu);
    st[20] = (u8)((color >> 16) & 0xFFu);
    st[21] = s_hlStrength[kind - 1];
    u32 p = 0;
    GpuProperty(520u, &p);
    const u32 end = reinterpret_cast<u32>(TevStageEmit(st, reinterpret_cast<u32 *>(p)));
    GpuAdvance((end - p) & ~3u);
}

// 描いたあと: 材質番号・形状のキャッシュと描画のキャッシュを捨てる（sub_58F6B8 の 0x800 のあとと同じ 0x58F8A8〜0x58F8C4）
void ForgetDrawState(void) {
    W32(kLastShapeKey, 0xFFFFFFFFu);
    W32(kLastMatGroup, 0xFFFFFFFFu);
    W32(kLastShape, 0u);
    ResetDrawCache(0x1FFFu);
}

void TintPreDraw(u32 obj, u32 ctx) {
    const u32 chain = s_chainPreDraw;
    if (chain != 0u)
        reinterpret_cast<PreDrawFn>(chain)(obj, ctx);
    EmitTint();
}

void DrawTinted0(u32 obj, u32 ctx, const float *view) {
    const u8 kind = TintKind(obj);
    if (kind == kHighlightNone) {
        reinterpret_cast<DrawFn>(kDrawKind0)(obj, ctx, view);
        return;
    }
    s_curKind = kind;
    const u32 old = R32(obj + kObjPreDraw);
    s_chainPreDraw = old;
    W32(obj + kObjPreDraw, reinterpret_cast<u32>(&TintPreDraw));
    reinterpret_cast<DrawFn>(kDrawKind0)(obj, ctx, view);
    W32(obj + kObjPreDraw, old);
    s_chainPreDraw = 0u;
    ForgetDrawState();
}

// sub_48D26C と同じ手順で 1 メッシュ。材質を結んだ直後に赤を積む
void DrawMeshTinted(u32 ctx, u32 mesh, u32 node) {
    const u32 res = R32(node + kNodeRes);
    const u32 shapesRel = R32(res + kResShapes);
    const u32 shapes = shapesRel != 0u ? res + kResShapes + shapesRel : 0u;
    const u32 entry = shapes + 4u * R32(mesh + kMeshShape);
    const u32 shape = R32(entry) != 0u ? entry + R32(entry) : 0u;
    const u32 inner = R32(ctx + kCtxInner);
    W32(inner + kInnerMaterial, R32(R32(node + kModelMaterials) + 4u * R32(mesh + kMeshMaterial)));
    BindMaterial(inner, 0u);
    EmitTint();
    BindShape(inner, mesh);
    DrawMesh(ctx, node, shape, R32(mesh + kMeshPrimitive));
    const u32 bytes = R32(mesh + kMeshTailBytes);
    CopyBytes(reinterpret_cast<void *>(R32(kGpuCmdPtr)), reinterpret_cast<const void *>(R32(mesh + kMeshTail)), bytes);
    W32(kGpuCmdPtr, R32(kGpuCmdPtr) + (bytes & ~3u));
}

// sub_58FA78 と同じ手順（メッシュの描き方だけ DrawMeshTinted）
void DrawTinted1(u32 obj, u32 ctx, const float *view) {
    const u8 kind = TintKind(obj);
    if (kind == kHighlightNone) {
        reinterpret_cast<DrawFn>(kDrawKind1)(obj, ctx, view);
        return;
    }
    s_curKind = kind;
    const u32 node = R32(obj + kObjDrawNode);
    ModelView(obj, view);
    ForgetDrawState();
    u32 it = R32(node + kNodeMeshBegin), end = R32(node + kNodeMeshEnd);
    if (it == end) {
        const u32 res = R32(node + kNodeRes);
        const u32 rel = R32(res + kResMeshes);
        it = rel != 0u ? res + kResMeshes + rel : 0u;
        end = it + 4u * R32(res + kResMeshCount);
    }
    for (; it != end; it += 4u) {
        const u32 r = R32(it);
        if (r != 0u)
            DrawMeshTinted(ctx, it + r, node);
    }
    ForgetDrawState();
}

// 表の差し替え・戻し。どちらも描画スレッド（sub_59A918 と同じスレッド）から。ほかの誰かが書き換えていたら触らない
void HookDrawTable(void) {
    if (s_tableHooked)
        return;
    if (R32(kDrawTable) != kDrawKind0 || R32(kDrawTable + 4) != 0u || R32(kDrawTable + 8) != kDrawKind1 || R32(kDrawTable + 12) != 0u)
        return;
    W32(kDrawTable, reinterpret_cast<u32>(&DrawTinted0));
    W32(kDrawTable + 8, reinterpret_cast<u32>(&DrawTinted1));
    s_tableHooked = true;
}

void UnhookDrawTable(void) {
    s_tintAny = false;
    if (!s_tableHooked)
        return;
    if (R32(kDrawTable) == reinterpret_cast<u32>(&DrawTinted0))
        W32(kDrawTable, kDrawKind0);
    if (R32(kDrawTable + 8) == reinterpret_cast<u32>(&DrawTinted1))
        W32(kDrawTable + 8, kDrawKind1);
    s_tableHooked = false;
}

// ---- 自前の体 ----------------------------------------------------------------------------------
struct Slot {
    alignas(8) u8 holder[kNodeHolderBytes];
    void *res;
    u32 idle;                               // 描いていないフレーム
    u8 style;
    bool made;                              // ホルダを作った
    bool live;                              // 体がある（壊してよい形）
    bool broken;                            // 壊すと資源に書く形。壊さずにヒープごと返す
    bool tinted;                            // 色を混ぜられた（汎用の書き出しへ替えた）
    bool used;
};
const u32 kMaxSlots = 32;                   // 同時に描くモデルの種類
Slot s_slots[kMaxSlots];
struct LayerUndo { u32 res, old; };
const u32 kMaxLayerUndo = 64;
LayerUndo s_layers[kMaxLayerUndo];
u32 s_layerCount;
const u32 kCreatesPerFrame = 3;

void SetLayer(u32 res) {
    if (!Readable(res + kResLayer, 4))
        return;
    for (u32 i = 0; i < s_layerCount; ++i)
        if (s_layers[i].res == res)
            return;
    if (s_layerCount >= kMaxLayerUndo)
        return;
    s_layers[s_layerCount++] = { res, R32(res + kResLayer) };
    W32(res + kResLayer, (R32(res + kResLayer) & ~0xFFu) | 1u);   // 層 1 = 不透明の後（BuildingHighlight と同じ）
}

void RestoreLayers(bool write) {
    if (write)
        for (u32 i = s_layerCount; i-- > 0;)
            W32(s_layers[i].res + kResLayer, s_layers[i].old);
    s_layerCount = 0;
}

// 材質 1 つ: 体ごとの色の部分に TEV の写し（参照表は安全な物）を結び、色を混ぜる。戻り値: この材質を汎用で書き出してよい
bool PrepareMaterial(u32 m, u8 style) {
    const u32 res = R32(m + kMatResource);
    const u32 colour = R32(m + kMatColour);
    const u32 tevres = R32(m + kMatTev);
    const u32 frag = R32(m + kMatFrag);
    const u32 light = R32(m + kMatLighting);
    if (!Readable(res, kResFragKey + 4) || !Readable(colour, kResFragKey + 4) || !Readable(tevres, kResTevKey + 4)
        || !Readable(frag, kResFragKey + 4) || !Readable(light, kResTevKey + 4))
        return false;
    if (colour == res)
        return false;                       // 色の部分が体ごとでない（資源を書き換えることになる）
    if (light != res && light != colour)
        return false;                       // 知らない形（ライティングの部分が別の写し）: 汎用へ替えない
    u32 tev;
    if (colour != tevres) {                 // 共有の TEV: 写しを自前のヒープに置き、色の部分の未使用 +648 から指す
        const u32 srcRel = R32(tevres + kResTevRel);
        if (R32(colour + kResTevRel) != 0 || srcRel == 0 || !HeapRoom(kTevBytes))
            return false;
        const u32 src = tevres + kResTevRel + srcRel;
        const u32 buf = reinterpret_cast<u32>(Alloc(kTevBytes));
        if (buf == 0)
            return false;
        if (!Readable(src, kTevBytes)) {
            FreeOwn(buf);
            return false;
        }
        std::memcpy(reinterpret_cast<void *>(buf), reinterpret_cast<const void *>(src), kTevBytes);
        LinkLut(buf, src);
        W32(colour + kResTevRel, buf - (colour + kResTevRel));
        W32(m + kMatTev, colour);
        W32(m + kMatLighting, colour);      // 参照表も色の部分（資源の本体の写し）の +648 の先 = 安全な表から読ませる
        tev = buf;
    } else {
        return false;                       // ゲームが写した TEV（mask 0x780）: 後片付けがその表を解放するので触らない。0x834 では起きない
    }
    const bool blended = BuildingHighlight::FragBlendsAlready(frag);
    const int plan = BuildingHighlight::PlanTev(reinterpret_cast<u8 *>(tev), colour);
    const u32 color = style == kRed ? kRedColor : kWhite;
    const u8 strength = style == kRed ? kRedTint : kWhiteTint;
    if (plan != 0) {
        u32 c5;
        if (style == kPreview && blended && (plan == 1 || plan == 2)) {    // もともと半透明: TEV でアルファに掛ける
            BuildingHighlight::StageScalesAlpha(reinterpret_cast<u8 *>(tev), true);
            c5 = ((u32)kPreviewAlpha << 24) | (BuildingHighlight::TintConstant(color, strength, true) & 0x00FFFFFFu);
        } else {
            c5 = BuildingHighlight::TintConstant(color, strength, plan == 3);
        }
        W32(colour + kResConst5, c5);
    }
    if (style == kPreview && !blended && R32(frag + kFragCheckA) == 0x00E40100u && R32(frag + kFragCheckB) == 0x803F0100u) {
        W32(frag + kFragFbRead, 0);
        W32(frag + kFragBlend, kBlendConstAlpha);
        W32(frag + kFragBlendColor, (u32)kPreviewAlpha << 24);
        W32(colour + kResFragKey, 0);
        if (frag != colour)
            W32(frag + kResFragKey, 0);
        SetLayer(res);
    }
    return true;
}

// 体の材質を全部用意できたときだけ汎用の書き出しへ替える（1 つでもだめなら替えない = 元の見た目のまま）
bool PrepareNode(u32 node, u8 style) {
    const u32 arr = R32(node + kModelMaterials);
    if (!Readable(arr, 4 * kMaxMatsPerModel))
        return false;
    u32 count = 0;
    for (u32 k = 0; k < kMaxMatsPerModel; ++k) {
        const u32 m = R32(arr + 4 * k);
        if (!BuildingHighlight::LooksLikeMaterial(m) || !Readable(m, kMatFrag + 4))
            break;
        if (!PrepareMaterial(m, style))
            return false;
        ++count;
    }
    return count != 0 && SwapToGeneric(node);
}

bool BuildSlot(Slot &s, void *res, u8 style) {
    if (!EnsureHeap() || !HeapRoom(0x3000))
        return false;
    std::memset(s.holder, 0, sizeof(s.holder));
    NodeHolderCtor(s.holder);
    s.made = true;
    s.res = res;
    s.style = style;
    s.tinted = false;
    const int created = ModelInstanceCreate(s.holder, res, s_alloc, s_alloc, 0x834u, 1u, 3u);
    const u32 node = Word(s.holder, 4);
    if (created != 1 || !HeapPtr(node) || !HeapPtr(R32(node + kNodeMaterialActivator))) {
        s.broken = HeapPtr(node);           // 途中まで作った体は壊さない（ヒープごと返す）
        return false;
    }
    if (R32(node + kNodeMeshArrayBegin) == R32(node + kNodeMeshArrayEnd)) {
        s.broken = true;                    // 壊すと資源側へ書く形（IDA-opus-5-F023）
        return false;
    }
    s.live = true;
    s.tinted = PrepareNode(node, style);
    return true;
}

// 写しの材質の +648 に繋いだ自前の TEV を外して返す（壊す前に必ず）。
//   0x834 の写しは TEV を写さないので、gfx_ResMaterial_CopyForInstance が +648 を 0 にしている（0x4ADEEC）→ 0 でない = 自前の TEV。
void DetachOwnTev(u32 node) {
    const u32 arr = R32(node + kModelMaterials);
    if (!Readable(arr, 4 * kMaxMatsPerModel))
        return;
    for (u32 k = 0; k < kMaxMatsPerModel; ++k) {
        const u32 m = R32(arr + 4 * k);
        if (!BuildingHighlight::LooksLikeMaterial(m) || !Readable(m, kMatFrag + 4))
            break;
        const u32 colour = R32(m + kMatColour);
        if (colour == R32(m + kMatResource) || !Readable(colour, kResTevRel + 4))
            continue;
        const u32 rel = R32(colour + kResTevRel);
        if (rel == 0)
            continue;
        W32(colour + kResTevRel, 0);
        FreeOwn(colour + kResTevRel + rel);
    }
}

void DestroySlot(Slot &s) {
    if (s.live && Word(s.holder, 4) != 0u) {
        DetachOwnTev(Word(s.holder, 4));
        ModelInstanceDestroy(s.holder);
    }
    s.live = s.made = s.tinted = false;
    s.res = nullptr;
}

// マス (x, y) に少し浮かせて置く行列（体へは描画ノードが書く）
void MatrixAtTile(s32 x, s32 y, float *matrix) {
    float in[3] = { (float)(32 * x + 16), 0.0f, (float)(32 * y + 16) };
    in[1] = GroundHeight(in, 0) + kPreviewLift;
    float out[3] = { in[0], in[1], in[2] };
    u16 angle = 0;
    if ((kRoomFlags[*kRoomId] & kRoomFlagCurved) != 0u)
        angle = FieldPositionToRenderSpace(out, in);
    for (u32 i = 0; i < 12; ++i)
        matrix[i] = 0.0f;
    matrix[0] = matrix[5] = matrix[10] = 1.0f;
    matrix[3] = out[0];
    matrix[7] = out[1];
    matrix[11] = out[2];
    if (angle != 0)
        AppendRotationX16(matrix, angle);
}

// 要求（このフレームに描く物）
struct Want { void *res; s16 x, y; u8 style; u8 slot; };
const u32 kMaxWants = kMaxClones;
const u8 kNoSlot = 0xFF;
Want s_wants[kMaxWants];
u32 s_wantCount;
float s_matrices[kMaxWants][12];            // 束ごとに続けて並べる（描画ノードがこのフレームの描画で読む）
InstancedDraw::Batch s_batches[kMaxSlots];

void StepSlots(void) {
    for (u32 i = 0; i < kMaxSlots; ++i)
        s_slots[i].used = false;
    // 1. 要求ごとに体を決める（同じ資源なら同じ体。無ければ作る）
    u32 creates = 0;
    for (u32 c = 0; c < s_wantCount; ++c) {
        Want &w = s_wants[c];
        w.slot = kNoSlot;
        Slot *slot = nullptr;
        for (u32 i = 0; i < kMaxSlots && slot == nullptr; ++i)
            if (s_slots[i].live && s_slots[i].res == w.res && s_slots[i].style == w.style)
                slot = &s_slots[i];
        if (slot == nullptr) {
            if (creates >= kCreatesPerFrame)
                continue;
            for (u32 i = 0; i < kMaxSlots && slot == nullptr; ++i)
                if (!s_slots[i].made)
                    slot = &s_slots[i];
            for (u32 i = 0; i < kMaxSlots && slot == nullptr; ++i)  // 無ければ しばらく描いていない体を作り直す
                if (s_slots[i].live && !s_slots[i].used && s_slots[i].idle >= kQuietFrames) {
                    DestroySlot(s_slots[i]);
                    slot = &s_slots[i];
                }
            if (slot == nullptr)
                continue;
            ++creates;
            if (!BuildSlot(*slot, w.res, w.style))
                continue;
        }
        slot->used = true;
        slot->idle = 0;
        w.slot = (u8)(slot - s_slots);
    }
    // 2. 体ごとに置き場の行列を続けて並べ、束にして描画ノードへ渡す
    u32 n = 0, batches = 0;
    for (u32 i = 0; i < kMaxSlots; ++i) {
        if (!s_slots[i].used)
            continue;
        const u32 start = n;
        for (u32 c = 0; c < s_wantCount && n < kMaxWants; ++c)
            if (s_wants[c].slot == i)
                MatrixAtTile(s_wants[c].x, s_wants[c].y, s_matrices[n++]);
        if (n == start)
            continue;
        s_batches[batches++] = { s_slots[i].holder, &s_matrices[start], n - start };
    }
    InstancedDraw::Submit(s_drawer, s_batches, batches);
    for (u32 i = 0; i < kMaxSlots; ++i)
        if (!s_slots[i].used && s_slots[i].idle < 0xFFFFu)
            ++s_slots[i].idle;
}

void ForgetAll(void) {
    for (u32 i = 0; i < kMaxSlots; ++i) {
        s_slots[i].made = s_slots[i].live = s_slots[i].broken = s_slots[i].used = s_slots[i].tinted = false;
        s_slots[i].res = nullptr;
        s_slots[i].idle = 0;
    }
    s_layerCount = 0;
    s_wantCount = 0;
}

bool SceneSame(void) {
    const u32 proc = R32(kProcPtr);
    return HeapPtr(proc) && proc == s_heapProc && R32(proc + kProcFgResource) == s_heapRes
        && *reinterpret_cast<const volatile s8 *>(kOutdoorFlag) != 0;
}

u32 s_quiet;

void TearDown(bool sameScene) {
    if (sameScene)
        InstancedDraw::Destroy(s_drawer);       // 描画ノードが先（コールバックが体を読む）
    else
        std::memset(&s_drawer, 0, sizeof(s_drawer));   // 場面が変わった: 何も呼ばずにヒープごと捨てる
    if (sameScene) {
        for (u32 i = 0; i < kMaxSlots; ++i)
            if (s_slots[i].made && !s_slots[i].broken)
                DestroySlot(s_slots[i]);
        RestoreLayers(true);
    }
    ForgetAll();
    DropHeap();
    s_quiet = 0;
}

}  // namespace

void Frame(u8 (*highlight)(s32 x, s32 y), const Clone *clones, u32 count) {
    if (s_heapMade && !SceneSame()) {       // 場面が変わった: 何も書かずに捨てる
        s_tintAny = false;
        TearDown(false);
        return;
    }
    const u32 proc = R32(kProcPtr);
    if (!HeapPtr(proc) || *reinterpret_cast<const volatile s8 *>(kOutdoorFlag) == 0) {
        s_tintAny = false;
        return;
    }
    // 実体を辿って、赤くするマスと、複製の元の資源を拾う（実体には書かない）
    s_wantCount = 0;
    std::memset(s_tintMap, 0, sizeof(s_tintMap));
    bool anyTint = false;
    void *srcRes[kMaxClones];
    for (u32 c = 0; c < kMaxClones; ++c)
        srcRes[c] = nullptr;
    u32 obj = Readable(proc + kProcObjects, 4) ? R32(proc + kProcObjects) : 0;
    for (u32 n = 0; obj != 0 && n < kMaxWalk; ++n) {
        if (!Readable(obj, kObjNode + 4))
            break;
        const u32 next = R32(obj + kObjNext);
        const s32 x = (s32)R32(obj + kObjX), y = (s32)R32(obj + kObjY);
        const u32 node = R32(obj + kObjNode);
        if (x >= 0 && y >= 0 && x < 112 && y < 96 && Readable(node, kNodeScale + 12)) {
            const u32 res = R32(node + kNodeRes);
            if (HeapPtr(res)) {
                for (u32 c = 0; c < count && c < kMaxClones; ++c)
                    if (srcRes[c] == nullptr && clones[c].srcX == x && clones[c].srcY == y)
                        srcRes[c] = reinterpret_cast<void *>(res);
                const u8 kind = highlight != nullptr ? highlight(x, y) : kHighlightNone;
                if (kind != kHighlightNone && kind <= kHighlightKinds) {
                    s_tintMap[(u32)y * kTilesX + (u32)x] = kind;
                    anyTint = true;
                }
            }
        }
        obj = next;
    }
    for (u32 c = 0; c < count && c < kMaxClones && s_wantCount < kMaxWants; ++c) {
        void *res = srcRes[c] != nullptr ? srcRes[c] : ResolveModel(clones[c].item, clones[c].x, clones[c].y);
        if (res != nullptr)
            s_wants[s_wantCount++] = { res, (s16)clones[c].x, (s16)clones[c].y, kPreview };
    }
    if (anyTint)
        HookDrawTable();
    s_tintAny = anyTint && s_tableHooked;
    StepSlots();
    // 何も描かなくなったらしばらく待ってヒープを返す
    if (s_wantCount != 0) {
        s_quiet = 0;
    } else if (s_heapMade && ++s_quiet >= kQuietFrames) {
        TearDown(true);
    }
}

bool Release(void) {
    UnhookDrawTable();
    if (!s_heapMade) {
        ForgetAll();
        return true;
    }
    if (!SceneSame()) {
        TearDown(false);
        return true;
    }
    if (++s_quiet < kQuietFrames)           // 描くのをやめてから数フレーム待って壊す
        return false;
    TearDown(true);
    return true;
}

void Abandon(void) {
    UnhookDrawTable();
    if (s_heapMade)
        TearDown(SceneSame());
    else
        ForgetAll();
}

void SetHighlightStrength(u8 kind, u8 strength) {
    if (kind >= 1 && kind <= kHighlightKinds)
        s_hlStrength[kind - 1] = strength;
}

u8 HighlightStrength(u8 kind) {
    return kind >= 1 && kind <= kHighlightKinds ? s_hlStrength[kind - 1] : 0u;
}

}  // namespace MapEditor3D
