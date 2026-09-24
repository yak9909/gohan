#include "PlayerClone.hpp"

#include "Cheats.hpp"
#include "GridCursor.hpp"
#include "LatePassStub.h"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace PlayerClone {

namespace {

// ---- ゲームの関数（IDA-opus-5.5-F028 / F029）------------------------------------------------------
typedef void (*CtorFn)(void *pm);
typedef u32 (*CreateFromProfileFn)(void *pm, u32 profile, u32 a3, u32 maskBit, u32 a5, u32 a6);
typedef void (*DtorFn)(void *pm);
typedef void (*SetMatrixFn)(void *pm, const float *m);
typedef void (*ModelStepFn)(void *pm);
typedef void (*CalcAnimFn)(void *pm, u32 a2, u32 a3);
typedef u32 (*ProfileFn)(u32 index);
typedef void (*SubmitFn)(void *holder, u32 scene);
typedef u32 (*ReleaseFn)(u32 table, u32 *handle);
typedef void (*CameraBindFn)(u32 context, u32 camera, u32 force);
typedef void (*DrawMeshFn)(u32 drawContext, u32 mesh, u32 node);
typedef void (*EntryFn)(u32 entry);

const CtorFn PlayerModelCtor = reinterpret_cast<CtorFn>(0x001D3854);
const CreateFromProfileFn CreateFromProfile = reinterpret_cast<CreateFromProfileFn>(0x001CF090);
const DtorFn PlayerModelDtor = reinterpret_cast<DtorFn>(0x001D3980);
const SetMatrixFn SetMatrix = reinterpret_cast<SetMatrixFn>(0x001CEC10);        // vtbl[9]
const ModelStepFn StepFaceTool = reinterpret_cast<ModelStepFn>(0x001D2BE8);     // vtbl[2]
const CalcAnimFn CalcAnim = reinterpret_cast<CalcAnimFn>(0x001CE8D8);
const ModelStepFn StepAttach = reinterpret_cast<ModelStepFn>(0x001D33FC);       // vtbl[4]
const ModelStepFn StepParts = reinterpret_cast<ModelStepFn>(0x001D2CCC);        // vtbl[5]
const ProfileFn PlayerProfile = reinterpret_cast<ProfileFn>(0x002FEB60);        // vc_PSOFFSET
const SubmitFn Submit = reinterpret_cast<SubmitFn>(0x004ED630);                 // Scene_SubmitNode
// ★枠を返すのは BankTable_Release。vt[0]（頭なら 0x822B98）で部品を下ろし終えてから空きにし、済めば 1（IDA-opus-5.5-F030）。
//   使用中ビットと使用数だけを消すと部品が読み込まれたまま残り、管理役の片付け（sub_2A0E74 はテクスチャアニメを先に消す）が
//   あとで頭を下ろすときにアロケータ 0 で落ちた（実機 SIGSEGV 0x569B48）。
const ReleaseFn BankRelease = reinterpret_cast<ReleaseFn>(0x002138FC);
const EntryFn FaceCancel = reinterpret_cast<EntryFn>(0x002711A4);              // PlayerModel_DestroyStep が顔の枠に先に呼ぶ
const ModelStepFn DestroyBody = reinterpret_cast<ModelStepFn>(0x001AC1E8);      // 体のインスタンスを消す（DestroyStep が体の枠の前に呼ぶ）

// ---- 画面に固定（late pass。IDA-opus-5.5-F031）-------------------------------------------------------
// Render_DrawSceneIndexed 0x4EEDD8 の記録リスト再生の直後（0x4EF1B0 の NOP）で、専用のカメラを結んで複製のメッシュを描き、
// 元のカメラへ戻す（IDA-gpt-6-astra-F003 の木 1 体と同じ手順）。Scene 1 へ積む案は、記録の格納先が Scene で共有
// （dword_94CA54 はフレームごとの 0/1）なので右目が壊れるため採らない。
// CameraBind 0x494D3C は view（+328、3x4）と projection（+424、4x4）を頂点シェーダの定数へ送るだけ。深度の変換は触らない。
const CameraBindFn CameraBind = reinterpret_cast<CameraBindFn>(0x00494D3C);
const DrawMeshFn DrawMesh = reinterpret_cast<DrawMeshFn>(0x0048D26C);           // Scene_DrawLayers が使う sub_48D26C
const u32 kSceneContexts = 0x009C3098;      // g_SceneContexts[3]
const u32 kSceneMode = 0x0094CA2C;          // 0 = Scene 0 を描く（1 はメニューの Scene 1）
const u32 kDrawContext = 0x0094CA48;        // g_DrawContext（DrawMesh の R0。+8 が描画の文脈）
const u32 kRenderContext = 0x0094CA44;      // 描画の文脈（+40 が今のカメラ）
const u32 kCurrentCamera = 0x0094CA58;      // Render_DrawSceneIndexed が結んだカメラ
const u32 kContextCamera = 40;
const u32 kContextCacheA = 28;              // 木の late pass と同じく、描く前に材質・モデルのキャッシュを消す
const u32 kContextCacheB = 36;
const u32 kCameraView = 328;
const u32 kCameraProjection = 424;
const u32 kPartListBase = 76768;            // BsPlayerMgr の部品の表 8 本（生の管理役から。各 7 件 + 数 +28）
const u32 kPartListStride = 32;
const u32 kPartLists = 8;
const u32 kPartListMax = 7;
const u32 kPartListCount = 28;
const u32 kMaxParts = 16;
// ノードの欄（Scene_DrawLayers 0x4EB840 と同じ読み方）
const u32 kNodeRes = 8;                     // ResModel
const u32 kNodeMaterials = 0x164;
const u32 kNodeMeshBegin = 0x170;
const u32 kNodeMeshEnd = 0x174;
const u32 kNodeVisBegin = 0x17C;
const u32 kNodeVisEnd = 0x180;
const u32 kResMeshCount = 180;
const u32 kResMeshes = 184;
const u32 kResVis = 208;
const u32 kMeshMaterial = 28;
const u32 kMeshVisible = 36;
const u32 kMeshVisIndex = 38;
const u32 kMatResource = 8;
const u32 kResLayer = 32;
// 深度: ゲームの projection は z/w が near(50) で 0、far(1750) で -1。z の行に小さい係数を掛けて複製を near のすぐ前に寄せ、
// 世界の物に隠れないようにする（係数 0.01 で複製の深度は -0.006 前後＝ゲームのカメラから約 50.3 より手前）。
const float kDepthSqueeze = 0.01f;
const float kCameraHeight = 12.0f;          // 複製の原点から見る高さ
const float kCameraDistance = 120.0f;       // 複製までの距離（near 50 より遠いこと）
const u32 kCameraBytes = 488;               // view +328（48 B）と projection +424（64 B）が収まる大きさ

const u32 kPlayerPtr = 0x00AA7994;          // AcPlayer*
const u32 kPlayerMgrPtr = 0x0094A374;       // BsPlayerMgr*（部品バンクの管理役 = +24）
const u32 kRoomIdByte = 0x0095133A;         // 今の部屋（g_CurrentRoomId）
const u32 kSceneOwnerPtr = 0x00948E70;      // 場面の持ち主（部屋の切り替えで 0 になり、別の番地で戻る。GridCursor と同じ検査）
const u32 kBankMgrOffset = 24;
const u32 kActorModel = 436;                // AcPlayer + 436 = PlayerModel
const u32 kActorPlayerIndex = 428;          // u8
const u32 kModelBody = 120;                 // TransformNodeHolder（+4 がノード、ノード +0x4C が行列）
const u32 kHolderNode = 4;
const u32 kNodeMatrix = 0x4C;
const u32 kModelHair = 548;                 // u8
const u32 kModelHairColor = 552;            // u32
const u32 kModelBytes = 624;                // AcNpcDemoDollPlayer: +2852 から次の欄 +3476 まで
const u32 kProfileMaskByte = 22286;         // bit6 = Mii マスク（人形 0x2ECB30 と同じ）
const u32 kDollToolParam = 4;               // 人形の第 6 引数

const float kSideOffset = 30.0f;            // 体の行列の X 軸の向きへずらす量（world 単位）
const u32 kCreateLimit = 300;               // 10 秒（30fps）で作成が終わらなければ諦める
const u32 kQuietFrames = 4;                 // 積むのをやめてから消すまで待つフレーム数（GridCursor と同じ。描画中の参照を残さない）

// 部品バンク（管理役からの位置と、1 体が取る枠の数）。PlayerModel_Setup 0x1CF474 が取るもの
struct BankNeed { u32 offset; u16 need; };
const BankNeed kBanks[] = {
    { 0, 1 }, { 156, 2 }, { 4148, 1 }, { 13292, 1 }, { 21680, 1 }, { 30348, 1 }, { 39016, 1 },
    { 47376, 1 }, { 51872, 1 }, { 60624, 1 }, { 76152, 2 }, { 76448, 2 },
};
const u32 kBankCount = 4;                   // BankTable +4 = 枠数
const u32 kBankUsed = 8;                    // BankTable +8 = 使用中の数（u16。BankTable_Acquire 0x2137CC が増やす）

// 片付けの順（PlayerModel_DestroyStep 0x1D30D0 と同じ）: {管理役からの位置, pm の組の位置}。顔・体は別に扱う
struct Pair { u32 bank; u32 handle; };
const Pair kReleaseOrder[] = {
    { 60624, 404 }, { 51872, 396 }, { 47376, 388 }, { 39016, 380 }, { 30348, 372 }, { 21680, 356 },
    { 13292, 364 }, { 4148, 348 },
};
const Pair kFacePairs[] = { { 156, 332 }, { 156, 340 } };
const Pair kTexAnimPairs[] = { { 76448, 436 }, { 76448, 428 }, { 76152, 420 }, { 76152, 412 } };
const Pair kBodyPair = { 0, 324 };
const u32 kFaceEntryBytes = 284;
const u32 kTableEntries = 12;

enum Stage : u32 { kOff = 0, kCreating = 1, kLive = 2, kDestroying = 3, kFailed = 4 };

u8 s_model[kModelBytes] __attribute__((aligned(8)));
bool s_constructed;
u32 s_mgr;                                  // 作ったときの BsPlayerMgr
u32 s_player;                               // 作ったときの AcPlayer
u8 s_room;                                  // 作ったときの部屋
u32 s_owner;                                // 作ったときの場面の持ち主

volatile bool s_want;
volatile u32 s_stage = kOff;
volatile u32 s_fail;
volatile u32 s_frames;
volatile u32 s_submits;
volatile s32 s_hairStyle = -1;
volatile s32 s_hairColor = -1;
bool s_hooked;

// 画面に固定
volatile bool s_screen;                     // メニュー: 画面に固定する
volatile s32 s_yaw;                         // 度
volatile s32 s_pixelX = 330;                // 上画面のピクセル（400x240）。複製の足元付近が来る位置ではなく、カメラの中心
volatile s32 s_pixelY = 150;
volatile s32 s_zoom = 60;                   // 百分率
u8 s_camera[kCameraBytes] __attribute__((aligned(8)));
u32 s_parts[kMaxParts];                     // このフレームに部品の表から抜き取った holder（+4 がノード）
volatile u32 s_partCount;
volatile bool s_lateReady;                  // s_parts が今の複製のもの
volatile u32 s_lateDraws;
bool s_lateHooked;

u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
u16 R16(u32 a) { return *reinterpret_cast<const volatile u16 *>(a); }
u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }
bool IsHeap(u32 p) { return p >= 0x08000000u && p < 0x40000000u && (p & 3u) == 0u; }
u32 Model(void) { return reinterpret_cast<u32>(s_model); }

bool BanksHaveRoom(u32 mgr) {
    for (const BankNeed &b : kBanks) {
        const u32 table = mgr + b.offset;
        if ((u32)R16(table + kBankUsed) + b.need > R32(table + kBankCount))
            return false;
    }
    return true;
}

// 場面が変わった（管理役かプレイヤーが作り直された）。バンクごと消えているのでゲームの関数は呼ばない。
// 部屋の番号と場面の持ち主も見る（切り替えの途中は管理役の番地が同じまま片付けが進みうる）。
bool SceneChanged(void) {
    return R32(kPlayerMgrPtr) != s_mgr || R32(kPlayerPtr) != s_player || R8(kRoomIdByte) != s_room
        || R32(kSceneOwnerPtr) != s_owner;
}

// 組がまだこの管理役の枠を指しているか。返し終えた組は {0, -1}（BankTable_Release）。
bool Held(const Pair &p) {
    const u32 table = s_mgr + kBankMgrOffset + p.bank;
    return R32(Model() + p.handle) == table && R32(Model() + p.handle + 4) < R32(table + kBankCount);
}

u32 Release(const Pair &p) {
    if (!Held(p))
        return 1;
    return BankRelease(s_mgr + kBankMgrOffset + p.bank, reinterpret_cast<u32 *>(Model() + p.handle));
}

// PlayerModel_DestroyStep と同じことを、大域の管理役（場面の切り替え中は 0）ではなく作ったときの管理役で行う。
// 管理役は全部の枠が返るまで片付けを待つ（BsPlayerMgr vtbl[20] 0x1C475C → sub_2A0E74）ので、その間は生きている。
bool ReleaseStep(void) {
    u32 done = 1;
    for (const Pair &p : kReleaseOrder)
        done &= Release(p);
    for (const Pair &p : kFacePairs) {
        if (Held(p)) {
            const u32 table = s_mgr + kBankMgrOffset + p.bank;
            FaceCancel(table + kTableEntries + kFaceEntryBytes * R32(Model() + p.handle + 4));
        }
    }
    for (const Pair &p : kFacePairs)
        done &= Release(p);
    for (const Pair &p : kTexAnimPairs)
        done &= Release(p);
    DestroyBody(s_model);
    done &= Release(kBodyPair);
    return done != 0u;
}

void StepDestroy(void);

void Abandon(u32 reason) {
    std::memset(s_model, 0, sizeof(s_model));
    s_constructed = false;
    s_fail = reason;
    s_stage = kFailed;
    s_frames = 0;
}

void ApplyHair(void) {
    const s32 style = s_hairStyle;
    const s32 color = s_hairColor;
    if (style >= 0 && style < 0x22 && R8(Model() + kModelHair) != (u8)style)
        *reinterpret_cast<volatile u8 *>(Model() + kModelHair) = (u8)style;
    if (color >= 0 && color < 0x10 && R32(Model() + kModelHairColor) != (u32)color)
        *reinterpret_cast<volatile u32 *>(Model() + kModelHairColor) = (u32)color;
}

// 画面に固定のとき: 原点に置き、Y 軸まわりに回すだけ（Scene 0 へは積まないので、ワールドのどこでもよい）
void PoseScreen(void) {
    const float r = (float)s_yaw * (3.14159265f / 180.0f);
    const float c = std::cos(r), s = std::sin(r);
    const float m[12] = { c, 0.0f, s, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -s, 0.0f, c, 0.0f };
    SetMatrix(s_model, m);
}

// vtbl[5] が管理役の部品の表へ積んだ分を抜き取って s_parts へ移す（Scene 0 で描かせない）
void TakeParts(const u32 *before) {
    const u32 lists = R32(kPlayerMgrPtr) + kPartListBase;
    u32 n = 0;
    for (u32 k = 0; k < kPartLists; ++k) {
        const u32 list = lists + kPartListStride * k;
        const u32 count = R32(list + kPartListCount);
        for (u32 i = before[k]; i < count && i < kPartListMax; ++i) {
            if (n < kMaxParts)
                s_parts[n++] = R32(list + 4 * i);
            *reinterpret_cast<volatile u32 *>(list + 4 * i) = 0;
        }
        if (count > before[k])
            *reinterpret_cast<volatile u32 *>(list + kPartListCount) = before[k];
    }
    s_partCount = n;
}

// 本物の体の行列を、その X 軸の向きへずらす
void Pose(void) {
    const u32 node = R32(s_player + kActorModel + kModelBody + kHolderNode);
    if (!IsHeap(node))
        return;
    float m[12];
    std::memcpy(m, reinterpret_cast<const void *>(node + kNodeMatrix), sizeof(m));
    m[3] += m[0] * kSideOffset;
    m[7] += m[4] * kSideOffset;
    m[11] += m[8] * kSideOffset;
    SetMatrix(s_model, m);
}

void StepCreate(void) {
    const u32 player = R32(kPlayerPtr);
    const u32 manager = R32(kPlayerMgrPtr);
    if (!s_constructed) {
        if (!IsHeap(player) || !IsHeap(manager) || !IsHeap(R32(kSceneOwnerPtr))) {
            Abandon(1);
            return;
        }
        const u32 profile = PlayerProfile(R8(player + kActorPlayerIndex));
        if (profile == 0u) {
            Abandon(2);
            return;
        }
        if (!BanksHaveRoom(manager + kBankMgrOffset)) {
            Abandon(5);
            return;
        }
        std::memset(s_model, 0, sizeof(s_model));
        PlayerModelCtor(s_model);
        s_constructed = true;
        s_mgr = manager;
        s_player = player;
        s_room = R8(kRoomIdByte);
        s_owner = R32(kSceneOwnerPtr);
        s_frames = 0;
    }
    if (SceneChanged()) {
        s_fail = 4;
        s_stage = kDestroying;                      // 取った枠を返す（手放すと無限ロード）
        s_frames = 0;
        StepDestroy();
        return;
    }
    const u32 profile = PlayerProfile(R8(s_player + kActorPlayerIndex));
    if (profile == 0u) {
        s_stage = kDestroying;                      // 作りかけを片付ける
        s_frames = 0;
        return;
    }
    const u32 maskBit = (R8(profile + kProfileMaskByte) >> 6) & 1u;
    if (CreateFromProfile(s_model, profile, 0, maskBit, 0, kDollToolParam) != 0u) {
        s_stage = kLive;
        s_frames = 0;
        return;
    }
    if (++s_frames >= kCreateLimit) {
        s_fail = 3;
        s_stage = kDestroying;
        s_frames = 0;
    }
}

void StepLive(void) {
    if (SceneChanged()) {
        s_lateReady = false;
        s_fail = 4;
        s_stage = kDestroying;                      // 取った枠を返す（手放すと無限ロード）
        s_frames = 0;
        StepDestroy();
        return;
    }
    ApplyHair();
    const bool screen = s_screen;
    if (screen)
        PoseScreen();
    else
        Pose();
    StepFaceTool(s_model);
    CalcAnim(s_model, 1, 1);
    StepAttach(s_model);
    if (screen) {
        const u32 lists = R32(kPlayerMgrPtr) + kPartListBase;
        u32 before[kPartLists];
        for (u32 k = 0; k < kPartLists; ++k)
            before[k] = R32(lists + kPartListStride * k + kPartListCount);
        s_lateReady = false;
        StepParts(s_model);
        TakeParts(before);
        s_lateReady = true;
    } else {
        s_lateReady = false;
        StepParts(s_model);
        Submit(reinterpret_cast<void *>(Model() + kModelBody), 0);
    }
    ++s_submits;
    ++s_frames;
}

// 片付け。場面の切り替え中でも同じ（覚えておいた管理役の枠を返す）。返し終えるまで毎フレーム。
// ★諦めて捨てない: 1 枠でも残ると管理役の片付けが永久に待ち、無限ロードになる（実機 2026-09-24）。
void StepDestroy(void) {
    if (!s_constructed) {
        s_stage = s_fail != 0u ? kFailed : kOff;
        return;
    }
    s_lateReady = false;
    if (++s_frames <= kQuietFrames)
        return;
    if (!ReleaseStep())
        return;
    PlayerModelDtor(s_model);
    std::memset(s_model, 0, sizeof(s_model));
    s_constructed = false;
    s_stage = s_fail != 0u ? kFailed : kOff;
    s_frames = 0;
}

// ---- late pass（描画スレッド。Render_DrawSceneIndexed の再生直後から）----------------------------------

// Scene_DrawLayers 0x4EB840 と同じ判定で、node のうち層 layer のメッシュを描く
void DrawNodeLayer(u32 node, u32 layer, u32 drawContext) {
    if (!IsHeap(node))
        return;
    u32 begin = R32(node + kNodeMeshBegin), end = R32(node + kNodeMeshEnd);
    if (begin == end) {
        const u32 res = R32(node + kNodeRes);
        const u32 rel = R32(res + kResMeshes);
        begin = rel ? res + kResMeshes + rel : 0u;
        end = begin + 4u * R32(res + kResMeshCount);
    }
    const u32 materials = R32(node + kNodeMaterials);
    for (u32 p = begin; p != 0u && p < end; p += 4) {
        const u32 off = R32(p);
        if (off == 0u)
            continue;
        const u32 mesh = p + off;
        const s16 vis = *reinterpret_cast<const volatile s16 *>(mesh + kMeshVisIndex);
        if (vis >= 0) {
            u32 entry;
            const u32 vb = R32(node + kNodeVisBegin);
            if (vb == R32(node + kNodeVisEnd)) {
                const u32 res = R32(node + kNodeRes);
                const u32 rel = R32(res + kResVis);
                u32 table = rel ? res + kResVis + rel : 0u;
                if (table == 0u)
                    continue;
                const u32 slot = table + 16u * (u32)vis + 40u;
                const u32 rel2 = R32(slot);
                entry = rel2 ? slot + rel2 : 0u;
                if (entry == 0u)
                    continue;
            } else {
                entry = vb + 8u * (u32)vis;
            }
            if (R8(entry + 4) == 0u)
                continue;
        }
        if (R8(mesh + kMeshVisible) == 0u)
            continue;
        const u32 material = R32(materials + 4u * R32(mesh + kMeshMaterial));
        if (!IsHeap(material))
            continue;
        if ((R32(R32(material + kMatResource) + kResLayer) & 0xFFu) != layer)
            continue;
        DrawMesh(drawContext, mesh, node);
        ++s_lateDraws;
    }
}

// ゲームのカメラの projection から倍率と z の行を取り、画面の位置・大きさ・深度の寄せを掛けた専用カメラを作る。
// LCD は 90 度回っているので、画面 x = 200 * (1 - clip.y / w)、画面 y = 120 * (1 - clip.x / w)（IDA-gpt-6-astra-F003）。
bool BuildCamera(u32 gameCamera) {
    const float *g = reinterpret_cast<const float *>(gameCamera + kCameraProjection);
    const float sy = g[1];                      // 行 0 = [0, sy, *, *]（画面の縦）
    const float sx = -g[4];                     // 行 1 = [-sx, 0, *, *]（画面の横）
    const float zz = g[10], zw = g[11];         // 行 2 = [0, 0, zz, zw]
    if (!(sy > 0.1f && sx > 0.1f && zw > 0.0f))
        return false;
    const float zoom = (float)s_zoom / 100.0f;
    const float ox = 1.0f - (float)s_pixelY / 120.0f;
    const float oy = 1.0f - (float)s_pixelX / 200.0f;
    float *view = reinterpret_cast<float *>(s_camera + kCameraView);
    const float v[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -kCameraHeight, 0.0f, 0.0f, 1.0f, -kCameraDistance };
    std::memcpy(view, v, sizeof(v));
    float *proj = reinterpret_cast<float *>(s_camera + kCameraProjection);
    // 行 3 = [0, 0, -1, 0]（clip.w = -view.z）。行 0/1 に w の行を足すと中心がずれる
    const float p[16] = {
        0.0f, zoom * sy, -ox, 0.0f,
        -zoom * sx, 0.0f, -oy, 0.0f,
        0.0f, 0.0f, kDepthSqueeze * zz, kDepthSqueeze * zw,
        0.0f, 0.0f, -1.0f, 0.0f,
    };
    std::memcpy(proj, p, sizeof(p));
    return true;
}

extern "C" void PlayerCloneLatePass(u32 sceneContext) {
    if (s_stage != kLive || !s_screen || !s_lateReady)
        return;
    if (sceneContext != R32(kSceneContexts) || R8(kSceneMode) != 0u)
        return;
    const u32 drawContext = R32(kDrawContext);
    if (!IsHeap(drawContext))
        return;
    const u32 context = R32(drawContext + 8);
    if (!IsHeap(context) || context != R32(kRenderContext))
        return;
    const u32 camera = R32(context + kContextCamera);
    if (!IsHeap(camera) || camera != R32(kCurrentCamera))
        return;
    if (!BuildCamera(camera))
        return;
    const u32 body = R32(Model() + kModelBody + kHolderNode);
    const u32 count = s_partCount;
    CameraBind(context, reinterpret_cast<u32>(s_camera), 1);
    *reinterpret_cast<volatile u32 *>(context + kContextCacheB) = 0;
    *reinterpret_cast<volatile u32 *>(context + kContextCacheA) = 0;
    for (u32 layer = 0; layer <= 3; ++layer) {
        DrawNodeLayer(body, layer, drawContext);
        for (u32 i = 0; i < count && i < kMaxParts; ++i) {
            const u32 holder = s_parts[i];
            if (IsHeap(holder))
                DrawNodeLayer(R32(holder + kHolderNode), layer, drawContext);
        }
    }
    CameraBind(context, camera, 1);
}

bool InstallLateHook(void) {
    if (s_lateHooked)
        return true;
    u8 *stub = reinterpret_cast<u8 *>(LateStub::kAddress);
    for (u32 i = 0; i < LateStub::kSize; ++i)
        if (stub[i] != 0)
            return false;                           // 置き場がふさがっている
    if (R32(LateStub::kHookAddress) != LateStub::kHookOriginal)
        return false;
    std::memcpy(stub, LateStub::kBytes, LateStub::kSize);
    *reinterpret_cast<u32 *>(stub + LateStub::kCallbackOffset) = reinterpret_cast<u32>(&PlayerCloneLatePass);
    CTRPluginFramework::GuiMenu::FlushMemory(LateStub::kAddress, LateStub::kSize);
    *reinterpret_cast<u32 *>(LateStub::kHookAddress) = LateStub::kHookWord;
    CTRPluginFramework::GuiMenu::FlushMemory(LateStub::kHookAddress, 4);
    s_lateHooked = true;
    return true;
}

}  // namespace

void FrameStep(void) {
    const u32 stage = s_stage;
    if (!s_want) {
        if (stage == kCreating || stage == kLive) {
            s_stage = kDestroying;
            s_frames = 0;
        } else if (stage == kDestroying) {
            StepDestroy();
        }
        return;
    }
    switch (stage) {
    case kOff:
        s_fail = 0;
        s_submits = 0;
        s_stage = kCreating;
        StepCreate();
        return;
    case kCreating:
        StepCreate();
        return;
    case kLive:
        StepLive();
        return;
    case kDestroying:
        StepDestroy();
        return;
    default:
        return;                                     // 失敗: 切って入れ直すまで何もしない
    }
}

bool Show(void) {
    if (!s_hooked) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep))
            return false;
        s_hooked = true;
    }
    if (!InstallLateHook())
        return false;
    if (s_stage == kFailed && !s_constructed)
        s_stage = kOff;
    s_want = true;
    return true;
}

void Hide(void) {
    s_want = false;
    if (s_stage == kFailed && !s_constructed)
        s_stage = kOff;
}

bool IsShown(void) {
    return s_want;
}

void SetScreen(bool on, s32 yaw, s32 x, s32 y, s32 zoom) {
    s_yaw = yaw;
    s_pixelX = x;
    s_pixelY = y;
    s_zoom = zoom;
    s_screen = on;
}

void Shutdown(void) {
    if (!s_lateHooked)
        return;
    // コールバックを先に 0 にし、フックを戻してから置き場を消す（描画スレッドが中にいるかもしれない）
    *reinterpret_cast<u32 *>(LateStub::kAddress + LateStub::kCallbackOffset) = 0;
    CTRPluginFramework::GuiMenu::FlushMemory(LateStub::kAddress + LateStub::kCallbackOffset, 4);
    *reinterpret_cast<u32 *>(LateStub::kHookAddress) = LateStub::kHookOriginal;
    CTRPluginFramework::GuiMenu::FlushMemory(LateStub::kHookAddress, 4);
    svcSleepThread(100000000ull);
    std::memset(reinterpret_cast<void *>(LateStub::kAddress), 0, LateStub::kSize);
    CTRPluginFramework::GuiMenu::FlushMemory(LateStub::kAddress, LateStub::kSize);
    s_lateHooked = false;
}

void SetHair(s32 style, s32 color) {
    s_hairStyle = style;
    s_hairColor = color;
}

Status Read(void) {
    Status s;
    s.stage = s_stage;
    s.failReason = s_fail;
    s.frames = s_frames;
    s.submits = s_submits;
    s.hair = s_constructed ? R8(Model() + kModelHair) : 0;
    s.hairColor = s_constructed ? R32(Model() + kModelHairColor) : 0;
    s.lateDraws = s_lateDraws;
    s.parts = s_partCount;
    return s;
}

const char *StageName(u32 stage) {
    switch (stage) {
    case kOff: return u8"無し";
    case kCreating: return u8"作成中";
    case kLive: return u8"表示中";
    case kDestroying: return u8"片付け中";
    case kFailed: return u8"失敗";
    default: return u8"?";
    }
}

const char *FailName(u32 reason) {
    switch (reason) {
    case 1: return u8"プレイヤーがいない";
    case 2: return u8"プロフィールが無い";
    case 3: return u8"作成が終わらない";
    case 4: return u8"場面が変わった";
    case 5: return u8"部品の空き枠が無い";
    default: return u8"";
    }
}

}  // namespace PlayerClone

// ---------------------------------------------------------------------------------------
// gohan のメニューとの結び付け（gohan.md「テスト」フォルダ）
// ---------------------------------------------------------------------------------------

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            int             g_pcShowIndex = -1;
            int             g_pcHairIndex = -1;
            int             g_pcColorIndex = -1;
            int             g_pcScreenIndex = -1;
            int             g_pcYawIndex = -1;
            int             g_pcXIndex = -1;
            int             g_pcYIndex = -1;
            int             g_pcZoomIndex = -1;
            bool            g_pcScreenOn;

            s32     Applied(int index, s32 fallback)
            {
                return index >= 0 ? GuiMenu::ItemApplied(index) : fallback;
            }

            void    ScreenApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                PlayerClone::SetScreen(g_pcScreenOn, Applied(g_pcYawIndex, 0), Applied(g_pcXIndex, 330),
                                       Applied(g_pcYIndex, 150), Applied(g_pcZoomIndex, 60));
            }

            bool    ScreenIsActive(int index)
            {
                (void)index;
                return g_pcScreenOn;
            }

            void    ScreenSetActive(int index, bool active)
            {
                g_pcScreenOn = active;
                ScreenApplied(index, 0);
            }

            const GuiMenu::ToggleEffectFuncs kScreenFuncs = { ScreenIsActive, ScreenSetActive };

            void    HairApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                PlayerClone::SetHair(g_pcHairIndex >= 0 ? GuiMenu::ItemApplied(g_pcHairIndex) : -1,
                                     g_pcColorIndex >= 0 ? GuiMenu::ItemApplied(g_pcColorIndex) : -1);
            }

            bool    CloneIsActive(int index)
            {
                (void)index;
                return PlayerClone::IsShown();
            }

            void    CloneSetActive(int index, bool active)
            {
                (void)index;
                if (!active)
                {
                    PlayerClone::Hide();
                    return;
                }
                HairApplied(0, 0);
                ScreenApplied(0, 0);
                if (!PlayerClone::Show())
                    GuiNotification::NotifyRed(kPcShow, u8"フックを入れられない");
            }

            const GuiMenu::ToggleEffectFuncs kCloneFuncs = { CloneIsActive, CloneSetActive };

            void    CloneStatus(int index)
            {
                (void)index;
                const PlayerClone::Status s = PlayerClone::Read();
                static char message[96];

                if (s.failReason != 0u)
                    std::snprintf(message, sizeof(message), u8"%s: %s", PlayerClone::StageName(s.stage),
                                  PlayerClone::FailName(s.failReason));
                else
                    std::snprintf(message, sizeof(message), u8"%s %luF 髪%u/%lu 部品%lu 描%lu", PlayerClone::StageName(s.stage),
                                  (unsigned long)s.frames, (unsigned)s.hair, (unsigned long)s.hairColor,
                                  (unsigned long)s.parts, (unsigned long)s.lateDraws);
                GuiNotification::Notify(kPcStat, message);
            }
        }

        void    WirePlayerClone(void)
        {
            g_pcShowIndex = GuiMenu::FindItem(kPcShow);
            g_pcHairIndex = GuiMenu::FindItem(kPcHair);
            g_pcColorIndex = GuiMenu::FindItem(kPcColor);
            const int statIndex = GuiMenu::FindItem(kPcStat);

            if (g_pcShowIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_pcShowIndex, &kCloneFuncs);
            if (g_pcHairIndex >= 0)
                GuiMenu::RegisterApply(g_pcHairIndex, HairApplied);
            if (g_pcColorIndex >= 0)
                GuiMenu::RegisterApply(g_pcColorIndex, HairApplied);
            if (statIndex >= 0)
                GuiMenu::RegisterExecute(statIndex, CloneStatus);
            g_pcScreenIndex = GuiMenu::FindItem(kPcScreen);
            g_pcYawIndex = GuiMenu::FindItem(kPcYaw);
            g_pcXIndex = GuiMenu::FindItem(kPcX);
            g_pcYIndex = GuiMenu::FindItem(kPcY);
            g_pcZoomIndex = GuiMenu::FindItem(kPcZoom);
            if (g_pcScreenIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_pcScreenIndex, &kScreenFuncs);
            const int values[] = { g_pcYawIndex, g_pcXIndex, g_pcYIndex, g_pcZoomIndex };
            for (int v : values)
                if (v >= 0)
                    GuiMenu::RegisterApply(v, ScreenApplied);
        }
    }
}
