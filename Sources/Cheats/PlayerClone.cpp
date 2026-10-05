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
#include <cstdlib>

namespace PlayerClone {

namespace {

// ---- ゲームの関数（IDA-opus-5.5-F028 / F029）------------------------------------------------------
typedef void (*CtorFn)(void *pm);
typedef u32 (*CreateFromProfileFn)(void *pm, u32 profile, u32 a3, u32 maskBit, u32 a5, u32 a6);
typedef u32 (*CreateStepFn)(void *pm, u32 record, u32 isBoy, u32 a4, u32 maskBit, u32 a6, u32 a7);
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
// CreateFromProfile の中身 = CreateStep(pm, profile + 4, !(profile+21946 & 1), a3, maskBit, a5, a6)（IDA-opus-5.5-F028）
const CreateStepFn CreateStep = reinterpret_cast<CreateStepFn>(0x001CF0D8);
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
const EntryFn FaceCancel = reinterpret_cast<EntryFn>(0x002711A4);              // PlayerModel_DestroyStep が顔の枠に先に呼ぶ（PlayerFaceSlot_Unload）
// プレイヤー本人の目の形を差し替える（IDA-opus-5.5-F086）
typedef void (*AcquireFn)(u32 table, u32 *pair);
typedef u32 (*FaceLoadFn)(u32 slot, u32 record, u32 isBoy, u32 variant);
const AcquireFn  BankAcquire  = reinterpret_cast<AcquireFn>(0x002137CC);         // BankTable_Acquire(表, {表, 番号} の組)
const FaceLoadFn FaceLoad     = reinterpret_cast<FaceLoadFn>(0x002711AC);        // PlayerFaceSlot_RequestLoad(枠, 記録, 男の子か, a/b)。読めたら 1
typedef void (*PlayerUpdateFn)(u32 player);
const PlayerUpdateFn PlayerUpdate = reinterpret_cast<PlayerUpdateFn>(0x00689DDC);  // vc_UPDATE: 記録を pm へ写し、部品の更新の印（+371 |= 0x28）を立てる
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
// ★法線は頂点シェーダの c3〜c5 で視点の空間へ回る。c3〜c5 は描画ごとに sub_495A10 / sub_495538 がノード +444 から送る
//   （c0〜c2 = ノード +140 か単位行列）。+444 は Scene の更新がゲームのカメラで計算した model-view なので、そのままだと法線は
//   ゲームのカメラ基準になり、専用カメラ基準で送る光と食い違う（屋外はカメラの上下が逆で暗く平らになった。実機 2026-09-25、
//   IDA-opus-5.5-F033）。描く間だけ +444 を V_専用 * V_ゲーム^-1 * (+444) に置き換え、描いた後に戻す。
const u32 kNodeModelView = 444;

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
// ライトの設定（メニューで変える。利用者: gohan 側で調整して良い当たり方を探る）。既定は下の表。
// 色は百分率（明るさ 100% のとき。材質の色がさらに掛かる）、向きは百分率の成分（専用カメラの view。x = 右、y = 上、z = カメラ側。
// 長さは正規化する。0 ベクトルなら正面）
enum LightParam : u32 {
    kLpDirX = 0, kLpDirY, kLpDirZ,          // 光の来る向き
    kLpAmbient,                             // AmbientLight（大域アンビエント = これ * 材質のアンビエント）
    kLpFragAmbient,                         // 方向光のアンビエント
    kLpDiffuse,                             // 方向光のディフューズ
    kLpSpecular,                            // 方向光のスペキュラ 0・1
    kLpHemi,                                // 半球（空と地面を同じ色にして向きの影響を無くす）
    kLpCount
};
const s32 kLightDefaults[kLpCount] = { -30, -40, 100, 35, 15, 50, 15, 20 };

const u32 kPlayerPtr = 0x00AA7994;          // 通信番号ごとの AcPlayer* の表の先頭（0 番目 = ホスト。自分は LocalPlayer()。F091）
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
const u32 kModelEyeColor = 556;             // u32。頭の読み込みキー（PlayerHeadKey_Build 0x329994 が pm+548 の [4..11] = 髪色・目の色を入れる）→ 変えれば頭が読み直される（IDA-opus-5.5-F085）
const u32 kModelTan = 448;                  // 日焼けの段階 0..7（PlayerModel_UpdateParts / sub_1D05B0 が毎フレーム肌の色にする。IDA-opus-5.5-F083）
const u32 kModelOutfit = 512;               // 服の欄（Item 4 B ずつ）。0 = 帽子、1 = アクセサリー（sub_719AF0 / sub_719B24(pm+512)。F083）
const u32 kEmptyItem = 0x00955FF4;
// ---- 半透明で描く（フェード。IDA-opus-5.5-F084）-------------------------------------------------------
// gfx_DrawMesh 0x48D26C を写し、行列（sub_4957C4）と形（gfx_DrawShapePrimitives）の間に PICA の命令を積む:
//   フレームバッファの読み書き（gfx_MaterialActivator_Activate 0x49C08C の末尾と同じ 40 B。0x111 フラッシュ・0x110 無効化・0x112..0x115、
//   色の読み = 15、残り 3 つはエンジンの今の値 dword_96F088 / 96F08C / 96F090）と、合成（0x100..0x103 = COLOR_OPERATION 0xE40100・
//   BLEND_FUNC 色 = 元 x 定数α + 先 x (1 - 定数α)・LOGIC_OP 3・BLEND_COLOR α<<24。IDA-gpt-6-astra-F002 の木の半透明と同じ値）。
typedef void (*BindMaterialFn)(u32 context, u32 zero);
typedef void (*MeshMatrixFn)(u32 context, u32 mesh);
typedef void (*DrawShapeFn)(u32 drawContext, u32 node, u32 shape, u32 count);
typedef void (*CopyBytesFn)(u32 dst, u32 src, u32 bytes);
const BindMaterialFn BindMaterial = reinterpret_cast<BindMaterialFn>(0x00494B4C);   // gfx_RenderContext_BindMaterial
const MeshMatrixFn   MeshMatrix   = reinterpret_cast<MeshMatrixFn>(0x004957C4);
const DrawShapeFn    DrawShape    = reinterpret_cast<DrawShapeFn>(0x0048D51C);      // gfx_DrawShapePrimitives
const CopyBytesFn    CopyBytes    = reinterpret_cast<CopyBytesFn>(0x0012EC54);      // Mem_CopyBytes
const u32 kGpuCmdPtr = 0x0096EA94;          // g_GpuCmdPtr
const u32 kFbColorWrite = 0x0096F088, kFbDepthRead = 0x0096F08C, kFbDepthWrite = 0x0096F090;
const u32 kContextKeys = 256, kContextKeyCount = 11;   // 材質の部分ごとの前回の鍵（Activate の a2 +256..+296。0 にすると次で送り直す）
const u32 kContextMaterial = 0x20;          // DrawMesh が材質を入れる欄（文脈 +32）          // ItemToPlace（美容室が帽子・アクセサリーを外すときに写す空の品物。PlayerState_AppearanceChangeUpdate 0x6832E8）
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

// 複製の置き場は 2 つ。目の形を変えるときは、もう一方で新しい複製を裏で作り、できたら入れ替えて古いほうを片付ける
// （利用者 2026-10-05: フェードせず即切り替え）。片付け・作成の関数は「今の複製」s_model を一時的に差し替えて使う（UseModel）
// 2 つとも初めて Show するときにヒープから取り、以後は返さない（静的に置くと 3gx の実行部が 2 MiB を超えた。2026-10-06）
u8 *s_modelBuf[2];
u8 *s_model;                                // 今の複製（表示・更新・片付けの対象）。Show より前は nullptr
bool s_constructed;
u8 *s_profileCopies[2];                     // 置き場ごとのプロフィールの写し（記録は表示中も pm+452 と頭の枠から読まれる）
// 裏で作っている新しい複製 / 入れ替えたあと片付けている古い複製
bool s_nextOn, s_oldOn;
u8 *s_next, *s_old;
u32 s_nextFrames, s_oldFrames;
s32 s_nextFace;
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
volatile s32 s_tan = -1;                    // 日焼けの段階 0..7（-1 = 本物のまま）
volatile bool s_hideHead;                   // 帽子・アクセサリーを外して見せる（複製だけ）
volatile u32 s_alpha = 255;
// 目の形（顔のテクスチャ）は作るときにしか読まない（顔の枠は PlayerModel_Setup の PlayerHeadBank_SetSources で頭に結ばれ、二重化されていない）。
// 目の形を変えるときは、プロフィールの写し（プレイヤー 1 人分 = 42,112 B。vc_PLAYER_1_PPOFFSET 0x2FB920 の刻み）の見た目の記録 +2 を書き換え、
// それで作り直す（本物のプロフィールには書かない。IDA-opus-5.5-F085）
const u32 kProfileBytes = 42112, kLookFaceOffset = 4 + 2, kProfileSexByte = 21946;
volatile s32 s_eyeColor = -1;               // 0..5（-1 = 本物のまま）
volatile s32 s_face = -1;                   // 0..11（-1 = 本物のまま）
s32 s_builtFace = -1;                       // 今の複製を作ったときの目の形
u8 *s_profileCopy;                          // 今の複製の写し（s_profileCopies のどちらか）。初めて作るときにヒープから取る（静的に置くと 3gx の実行部が 2 MiB を超える）。以後は返さない                 // 複製の不透明度（画面に固定のとき。255 = ふつうに描く、0 = 描かない）
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
float (*s_savedModelView)[12];              // late pass で置き換えたノード +444 の元の値（1 + kMaxParts 個）。初めて Show するときにヒープから取る（2 MiB。2026-10-06）
u32 s_savedModelViewNode[1 + kMaxParts];
volatile u32 s_partCount;
volatile bool s_lateReady;                  // s_parts が今の複製のもの
volatile u32 s_lateDraws;
bool s_lateHooked;
volatile s32 s_bright = 100;                // 複製のライトの明るさ（百分率。利用者: 法線の直し後は 100 が良い）
volatile s32 s_light[kLpCount] = { -30, -40, 100, 35, 15, 50, 15, 20 };    // kLightDefaults と同じ（検査で突き合わせる）
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

// 自分のプレイヤー = 表の [自分の通信番号]（vc_A_GETONLINEPLAYERINDEX 0x305F6C。オフラインでは 0 = 今までどおり）
typedef u32 (*LocalIndexFn)(void);
const LocalIndexFn LocalPlayerIndex = reinterpret_cast<LocalIndexFn>(0x00305F6C);
u32 LocalPlayer(void) {
    const u32 index = LocalPlayerIndex();
    return index < 4 ? R32(kPlayerPtr + 4 * index) : 0u;
}
u32 Model(void) { return reinterpret_cast<u32>(s_model); }

u32 BufIndex(const u8 *m) { return m == s_modelBuf[1] ? 1u : 0u; }

// 片付け・作成の関数（Model() / s_model を読む）を別の置き場に対して使う間だけ差し替える
struct UseModel {
    u8 *saved;
    explicit UseModel(u8 *m) : saved(s_model) { s_model = m; }
    ~UseModel() { s_model = saved; }
};

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
    return R32(kPlayerMgrPtr) != s_mgr || LocalPlayer() != s_player || R8(kRoomIdByte) != s_room
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
    std::memset(s_model, 0, kModelBytes);
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
    // 目の色: 頭のキーに入るので、変えれば頭が読み直される（F085）
    const s32 eye = s_eyeColor >= 0 ? s_eyeColor : (s32)R32(real + kModelEyeColor);
    if (eye >= 0 && eye < 6 && R32(Model() + kModelEyeColor) != (u32)eye)
        *reinterpret_cast<volatile u32 *>(Model() + kModelEyeColor) = (u32)eye;
    // 肌: 部品の更新が毎フレーム pm+448 から色を作る（F083）
    const s32 tan = s_tan >= 0 ? s_tan : (s32)R32(real + kModelTan);
    if (tan >= 0 && tan < 8 && R32(Model() + kModelTan) != (u32)tan)
        *reinterpret_cast<volatile u32 *>(Model() + kModelTan) = (u32)tan;
    // 頭の小物: 外すときは美容室と同じ空の品物、戻すときは本物の服の欄から（部品の更新が読み直す）
    for (u32 k = 0; k < 2; ++k) {
        const u32 want = s_hideHead ? R32(kEmptyItem) : R32(real + kModelOutfit + 4 * k);
        if (R32(Model() + kModelOutfit + 4 * k) != want)
            *reinterpret_cast<volatile u32 *>(Model() + kModelOutfit + 4 * k) = want;
    }
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
    const u32 player = LocalPlayer();
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
        u8 *&copySlot = s_profileCopies[BufIndex(s_model)];
        if (copySlot == nullptr)
            copySlot = static_cast<u8 *>(std::malloc(kProfileBytes));
        if (copySlot == nullptr) {
            Abandon(2);                             // 写しの置き場が取れない（作る前なので片付けは要らない）
            return;
        }
        s_profileCopy = copySlot;
        std::memset(s_model, 0, kModelBytes);
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
    if (s_frames == 0u) {
        // 作り始め: プロフィールを写し、目の形だけ指定に替える（作っている間と表示中はこの写しを読ませ続ける）
        std::memcpy(s_profileCopy, reinterpret_cast<const void *>(profile), kProfileBytes);
        const s32 face = s_face;
        if (face >= 0 && face < 12)
            s_profileCopy[kLookFaceOffset] = (u8)face;
        s_builtFace = face;
    }
    const u32 copy = reinterpret_cast<u32>(s_profileCopy);
    const u32 isBoy = (R8(copy + kProfileSexByte) & 1u) == 0u ? 1u : 0u;
    if (CreateStep(s_model, copy + 4, isBoy, 0, maskBit, 0, kDollToolParam) != 0u) {
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

// ---- プレイヤー本人の目の形の差し替え（IDA-opus-5.5-F086）--------------------------------------------------
// 顔のテクスチャは頭を読み込むとき（sub_2718FC）に頭の枠の「顔の枠」の欄（+668 = a / +672 = b）から結ばれる。頭は 2 つの読み込み先を
// 交互に使う（sub_7C847C: 今の番号 +36、読み込み済みのキー +64 + 20 x 番号、違えばもう一方へ読んで入れ替える）。そこで:
//   1. 顔の倉庫（管理役 +156）に新しい枠を取り、見た目の記録（プロフィール +4。ApplyToPlayer が目の形を書いた後）から読む
//   2. 頭の枠の顔の欄を新しい枠へ向け、今の読み込み先のキーを壊す → ゲーム自身の毎フレームの部品更新が頭を読み直して入れ替える
//   3. 入れ替わったら本人の pm の組を新しい枠にし、古い顔の枠を返す
// 使用中のもの（表示中の頭と古い顔）は入れ替わるまで触らないので、世界を描いたままでよい。
const u32 kRealFaceTable = 156, kRealHeadTable = 4148, kHeadEntryBytes = 1304;
const u32 kHeadFaceA = 668, kHeadFaceB = 672, kHeadBufIndex = 36, kHeadLoading = 60, kHeadLoadedKeys = 64, kHeadKeyBytes = 20;
const u32 kRealFacePair[2] = { 332, 340 };   // pm の組（a / b。PlayerModel_CreateStep の 0 / 1）
enum RealFaceStage : u32 { kRfIdle, kRfLoad, kRfHead, kRfRelease, kRfAbort };
// 結果（状態の通知に出す）: 1 = 済んだ / 1x = 始められなかった / 2x = 中止した
enum RealFaceResult : u32 { kRfrNone = 0, kRfrDone = 1, kRfrNoPlayer = 10, kRfrNoProfile = 11, kRfrNoHead = 12, kRfrNoFace = 13, kRfrNoRoom = 14,
                            kRfrTimeout = 20, kRfrScene = 21, kRfrTeardown = 22 };
// 本人の部品の更新（PlayerModel vtbl[5] 0x1D2CCC = 頭の読み直しを含む）は、複製と違って毎フレーム呼ばれるとは限らない。vc_UPDATE が
// プレイヤー +371 に印を立てたときに回る見込み（hhd_t018e 実機: 目が変わらず、その後の建物の出入りで無限ロード = 入れ替わり待ちで止まり
// 枠を抱えたままだったと推定）。キーを壊した直後と、進まないとき kRfKickFrames ごとに印を立て直す
const u32 kRfKickFrames = 30, kRfTimeoutFrames = 150;   // 30 fps で 1 秒 / 5 秒
volatile bool s_rfWant;
u32 s_rfStage = kRfIdle;
u32 s_rfMgr, s_rfPm, s_rfPlayer, s_rfRecord, s_rfIsBoy, s_rfHead, s_rfHeadIndex, s_rfFrames;
bool s_rfHas[2];
bool s_rfPointed;                               // 頭の顔の欄を新しい枠へ向けた（中止するときは古い枠へ戻す）
u32 s_rfNew[2][2], s_rfOld[2][2], s_rfOldData[2];
volatile u32 s_rfDone;                          // 差し替え終えた回数（状態の通知用）
volatile u32 s_rfResult, s_rfKicks, s_rfLastFrames, s_rfLastStage;

u32 FaceSlotData(u32 table, u32 index) { return table + kTableEntries + kFaceEntryBytes * index; }

bool ReleasePair(u32 table, u32 *pair) {
    if (pair[0] != table || pair[1] >= R32(table + kBankCount))
        return true;
    FaceCancel(FaceSlotData(table, pair[1]));
    return BankRelease(table, pair) != 0u;
}

void RfFinish(u32 result) {
    s_rfResult = result;
    s_rfLastFrames = s_rfFrames;
    s_rfStage = kRfIdle;
}

// 本人の頭の枠と顔の組がまだ記録どおりか（違えば片付けが始まった・場面が変わった）
bool RfOwnerAlive(void) {
    if (R32(kPlayerMgrPtr) != s_rfMgr || LocalPlayer() != s_rfPlayer)
        return false;
    const u32 heads = s_rfMgr + kBankMgrOffset + kRealHeadTable;
    if (R32(s_rfPm + 348) != heads)
        return false;
    for (u32 k = 0; k < 2; ++k)
        if (s_rfHas[k] && (R32(s_rfPm + kRealFacePair[k]) != s_rfOld[k][0] || R32(s_rfPm + kRealFacePair[k] + 4) != s_rfOld[k][1]))
            return false;
    return true;
}

void RfBeginAbort(u32 result) {
    s_rfResult = result;
    s_rfLastStage = s_rfStage;
    s_rfLastFrames = s_rfFrames;
    s_rfStage = kRfAbort;
}

u32 RfAbortReason(void) {
    return R32(kPlayerMgrPtr) != s_rfMgr || LocalPlayer() != s_rfPlayer ? kRfrScene : kRfrTeardown;
}

void StepRealFace(void) {
    const u32 table = s_rfMgr + kBankMgrOffset + kRealFaceTable;
    switch (s_rfStage) {
    case kRfIdle: {
        if (!s_rfWant)
            return;
        s_rfWant = false;
        s_rfFrames = 0;
        s_rfKicks = 0;
        s_rfLastStage = kRfIdle;
        const u32 player = LocalPlayer(), mgr = R32(kPlayerMgrPtr);
        if (!IsHeap(player) || !IsHeap(mgr))
            return RfFinish(kRfrNoPlayer);
        const u32 profile = PlayerProfile(R8(player + kActorPlayerIndex));
        if (profile == 0u)
            return RfFinish(kRfrNoProfile);
        const u32 pm = player + kActorModel;
        const u32 faces = mgr + kBankMgrOffset + kRealFaceTable;
        const u32 heads = mgr + kBankMgrOffset + kRealHeadTable;
        if (R32(pm + 348) != heads || R32(pm + 352) >= R32(heads + kBankCount))
            return RfFinish(kRfrNoHead);            // 頭の枠が無い（Mii マスクなど）
        u32 need = 0;
        for (u32 k = 0; k < 2; ++k) {
            s_rfHas[k] = R32(pm + kRealFacePair[k]) == faces && R32(pm + kRealFacePair[k] + 4) < R32(faces + kBankCount);
            need += s_rfHas[k] ? 1u : 0u;
        }
        if (need == 0u)
            return RfFinish(kRfrNoFace);
        if ((u32)R16(faces + kBankUsed) + need > R32(faces + kBankCount))
            return RfFinish(kRfrNoRoom);            // 空きが無い: 次に建物を出入りしたときに変わる（記録は書いてある）
        s_rfMgr = mgr;
        s_rfPm = pm;
        s_rfPlayer = player;
        s_rfRecord = profile + 4;
        s_rfIsBoy = (R8(profile + kProfileSexByte) & 1u) == 0u ? 1u : 0u;
        s_rfHead = heads + kTableEntries + kHeadEntryBytes * R32(pm + 352);
        s_rfPointed = false;
        s_rfOldData[0] = R32(s_rfHead + kHeadFaceA);
        s_rfOldData[1] = R32(s_rfHead + kHeadFaceB);
        for (u32 k = 0; k < 2; ++k) {
            s_rfOld[k][0] = R32(pm + kRealFacePair[k]);
            s_rfOld[k][1] = R32(pm + kRealFacePair[k] + 4);
            s_rfNew[k][0] = 0;
            s_rfNew[k][1] = 0xFFFFFFFFu;
            if (s_rfHas[k])
                BankAcquire(faces, s_rfNew[k]);
        }
        s_rfResult = kRfrNone;
        s_rfStage = kRfLoad;
        return;
    }
    case kRfLoad: {
        ++s_rfFrames;
        if (!RfOwnerAlive())
            return RfBeginAbort(RfAbortReason());
        if (s_rfFrames > kRfTimeoutFrames)
            return RfBeginAbort(kRfrTimeout);
        u32 done = 1;
        for (u32 k = 0; k < 2; ++k)
            if (s_rfHas[k])
                done &= FaceLoad(FaceSlotData(table, s_rfNew[k][1]), s_rfRecord, s_rfIsBoy, k);
        if (done == 0u)
            return;
        // 頭の顔の欄を新しい枠へ。今の読み込み先のキーを壊し、部品の更新の印を立てて頭を読み直させる
        if (s_rfHas[0])
            *reinterpret_cast<volatile u32 *>(s_rfHead + kHeadFaceA) = FaceSlotData(table, s_rfNew[0][1]);
        if (s_rfHas[1])
            *reinterpret_cast<volatile u32 *>(s_rfHead + kHeadFaceB) = FaceSlotData(table, s_rfNew[1][1]);
        s_rfPointed = true;
        s_rfHeadIndex = R8(s_rfHead + kHeadBufIndex);
        *reinterpret_cast<volatile u8 *>(s_rfHead + kHeadLoadedKeys + kHeadKeyBytes * (s_rfHeadIndex & 1u)) = 0xFE;
        PlayerUpdate(s_rfPlayer);
        ++s_rfKicks;
        s_rfLastFrames = s_rfFrames;
        s_rfFrames = 0;
        s_rfStage = kRfHead;
        return;
    }
    case kRfHead:
        ++s_rfFrames;
        if (!RfOwnerAlive())
            return RfBeginAbort(RfAbortReason());
        if (R8(s_rfHead + kHeadBufIndex) == s_rfHeadIndex || R8(s_rfHead + kHeadLoading) != 0u) {
            // まだ入れ替わっていない
            if (R8(s_rfHead + kHeadLoading) == 0u && s_rfFrames > kRfTimeoutFrames)
                return RfBeginAbort(kRfrTimeout);
            if (s_rfFrames % kRfKickFrames == 0u) {
                PlayerUpdate(s_rfPlayer);       // 部品の更新が回っていない: 印を立て直す
                ++s_rfKicks;
            }
            return;
        }
        // 入れ替わった: 本人の組を新しい枠にする（以後の片付けは新しい枠を返す）。古い枠は少し待ってから返す
        for (u32 k = 0; k < 2; ++k)
            if (s_rfHas[k]) {
                *reinterpret_cast<volatile u32 *>(s_rfPm + kRealFacePair[k]) = s_rfNew[k][0];
                *reinterpret_cast<volatile u32 *>(s_rfPm + kRealFacePair[k] + 4) = s_rfNew[k][1];
            }
        s_rfLastFrames = s_rfFrames;
        s_rfFrames = 0;
        s_rfStage = kRfRelease;
        return;
    case kRfRelease: {
        if (++s_rfFrames <= kQuietFrames)
            return;
        if (R32(kPlayerMgrPtr) != s_rfMgr) {
            s_rfStage = kRfIdle;                // 管理役ごと消えた（古い枠も一緒に消えている）
            return;
        }
        bool done = true;
        for (u32 k = 0; k < 2; ++k)
            if (s_rfHas[k])
                done = ReleasePair(table, s_rfOld[k]) && done;
        if (done) {
            ++s_rfDone;
            s_rfResult = kRfrDone;
            s_rfStage = kRfIdle;
        }
        return;
    }
    case kRfAbort: {
        // 管理役が消えていれば枠も一緒に消えている（触らない）。残っていれば、頭の顔の欄を古い枠へ戻し（頭がまだ使っている）、取った新しい枠を返す
        if (R32(kPlayerMgrPtr) != s_rfMgr) {
            s_rfStage = kRfIdle;
            return;
        }
        if (s_rfPointed) {
            if (R8(s_rfHead + kHeadLoading) != 0u)
                return;                         // 新しい枠で読み込み中: 終わってから戻す
            if (s_rfHas[0])
                *reinterpret_cast<volatile u32 *>(s_rfHead + kHeadFaceA) = s_rfOldData[0];
            if (s_rfHas[1])
                *reinterpret_cast<volatile u32 *>(s_rfHead + kHeadFaceB) = s_rfOldData[1];
            s_rfPointed = false;
        }
        bool done = true;
        for (u32 k = 0; k < 2; ++k)
            if (s_rfHas[k])
                done = ReleasePair(table, s_rfNew[k]) && done;
        if (done)
            s_rfStage = kRfIdle;
        return;
    }
    }
}

// 目の形が変わった: もう一方の置き場で、写し（目の形を替えたもの）から新しい複製を作り始める。枠が足りなければ false
bool StartNext(void) {
    if (!BanksHaveRoom(s_mgr + kBankMgrOffset))
        return false;
    const u32 profile = PlayerProfile(R8(s_player + kActorPlayerIndex));
    if (profile == 0u)
        return false;
    u8 *next = s_model == s_modelBuf[0] ? s_modelBuf[1] : s_modelBuf[0];
    u8 *&copySlot = s_profileCopies[BufIndex(next)];
    if (copySlot == nullptr)
        copySlot = static_cast<u8 *>(std::malloc(kProfileBytes));
    if (copySlot == nullptr)
        return false;
    std::memcpy(copySlot, reinterpret_cast<const void *>(profile), kProfileBytes);
    const s32 face = s_face;
    if (face >= 0 && face < 12)
        copySlot[kLookFaceOffset] = (u8)face;
    std::memset(next, 0, kModelBytes);
    PlayerModelCtor(next);
    s_next = next;
    s_nextFace = face;
    s_nextFrames = 0;
    s_nextOn = true;
    return true;
}

// 裏の作成を 1 フレーム進め、できたら入れ替える。古いほうは積むのをやめてから kQuietFrames 待って枠を返す
void StepSwap(void) {
    if (s_oldOn) {
        UseModel use(s_old);
        if (++s_oldFrames > kQuietFrames && ReleaseStep()) {
            PlayerModelDtor(s_old);
            std::memset(s_old, 0, kModelBytes);
            s_oldOn = false;
        }
    }
    if (!s_nextOn)
        return;
    const u32 profile = PlayerProfile(R8(s_player + kActorPlayerIndex));
    const u32 maskBit = profile != 0u ? (R8(profile + kProfileMaskByte) >> 6) & 1u : 0u;
    const u32 copy = reinterpret_cast<u32>(s_profileCopies[BufIndex(s_next)]);
    const u32 isBoy = (R8(copy + kProfileSexByte) & 1u) == 0u ? 1u : 0u;
    u32 done;
    {
        UseModel use(s_next);
        done = CreateStep(s_next, copy + 4, isBoy, 0, maskBit, 0, kDollToolParam);
    }
    if (done != 0u) {
        // 入れ替え: このフレームから新しい複製を更新・積む・描く。古いほうは積まれなくなる
        s_old = s_model;
        s_oldFrames = 0;
        s_oldOn = true;
        s_model = s_next;
        s_profileCopy = s_profileCopies[BufIndex(s_next)];
        s_builtFace = s_nextFace;
        s_nextOn = false;
    } else if (++s_nextFrames >= kCreateLimit) {
        s_old = s_next;                             // 作り終わらない: 作りかけを片付ける（枠は返す）
        s_oldFrames = 0;
        s_oldOn = true;
        s_nextOn = false;
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
    StepSwap();
    if (s_face != s_builtFace && !s_nextOn && !s_oldOn && !StartNext()) {
        // 枠に空きが無い（オンラインで人が多いなど）: 片付けて（枠を返して）から写しで作り直す（FrameStep が kOff から作成へ戻す）
        s_lateReady = false;
        s_stage = kDestroying;
        s_frames = 0;
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
        if (R8(kSceneMode) == 1u) {
            // メニューの 3D（Scene 1。上画面の 2D の後に描かれる）のときは Scene 0 が更新されないので、体と部品を Scene 1 にも積む。
            //   骨格・スキニング・ノード +444 は Scene 1 の更新が計算する（Render_DrawSceneIndexed 0x4EEDD8 → sub_4E9EDC。IDA-opus-5.5-F083）
            Submit(reinterpret_cast<void *>(Model() + kModelBody), 1);
            for (u32 i = 0; i < s_partCount && i < kMaxParts; ++i)
                if (IsHeap(s_parts[i]))
                    Submit(reinterpret_cast<void *>(s_parts[i]), 1);
        }
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
    // 入れ替えの途中で片付けになった: 古い複製と作りかけの複製の枠も返す（1 枠でも残すと無限ロード。F030）
    bool others = true;
    if (s_oldOn) {
        UseModel use(s_old);
        if (ReleaseStep()) {
            PlayerModelDtor(s_old);
            std::memset(s_old, 0, kModelBytes);
            s_oldOn = false;
        } else {
            others = false;
        }
    }
    if (s_nextOn) {
        UseModel use(s_next);
        if (ReleaseStep()) {
            PlayerModelDtor(s_next);
            std::memset(s_next, 0, kModelBytes);
            s_nextOn = false;
        } else {
            others = false;
        }
    }
    if (!ReleaseStep() || !others)
        return;
    PlayerModelDtor(s_model);
    std::memset(s_model, 0, kModelBytes);
    s_constructed = false;
    s_stage = s_fail != 0u ? kFailed : kOff;
    s_frames = 0;
}

// ---- late pass（描画スレッド。Render_DrawSceneIndexed の再生直後から）----------------------------------

// gfx_DrawMesh 0x48D26C の写しに、半透明の命令を挟んだもの（alpha < 255 のときだけ使う）
void DrawMeshAlpha(u32 drawContext, u32 mesh, u32 node, u32 alpha) {
    const u32 res = R32(node + 8);
    const u32 rel = R32(res + 0xC8);
    const u32 table = rel ? res + 0xC8 + rel : 0u;
    const u32 slot = table + 4u * R32(mesh + 0x18);
    const u32 rel2 = R32(slot);
    const u32 shape = rel2 ? slot + rel2 : 0u;
    const u32 context = R32(drawContext + 8);
    *reinterpret_cast<volatile u32 *>(context + kContextMaterial) = R32(R32(node + 0x164) + 4u * R32(mesh + 0x1C));
    BindMaterial(context, 0);
    MeshMatrix(context, mesh);
    volatile u32 *cmd = reinterpret_cast<volatile u32 *>(R32(kGpuCmdPtr));
    const u32 words[16] = {
        1u, 0x000F0111u, 1u, 0x000F0110u,
        15u, 0x803F0112u, R32(kFbColorWrite), R32(kFbDepthRead), R32(kFbDepthWrite), 0u,
        0x00E40100u, 0x803F0100u, (12u << 16) | (13u << 20) | (1u << 24) | (13u << 28), 3u, alpha << 24, 0u,
    };
    for (u32 i = 0; i < 16; ++i)
        cmd[i] = words[i];
    *reinterpret_cast<volatile u32 *>(kGpuCmdPtr) = reinterpret_cast<u32>(cmd + 16);
    DrawShape(drawContext, node, shape, R32(mesh + 0x28));
    const u32 n = R32(mesh + 0x6C);
    const u32 dst = R32(kGpuCmdPtr);
    CopyBytes(dst, R32(mesh + 0x68), n);
    *reinterpret_cast<volatile u32 *>(kGpuCmdPtr) = dst + (n & ~3u);
}

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
        const u32 alpha = s_alpha;
        if (alpha >= 255u)
            DrawMesh(drawContext, mesh, node);
        else
            DrawMeshAlpha(drawContext, mesh, node, alpha);
        ++s_lateDraws;
    }
}

// ゲームのカメラの projection から倍率と z の行を取り、画面の位置・大きさ・深度の寄せを掛けた専用カメラを作る。
// LCD は 90 度回っているので、画面 x = 200 * (1 - clip.y / w)、画面 y = 120 * (1 - clip.x / w)（IDA-gpt-6-astra-F003）。
// 世界のカメラ（Scene 0）の投影の倍率と z の行を覚える。メニューの 3D（Scene 1）のカメラは場面によって設定されていないので、
// Scene 1 で描くときはこの値を使う（大きさ・深度の寄せが Scene 0 のときと同じになる）
float s_proj[4];
bool s_projValid;

bool BuildCamera(u32 gameCamera, bool scene1) {
    const float *g = reinterpret_cast<const float *>(gameCamera + kCameraProjection);
    float sy = g[1];                            // 行 0 = [0, sy, *, *]（画面の縦）
    float sx = -g[4];                           // 行 1 = [-sx, 0, *, *]（画面の横）
    float zz = g[10], zw = g[11];               // 行 2 = [0, 0, zz, zw]
    if (scene1) {
        if (!s_projValid)
            return false;
        sy = s_proj[0], sx = s_proj[1], zz = s_proj[2], zw = s_proj[3];
    } else {
        if (!(sy > 0.1f && sx > 0.1f && zw > 0.0f))
            return false;
        s_proj[0] = sy, s_proj[1] = sx, s_proj[2] = zz, s_proj[3] = zw;
        s_projValid = true;
    }
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

float LightPercent(u32 i) {
    return (float)s_light[i] / 100.0f;
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
    *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(s_litAmbRes) + kResAmbientColor) = LitColor(LightPercent(kLpAmbient));
    *reinterpret_cast<u32 *>(set + kSetAmbient) = reinterpret_cast<u32>(s_litAmbObj);

    MakeLight(s_litHemiObj, sizeof(s_litHemiObj), s_litHemiRes, sizeof(s_litHemiRes), hemi, kHemiResBytes, kVtblHemiLight);
    {
        u8 *res = reinterpret_cast<u8 *>(s_litHemiRes);
        const float h = (float)LitByte(LightPercent(kLpHemi)) / 255.0f;
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
        colors[0] = LitColor(LightPercent(kLpFragAmbient));
        colors[1] = LitColor(LightPercent(kLpDiffuse));
        colors[2] = LitColor(LightPercent(kLpSpecular));
        colors[3] = LitColor(LightPercent(kLpSpecular));
        // 送られるのは -(V * dir)（V = ゲームのカメラの view の回転）。これを専用カメラの view での光の向き L にしたいので
        // dir = -V^T L（view は正規直交）
        const float *v = reinterpret_cast<const float *>(gameCamera + kCameraView);
        float lx = LightPercent(kLpDirX), ly = LightPercent(kLpDirY), lz = LightPercent(kLpDirZ);
        float n = std::sqrt(lx * lx + ly * ly + lz * lz);
        if (n < 0.001f) {
            lx = ly = 0.0f;
            lz = n = 1.0f;
        }
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

// 3x4 の剛体（回転は正規直交）: o = a * b
void Mul34(const float *a, const float *b, float *o) {
    for (u32 r = 0; r < 3; ++r) {
        for (u32 c = 0; c < 4; ++c) {
            float s = a[4 * r + 0] * b[c] + a[4 * r + 1] * b[4 + c] + a[4 * r + 2] * b[8 + c];
            if (c == 3)
                s += a[4 * r + 3];
            o[4 * r + c] = s;
        }
    }
}

// o = m^-1（m の回転は正規直交。ゲームの view は正規直交であることを屋内・屋外とも実機で確認）
void InvRigid34(const float *m, float *o) {
    for (u32 r = 0; r < 3; ++r)
        for (u32 c = 0; c < 3; ++c)
            o[4 * r + c] = m[4 * c + r];
    for (u32 r = 0; r < 3; ++r)
        o[4 * r + 3] = -(o[4 * r + 0] * m[3] + o[4 * r + 1] * m[7] + o[4 * r + 2] * m[11]);
}

// ノード +444 を toOurs * (+444) に置き換え、元の値を slot に取っておく
void RetargetModelView(u32 node, u32 slot, const float *toOurs) {
    s_savedModelViewNode[slot] = 0;
    if (!IsHeap(node))
        return;
    float *mv = reinterpret_cast<float *>(node + kNodeModelView);
    std::memcpy(s_savedModelView[slot], mv, sizeof(s_savedModelView[slot]));
    float o[12];
    Mul34(toOurs, s_savedModelView[slot], o);
    std::memcpy(mv, o, sizeof(o));
    s_savedModelViewNode[slot] = node;
}

void RestoreModelViews(u32 slots) {
    for (u32 i = 0; i < slots; ++i) {
        const u32 node = s_savedModelViewNode[i];
        if (node != 0u)
            std::memcpy(reinterpret_cast<float *>(node + kNodeModelView), s_savedModelView[i], sizeof(s_savedModelView[i]));
        s_savedModelViewNode[i] = 0;
    }
}

extern "C" void PlayerCloneLatePass(u32 sceneContext) {
    if (s_stage != kLive || !s_screen || !s_lateReady)
        return;
    // 世界（Scene 0、モード 0）か、メニューの 3D（Scene 1、モード 1。上画面の 2D より手前に出る）
    const u8 mode = R8(kSceneMode);
    const bool scene1 = mode == 1u && sceneContext == R32(kSceneContexts + 4);
    if (!scene1 && (sceneContext != R32(kSceneContexts) || mode != 0u))
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
    if (!BuildCamera(camera, scene1))
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
    // 法線の基準を専用カメラへ（c3〜c5 = ノード +444）
    u32 retargeted = 0;
    const u32 gameCamera = R32(context + kContextActiveCamera);
    if (IsHeap(gameCamera)) {
        float inv[12], toOurs[12];
        InvRigid34(reinterpret_cast<const float *>(gameCamera + kCameraView), inv);
        Mul34(reinterpret_cast<const float *>(s_camera + kCameraView), inv, toOurs);
        RetargetModelView(body, 0, toOurs);
        retargeted = 1;
        for (u32 i = 0; i < count && i < kMaxParts; ++i) {
            const u32 holder = s_parts[i];
            RetargetModelView(IsHeap(holder) ? R32(holder + kHolderNode) : 0u, 1 + i, toOurs);
            retargeted = 2 + i;
        }
    }
    if (s_alpha != 0u) {                            // 0 = 透明: 描かない（ライト・行列は戻す）
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
    }
    RestoreModelViews(retargeted);
    // 次の描画が +444 を送り直すように、文脈の「前回のノード」（+28）を消す
    *reinterpret_cast<volatile u32 *>(context + kContextCacheA) = 0;
    // 次の材質が状態を全部送り直すように、前回の材質（+36）と部分ごとの鍵を消す（半透明の合成を次へ残さない）
    *reinterpret_cast<volatile u32 *>(context + kContextCacheB) = 0;
    for (u32 k = 0; k < kContextKeyCount; ++k)
        *reinterpret_cast<volatile u32 *>(context + kContextKeys + 4 * k) = 0;
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
    StepRealFace();
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
    if (s_savedModelView == nullptr) {
        s_savedModelView = static_cast<float (*)[12]>(std::malloc(sizeof(float[12]) * (1 + kMaxParts)));
        if (s_savedModelView == nullptr)
            return false;
    }
    if (s_modelBuf[0] == nullptr || s_modelBuf[1] == nullptr) {
        for (u32 k = 0; k < 2; ++k) {
            if (s_modelBuf[k] == nullptr)
                s_modelBuf[k] = static_cast<u8 *>(std::malloc(kModelBytes));
            if (s_modelBuf[k] == nullptr || (reinterpret_cast<u32>(s_modelBuf[k]) & 7u) != 0)
                return false;                       // 取れない／8 バイト境界でない（newlib の malloc は 8 境界）
            std::memset(s_modelBuf[k], 0, kModelBytes);
        }
        s_model = s_modelBuf[0];
    }
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

void SetLight(u32 index, s32 value) {
    if (index < kLpCount)
        s_light[index] = value;
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

void ReloadRealFace(void) {
    s_rfWant = true;
}

bool RealFaceBusy(void) {
    return s_rfWant || s_rfStage != kRfIdle;
}

void RealFaceInfo(char *out, u32 size) {
    // 段階（0 待ち / 1 顔を読む / 2 頭の入れ替わり待ち / 3 古い枠を返す / 4 中止）・結果・フレーム数・印を立てた回数・済んだ回数・中止した段
    std::snprintf(out, size, "rf %lu r%lu f%lu k%lu d%lu a%lu", (unsigned long)s_rfStage, (unsigned long)s_rfResult,
                  (unsigned long)(s_rfStage == kRfIdle ? s_rfLastFrames : s_rfFrames), (unsigned long)s_rfKicks,
                  (unsigned long)s_rfDone, (unsigned long)s_rfLastStage);
}

void SetEyes(s32 face, s32 eyeColor) {
    s_face = face;
    s_eyeColor = eyeColor;
}

void SetAlpha(u32 alpha) {
    s_alpha = alpha > 255u ? 255u : alpha;
}

void SetLook(s32 tan, bool hideHead) {
    s_tan = tan;
    s_hideHead = hideHead;
}

bool ProjectionReady(void) {
    return s_projValid;
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
            // ライトの設定の項目（並びは PlayerClone::LightParam と同じ）
            const char *const kPcLightItems[] = { kPcLitX, kPcLitY, kPcLitZ, kPcLitAmb, kPcLitFragAmb, kPcLitDiff, kPcLitSpec,
                                                  kPcLitHemi };
            const s32 kPcLightFallback[] = { -30, -40, 100, 35, 15, 50, 15, 20 };
            int             g_pcLightItems[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
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
                PlayerClone::SetBrightness(Applied(g_pcLightIndex, 100));
                for (u32 k = 0; k < 8; ++k)
                    PlayerClone::SetLight(k, Applied(g_pcLightItems[k], kPcLightFallback[k]));
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
                    std::snprintf(message, sizeof(message), u8"%s 部品%lu 光%lu/%lu", PlayerClone::StageName(s.stage),
                                  (unsigned long)s.parts, (unsigned long)s.litDraws, (unsigned long)s.litTemplates);
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
            for (u32 k = 0; k < 8; ++k) {
                g_pcLightItems[k] = GuiMenu::FindItem(kPcLightItems[k]);
                if (g_pcLightItems[k] >= 0)
                    GuiMenu::RegisterApply(g_pcLightItems[k], ScreenApplied);
            }
        }
    }
}
