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
// ★画面に固定でも Scene 0 へは積む（IDA-opus-5.5-F032）。部品の骨格のワールド姿勢とスキニング行列は Scene の更新
//   （nwgfx_SceneUpdater_UpdateAll 0x490818 → sub_7375A8 / sub_49009C → sub_737378）が計算するので、積まないとズボン（膝で曲がる
//   スムーズスキニング）が止まる（実機 2026-09-24）。ゲームのカメラの far（1750）より遠くに置いて世界では見えないようにし、
//   同じ位置を late pass の専用カメラから見て描く。
const float kHideDepth = -5000.0f;
const float kCameraDistance = 120.0f;       // 複製までの距離（near 50 より遠いこと）
const u32 kCameraBytes = 488;               // view +328（48 B）と projection +424（64 B）が収まる大きさ

// ---- 複製だけのライト（IDA-opus-5.5-F033）------------------------------------------------------------
// 描画の文脈 +64 がライトの状態。BindMaterial 0x494B4C → sub_49586C が材質の組の番号（ResMaterial +664）で sub_49AD20 を呼び、
// 組の表（+64+144 の先の 4 本。Render_DrawSceneIndexed が BsLightMgr の組 dword_98529C を入れる）から アンビエント（組 +12）・
// 半球（+16）・フラグメント（+68..+72）を写す。番号が前と同じなら何もしない（+64+156 が前の番号）。
// フラグメントの向きは sub_494E20 が文脈 +164 のカメラ（ゲームのカメラ）の view で変換し、半球の向きは sub_495624 が同じ view で
// 変換する。複製は専用カメラの view（回転なし）で法線を出すので、ゲームのライトのままだと光の向きが食い違う（利用者:
// 「左下から小さいポイントライト」）。描く間だけ組の表を自前の組に差し替え、終わったら戻して sub_49AE18（状態の初期化。
// 番号 -1・変更印を立てる。Render_DrawSceneIndexed の頭 sub_4EFA00 → sub_49489C も同じものを呼ぶ）で次の材質に取り直させる。
// 自前のライトは今の組のライトを写したもの（vtable などはそのまま）で、色と向きだけを変える。
typedef void (*LightStateResetFn)(u32 state);
const LightStateResetFn LightStateReset = reinterpret_cast<LightStateResetFn>(0x0049AE18);
const u32 kContextLightState = 64;
const u32 kContextActiveCamera = 164;       // sub_4EFA00 が **(文脈 +172) を入れる。ライトの向きはこのカメラの view（+328）で変換
// ★sub_49AE18 はライトの状態の後ろの文脈 +160（霧）・+164（カメラ）・+184 も 0 / 0 / -1 にする（state+96 / +100 / +120）。
//   ゲームは直後の sub_4EFA00 でカメラを入れ直す。入れ直さずに描くと sub_494E20 が view を 0+328 から読んで落ちた（実機 SIGSEGV 0x495998）
const u32 kContextFog = 160;
const u32 kContextStateTail = 184;
const u32 kLightStateSets = 144;            // 組の表（LightSet* を 4 本）へのポインタ
const u32 kLightSetCount = 4;
const u32 kSetAmbient = 12;
const u32 kSetHemi = 16;
const u32 kSetFragBegin = 68;
const u32 kSetFragEnd = 72;
const u32 kSetBytes = 0x60;
const u32 kLightRes = 8;                    // nw ライト +8 = 資源
const u32 kLightLink = 12;                  // 半球: 0 でなく資源の印 bit1 が立つと vtbl+20 で行列を取る。自前では 0
const u32 kFragDirection = 380;             // FragmentLight: ワールドの向き（sub_494E20 が -(view * dir) を送る）
const u32 kResKind = 184;                   // 0 = 方向光
const u32 kResFragColors = 0xFC;            // アンビエント・ディフューズ・スペキュラ 0・1（0xAABBGGRR。sub_4AFA0C が材質の色と掛ける）
const u32 kResAmbientColor = 200;           // AmbientLight（sub_4AFA0C: エミッション + これ * 材質のアンビエント）
const u32 kResHemiGround = 184;             // HemiSphereLight: float4 地面色（頂点シェーダ c22）
const u32 kResHemiSky = 200;                // float4 空の色（c23）
const u32 kResFlags = 24;                   // 資源の印（写し元はどれも 1。bit1 = 半球の行列 / 片面、bit2 = スポット・距離減衰）
const u32 kResFlagsDefault = 1;
const u32 kResEnabled = 180;                // u8
const u32 kVtblAmbientLight = 0x008FB6B4;   // vtbl_nw_gfx_AmbientLight（写し元が無いとき）
const u32 kVtblFragmentLight = 0x008FB7A4;  // vtbl_nw_gfx_FragmentLight
const u32 kVtblHemiLight = 0x008FB8FC;      // vtbl_nw_gfx_HemiSphereLight
const u32 kFragObjBytes = 0x190;
const u32 kFragResBytes = 0x110;            // +280/+284 の相対位置（スポット・距離減衰）は写さない（方向光は読まない）
const u32 kAmbObjBytes = 0x20;
const u32 kAmbResBytes = 0xD0;
const u32 kHemiObjBytes = 0x20;
const u32 kHemiResBytes = 0xF0;
// 色（明るさ 100% のとき）。正面の面でおよそ 0.35 + 0.15 + 0.5 = 1.0、横で 0.7、裏で 0.5 になる（材質の色がさらに掛かる）
const float kLitAmbient = 0.35f;            // AmbientLight
const float kLitFragAmbient = 0.15f;
const float kLitDiffuse = 0.50f;
const float kLitSpecular = 0.15f;
const float kLitHemi = 0.20f;               // 空と地面を同じ色にして向きの影響を無くす
// 光の来る向き（専用カメラの view。x = 右、y = 上、z = カメラ側）。正面やや左下から
const float kLitToLight[3] = { -0.30f, -0.40f, 1.0f };     // 利用者: 常に左下から当たって見えてほしい

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
volatile s32 s_pitch = 45;                  // 度（X 軸まわり。利用者: 45 度ぐらいがちょうどいい）
volatile s32 s_pixelX = 330;                // 上画面のピクセル（400x240）。複製の足元付近が来る位置ではなく、カメラの中心
volatile s32 s_pixelY = 150;
volatile s32 s_zoom = 60;                   // 百分率
u8 s_camera[kCameraBytes] __attribute__((aligned(8)));
u32 s_parts[kMaxParts];                     // このフレームに部品の表から抜き取った holder（+4 がノード）
volatile u32 s_partCount;
volatile bool s_lateReady;                  // s_parts が今の複製のもの
volatile u32 s_lateDraws;
bool s_lateHooked;
volatile s32 s_bright = 130;                // 複製のライトの明るさ（百分率。利用者: 130 がちょうど良い）
volatile u32 s_litTemplates;                // 最後に写し元にできたライト（1 アンビエント・2 半球・4 方向光。0 = 全部一から）
u32 s_litSets[kLightSetCount];
volatile u32 s_litDraws;                    // 自前のライトで描いた回数
u32 s_litSet[kSetBytes / 4];
u32 s_litFragList[1];
u32 s_litFragObj[kFragObjBytes / 4];
u32 s_litFragRes[0x120 / 4];
u32 s_litAmbObj[kAmbObjBytes / 4];
u32 s_litAmbRes[kAmbResBytes / 4];
u32 s_litHemiObj[kHemiObjBytes / 4];
u32 s_litHemiRes[0x100 / 4];

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

// -1 は「本物のまま」: 本物のプレイヤーの PlayerModel の値を毎フレーム写す（以前は最後に書いた値が残った。利用者報告）
void ApplyHair(void) {
    const u32 real = s_player + kActorModel;
    const s32 style = s_hairStyle >= 0 ? s_hairStyle : (s32)R8(real + kModelHair);
    const s32 color = s_hairColor >= 0 ? s_hairColor : (s32)R32(real + kModelHairColor);
    if (style >= 0 && style < 0x22 && R8(Model() + kModelHair) != (u8)style)
        *reinterpret_cast<volatile u8 *>(Model() + kModelHair) = (u8)style;
    if (color >= 0 && color < 0x10 && R32(Model() + kModelHairColor) != (u32)color)
        *reinterpret_cast<volatile u32 *>(Model() + kModelHairColor) = (u32)color;
}

// 画面に固定のとき: (0, kHideDepth, 0) に置き、RotX(傾き) * RotY(向き)。傾きが正だと頭が専用カメラ（+Z）のほうへ倒れる
void PoseScreen(void) {
    const float y = (float)s_yaw * (3.14159265f / 180.0f);
    const float x = (float)s_pitch * (3.14159265f / 180.0f);
    const float cy = std::cos(y), sy = std::sin(y), cx = std::cos(x), sx = std::sin(x);
    // RotX = [1 0 0; 0 cx -sx; 0 sx cx]、RotY = [cy 0 sy; 0 1 0; -sy 0 cy]
    const float m[12] = {
        cy, 0.0f, sy, 0.0f,
        sx * sy, cx, -sx * cy, kHideDepth,
        -cx * sy, sx, cx * cy, 0.0f,
    };
    SetMatrix(s_model, m);
}

// vtbl[5] が管理役の部品の表へ積んだ分を読んで s_parts へ写す（表には残す。Scene 0 の更新で骨格とスキニングを計算させる）
void ReadParts(const u32 *before) {
    const u32 lists = R32(kPlayerMgrPtr) + kPartListBase;
    u32 n = 0;
    for (u32 k = 0; k < kPartLists; ++k) {
        const u32 list = lists + kPartListStride * k;
        const u32 count = R32(list + kPartListCount);
        for (u32 i = before[k]; i < count && i < kPartListMax; ++i)
            if (n < kMaxParts)
                s_parts[n++] = R32(list + 4 * i);
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
        ReadParts(before);
        Submit(reinterpret_cast<void *>(Model() + kModelBody), 0);
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
    const float v[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -(kHideDepth + kCameraHeight),
                          0.0f, 0.0f, 1.0f, -kCameraDistance };
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

u32 LitByte(float v) {
    const float s = v * (float)s_bright / 100.0f * 255.0f + 0.5f;
    return s <= 0.0f ? 0u : s >= 255.0f ? 255u : (u32)s;
}

u32 LitColor(float v) {
    const u32 c = LitByte(v);
    return 0xFF000000u | (c << 16) | (c << 8) | c;      // 0xAABBGGRR
}

void Copy(u32 *dst, u32 src, u32 bytes) {
    for (u32 i = 0; i < bytes / 4; ++i)
        dst[i] = R32(src + 4 * i);
}

// 自前の組を作る。写し元（今の組のライト）があれば写して色と向きだけ変え、無ければ（屋外など。利用者: 屋内と屋外で光の向きが
// 違った）ゲームのライトのクラスの vtable を持つ 0 埋めの物を一から作る。読まれる欄は資源 +24 印・+180 有効・+184 種類・色・
// 物 +8 資源・+12（半球）・+380 向きだけ（LightState_BindSet / RenderContext_SendFragmentLight / SendHemiLight /
// MaterialLight_SendColors / gfx_MaterialActivator_Activate の静的確認）。戻り値は写し元のビット（1 アンビエント・2 半球・4 方向光）
u32 FindTemplates(u32 context, u32 &amb, u32 &hemi, u32 &frag) {
    amb = hemi = frag = 0;
    const u32 sets = R32(context + kContextLightState + kLightStateSets);
    if (!IsHeap(sets))
        return 0;
    for (u32 k = 0; k < kLightSetCount; ++k) {
        const u32 set = R32(sets + 4 * k);
        if (!IsHeap(set))
            continue;
        if (amb == 0u && IsHeap(R32(set + kSetAmbient)) && IsHeap(R32(R32(set + kSetAmbient) + kLightRes)))
            amb = R32(set + kSetAmbient);
        if (hemi == 0u && IsHeap(R32(set + kSetHemi)) && IsHeap(R32(R32(set + kSetHemi) + kLightRes)))
            hemi = R32(set + kSetHemi);
        const u32 b = R32(set + kSetFragBegin), e = R32(set + kSetFragEnd);
        if (frag == 0u && IsHeap(b) && e > b && IsHeap(R32(b)) && IsHeap(R32(R32(b) + kLightRes)))
            frag = R32(b);
    }
    return (amb ? 1u : 0u) | (hemi ? 2u : 0u) | (frag ? 4u : 0u);
}

void MakeLight(u32 *obj, u32 objBytes, u32 *res, u32 resBytes, u32 tmpl, u32 copyBytes, u32 vtable) {
    std::memset(obj, 0, objBytes);
    std::memset(res, 0, resBytes);
    if (tmpl != 0u) {
        Copy(obj, tmpl, objBytes);
        Copy(res, R32(tmpl + kLightRes), copyBytes);
    } else {
        obj[0] = vtable;
        res[kResFlags / 4] = kResFlagsDefault;
        reinterpret_cast<u8 *>(res)[kResEnabled] = 1;
    }
    obj[kLightRes / 4] = reinterpret_cast<u32>(res);
}

bool BuildLights(u32 context) {
    const u32 gameCamera = R32(context + kContextActiveCamera);
    if (!IsHeap(gameCamera))
        return false;
    u32 amb, hemi, frag;
    s_litTemplates = FindTemplates(context, amb, hemi, frag);

    std::memset(s_litSet, 0, sizeof(s_litSet));
    u8 *set = reinterpret_cast<u8 *>(s_litSet);
    MakeLight(s_litAmbObj, sizeof(s_litAmbObj), s_litAmbRes, sizeof(s_litAmbRes), amb, kAmbResBytes, kVtblAmbientLight);
    *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(s_litAmbRes) + kResAmbientColor) = LitColor(kLitAmbient);
    *reinterpret_cast<u32 *>(set + kSetAmbient) = reinterpret_cast<u32>(s_litAmbObj);

    MakeLight(s_litHemiObj, sizeof(s_litHemiObj), s_litHemiRes, sizeof(s_litHemiRes), hemi, kHemiResBytes, kVtblHemiLight);
    {
        u8 *res = reinterpret_cast<u8 *>(s_litHemiRes);
        const float h = (float)LitByte(kLitHemi) / 255.0f;
        const float c[4] = { h, h, h, 1.0f };
        std::memcpy(res + kResHemiGround, c, sizeof(c));
        std::memcpy(res + kResHemiSky, c, sizeof(c));       // 空と地面が同じなので向きと補間は効かない
        s_litHemiObj[kLightLink / 4] = 0;
        *reinterpret_cast<u32 *>(set + kSetHemi) = reinterpret_cast<u32>(s_litHemiObj);
    }

    MakeLight(s_litFragObj, sizeof(s_litFragObj), s_litFragRes, sizeof(s_litFragRes), frag, kFragResBytes, kVtblFragmentLight);
    {
        u8 *res = reinterpret_cast<u8 *>(s_litFragRes);
        *reinterpret_cast<u32 *>(res + kResKind) = 0;
        u32 *colors = reinterpret_cast<u32 *>(res + kResFragColors);
        colors[0] = LitColor(kLitFragAmbient);
        colors[1] = LitColor(kLitDiffuse);
        colors[2] = LitColor(kLitSpecular);
        colors[3] = LitColor(kLitSpecular);
        // 送られるのは -(V * dir)（V = ゲームのカメラの view の回転）。これを専用カメラの view での光の向き L にしたいので
        // dir = -V^T L（view は正規直交）
        const float *v = reinterpret_cast<const float *>(gameCamera + kCameraView);
        const float lx = kLitToLight[0], ly = kLitToLight[1], lz = kLitToLight[2];
        const float n = std::sqrt(lx * lx + ly * ly + lz * lz);
        float *dir = reinterpret_cast<float *>(reinterpret_cast<u8 *>(s_litFragObj) + kFragDirection);
        for (u32 j = 0; j < 3; ++j)
            dir[j] = -(v[j] * lx + v[4 + j] * ly + v[8 + j] * lz) / n;
        s_litFragList[0] = reinterpret_cast<u32>(s_litFragObj);
    }
    *reinterpret_cast<u32 *>(set + kSetFragBegin) = reinterpret_cast<u32>(&s_litFragList[0]);
    *reinterpret_cast<u32 *>(set + kSetFragEnd) = reinterpret_cast<u32>(&s_litFragList[1]);
    for (u32 k = 0; k < kLightSetCount; ++k)
        s_litSets[k] = reinterpret_cast<u32>(s_litSet);
    return true;
}

// ライトの状態を初期化する（次の材質でライトを送り直させる）。巻き添えで消える霧・カメラ・+184 は元の値へ戻す
void ResetLights(u32 context) {
    const u32 fog = R32(context + kContextFog);
    const u32 camera = R32(context + kContextActiveCamera);
    const u32 tail = R32(context + kContextStateTail);
    LightStateReset(context + kContextLightState);
    *reinterpret_cast<volatile u32 *>(context + kContextFog) = fog;
    *reinterpret_cast<volatile u32 *>(context + kContextActiveCamera) = camera;
    *reinterpret_cast<volatile u32 *>(context + kContextStateTail) = tail;
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
    // 組の表へのポインタ（文脈 +64+144）を自前の 4 本へ向ける（ゲームの配列には書かない。屋外で空でも効く）
    volatile u32 *setsPtr = reinterpret_cast<volatile u32 *>(context + kContextLightState + kLightStateSets);
    const u32 savedSets = *setsPtr;
    const bool lit = BuildLights(context);
    if (lit) {
        *setsPtr = reinterpret_cast<u32>(s_litSets);
        ResetLights(context);
        ++s_litDraws;
    }
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
    if (lit) {
        *setsPtr = savedSets;
        ResetLights(context);
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

void SetBrightness(s32 percent) {
    s_bright = percent;
}

void SetScreen(bool on, s32 yaw, s32 pitch, s32 x, s32 y, s32 zoom) {
    s_yaw = yaw;
    s_pitch = pitch;
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
    s.litDraws = s_litDraws;
    s.litTemplates = s_litTemplates;
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
            int             g_pcPitchIndex = -1;
            int             g_pcXIndex = -1;
            int             g_pcYIndex = -1;
            int             g_pcZoomIndex = -1;
            int             g_pcLightIndex = -1;
            bool            g_pcScreenOn;

            s32     Applied(int index, s32 fallback)
            {
                return index >= 0 ? GuiMenu::ItemApplied(index) : fallback;
            }

            void    ScreenApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                PlayerClone::SetScreen(g_pcScreenOn, Applied(g_pcYawIndex, 0), Applied(g_pcPitchIndex, 45),
                                       Applied(g_pcXIndex, 330), Applied(g_pcYIndex, 150), Applied(g_pcZoomIndex, 60));
                PlayerClone::SetBrightness(Applied(g_pcLightIndex, 130));
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
                    std::snprintf(message, sizeof(message), u8"%s %luF 髪%u/%lu 部品%lu 描%lu 光%lu/%lu",
                                  PlayerClone::StageName(s.stage), (unsigned long)s.frames, (unsigned)s.hair,
                                  (unsigned long)s.hairColor, (unsigned long)s.parts, (unsigned long)s.lateDraws,
                                  (unsigned long)s.litDraws, (unsigned long)s.litTemplates);
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
            g_pcPitchIndex = GuiMenu::FindItem(kPcPitch);
            g_pcXIndex = GuiMenu::FindItem(kPcX);
            g_pcYIndex = GuiMenu::FindItem(kPcY);
            g_pcZoomIndex = GuiMenu::FindItem(kPcZoom);
            g_pcLightIndex = GuiMenu::FindItem(kPcLight);
            if (g_pcScreenIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_pcScreenIndex, &kScreenFuncs);
            const int values[] = { g_pcYawIndex, g_pcPitchIndex, g_pcXIndex, g_pcYIndex, g_pcZoomIndex, g_pcLightIndex };
            for (int v : values)
                if (v >= 0)
                    GuiMenu::RegisterApply(v, ScreenApplied);
        }
    }
}
