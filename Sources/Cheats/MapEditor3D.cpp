#include "MapEditor3D.hpp"

#include "BuildingHighlight.hpp"
#include "GridCursorGameApi.hpp"

#include <cstring>

// 設計: acnl_disassemble docs/topics/map_editor_3d.md（IDA-opus-5.5-D004）、根拠 IDA-opus-5.5-F055 / F056。
// 色の混ぜ方は BuildingHighlight（F010〜F013）、自前の体と半透明は BuildingPreview（F002 / F035）と同じ。
//
// ★村の実体（fgobj）の材質は書き換えない（F056）。材質の物体は同じモデルの実体で共有されていて（利用者報告: 触っていない
//   同じモデルも赤くなった）、ゲームの簡易版の書き出し（0x4A17B8）が季節資源の未解決の参照表を読んで落ちた（0x4B0C54、実機）。
//   赤も複製も、実体の体 +8 の資源から作った自前の体で描く（赤は実体の行列を写して少し大きく重ねる）。

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
const u32 kNodeMatrix = 0x4C;               // 行列 3x4（SetMatrix3x4 が書く所。GridCursorGameApi の kNodeLocalMatrix）
const u32 kNodeScale = 0x7C;                // 拡大率 x, y, z

typedef void *(*ItemPtrFn)(const void *item);
typedef int (*ItemTestFn)(const void *item);
typedef const char *(*LeafNameFn)(const u8 *type, int outdoor);
typedef int (*SandFn)(s32 x, s32 y, u32 zero);
typedef float (*GroundHeightFn)(const float *pos, u32 zero);
typedef void *(*AllocFn)(void *allocator, u32 size, s32 align);
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
const float kRedScale = 1.04f;              // 実体より少し大きく重ねる（同じ面で奥行きが競らないように）
const float kRedLift = 0.5f;
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
const u32 kMaxSlots = 64;
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
        if (buf == 0 || !Readable(src, kTevBytes))
            return false;
        std::memcpy(reinterpret_cast<void *>(buf), reinterpret_cast<const void *>(src), kTevBytes);
        LinkLut(buf, src);
        W32(colour + kResTevRel, buf - (colour + kResTevRel));
        W32(m + kMatTev, colour);
        W32(m + kMatLighting, colour);      // 参照表も色の部分（資源の本体の写し）の +648 の先 = 安全な表から読ませる
        tev = buf;
    } else {                                // 体ごとの TEV: その場で
        const u32 rel = R32(colour + kResTevRel);
        if (rel == 0)
            return false;
        tev = colour + kResTevRel + rel;
        if (!LutSafe(tev))
            W32(tev + kTevLutRel, reinterpret_cast<u32>(s_zeroLut) - (tev + kTevLutRel));
        W32(colour + kResTevKey, 0);
        W32(m + kMatLighting, colour);
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

void DestroySlot(Slot &s) {
    if (s.live && Word(s.holder, 4) != 0u)
        ModelInstanceDestroy(s.holder);
    s.live = s.made = s.tinted = false;
    s.res = nullptr;
}

void PoseAtTile(Slot &s, s32 x, s32 y) {
    float in[3] = { (float)(32 * x + 16), 0.0f, (float)(32 * y + 16) };
    in[1] = GroundHeight(in, 0) + kPreviewLift;
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
    const float one[3] = { 1.0f, 1.0f, 1.0f };
    SetMatrix3x4(s.holder, matrix);
    SetScale(s.holder, one);                // 体を使い回すので赤のときの拡大率を戻す
    UpdateWorldAndSkeleton(s.holder);
}

// 実体の体の行列と拡大率を写す（赤）。少し大きく、少し上へ（自分の上向きに）
void PoseLike(Slot &s, u32 srcNode) {
    float matrix[12];
    std::memcpy(matrix, reinterpret_cast<const void *>(srcNode + kNodeMatrix), sizeof(matrix));
    matrix[3] += matrix[1] * kRedLift;
    matrix[7] += matrix[5] * kRedLift;
    matrix[11] += matrix[9] * kRedLift;
    float scale[3];
    std::memcpy(scale, reinterpret_cast<const void *>(srcNode + kNodeScale), sizeof(scale));
    for (u32 i = 0; i < 3; ++i)
        scale[i] *= kRedScale;
    SetMatrix3x4(s.holder, matrix);
    SetScale(s.holder, scale);
    UpdateWorldAndSkeleton(s.holder);
}

// 要求（このフレームに描く物）
struct Want { void *res; u32 srcNode; s16 x, y; u8 style; };
const u32 kMaxWants = kMaxSlots;
Want s_wants[kMaxWants];
u32 s_wantCount;

void StepSlots(void) {
    for (u32 i = 0; i < kMaxSlots; ++i)
        s_slots[i].used = false;
    u32 creates = 0;
    for (u32 c = 0; c < s_wantCount; ++c) {
        const Want &w = s_wants[c];
        Slot *slot = nullptr;
        for (u32 i = 0; i < kMaxSlots && slot == nullptr; ++i)      // 同じ資源・同じ色の体を使い回す
            if (s_slots[i].live && !s_slots[i].used && s_slots[i].res == w.res && s_slots[i].style == w.style)
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
        if (w.style == kRed && !slot->tinted)
            continue;                       // 赤くできない体は重ねない（ただの大きな写しになるので）
        if (w.srcNode != 0)
            PoseLike(*slot, w.srcNode);
        else
            PoseAtTile(*slot, w.x, w.y);
        Submit(slot->holder, 0);
    }
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

void Frame(bool (*highlight)(s32 x, s32 y), const Clone *clones, u32 count) {
    if (s_heapMade && !SceneSame()) {       // 場面が変わった: 何も書かずに捨てる
        TearDown(false);
        return;
    }
    const u32 proc = R32(kProcPtr);
    if (!HeapPtr(proc) || *reinterpret_cast<const volatile s8 *>(kOutdoorFlag) == 0)
        return;
    // 実体を辿って、赤く重ねる物と、複製の元の資源を拾う（実体には書かない）
    s_wantCount = 0;
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
                if (highlight != nullptr && s_wantCount < kMaxWants && highlight(x, y))
                    s_wants[s_wantCount++] = { reinterpret_cast<void *>(res), node, (s16)x, (s16)y, kRed };
            }
        }
        obj = next;
    }
    for (u32 c = 0; c < count && c < kMaxClones && s_wantCount < kMaxWants; ++c) {
        void *res = srcRes[c] != nullptr ? srcRes[c] : ResolveModel(clones[c].item, clones[c].x, clones[c].y);
        if (res != nullptr)
            s_wants[s_wantCount++] = { res, 0, (s16)clones[c].x, (s16)clones[c].y, kPreview };
    }
    StepSlots();
    // 何も描かなくなったらしばらく待ってヒープを返す
    if (s_wantCount != 0) {
        s_quiet = 0;
    } else if (s_heapMade && ++s_quiet >= kQuietFrames) {
        TearDown(true);
    }
}

bool Release(void) {
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
    if (s_heapMade)
        TearDown(SceneSame());
    else
        ForgetAll();
}

}  // namespace MapEditor3D
