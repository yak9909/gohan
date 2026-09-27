#include "MapEditor3D.hpp"

#include "BuildingHighlight.hpp"
#include "GridCursorGameApi.hpp"

#include <cstring>

// 設計: acnl_disassemble docs/topics/map_editor_3d.md（IDA-opus-5.5-D004）、根拠 IDA-opus-5.5-F055。
// 色の混ぜ方は BuildingHighlight（F010〜F013）、自前の体と半透明は BuildingPreview（F002 / F035）と同じ。

namespace MapEditor3D {

using namespace GridCursor::Game;

namespace {

// ---- ゲーム側 --------------------------------------------------------------------------------
const u32 kProcPtr = 0x00948E70;            // u32: fgobj::Proc（村の実体と季節資源の持ち主。kSceneOwner と同じ番地）
const u32 kOutdoorFlag = 0x00948E5F;        // s8: 屋外
const u32 kProcFgResource = 0x34;           // 季節資源（FieldSeasonResource_FindModel 0x5992D4 = FindModel(proc + 0x34)）
const u32 kProcItemResource = 0x13C;        // 一般のアイテムのモデル（sub_58E978 の FindModel(proc + 0x13C)）
const u32 kProcFgTable = 0x414;             // fgobj の形の表（22 B。+5 = 名前。sub_58FB40 0x58FD44）
const u32 kProcObjects = 13732;             // 実体の連結の先頭（fgobj_DestroyObjectsAt 0x5A1A90 と同じ辿り方）
const u32 kObjNext = 8, kObjX = 40, kObjY = 44, kObjNode = 92;     // ノード = ホルダ（+88）の +4
const u32 kMaxWalk = 4096;

typedef void *(*ItemPtrFn)(const void *item);
typedef int (*ItemTestFn)(const void *item);
typedef const char *(*LeafNameFn)(const u8 *type, int outdoor);
typedef int (*SandFn)(s32 x, s32 y, u32 zero);
typedef float (*GroundHeightFn)(const float *pos, u32 zero);
typedef void (*CopyMaterialFn)(u32 *out, u32 material, void *allocator, u32 mask);
typedef void *(*AllocFn)(void *allocator, u32 size, s32 align);

const ItemPtrFn      ItemRecord     = reinterpret_cast<ItemPtrFn>(0x00535188);      // ItemParam_GetRecord
const ItemPtrFn      FieldObjDef    = reinterpret_cast<ItemPtrFn>(0x002FD064);      // Item_GetFieldObjDef（ID >= 0xFE は 0）
const ItemTestFn     ItemTestA      = reinterpret_cast<ItemTestFn>(0x0076BE04);     // sub_58E978 の特例（8 / 9）
const ItemTestFn     ItemTestB      = reinterpret_cast<ItemTestFn>(0x00313654);
const LeafNameFn     LeafName       = reinterpret_cast<LeafNameFn>(0x005350C0);
const SandFn         IsSand         = reinterpret_cast<SandFn>(0x00300A84);
const GroundHeightFn GroundHeight   = reinterpret_cast<GroundHeightFn>(0x006C69C0);
const CopyMaterialFn CopyMaterial   = reinterpret_cast<CopyMaterialFn>(0x004ADD44);  // (out, M, allocator, mask)

// 材質（BuildingHighlight / BuildingPreview と同じ欄。IDA-opus-5.5-F010）
const u32 kModelMaterials = 0x164;
const u32 kModelActivator = 0x1EC;
const u32 kMatResource = 8, kMatColour = 48, kMatTev = 72, kMatFrag = 80;
const u32 kResTevRel = 648, kResTevKey = 712, kResFragKey = 720, kResLayer = 32;
const u32 kResConst5 = 36 + 4 * 54;
const u32 kTevBytes = 244, kTevLutRel = 40;
const u32 kFragFbRead = 300, kFragCheckA = 320, kFragCheckB = 324, kFragBlend = 328, kFragBlendColor = 336;
const u32 kBlendConstAlpha = (12u << 16) | (13u << 20) | (1u << 24) | (13u << 28);
const u32 kGenericActivate = 0x0049C08C, kSimpleActivate = 0x004A17B8;
const u32 kCopyTev = 0x80;                  // 0x4ADD44: 0x780 のどれか = TEV も写す（参照表 +40 は 0 にされる → 付け直す）
const u32 kMaxMatsPerModel = 16;

// 見た目（利用者の指示 2026-09-27: 選んだ物は赤、移動の複製は薄い白・半透明・少し上）
const u32 kRed = 0x004040FFu;               // 0x00BBGGRR（BuildingHighlight::kRed と同じ）
const u8 kRedTint = 0xA0;
const u32 kWhite = 0x00FFFFFFu;
const u8 kWhiteTint = 0x60;
const u8 kCloneAlpha = 0xA0;
const float kCloneLift = 6.0f;              // 世界の長さ（1 マス = 32）

// ヒープ（親ヒープから。BuildingPreview と同じく親に必ず残す）
const u32 kHeapBytesBig = 0x30000, kHeapBytesSmall = 0x18000;
const u32 kParentReserve = 0x10000;
const u32 kHeapLow = 0x1000;                // これより空きが少なければ新しく作らない
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

// ---- 書き出し関数の付け替え（事前に作ったコマンドを流すだけの簡易版は材質の部分を読まない。F002）----------------
//   写しの vtable は元の vtable ごとに 1 つを gohan の .bss に置き、決して返さない（戻し損ねた書き出し関数が
//   あとで返したヒープを指すことが無いように）。汎用のままでも見た目は同じ（部分を毎回書き出すだけ）。
struct VtCopy { u32 orig; u32 words[6]; };
const u32 kMaxVt = 8;
VtCopy s_vt[kMaxVt];
u32 s_vtCount;
struct ActRef { u32 act, oldVt, refs; };
const u32 kMaxActs = 96;
ActRef s_acts[kMaxActs];
u32 s_actCount;

u32 GenericVtable(u32 vt) {
    for (u32 i = 0; i < s_vtCount; ++i)
        if (s_vt[i].orig == vt)
            return reinterpret_cast<u32>(&s_vt[i].words[1]);
    if (s_vtCount >= kMaxVt)
        return 0;
    VtCopy &c = s_vt[s_vtCount++];
    c.orig = vt;
    for (u32 j = 0; j < 6; ++j)
        c.words[j] = R32(vt - 4 + 4 * j);
    c.words[4] = kGenericActivate;          // slot 3（words[0] は TypeInfo）
    return reinterpret_cast<u32>(&c.words[1]);
}

// 汎用へ替える。替えた（数えた）なら真
bool SwapActivator(u32 node, bool track) {
    const u32 act = R32(node + kModelActivator);
    if (!Readable(act, 4))
        return false;
    for (u32 i = 0; i < s_actCount; ++i)
        if (s_acts[i].act == act) {
            ++s_acts[i].refs;
            return true;
        }
    const u32 vt = R32(act);
    if (vt < 0x00100000u || vt >= 0x01000000u || R32(vt + 12) != kSimpleActivate)
        return false;                       // 元から汎用（建物と同じ）か、知らない形
    if (track && s_actCount >= kMaxActs)
        return false;
    const u32 copy = GenericVtable(vt);
    if (copy == 0)
        return false;
    if (track)
        s_acts[s_actCount++] = { act, vt, 1 };
    W32(act, copy);
    return track;
}

void ReleaseActivator(u32 act, bool write) {
    for (u32 i = 0; i < s_actCount; ++i) {
        if (s_acts[i].act != act)
            continue;
        if (--s_acts[i].refs == 0) {
            if (write)
                W32(act, s_acts[i].oldVt);
            s_acts[i] = s_acts[--s_actCount];
        }
        return;
    }
}

// ---- 赤いハイライト（村の実体の材質を写して混ぜる）------------------------------------------------
// 戻すための元の値。kind 0 = 材質の部分を丸ごと写しへ（M+48 / M+72）、1 = 体ごとの色の部分に TEV の写しを結ぶ
struct HiMat { u32 m, old48, old72, colour, oldRel, oldC5; u8 kind; };
struct HiObj { u32 obj, node, act; u16 x, y; u8 matCount; bool seen, swapped; HiMat mats[8]; };
const u32 kMaxHiObjs = 128;
HiObj s_hi[kMaxHiObjs];
u32 s_hiCount;
const u32 kAppliesPerFrame = 12;

// 写しの TEV（+648 の先）の参照表 +40 を、元の資源の参照表へ付け直す（0x4ADD44 は 0x100 が無いと 0 にする）
void FixLut(u32 copyTev, u32 res) {
    const u32 srcRel = R32(res + kResTevRel);
    if (srcRel == 0)
        return;
    const u32 srcTev = res + kResTevRel + srcRel;
    const u32 lut = R32(srcTev + kTevLutRel);
    if (lut != 0)
        W32(copyTev + kTevLutRel, (srcTev + kTevLutRel + lut) - (copyTev + kTevLutRel));
}

// 材質 1 つを赤くする。★ゲームが持ち主の部分（体ごとの写し）を指している欄は差し替えない
//   （体を壊すときにゲームが自分の写しとして返そうとするかもしれないので）。資源と共有している欄だけ自前の物へ向ける。
bool RedMaterial(u32 m, HiMat &out) {
    const u32 res = R32(m + kMatResource);
    const u32 colour = R32(m + kMatColour);
    const u32 tevres = R32(m + kMatTev);
    if (!Readable(res, kResFragKey + 4) || !Readable(colour, kResFragKey + 4) || !Readable(tevres, kResTevKey + 4)
        || !HeapRoom(2048))
        return false;
    if (colour == res && tevres == res) {
        // 色も TEV も資源と共有（bufferOption 0 の一般のアイテム）: 部分を丸ごと写して（0x4ADD44）そちらへ向ける
        u32 cp[2] = { 0, 0 };
        CopyMaterial(cp, m, s_alloc, kCopyTev);
        const u32 copy = cp[0];
        if (copy == 0 || cp[1] != 0 || R32(copy + kResTevRel) == 0)
            return false;
        const u32 tev = copy + kResTevRel + R32(copy + kResTevRel);
        FixLut(tev, res);
        const int plan = BuildingHighlight::PlanTev(reinterpret_cast<u8 *>(tev), copy);
        if (plan == 0)
            return false;                   // 最終段を使えない材質は赤くしない
        W32(copy + kResConst5, BuildingHighlight::TintConstant(kRed, kRedTint, plan == 3));
        out = { m, colour, tevres, 0, 0, 0, 0 };
        W32(m + kMatColour, copy);
        W32(m + kMatTev, copy);
        return true;
    }
    if (colour != res && tevres == res && R32(colour + kResTevRel) == 0 && R32(res + kResTevRel) != 0) {
        // 色だけ体ごと（ゲームの写し）: TEV の写しを自前のヒープに置き、色の部分の未使用 +648 から指す（BuildingHighlight と同じ）
        const u32 src = res + kResTevRel + R32(res + kResTevRel);
        const u32 buf = reinterpret_cast<u32>(Alloc(kTevBytes));
        if (buf == 0 || !Readable(src, kTevBytes))
            return false;
        std::memcpy(reinterpret_cast<void *>(buf), reinterpret_cast<const void *>(src), kTevBytes);
        const u32 lut = R32(src + kTevLutRel);
        if (lut != 0)
            W32(buf + kTevLutRel, (src + kTevLutRel + lut) - (buf + kTevLutRel));
        const int plan = BuildingHighlight::PlanTev(reinterpret_cast<u8 *>(buf), colour);
        if (plan == 0)
            return false;
        out = { m, colour, tevres, colour, 0, R32(colour + kResConst5), 1 };
        W32(colour + kResTevRel, buf - (colour + kResTevRel));
        W32(colour + kResConst5, BuildingHighlight::TintConstant(kRed, kRedTint, plan == 3));
        W32(m + kMatTev, colour);
        return true;
    }
    return false;                           // TEV が体ごと: 触らない
}

bool ApplyRed(u32 obj, u32 node, s32 x, s32 y) {
    if (s_hiCount >= kMaxHiObjs || !Readable(node + kModelMaterials, 4))
        return false;
    const u32 arr = R32(node + kModelMaterials);
    if (!Readable(arr, 4 * kMaxMatsPerModel))
        return false;
    HiObj &h = s_hi[s_hiCount];
    h.obj = obj;
    h.node = node;
    h.act = R32(node + kModelActivator);
    h.x = (u16)x;
    h.y = (u16)y;
    h.matCount = 0;
    h.seen = true;
    h.swapped = false;
    for (u32 k = 0; k < kMaxMatsPerModel && h.matCount < 8; ++k) {
        const u32 m = R32(arr + 4 * k);
        if (!BuildingHighlight::LooksLikeMaterial(m) || !Readable(m, kMatFrag + 4))
            break;
        if (RedMaterial(m, h.mats[h.matCount]))
            ++h.matCount;
    }
    if (h.matCount == 0)
        return false;
    h.swapped = SwapActivator(node, true);
    ++s_hiCount;
    return true;
}

// 戻す。write = 偽なら実体はもう無いので書かない
void RestoreRed(u32 i, bool write) {
    HiObj &h = s_hi[i];
    if (write)
        for (u32 k = h.matCount; k-- > 0;) {
            const HiMat &hm = h.mats[k];
            if (hm.kind == 1) {
                W32(hm.colour + kResConst5, hm.oldC5);
                W32(hm.colour + kResTevRel, hm.oldRel);
            } else {
                W32(hm.m + kMatColour, hm.old48);
            }
            W32(hm.m + kMatTev, hm.old72);
        }
    if (h.swapped)
        ReleaseActivator(h.act, write);
    s_hi[i] = s_hi[--s_hiCount];
}

void StepHighlight(bool (*highlight)(s32, s32)) {
    for (u32 i = 0; i < s_hiCount; ++i)
        s_hi[i].seen = false;
    const u32 proc = R32(kProcPtr);
    u32 applies = 0;
    u32 obj = Readable(proc + kProcObjects, 4) ? R32(proc + kProcObjects) : 0;
    for (u32 n = 0; obj != 0 && n < kMaxWalk; ++n) {
        if (!Readable(obj, kObjNode + 4))
            break;
        const u32 next = R32(obj + kObjNext);
        const s32 x = (s32)R32(obj + kObjX), y = (s32)R32(obj + kObjY);
        const u32 node = R32(obj + kObjNode);
        s32 idx = -1;
        for (u32 i = 0; i < s_hiCount; ++i)
            if (s_hi[i].obj == obj) {
                idx = (s32)i;
                break;
            }
        if (idx >= 0 && (s_hi[idx].node != node || s_hi[idx].x != x || s_hi[idx].y != y)) {
            RestoreRed((u32)idx, false);    // 作り直された: 前の材質はもう無い
            idx = -1;
        }
        const bool want = highlight != nullptr && x >= 0 && y >= 0 && x < 112 && y < 96 && HeapPtr(node) && highlight(x, y);
        if (idx >= 0) {
            s_hi[idx].seen = true;
            if (!want)
                RestoreRed((u32)idx, true);
        } else if (want && applies < kAppliesPerFrame && EnsureHeap()) {
            ++applies;
            ApplyRed(obj, node, x, y);
        }
        obj = next;
    }
    for (u32 i = s_hiCount; i-- > 0;)
        if (!s_hi[i].seen)
            RestoreRed(i, false);           // 実体が消えた（場面の端を越えた・消した）
}

// ---- 移動の複製 --------------------------------------------------------------------------------
struct Slot {
    alignas(8) u8 holder[kNodeHolderBytes];
    void *res;
    u32 item;
    u32 idle;                               // 描いていないフレーム
    bool made;                              // ホルダを作った
    bool live;                              // 体がある（壊してよい形）
    bool broken;                            // 壊すと資源に書く形。壊さずにヒープごと返す
    bool used;
};
Slot s_slots[kMaxClones];
struct LayerUndo { u32 res, old; };
const u32 kMaxLayerUndo = 64;
LayerUndo s_layers[kMaxLayerUndo];
u32 s_layerCount;
const u32 kCreatesPerFrame = 3;

// アイテムのモデル（sub_58FB40 と同じ引き方）。無ければ nullptr
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
        if ((u32)(id - 0x2000u) < 0x172Bu) {                // sub_2FCB40 → sub_58E978
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

// 自前の体の材質: 白を混ぜて半透明に（BuildingHighlight::ApplyMaterial と BuildingPreview::MakeTranslucent を合わせた形）
void MakeGhost(u32 node) {
    const u32 arr = R32(node + kModelMaterials);
    if (!Readable(arr, 4 * kMaxMatsPerModel))
        return;
    for (u32 k = 0; k < kMaxMatsPerModel; ++k) {
        const u32 m = R32(arr + 4 * k);
        if (!BuildingHighlight::LooksLikeMaterial(m) || !Readable(m, kMatFrag + 4))
            break;
        const u32 res = R32(m + kMatResource);
        const u32 colour = R32(m + kMatColour);
        const u32 tevres = R32(m + kMatTev);
        const u32 frag = R32(m + kMatFrag);
        if (colour == res || !Readable(colour, kResFragKey + 4) || !Readable(tevres, kResTevKey + 4) || !Readable(frag, kResFragKey + 4))
            continue;                       // 色の部分が体ごとでない（資源を書き換えることになる）
        u8 *work;
        u32 buf = 0;
        if (colour != tevres) {             // 共有の TEV: 写しを自前のヒープに置き、色の部分の未使用 +648 から指す
            if (R32(colour + kResTevRel) != 0 || R32(tevres + kResTevRel) == 0 || !HeapRoom(kTevBytes))
                continue;
            const u32 src = tevres + kResTevRel + R32(tevres + kResTevRel);
            buf = reinterpret_cast<u32>(Alloc(kTevBytes));
            if (buf == 0 || !Readable(src, kTevBytes))
                continue;
            std::memcpy(reinterpret_cast<void *>(buf), reinterpret_cast<const void *>(src), kTevBytes);
            const u32 lut = R32(src + kTevLutRel);
            if (lut != 0)
                W32(buf + kTevLutRel, (src + kTevLutRel + lut) - (buf + kTevLutRel));
            work = reinterpret_cast<u8 *>(buf);
        } else {
            const u32 rel = R32(colour + kResTevRel);
            if (rel == 0)
                continue;
            work = reinterpret_cast<u8 *>(colour + kResTevRel + rel);
        }
        const bool blended = BuildingHighlight::FragBlendsAlready(frag);
        const int plan = BuildingHighlight::PlanTev(work, colour);
        if (plan == 0)
            continue;
        u32 c5;
        if (blended && (plan == 1 || plan == 2)) {      // もともと半透明: TEV でアルファに掛ける
            BuildingHighlight::StageScalesAlpha(work, true);
            c5 = ((u32)kCloneAlpha << 24) | (BuildingHighlight::TintConstant(kWhite, kWhiteTint, true) & 0x00FFFFFFu);
        } else {
            c5 = BuildingHighlight::TintConstant(kWhite, kWhiteTint, plan == 3);
        }
        if (buf != 0) {
            W32(colour + kResTevRel, buf - (colour + kResTevRel));
            W32(m + kMatTev, colour);
        } else {
            W32(colour + kResTevKey, 0);
        }
        W32(colour + kResConst5, c5);
        if (!blended && R32(frag + kFragCheckA) == 0x00E40100u && R32(frag + kFragCheckB) == 0x803F0100u) {
            W32(frag + kFragFbRead, 0);
            W32(frag + kFragBlend, kBlendConstAlpha);
            W32(frag + kFragBlendColor, (u32)kCloneAlpha << 24);
            W32(colour + kResFragKey, 0);
            if (frag != colour)
                W32(frag + kResFragKey, 0);
            SetLayer(res);
        }
    }
}

bool BuildSlot(Slot &s, void *res) {
    if (!EnsureHeap() || !HeapRoom(0x2000))
        return false;
    std::memset(s.holder, 0, sizeof(s.holder));
    NodeHolderCtor(s.holder);
    s.made = true;
    s.res = res;
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
    MakeGhost(node);
    SwapActivator(node, false);
    return true;
}

void DestroySlot(Slot &s) {
    if (s.live && Word(s.holder, 4) != 0u)
        ModelInstanceDestroy(s.holder);
    s.live = false;
    s.made = false;
    s.res = nullptr;
    s.item = 0;
}

void Pose(Slot &s, s32 x, s32 y) {
    float in[3] = { (float)(32 * x + 16), 0.0f, (float)(32 * y + 16) };
    in[1] = GroundHeight(in, 0) + kCloneLift;
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
    SetMatrix3x4(s.holder, matrix);
    UpdateWorldAndSkeleton(s.holder);
}

void StepClones(const Clone *clones, u32 count) {
    for (u32 i = 0; i < kMaxClones; ++i)
        s_slots[i].used = false;
    u32 creates = 0;
    for (u32 c = 0; c < count && c < kMaxClones; ++c) {
        const Clone &w = clones[c];
        Slot *slot = nullptr;
        for (u32 i = 0; i < kMaxClones && slot == nullptr; ++i)     // 同じアイテムの体
            if (s_slots[i].live && !s_slots[i].used && s_slots[i].item == w.item)
                slot = &s_slots[i];
        void *res = nullptr;
        if (slot == nullptr) {
            res = ResolveModel(w.item, w.x, w.y);
            if (res == nullptr)
                continue;
            for (u32 i = 0; i < kMaxClones && slot == nullptr; ++i)  // 同じモデルの体（使い回す）
                if (s_slots[i].live && !s_slots[i].used && s_slots[i].res == res)
                    slot = &s_slots[i];
        }
        if (slot == nullptr) {
            if (creates >= kCreatesPerFrame)
                continue;
            for (u32 i = 0; i < kMaxClones && slot == nullptr; ++i)  // 空き。無ければ しばらく描いていない体を作り直す
                if (!s_slots[i].made)
                    slot = &s_slots[i];
            for (u32 i = 0; i < kMaxClones && slot == nullptr; ++i)
                if (s_slots[i].live && !s_slots[i].used && s_slots[i].idle >= kQuietFrames) {
                    DestroySlot(s_slots[i]);
                    slot = &s_slots[i];
                }
            if (slot == nullptr)
                continue;
            ++creates;
            if (!BuildSlot(*slot, res))
                continue;
        }
        slot->used = true;
        slot->item = w.item;
        slot->idle = 0;
        Pose(*slot, w.x, w.y);
        Submit(slot->holder, 0);
    }
    for (u32 i = 0; i < kMaxClones; ++i)
        if (!s_slots[i].used && s_slots[i].idle < 0xFFFFu)
            ++s_slots[i].idle;
}

bool AnyClone(void) {
    for (u32 i = 0; i < kMaxClones; ++i)
        if (s_slots[i].made)
            return true;
    return false;
}

void ForgetAll(void) {
    for (u32 i = 0; i < kMaxClones; ++i) {
        s_slots[i].made = s_slots[i].live = s_slots[i].broken = s_slots[i].used = false;
        s_slots[i].res = nullptr;
        s_slots[i].item = 0;
        s_slots[i].idle = 0;
    }
    s_hiCount = 0;
    s_actCount = 0;
    s_layerCount = 0;
}

bool SceneSame(void) {
    const u32 proc = R32(kProcPtr);
    return HeapPtr(proc) && proc == s_heapProc && R32(proc + kProcFgResource) == s_heapRes
        && *reinterpret_cast<const volatile s8 *>(kOutdoorFlag) != 0;
}

u32 s_quiet;

// 何も要らなくなったら（赤も複製も無い）しばらく待ってヒープを返す（写しの溜まりを捨てる）
void Reclaim(u32 count) {
    const bool busy = s_hiCount != 0 || count != 0;
    if (busy) {
        s_quiet = 0;
        return;
    }
    if (!s_heapMade || ++s_quiet < kQuietFrames)
        return;
    for (u32 i = 0; i < kMaxClones; ++i)
        if (s_slots[i].made && !s_slots[i].broken)
            DestroySlot(s_slots[i]);
    RestoreLayers(true);
    s_actCount = 0;
    ForgetAll();
    DropHeap();
    s_quiet = 0;
}

}  // namespace

void Frame(bool (*highlight)(s32 x, s32 y), const Clone *clones, u32 count) {
    if (s_heapMade && !SceneSame()) {       // 場面が変わった: 何も書かずに捨てる
        Abandon();
        return;
    }
    if (!HeapPtr(R32(kProcPtr)) || *reinterpret_cast<const volatile s8 *>(kOutdoorFlag) == 0)
        return;
    StepHighlight(highlight);
    StepClones(clones, count);
    Reclaim(count);
}

bool Release(void) {
    if (!s_heapMade) {
        ForgetAll();
        return true;
    }
    if (!SceneSame()) {
        Abandon();
        return true;
    }
    for (u32 i = s_hiCount; i-- > 0;)
        RestoreRed(i, true);
    for (u32 i = 0; i < kMaxClones; ++i)
        s_slots[i].used = false;
    if (++s_quiet < kQuietFrames)           // 描くのをやめてから数フレーム待って壊す
        return false;
    for (u32 i = 0; i < kMaxClones; ++i)
        if (s_slots[i].made && !s_slots[i].broken)
            DestroySlot(s_slots[i]);
    RestoreLayers(true);
    ForgetAll();
    DropHeap();
    s_quiet = 0;
    return true;
}

void Abandon(void) {
    if (s_heapMade && SceneSame()) {        // 場面が同じ（エディターの取り外し）: 赤を戻し、複製を壊してから返す
        for (u32 i = s_hiCount; i-- > 0;)
            RestoreRed(i, true);
        for (u32 i = 0; i < kMaxClones; ++i)
            if (s_slots[i].made && !s_slots[i].broken)
                DestroySlot(s_slots[i]);
        RestoreLayers(true);
    }
    ForgetAll();
    s_quiet = 0;
    if (s_heapMade)
        DropHeap();
}

}  // namespace MapEditor3D
