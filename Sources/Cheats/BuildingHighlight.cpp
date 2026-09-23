#include "BuildingHighlight.hpp"

#include <3ds.h>
#include <cmath>
#include <cstring>

namespace BuildingHighlight {

namespace {

// ---- ゲーム側（JPN 無印 更新版）------------------------------------------------------------
const u32 kStrcMgr = 0x0094A6C0;            // u32: BsStrcMgr（IDA-opus-5.5-F004）
const u32 kStrcLists[] = { 0x6758, 0x6774, 0x67A8, 0x67CC, 0x68A0, 0x68B0, 0x68C0, 0x6A5C };
const u32 kActorBuildingId = 0x66;          // u8
const u32 kActorPosition = 0x14;            // float x, y, z
const u32 kActorDestroying = 0x0F;          // u8
const u32 kProcessSlotId = 0x04;            // u32: slot index is its low 10 bits (sub_51EF38)

const u32 kBaseMgr = 0x0096F2A4;            // u32: 基本マネージャ（F012）
const u32 kNodeStride = 20;                 // mgr + 20 * slot
const u32 kNodeFirstChild = 8;
const u32 kNodeNextSibling = 16;

const u32 kModelMaterials = 0x164;          // nw::gfx::Model: Material* 配列
const u32 kModelActivator = 0x1EC;          // MaterialActivator*
const u32 kMatColour = 48;                  // Material: 色の部分（a3[12]）
const u32 kMatTev = 72;                     // TEV の部分（a3[18]）
const u32 kMatFrag = 80;                    // フラグメント設定（a3[20]）
const u32 kResTevRel = 648;                 // ResMaterial: TEV ブロックへの相対ポインタ
const u32 kResTevKey = 712;                 // TEV の鍵（0 で毎回書き出す）
const u32 kResFragKey = 720;
const u32 kResColours = 36;                 // u32 色の表。49〜54 = Constant0〜5
const u32 kResConst5 = kResColours + 4 * 54;
const u32 kTevBytes = 244;
const u32 kTevStage0 = 44;                  // 1 段 28 B: 選択, 入力, ヘッダ, オペランド, 合成, 定数色, 倍率
const u32 kTevStageBytes = 28;
const u32 kTevUpdateWord = 228;             // レジスタ 0xE0 の値（更新ビット 8〜15）
const u32 kTevLutRel = 40;                  // TEV ブロック内の唯一の相対ポインタ
const u32 kFragDepth = 280;                 // bit1 = 深度書き込み
const u32 kFragFbRead = 300;
const u32 kFragCheckA = 320;                // 0x00E40100
const u32 kFragCheckB = 324;                // 0x803F0100
const u32 kFragBlend = 328;
const u32 kFragBlendColor = 336;
const u32 kGenericActivate = 0x0049C08C;    // gfx_MaterialActivator_Activate
const u32 kSimpleActivate = 0x004A17B8;     // nwgfx_SimpleMaterialActivator_Activate
// 定数アルファ／1-定数アルファ（色）、1／1-定数アルファ（アルファ）（IDA-gpt-6-astra-F002）
const u32 kBlendConstAlpha = (12u << 16) | (13u << 20) | (1u << 24) | (13u << 28);

const char kModel[] = "N2nw3gfx5ModelE";
const char kSkeletalModel[] = "N2nw3gfx13SkeletalModelE";
const char kMaterial[] = "N2nw3gfx8MaterialE";
const char kHobjPrefix[] = "N4hobj";

// ---- 持ち物（gohan 自身の配列。GPU はこれを直接読まない: F012）--------------------------------
const u32 kMaxProcs = 16;
const u32 kMaxModels = 24;
const u32 kMaxMaterials = 64;
const u32 kMaxActivators = 24;
const u32 kMaxUndo = 512;
const u32 kUndoBytes = 24 * 1024;

struct Undo { u32 addr; u16 len; u16 off; };
struct Animated { u32 const5; u32 blendColor; bool premultiplied; };

alignas(4) u8 s_tevPool[kMaxMaterials][kTevBytes];
alignas(4) u32 s_vtPool[kMaxActivators][6];
Undo s_undo[kMaxUndo];
alignas(4) u8 s_undoData[kUndoBytes];
Animated s_anim[kMaxMaterials];
u32 s_undoCount, s_undoUsed, s_tevUsed, s_vtUsed, s_animCount, s_modelCount;

// 当てている建物（生存確認用）
u32 s_actor, s_actorVtable, s_actorNode, s_actorSid, s_mgr;
u32 s_phase;

// ---- 要求（メニュー → ゲームのスレッド）--------------------------------------------------------
enum class Req : u32 { None, Select, Clear };
volatile Req s_req = Req::None;
volatile u16 s_reqId;
volatile u8 s_reqX, s_reqY;
volatile State s_state = State::Off;
Params s_params = { kBlue, 0xB0, 0xD0, 0x40, 11 };   // 青は F011〜F013 の実機の色

// ---- 読み書き -----------------------------------------------------------------------------
// ★範囲だけで番地と決めない。0x3F800000（float の 1.0）を番地とみなして読みに行き SIGSEGV になった。
// 読めるかどうかは OS に問い合わせる（svcQueryMemory）。結果は 1 フレームのあいだだけ覚えておく。
struct Region { u32 base; u32 end; bool readable; };
const u32 kMaxRegions = 24;
Region s_regions[kMaxRegions];
u32 s_regionCount, s_regionNext;

void ForgetRegions(void) { s_regionCount = s_regionNext = 0; }

bool ReadableAt(u32 a) {
    for (u32 i = 0; i < s_regionCount; ++i)
        if (a >= s_regions[i].base && a < s_regions[i].end)
            return s_regions[i].readable;
    MemInfo mi;
    PageInfo pi;
    bool ok = false;
    Region r = { a & ~0xFFFu, (a & ~0xFFFu) + 0x1000u, false };
    if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, a))) {
        ok = mi.state != MEMSTATE_FREE && (mi.perm & MEMPERM_READ) != 0;
        if (mi.size != 0 && a >= mi.base_addr && a - mi.base_addr < mi.size) {
            r.base = mi.base_addr;
            r.end = mi.base_addr + mi.size;
            if (r.end < r.base)
                r.end = 0xFFFFFFFFu;
        }
    }
    r.readable = ok;
    if (s_regionCount < kMaxRegions)
        s_regions[s_regionCount++] = r;
    else
        s_regions[s_regionNext++ % kMaxRegions] = r;
    return ok;
}

// [a, a+len) がすべて読めるか
bool Readable(u32 a, u32 len) {
    if (len == 0 || a + len < a)
        return false;
    const u32 last = a + len - 1;
    for (u32 p = a & ~0xFFFu;; p += 0x1000u) {
        if (!ReadableAt(p < a ? a : p))
            return false;
        if (p >= (last & ~0xFFFu))
            return true;
    }
}

bool IsHeap(u32 p) { return p >= 0x08000000u && (p & 3u) == 0u && Readable(p, 4); }
bool IsCode(u32 p) { return p >= 0x00100000u && p < 0x01000000u && (p & 3u) == 0u && Readable(p, 4); }
u32 R32(u32 a) { return *reinterpret_cast<volatile u32 *>(a); }
u8 R8(u32 a) { return *reinterpret_cast<volatile u8 *>(a); }

const char *ClassName(u32 obj) {
    if (!IsHeap(obj))
        return nullptr;
    const u32 vt = R32(obj);
    if (!IsCode(vt))
        return nullptr;
    if (!IsCode(vt - 4))
        return nullptr;
    const u32 ti = R32(vt - 4);
    if (!IsCode(ti) || !IsCode(ti + 4))
        return nullptr;
    const u32 name = R32(ti + 4);
    if (name < 0x00100000u || name >= 0x01000000u || !Readable(name, 32))
        return nullptr;
    return reinterpret_cast<const char *>(name);
}

bool IsClass(u32 obj, const char *mangled) {
    const char *n = ClassName(obj);
    return n != nullptr && std::strcmp(n, mangled) == 0;
}

bool Record(u32 addr, u32 len) {
    if (s_undoCount >= kMaxUndo || s_undoUsed + len > kUndoBytes)
        return false;
    s_undo[s_undoCount] = { addr, (u16)len, (u16)s_undoUsed };
    std::memcpy(s_undoData + s_undoUsed, reinterpret_cast<const void *>(addr), len);
    s_undoUsed += (len + 3u) & ~3u;
    ++s_undoCount;
    return true;
}

bool Put32(u32 addr, u32 value) {
    if (!Record(addr, 4))
        return false;
    *reinterpret_cast<volatile u32 *>(addr) = value;
    return true;
}

bool PutBlock(u32 addr, const u8 *data, u32 len) {
    if (!Record(addr, len))
        return false;
    std::memcpy(reinterpret_cast<void *>(addr), data, len);
    return true;
}

void RollBack(void) {
    for (u32 i = s_undoCount; i-- > 0;)
        std::memcpy(reinterpret_cast<void *>(s_undo[i].addr), s_undoData + s_undo[i].off, s_undo[i].len);
    s_undoCount = s_undoUsed = s_tevUsed = s_vtUsed = s_animCount = s_modelCount = 0;
}

// ---- 集める（F012）---------------------------------------------------------------------------
u32 NodeOf(u32 proc) {
    const u32 id = R32(proc + kProcessSlotId);
    const u32 low = id & 0x3FFu;
    const u32 slot = (id >= 0x400u && low < 0x200u) ? low + 0x200u : low;
    return R32(kBaseMgr) + kNodeStride * slot;
}

// 位置も合うものを優先。家など位置の決まり方が違う建物は、その id が 1 体だけならそれを使う。
u32 FindActor(u16 id, u8 x, u8 y) {
    const u32 mgr = R32(kStrcMgr);
    if (!IsHeap(mgr))
        return 0;
    u32 only = 0, same = 0;
    for (u32 l = 0; l < sizeof(kStrcLists) / sizeof(kStrcLists[0]); ++l) {
        const u32 count = R32(mgr + kStrcLists[l]);
        const u32 arr = R32(mgr + kStrcLists[l] + 8);
        if (!IsHeap(arr) || count > 0x100)
            continue;
        for (u32 i = 0; i < count; ++i) {
            const u32 a = R32(arr + 4 * i);
            if (!IsHeap(a) || R8(a + kActorDestroying) != 0 || R8(a + kActorBuildingId) != id)
                continue;
            const float *p = reinterpret_cast<const float *>(a + kActorPosition);
            if ((s32)p[0] == (s32)(32 * x + 16) && (s32)p[2] == (s32)(32 * y + 16))
                return a;
            if (a != only) {
                only = a;
                ++same;
            }
        }
    }
    return same == 1 ? only : 0;
}

struct Gather {
    u32 models[kMaxModels];
    u32 modelCount;
    u32 seen[16];
    u32 seenCount;
};

void AddModel(Gather &g, u32 m) {
    for (u32 i = 0; i < g.modelCount; ++i)
        if (g.models[i] == m)
            return;
    if (g.modelCount < kMaxModels)
        g.models[g.modelCount++] = m;
}

void ScanObject(Gather &g, u32 obj, u32 depth) {
    if (!Readable(obj - 16, 16))
        return;
    const u32 size = R32(obj - 4);          // ヒープの見出し +0xC（F012）
    if (size == 0 || size > 0x10000u || !Readable(obj, size & ~3u))
        return;
    for (u32 off = 0; off + 4 <= size; off += 4) {
        const u32 v = R32(obj + off);
        const char *n = ClassName(v);
        if (n == nullptr)
            continue;
        if (std::strcmp(n, kModel) == 0 || std::strcmp(n, kSkeletalModel) == 0) {
            AddModel(g, v);
        } else if (depth < 3 && std::strncmp(n, kHobjPrefix, sizeof(kHobjPrefix) - 1) == 0) {
            bool dup = false;
            for (u32 i = 0; i < g.seenCount; ++i)
                dup = dup || g.seen[i] == v;
            if (!dup && g.seenCount < 16) {
                g.seen[g.seenCount++] = v;
                ScanObject(g, v, depth + 1);
            }
        }
    }
}

void GatherModels(Gather &g, u32 actor) {
    u32 stack[kMaxProcs];
    u32 top = 0, visited = 0;
    stack[top++] = actor;
    while (top > 0 && visited < kMaxProcs) {
        const u32 p = stack[--top];
        ++visited;
        ScanObject(g, p, 0);
        u32 c = R32(NodeOf(p) + kNodeFirstChild);
        for (u32 n = 0; IsHeap(c) && n < 16 && top < kMaxProcs; ++n) {
            const u32 child = R32(c);
            if (IsHeap(child))
                stack[top++] = child;
            c = R32(c + kNodeNextSibling);
        }
    }
}

// ---- TEV（F011 / F013）-------------------------------------------------------------------------
enum class Plan { Skip, Free, Compact, Replace };

u32 *StageAt(u8 *block, u32 i) { return reinterpret_cast<u32 *>(block + kTevStage0 + kTevStageBytes * i); }

bool PassesThrough(const u32 *s, u32 colour) {
    if ((s[4] & 0xFFFFu) == 0 && (s[1] & 0xFu) == 0xFu && (s[3] & 0xFu) == 0)
        return true;
    if ((s[1] & 0xFFFFu) == 0x0EFDu && (s[3] & 0xFFFu) == 0x300u && (s[4] & 0xFFFFu) == 4u) {
        const u32 sel = s[0] & 0xFFu;
        const u32 index = (sel >= 1 && sel <= 5) ? 49 + sel : (sel >= 6 && sel <= 10) ? 38 + sel : 49;
        return (R32(colour + kResColours + 4 * index) >> 24) == 0xFFu;
    }
    return false;
}

Plan PlanBlock(u8 *b, u32 colour) {
    u32 stages[6][7];
    for (u32 i = 0; i < 6; ++i)
        std::memcpy(stages[i], StageAt(b, i), kTevStageBytes);
    const u32 upd = *reinterpret_cast<u32 *>(b + kTevUpdateWord);
    const u32 bits = ((upd >> 8) & 0xFu) | ((upd >> 12) & 0xFu);
    Plan plan;
    u32 alpha[7];
    if (PassesThrough(stages[5], colour)) {
        plan = Plan::Free;
        std::memcpy(alpha, stages[5], sizeof(alpha));
    } else {
        s32 k = -1;
        for (s32 i = 4; i >= 0 && k < 0; --i) {
            bool clean = true;
            for (s32 j = i; j < 4; ++j)
                clean = clean && ((bits >> j) & 1u) == 0;
            if (clean && PassesThrough(stages[i], colour))
                k = i;
        }
        if (k < 0) {
            // 全段使用: 段 5 を lerp(BASE, 定数, 定数.a) の MultiplyAdd に。Constant5 は前乗算。
            u32 *s5 = StageAt(b, 5);
            const u32 src = s5[1];
            u32 base = 0xFu;
            for (u32 n = 0; n < 3; ++n)
                if (((src >> (4 * n)) & 0xFu) == 0xDu)
                    base = 0xDu;
            s5[0] = 5;
            s5[1] = (src & 0xFFFF0000u) | 0x0EE0u | base;
            s5[3] = (s5[3] & ~0xFFFu) | 0x030u;
            s5[4] = (s5[4] & 0xFFFF0000u) | 0x8u;
            s5[6] = 0;
            return Plan::Replace;
        }
        for (u32 i = (u32)k; i < 5; ++i) {             // 段 k+1..5 を 1 つ前へ。ヘッダは位置に残す
            u32 *d = StageAt(b, i);
            const u32 header = d[2];
            std::memcpy(d, stages[i + 1], kTevStageBytes);
            d[2] = header;
        }
        plan = Plan::Compact;
        const u32 pass[7] = { 0, 0x0FFF0000u, 0, 0, 0, 0, 0 };
        std::memcpy(alpha, pass, sizeof(alpha));
    }
    u32 *s5 = StageAt(b, 5);
    s5[0] = 5;
    s5[1] = (alpha[1] & 0xFFFF0000u) | 0x0EFEu;      // rgb (定数, 前段, 定数)
    s5[3] = (alpha[3] & ~0xFFFu) | 0x200u;           // rgb (色, 色, アルファ)
    s5[4] = (alpha[4] & 0xFFFF0000u) | 0x4u;         // rgb Interpolate
    s5[6] = 0;
    return plan;
}

u32 Const5Value(bool premultiplied, u32 tint) {
    const u32 c = s_params.color;
    if (!premultiplied)
        return (tint << 24) | (c & 0x00FFFFFFu);
    const u32 r = (c & 0xFFu) * tint / 255u;
    const u32 g = ((c >> 8) & 0xFFu) * tint / 255u;
    const u32 bl = ((c >> 16) & 0xFFu) * tint / 255u;
    return (tint << 24) | (bl << 16) | (g << 8) | r;
}

bool ApplyMaterial(u32 m) {
    const u32 colour = R32(m + kMatColour);
    const u32 tevres = R32(m + kMatTev);
    const u32 frag = R32(m + kMatFrag);
    if (!IsHeap(colour) || !IsHeap(tevres) || s_animCount >= kMaxMaterials || s_tevUsed >= kMaxMaterials)
        return false;
    const u32 rel = R32(tevres + kResTevRel);
    if (rel == 0)
        return false;
    const u32 tev = tevres + kResTevRel + rel;
    u8 *work = s_tevPool[s_tevUsed];
    std::memcpy(work, reinterpret_cast<const void *>(tev), kTevBytes);
    const Plan plan = PlanBlock(work, colour);
    if (plan == Plan::Skip)
        return false;
    if (colour != tevres) {
        // 共有の TEV: 写しを gohan の配列に置き、インスタンス専用の色の部分の未使用 +648 から指す
        if (R32(colour + kResTevRel) != 0)
            return false;
        const u32 dst = reinterpret_cast<u32>(work);
        const u32 lut = *reinterpret_cast<u32 *>(work + kTevLutRel);
        if (lut != 0)
            *reinterpret_cast<u32 *>(work + kTevLutRel) = tev + kTevLutRel + lut - (dst + kTevLutRel);
        ++s_tevUsed;
        if (!Put32(colour + kResTevRel, dst - (colour + kResTevRel)) || !Put32(m + kMatTev, colour))
            return false;
    } else {
        // その建物専用のブロック: その場で書き、鍵を 0 に
        if (!PutBlock(tev, work, kTevBytes) || !Put32(colour + kResTevKey, 0))
            return false;
    }
    Animated &a = s_anim[s_animCount];
    a.const5 = colour + kResConst5;
    a.premultiplied = (plan == Plan::Replace);
    a.blendColor = 0;
    if (!Put32(a.const5, Const5Value(a.premultiplied, s_params.tint)))
        return false;
    // 透明度（F002）: この建物専用のフラグメント設定のときだけ
    if (frag == colour && R32(frag + kFragCheckA) == 0x00E40100u && R32(frag + kFragCheckB) == 0x803F0100u) {
        if (Put32(frag + kFragDepth, R32(frag + kFragDepth) & ~2u) && Put32(frag + kFragFbRead, 0)
            && Put32(frag + kFragBlend, kBlendConstAlpha) && Put32(frag + kFragBlendColor, (u32)s_params.alpha << 24)
            && Put32(frag + kResFragKey, 0))
            a.blendColor = frag + kFragBlendColor;
    }
    ++s_animCount;
    return true;
}

void SwapActivator(u32 model) {
    const u32 act = R32(model + kModelActivator);
    if (!IsHeap(act) || s_vtUsed >= kMaxActivators)
        return;
    const u32 vt = R32(act);
    if (!IsCode(vt) || !Readable(vt - 4, 24) || R32(vt + 12) != kSimpleActivate)
        return;
    u32 *copy = s_vtPool[s_vtUsed++];
    for (u32 j = 0; j < 6; ++j)
        copy[j] = R32(vt - 4 + 4 * j);
    copy[4] = kGenericActivate;             // slot 3（copy[0] は TypeInfo）
    Put32(act, reinterpret_cast<u32>(copy + 1));
}

bool Alive(void) {
    if (!IsHeap(s_actor) || R32(kStrcMgr) != s_mgr)
        return false;
    // プロセス id も比べる（作り直しで変わるなら、同じ番地に作り直された建物も見分けられる。未確認）
    if (R32(s_actorNode) != s_actor || R32(s_actor) != s_actorVtable || R32(s_actor + kProcessSlotId) != s_actorSid
        || R8(s_actor + kActorDestroying) != 0)
        return false;
    return true;
}

bool ApplyTo(u32 actor) {
    Gather g;
    g.modelCount = g.seenCount = 0;
    GatherModels(g, actor);
    s_modelCount = g.modelCount;
    for (u32 i = 0; i < g.modelCount; ++i) {
        const u32 arr = R32(g.models[i] + kModelMaterials);
        if (!IsHeap(arr))
            continue;
        for (u32 k = 0; k < 32; ++k) {
            const u32 m = R32(arr + 4 * k);
            if (!IsClass(m, kMaterial))
                break;
            ApplyMaterial(m);
        }
        SwapActivator(g.models[i]);
    }
    s_actor = actor;
    s_actorVtable = R32(actor);
    s_actorNode = NodeOf(actor);
    s_actorSid = R32(actor + kProcessSlotId);
    s_mgr = R32(kStrcMgr);
    return s_animCount > 0;
}

void Animate(void) {
    s_phase += s_params.speed;
    const float t = (float)(s_phase % 1000u) * (6.2831853f / 1000.0f);
    s32 alpha = (s32)s_params.alpha + (s32)(std::sin(t) * (float)s_params.wave);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    for (u32 i = 0; i < s_animCount; ++i) {
        *reinterpret_cast<volatile u32 *>(s_anim[i].const5) = Const5Value(s_anim[i].premultiplied, s_params.tint);
        if (s_anim[i].blendColor)
            *reinterpret_cast<volatile u32 *>(s_anim[i].blendColor) = (u32)alpha << 24;
    }
}

}  // namespace

bool SafeReadable(u32 addr, u32 len) {
    return Readable(addr, len);
}

bool LooksLikeMaterial(u32 obj) {
    return IsClass(obj, kMaterial);
}

int PlanTev(u8 *block, u32 colour) {
    return (int)PlanBlock(block, colour);
}

u32 TintConstant(u32 color, u8 strength, bool premultiplied) {
    const u32 tint = strength;
    if (!premultiplied)
        return (tint << 24) | (color & 0x00FFFFFFu);
    const u32 r = (color & 0xFFu) * tint / 255u;
    const u32 g = ((color >> 8) & 0xFFu) * tint / 255u;
    const u32 bl = ((color >> 16) & 0xFFu) * tint / 255u;
    return (tint << 24) | (bl << 16) | (g << 8) | r;
}

void Select(u16 id, u8 x, u8 y) {
    s_reqId = id;
    s_reqX = x;
    s_reqY = y;
    s_req = Req::Select;
}

void Clear(void) {
    s_req = Req::Clear;
}

void ClearNow(void) {
    if (s_state == State::Active && Alive())
        RollBack();
    s_undoCount = s_undoUsed = s_tevUsed = s_vtUsed = s_animCount = 0;
    s_actor = 0;
    s_state = State::Off;
}

void SetParams(const Params &params) {
    s_params = params;
    if (s_params.speed == 0)
        s_params.speed = 1;
}

const Params &GetParams(void) { return s_params; }

void SetColor(u32 color) {
    s_params.color = color & 0x00FFFFFFu;
}

bool Current(u16 &id, u8 &x, u8 &y) {
    if (s_req == Req::Select) {                 // まだ当てていない要求も「いま選んでいるもの」
        id = s_reqId;
        x = s_reqX;
        y = s_reqY;
        return true;
    }
    if (s_state != State::Active)
        return false;
    id = s_reqId;
    x = s_reqX;
    y = s_reqY;
    return true;
}
State GetState(void) { return s_state; }
u32 MaterialCount(void) { return s_animCount; }
u32 ModelCount(void) { return s_modelCount; }

void FrameStep(void) {
    ForgetRegions();
    const Req req = s_req;
    if (req != Req::None) {
        s_req = Req::None;
        ClearNow();
        if (req == Req::Select) {
            const u32 actor = FindActor(s_reqId, s_reqX, s_reqY);
            if (actor == 0) {
                s_state = State::Failed;
            } else if (ApplyTo(actor)) {
                s_state = State::Active;
            } else {
                RollBack();
                s_state = State::Failed;
            }
        }
    }
    if (s_state != State::Active)
        return;
    if (!Alive()) {                          // 場面が変わった・建物が消えた: 何も書かずに捨てる
        s_undoCount = s_undoUsed = s_tevUsed = s_vtUsed = s_animCount = 0;
        s_actor = 0;
        s_state = State::Off;
        return;
    }
    Animate();
}

}  // namespace BuildingHighlight
