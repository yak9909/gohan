#include "HhdScreen.hpp"
#include "HhdTables.h"
#include "TouchScroll.hpp"
#include "GridCursor.hpp"
#include "GameList.hpp"
#include "Cheats.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"
#include "Cheats/PlayerClone.hpp"

#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

// 根拠（解析リポジトリ project_v2、主 IDB acnl_jpn_v15_annotated.i64）:
//   メモリ上の DARC を渡す手順（IDA-opus-5.5-F073、実機で地を出せた）:
//   ArcResAccReader_LoadArcStep 0x567244 が読み終えたあとにすることを写す。holder+0x158 と holder+8 に arc の先頭、
//     nwlyt_ArcResourceAccessor_Attach(holder+0xC, arc, ".") 0x4B5900（戻り 1 = DARC を解けた）。
//   器: ヒープ = [[0x96FC40]+4]（sead::ExpHeap）、アライン = [0x96FC2C]。確保 = vtable +24 (heap, size, align)、解放 = vtable +28 (heap, ptr)。
//     写したら sub_12D294(先頭, 大きさ)。holder+0x15C の読み込み係は使わない（ArcResAccReader_Dtor はそれを返そうとしない）→ 器はこちらで返す。
//   書体: BsMenuCatalog_Init 0x21C76C と同じく種類 0 を holder+12 のアクセサへ登録（名前 = Garden_msg_size16.bcfnt。HHD のレイアウトもこれを参照）。
//   描く順: LayoutMgr_AddLayout 0x56928C は Layout+0x0C のバイトの昇順に並べ、同じ値なら後に足したものが後ろ（= 手前）。0xFF で必ず最前面（F-253）。
//     顔の画面がこの順で予想どおり出ることを実機で確認（IDA-opus-5.5-F074）。
//   コマンドの使用量: Layout+0x100 = リスト番号、+0x118 = 確保した大きさ。管理 = dword_AD98C0[(番号 & 0x1F) + 7] から +0x40 で辿り +0 が番号のもの、
//     +0x0C = 記録した長さ（GpuCmd_SelectList 0x1216BC が切り替えのたびに保存）。
//   ペインの書き換え（GameLabel.cpp と同じ欄）: 見える旗 = ペイン+0xB7 bit0、絵の材質 = +0x13C、材質の色 0 / 1 = +0x10 / +0x14（RGB）、
//     材質を GPU へ送り直させる = 材質+0x4D bit2 を落とす。ウィンドウの枠の材質 = [ウィンドウ+0x160]+4（nwlyt_Window_DrawSelf 0x73BCBC の 1 枠の分岐）。
//     文字 = nwlyt_TextBox_SetString 0x4BACBC (箱, 文字列, 0, 長さ)。ペインを名前で引く = 0x567BAC (layout, 名前)。
//   選択・色の値と当たり矩形は自動生成の HhdTables.h（tools/hhd/export_gohan_tables.py）。

namespace HhdScreen {

namespace {

using namespace CTRPluginFramework;

typedef void *(*CtorFn)(void *self);
typedef int (*AttachFn)(void *accessor, void *arc, const char *root);
typedef int (*LayoutBuildFn)(void *layout, const char *name, void *holder, u32 cmdBytes);
typedef void (*LayoutFn)(void *layout);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef int (*RegisterTexFn)(void *holder);
typedef void (*FlushFn)(void *p, u32 size);
typedef void *(*HeapAllocFn)(void *heap, u32 size, u32 align);
typedef void (*HeapFreeFn)(void *heap, void *p);
typedef u32 (*HeapFreeSizeFn)(void *heap);
typedef void *(*FontSlotFn)(void *fontMgr, u32 kind);
typedef void (*RegisterFontFn)(void *accessor, const char *name, void *font);
typedef void *(*FindFn)(void *layout, const char *name);
typedef int (*SetStringFn)(void *textBox, const u16 *str, u32 dst, u32 len);
typedef void (*GetTextureFn)(u32 *out, void *accessor, const char *name);
typedef void (*TexMapUpdateFn)(u32 *texMap);
typedef u32 (*ProfileFn)(u32 playerIndex);
typedef void *(*FindGroupFn)(void *layout, const char *name, u32 recursive);
typedef int (*AnimLoadFn)(void *anim, const char *name, void *holder);
typedef void (*GroupAnimFn)(void *layout, void *anim, void *group, u32 zero);
typedef void (*AnimSetFrameFn)(void *anim, float frame);
typedef int (*AnimFinishedFn)(void *anim);

const CtorFn         ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);
const CtorFn         ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const AttachFn       Attach         = reinterpret_cast<AttachFn>(0x004B5900);
const RegisterTexFn  RegisterTex    = reinterpret_cast<RegisterTexFn>(0x00568CFC);
const FlushFn        FlushRange     = reinterpret_cast<FlushFn>(0x0012D294);
const CtorFn         LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);
const CtorFn         LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutBuildFn  LayoutBuild    = reinterpret_cast<LayoutBuildFn>(0x005685A4);
const LayoutFn       LayoutFinalize = reinterpret_cast<LayoutFn>(0x00133A5C);
const LayoutFn       LayoutCalc     = reinterpret_cast<LayoutFn>(0x00568030);
const AddLayoutFn    AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);
const HeapFreeSizeFn HeapFreeSize   = reinterpret_cast<HeapFreeSizeFn>(0x0074D744);   // sead::ExpHeap::getFreeSize（GridCursor と同じ）
const FontSlotFn     FontName       = reinterpret_cast<FontSlotFn>(0x00747528);
const FontSlotFn     FontGet        = reinterpret_cast<FontSlotFn>(0x0052D6A8);
const RegisterFontFn RegisterFont   = reinterpret_cast<RegisterFontFn>(0x004B3FD4);
const FindFn         FindPane       = reinterpret_cast<FindFn>(0x00567BAC);
const SetStringFn    SetString      = reinterpret_cast<SetStringFn>(0x004BACBC);
const GetTextureFn   GetTexture     = reinterpret_cast<GetTextureFn>(0x004B5844);     // nwlyt_ArcResourceAccessor_GetTexture
const TexMapUpdateFn TexMapUpdate   = reinterpret_cast<TexMapUpdateFn>(0x004B9830);   // nwlyt_TexMap_UpdateGpuRegs
const ProfileFn      PlayerProfile  = reinterpret_cast<ProfileFn>(0x002FEB60);        // PlayerClone.cpp と同じ（vc_PSOFFSET）
// プレイヤーへの反映（段階 3。解析リポジトリ IDA-opus-5.5-F080 / 旧 F-68）
//   vc_UPDATE 0x689DDC(player): プロフィールの見た目の記録から髪型・髪色・目の色（とマスク）をアクターへ読み直して作り直しの印を立てる（旧 F-68 で実機確認）。
//   顔（目の形、記録 +2）はモデルを作るとき（PlayerModel_CreateStep → sub_2711AC）にしか読まない → 次にモデルが作り直されたときに出る。
//   日焼け（記録 +4、0..15）はモデルを作るときに >>1 して pm+448 に入り、毎フレームの部品更新が pm+448 から肌の色を作る → pm+448 も書く。
typedef void (*PlayerUpdateFn)(u32 player);
const PlayerUpdateFn PlayerUpdate   = reinterpret_cast<PlayerUpdateFn>(0x00689DDC);
// 出入りのアニメ（GameLabel.cpp と同じ関数・同じ順。実機確認済みの経路）
const CtorFn         AnimCtor       = reinterpret_cast<CtorFn>(0x001261EC);
const CtorFn         AnimDtor       = reinterpret_cast<CtorFn>(0x00568CAC);
const AnimLoadFn     AnimLoad       = reinterpret_cast<AnimLoadFn>(0x00568A04);
const FindGroupFn    FindGroup      = reinterpret_cast<FindGroupFn>(0x004B4328);
const GroupAnimFn    GroupBind      = reinterpret_cast<GroupAnimFn>(0x00567A50);
const GroupAnimFn    GroupUnbind    = reinterpret_cast<GroupAnimFn>(0x00567DA4);
const AnimSetFrameFn AnimSetFrame   = reinterpret_cast<AnimSetFrameFn>(0x00568C00);
const LayoutFn       AnimStep       = reinterpret_cast<LayoutFn>(0x00568964);
const AnimFinishedFn AnimFinished   = reinterpret_cast<AnimFinishedFn>(0x0074F58C);

const u32 kLayoutMgrPtr = 0x0096FC38;
const u32 kFontMgrPtr = 0x0094C9C8;
const u32 kLoaderPtr = 0x0096FC40, kLoaderHeap = 4;     // [[0x96FC40]+4] = 読み込みのヒープ
const u32 kLoaderAlignPtr = 0x0096FC2C;
const u32 kHeapAllocSlot = 24 / 4, kHeapFreeSlot = 28 / 4;
const u32 kHolderArc = 8, kHolderAccessor = 0xC, kHolderArcLoaded = 0x158;
const u32 kLayoutPriority = 12, kLayoutHolder = 236, kLayoutListId = 0x100, kLayoutListSize = 0x118;
const u32 kCmdListBuckets = 0x00AD98C0, kCmdMgrNext = 0x40, kCmdMgrUsed = 0x0C;
const u32 kPaneFlags = 0xB7, kPicMaterial = 0x13C, kWindowFrames = 0x160, kFrameMaterial = 4;
const u32 kMatColor0 = 0x10, kMatColor1 = 0x14, kMatFlags = 0x4D, kMatTexMaps = 52;
const u32 kPaneAlpha = 180;                 // ペインの不透明度（nwlyt_Pane_SetColorElement 0x4B65E4: CLVC の対象 16 がここ。毎フレームの Calc で子へ掛かる）
const u32 kPaneX = 0x28;                    // ペインの平行移動 x（GameLabel と同じ。書いたら +0xB7 の bit4-5 を落として行列を作り直させる）
// 性別: プレイヤー [0xAA7994] の +428 = プレイヤー番号 → プロフィール（0x2FEB60）の +21946 の bit0（docs/topics/player_clone_preview.md）。
//   0 = 男の子・1 = 女の子 と読む（公開情報の仮説。LOW。実機で利用者のキャラと照合する）
const u32 kPlayerPtr = 0x00AA7994, kActorPlayerIndex = 428, kProfileSexByte = 21946;
// ★0xAA7994 は「通信番号ごとのプレイヤー表」（0xA7E790 + 4×42113。vc_PLAYERINSTANCE 0x5C27D8 が引く）の 0 番目 = ホスト。自分はその
//   [自分の通信番号]（vc_A_GETONLINEPLAYERINDEX 0x305F6C。オフラインでは 0）。hhd_t020a 実機: ゲストで 0 番目を使うとホストを変えていた（F091）
typedef u32 (*LocalIndexFn)(void);
const LocalIndexFn LocalPlayerIndex = reinterpret_cast<LocalIndexFn>(0x00305F6C);
u32 LocalPlayerActor(void) {
    const u32 index = LocalPlayerIndex();
    return index < 4 ? *reinterpret_cast<const volatile u32 *>(kPlayerPtr + 4 * index) : 0u;
}
// 見た目の記録（profile+4）: +0 髪型 <34 / +1 髪色 <16 / +2 顔（目の形）<12 / +3 目の色 <6 / +4 日焼け 0..15（F080）
const u32 kProfileLook = 4, kLookHair = 0, kLookHairColor = 1, kLookFace = 2, kLookEyeColor = 3, kLookTan = 4;
const u32 kActorModel = 436, kModelTan = 448;   // AcPlayer+436 = PlayerModel、pm+448 = 日焼けの段階（PlayerModel_Setup 0x1CF4B4）
// 通信中の判定（旧 F-68 のケーブと同じ。[g_NetGameMgr]+0x1326F が 0 ならオフライン）
const u32 kNetGameMgrPtr = 0x0094D648, kNetOnline = 0x1326F;
// ---- 通信相手への見た目の反映（T017、IDA-opus-5.5-F087）--------------------------------------------------------------
// 相手の画面の見た目は「相手が持つ、こちらのプロフィールの写し」から作られ、写しの見た目を書くのは美容室の状態 0xB9 だけ。
// 改造していない相手でも即時に変わるよう、状態パケット（0x33〜0x36、38 B）を 4 つ続けて送る（自分の状態は変えない）:
//   1. 0xB9（+14 髪型 / +15 髪色 / +16 目の色 / +17 帽子の Item）: 相手のアクターが 0xB9 に入る
//   2. 0xBA（+14 帽子 / +18 アクセサリー。美容室の小物の付け外し）: 相手側で 0xB9 を抜ける = PlayerState_AppearanceChangeSyncLocal 0x685738 が
//      写しへ髪型・色を書く（★ここでアクセサリーが無条件に空になる）
//   3. 状態 0: 相手側で 0xBA を抜ける = PlayerState_HeadGoodsExit 0x68A6AC が写しの帽子・アクセサリーを書き戻す（利用者: アクセサリーは消さない。F088）
//   4. 今の本当の状態（アクター +2236 のバッファそのもの）: 相手側で状態 0 を抜ける = PlayerState00_Exit 0x6533C4 が位置を戻し vc_UPDATE で写しから作り直す
// アクターが無い相手（別の部屋）は記述子 +0x18（0xB9 = SyncRemote、0xBA = PlayerState_HeadGoodsSyncRemote 0x68C538）で写しだけ書く。
// けってい の次のフレーム（FrameStep の先頭）で待たずに送る（hhd_t017b 実機: 条件がそろうのを待つと送れずに終わった）。送り方はゲームと同じ（Player_SendStatePacketOnSetState 0x675F14:
// 部屋の旗 0x10 が無く、自分のオンライン番号がアクター +428 と同じとき、通し番号 +8 を 1 増やして vc_SENDPACKETFUNC(番号, バッファ)）。
// ★下位転送種別 0x0C（村データ転送）は使わない（利用者 2026-10-06）
typedef void (*SendStatePacketFn)(u32 playerIndex, const void *packet);
const SendStatePacketFn SendStatePacket = reinterpret_cast<SendStatePacketFn>(0x005C25B4);   // vc_SENDPACKETFUNC
const u32 kActorStatePacket = 2236, kStatePacketBytes = 38, kPacketState = 1, kPacketCounter = 8, kPacketArgs = 14;
const u32 kStateAppearance = 0xB9, kStateHeadGoods = 0xBA, kStateInitial = 0;
const u32 kProfileHat = 10, kProfileAccessory = 14;   // プロフィール +10 = 帽子 / +14 = アクセサリーの Item（4 B。F-67）
// 送れなかった理由（通知に出す）
enum PeerReason : u32 { kPeerSent = 0, kPeerPlayer = 5, kPeerOffline = 6 };   // 5 プレイヤーが替わった / 6 通信が切れた
volatile bool s_peerWant;
u32 s_peerPlayer, s_peerLastReason;
u8 s_peerLook[3], s_peerHat[4], s_peerAccessory[4];
// 髪のページ: 男女の区別なく 32 個 = 8 個 × 4 ページ（利用者指示 2026-10-05。HHD は性別で 16 個に絞る: HHD-F005）。
//   ページ 0〜1 = 男の子の髪 B00〜B15、ページ 2〜3 = 女の子の髪 G00〜G15。髪の番号 = ページ × 8 + 枠の中の番号（0〜31）。枠は左 pg0・中央 pg1・右 pg2 の 3 つ（位置 -276 / 0 / +276、fce_HairBase_00）。
//   スクロールは自由（ページに吸着しない。利用者 2026-10-05）で、漢字変換の候補欄と同じ算法（TouchScroll.hpp = ChatIme.cpp の写し）。
//   スクロール位置 = 0..幅 x (ページ数 - 1)（指を左へ動かすと増える）。中央の枠 = 位置に最も近いページ、N_All の x = 中央 x 幅 - 位置。
//   L / R・左右の端のタップは隣のページの位置まで滑らせる。滑り方は HHD の PageSlider（IDA-opus-5.5-HHD-F010）を 30 fps として時間に直したもの。
const s32 kHairPages = 4;
const float kPageStep = 276.0f;             // HHD の PageSlider +124 = 枠 2 と枠 1 の x の差（fce_HairBase_00 の L_HairPage_01 / 02）
const float kStepSpeed = 25.0f, kApproach = 0.2f, kMinStep = 0.5f;   // HHD の PageSlider +96 / +76 / +84（1 フレームあたり）
const float kFps = 30.0f;                   // ACNL の 1 秒のフレーム数（F-356: 2 VBlank = 30 fps）。HHD のフレームレートは未確認

// ---- 上画面の吹き出しの複製（T018）----
// 複製は gohan の PlayerClone（画面に固定・専用カメラ・専用ライト。IDA-opus-5.5-F031〜F033）。上画面の 2D より手前に出すため、
// 開いている間はゲームを「メニューの 3D」（Scene 1。持ち物・カタログと同じ）にする: 描画順が 世界 → 上画面の 2D → Scene 1 になる（IDA-opus-5.5-F083）。
// 吹き出し W_Balloon_00 の中心 = 上画面の画素 (200, 126)（hhd_top。大きさ 491 x 269）
const s32 kCloneX = 200, kCloneY = 170, kClonePitch = 10, kCloneZoom = 60;    // 利用者の実機での指定（2026-10-05: Y 170・大きさ 60）。向き 0 = 正面
const float kYawPerSecond = 240.0f;         // スライドパッドを倒し切ったときの回る速さ（度/秒。T019: 角度の上限なし）
const s32 kPadDead = 16, kPadFull = 156;    // スライドパッドの遊びと倒し切り（CTRPF の GetCirclePadPosition の値）
typedef void (*MenuSceneFn)(void);
const MenuSceneFn EnterMenuScene = reinterpret_cast<MenuSceneFn>(0x004EF5DC);   // 持ち物 sub_1959D0 と同じ（byte_94CA2C = 1 ほか）
const MenuSceneFn LeaveMenuScene = reinterpret_cast<MenuSceneFn>(0x004EF27C);   // 戻す（保存していた値へ）
const u32 kSceneModeByte = 0x0094CA2C;
// ---- 音（HHD の音は ACNL の音で代用。SOUND/index/sounds.csv の番号、Game_PlaySound 0x58C7D4）----
typedef void (*PlaySoundFn)(u32 id);
const PlaySoundFn PlaySound = reinterpret_cast<PlaySoundFn>(0x0058C7D4);
const u32 kSndOpen = 0x010003C2;            // SE_SYS_WIN_SELECT_OPEN
const u32 kSndTouch = 0x01000398;           // SE_SYS_BTN_ACTIVE（触れた）
const u32 kSndPick = 0x01000413;            // SE_SYS_BOOK_ICON_SELECTED（目の形・髪型を決めた。利用者指定 2026-10-06）
const u32 kSndColor = 0x01000449;           // SE_SYS_EDIT_COLOR_SELECTED（色を決めた。マイデザインの色選び）
const u32 kSndToHair = 0x01000417;          // SE_SYS_PRF_TAB_CHANGE（「かみがた」へ。利用者: かみがた・かおで違う音に 2026-10-05）
const u32 kSndToFace = 0x01000412;          // SE_SYS_BOOK_TAB_CHANGE（「かお」へ）
const u32 kSndModeExtra = 0x010004E1;       // SE_SYS_AMIIBO_CAMERA_LIGHT_BLUE（かみがた・かお の両方で重ねる。利用者指定 2026-10-06）
const u32 kSndDecide = 0x0100038F;          // SE_SYS_DECIDE_L（けってい。利用者指定 2026-10-06）
const u32 kSndCancel = 0x01000391;          // SE_SYS_DECIDE_QUIT（B で閉じる。利用者指定 2026-10-06）
const u32 kSndPageInc = 0x0100039C, kSndPageDec = 0x0100039D;   // SE_SYS_PAGE_INC / DEC
// Y（頭の小物）: 軽いポップ音（利用者 2026-10-05: 着替えの音は大げさ。美容室でも同じ）。美容室の SE_ACT_BARBER_GOODS_OFF / ON は
//   GROUP_SHOP で美容室の外では鳴らないので使わない（F083 / F084）
const u32 kSndHeadOff = 0x010003DF, kSndHeadOn = 0x010003DE;   // SE_SYS_SWK_TOGGLE_OFF / ON（GROUP_STATIC）
// 複製のフェードイン: 吹き出しの登場アニメが終わってから（利用者: 早すぎる。吹き出しの登場後にフェードイン）
const u32 kCloneFadeFrames = 10;
const u8 kPriority = 0xFF;                  // 最前面（ゲームの下画面の UI より手前）
const u32 kScreenLower = 1, kScreenUpper = 0;   // AddLayout の画面（上画面 = 0 / 下画面 = 1。LayoutMgr_AddLayout 0x56928C）
const u32 kTeardownWaitFrames = 3;          // GameLabel と同じ（描くのをやめてから壊すまで）

const char kArcPath[] = "/hhd_charcreate.arc";
const u32 kMaxArcBytes = 0x80000;

// 組むレイアウト（この順に足す = 後ろほど手前）。コマンド領域は実機の測定（F074: 地 784・顔 13,344・目 23,104 バイト）に余裕を足した値。
enum { kBg, kFace, kEye, kHair, kTop, kLayouts };
struct Def { const char *name; u32 cmdBytes; };
const Def kDefs[kLayouts] = {
    { "hhd_bg.bclyt",   0x2000  },
    { "hhd_face.bclyt", 0x10000 },
    { "hhd_eye.bclyt",  0x10000 },   // 縞の枠で材質が 4 倍・TEV の段（2026-10-05。前は 0x8000 で実測 23,104 B）
    { "hhd_hair.bclyt", 0x20000 },   // 同上（窓 24 個。前は 0x10000、実測なし）
    { "hhd_top.bclyt",  0x4000  },     // 上画面（HHD の fce_Top_00: 地・水玉・吹き出し・小さな丸・「まわす」。モデルは出さない）。実測 3,088 B（文字 4 字）。
                                       //   2026-10-06 に文字の欄を 2 つ（17 字）足したので倍にする（1 字の費用は未実測。状態の通知の top で確かめる）
};

enum class Stage : u8 { Idle, Copy, Draw, Teardown, Failed };

struct Lay {
    alignas(8) u8 obj[332];
    alignas(8) u8 in[40];                   // 出入りのアニメ（HHD の <レイアウト>_in / _out。GameLabel と同じ 40 B）
    alignas(8) u8 out[40];
    void *group;                            // アニメを結ぶグループ（HhdTables::kAnimGroup）
    void *bound;                            // いま結んでいるアニメ（in / out / nullptr）
    alignas(8) u8 loop[40];                 // 繰り返しのアニメ（HHD は G_Loop と loop があれば常に再生: LayoutObj_StartLoopAnim、HHD-F009）
    void *loopGroup;
    bool made, built, anims, hasLoop;
};

// 画面の段（ゲームのスレッドが進める）: 入場アニメ → 操作できる → 退場アニメ（終われば片付け）
enum class Phase : u8 { Entering, Live, Leaving };

// 画面の状態（メニューのスレッドが書き、ゲームのスレッドがペインへ写す）。-1 = 選んでいない
struct State {
    s8 mode;                                // 0 = 顔（目）、1 = 髪
    s8 eyeShape, eyeColor, skin, hair, hairColor;   // hair = 0..31（0〜15 男の子 B、16〜31 女の子 G）
    s8 sex, page;                           // sex: 0 男の子 / 1 女の子、page: 中央の枠のページ（ゲームのスレッドだけが書く）
};

// 引いておくペイン
struct Panes {
    void *eyeFrame[12], *iris[12], *eyeBase[12];
    void *eyePic[12];
    void *hairFrame[24], *hairPic[24], *hairSkin[24];  // [枠 * 8 + 番号]（枠 0 左 / 1 中央 / 2 右）
    void *ecFrame[6], *scFrame[8], *hcFrame[16];
    void *eyeColorGroup, *hairSkinGroup, *leftText, *hairIconB, *hairIconG;
    void *hairPage[3], *hairAll;
    void *bgAll, *topAll;                   // 入場の背景のフェードで不透明度を動かすペイン（地・上画面の N_All。どちらも子に不透明度を伝える）
};

alignas(8) u8 s_holder[584];                // GameLabel と同じ大きさ
Lay s_lay[kLayouts];
Panes s_p;
u8 *s_file;                                 // SD から読んだ arc（プラグインのメモリ。メニューのスレッドが作る）
u32 s_fileSize;
void *s_heap, *s_arc;                       // ゲームのヒープに写した arc
bool s_holderMade, s_hookReady;
volatile bool s_want;
volatile Stage s_stage = Stage::Idle;
volatile Phase s_phase = Phase::Entering;
u32 s_fadeFrame;                            // 入場の背景のフェードの経過フレーム（ゲームのスレッドだけ）
const char *volatile s_error = "";
u32 s_wait;
volatile State s_state;                     // 欲しい状態
volatile s8 s_pageReq;                      // メニューのスレッドからのページ送りの頼み（-1 / +1、0 = なし）
// 複製（メニューのスレッドが書く）と、Scene 1 にしたか（ゲームのスレッドだけ）
float s_cloneYaw;
volatile bool s_hideHead;
u64 s_padTick;
bool s_menuScene;
u32 s_cloneFade;                            // Scene 1 にしてからのフレーム数（フェードイン）
// 音の頼み（メニューのスレッドが積み、ゲームのスレッドが鳴らす）
volatile u32 s_soundQ[8];
volatile u32 s_soundHead, s_soundTail;
// 髪のスクロール（メニューのスレッドだけが書く。ゲームのスレッドは s_scrollPub を読んで枠を置くだけ）
TouchScroll s_scroller;
volatile float s_scrollPub;                 // スクロール位置（画素）
volatile bool s_scrollSync;                 // ゲームのスレッドが位置を決め直した（開いたとき）。メニューのスレッドは s_scrollPub から取り直す
bool s_onPage;                              // ページの当たりの中で触れ始めた指が、まだ離れていない
bool s_stepping;                            // L / R・端のタップで隣のページへ滑らせている
float s_stepTarget;
u64 s_stepTick;
volatile u8 s_sexByte = 0xFF;               // 読んだプロフィールの性別のバイト（状態の通知用）
volatile bool s_applyReq;                   // 「けってい」で閉じる: 閉じ始めに見た目をプレイヤーへ書く
const char *volatile s_applyResult = "";    // 反映の結果（状態の通知用）
State s_applied;                            // ペインへ写した状態（ゲームのスレッドだけ）
bool s_appliedValid;
// 測った値（ゲームのスレッドが書き、Measure が読む）
volatile u32 s_heapFreeBefore, s_heapFreeAfter, s_used[kLayouts], s_size[kLayouts];
// 入力（メニューのスレッドだけ）
bool s_touchPrev;
s32 s_touchStart = -1;                      // 指を置いたときの的（-1 = なし）
u32 s_keysPrev;

inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(p) + off); }
inline u8 &B(void *p, u32 off) { return *(reinterpret_cast<u8 *>(p) + off); }

bool IsHeapPointer(const void *p) {
    const u32 v = reinterpret_cast<u32>(p);
    return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u;
}

void Fail(const char *why) {
    s_error = why;
    s_stage = Stage::Failed;
}

// ---- ペインの書き換え（ゲームのスレッド）----

void SetVisible(void *pane, bool on) {
    if (pane == nullptr)
        return;
    B(pane, kPaneFlags) = (u8)((B(pane, kPaneFlags) & ~1u) | (on ? 1u : 0u));
}

void Paint(void *mat, const HhdTables::ColorPair &c) {
    if (!IsHeapPointer(mat))
        return;
    for (u32 k = 0; k < 3; ++k) {
        B(mat, kMatColor0 + k) = c.c0[k];
        B(mat, kMatColor1 + k) = c.c1[k];
    }
    B(mat, kMatFlags) &= ~4u;               // GPU へ送り直させる
}

void *PicMaterial(void *pic) {
    return pic != nullptr ? reinterpret_cast<void *>(W(pic, kPicMaterial)) : nullptr;
}

// 窓の全部の枠の材質を塗る。縞の枠は 4 枚（make_acnl_layouts.py FRAME4）。枠の数 = win+0x168、枠の配列 = win+0x160（1 枚 8 B、+4 = 材質）
//   （nwlyt_Window_DrawSelf 0x73BCBC。IDA-opus-5.5-F082）
const u32 kWindowFrameCount = 0x168, kFrameStride = 8;
void PaintWindow(void *win, const HhdTables::ColorPair &c) {
    if (win == nullptr)
        return;
    const u32 frames = W(win, kWindowFrames);
    if (!IsHeapPointer(reinterpret_cast<void *>(frames)))
        return;
    const u32 n = B(win, kWindowFrameCount);
    for (u32 i = 0; i < n && i < 8; ++i)
        Paint(reinterpret_cast<void *>(W(reinterpret_cast<void *>(frames + kFrameStride * i), kFrameMaterial)), c);
}

void *Find(u32 lay, const char *fmt, u32 a, u32 b = 0) {
    char name[24];
    std::snprintf(name, sizeof(name), fmt, (unsigned)a, (unsigned)b);
    return FindPane(s_lay[lay].obj, name);
}

bool FindAll(void) {
    for (u32 k = 0; k < 12; ++k) {
        s_p.eyeFrame[k] = Find(kEye, "e%02u_W_Frame", k);
        s_p.iris[k] = Find(kEye, "e%02u_P_Iris", k);
        s_p.eyeBase[k] = Find(kEye, "e%02u_P_Base", k);
        if (s_p.eyeFrame[k] == nullptr || s_p.iris[k] == nullptr || s_p.eyeBase[k] == nullptr)
            return false;
    }
    for (u32 pg = 0; pg < 3; ++pg)
        for (u32 k = 0; k < 8; ++k) {
            s_p.hairPic[pg * 8 + k] = Find(kHair, "h%u%u_P_Hair", pg, k);
            s_p.hairSkin[pg * 8 + k] = Find(kHair, "h%u%u_P_Skin", pg, k);
            if (s_p.hairPic[pg * 8 + k] == nullptr || s_p.hairSkin[pg * 8 + k] == nullptr)
                return false;
        }
    for (u32 pg = 0; pg < 3; ++pg)
        for (u32 k = 0; k < 8; ++k)
            if ((s_p.hairFrame[pg * 8 + k] = Find(kHair, "h%u%u_W_Frame", pg, k)) == nullptr)
                return false;
    for (u32 k = 0; k < 12; ++k)
        if ((s_p.eyePic[k] = Find(kEye, "e%02u_P_Eye", k)) == nullptr)
            return false;
    for (u32 pg = 0; pg < 3; ++pg)
        if ((s_p.hairPage[pg] = Find(kHair, "pg%u", pg)) == nullptr)
            return false;
    s_p.hairAll = FindPane(s_lay[kHair].obj, "N_All");
    s_p.bgAll = FindPane(s_lay[kBg].obj, "N_All");
    s_p.topAll = FindPane(s_lay[kTop].obj, "N_All");
    if (s_p.bgAll == nullptr || s_p.topAll == nullptr)
        return false;
    s_p.hairIconB = FindPane(s_lay[kFace].obj, "P_HairIconB_00");
    s_p.hairIconG = FindPane(s_lay[kFace].obj, "P_HairIconG_00");
    for (u32 k = 0; k < 6; ++k)
        if ((s_p.ecFrame[k] = Find(kFace, "ec%02u_P_Frame", k)) == nullptr)
            return false;
    for (u32 k = 0; k < 8; ++k)
        if ((s_p.scFrame[k] = Find(kFace, "sc%02u_P_Frame", k)) == nullptr)
            return false;
    for (u32 k = 0; k < 16; ++k)
        if ((s_p.hcFrame[k] = Find(kFace, "hc%02u_P_Frame", k)) == nullptr)
            return false;
    s_p.eyeColorGroup = FindPane(s_lay[kFace].obj, "N_EyeColor_00");
    s_p.hairSkinGroup = FindPane(s_lay[kFace].obj, "N_HairSkinCol");
    s_p.leftText = FindPane(s_lay[kFace].obj, "b0_T_Btn");
    return s_p.eyeColorGroup != nullptr && s_p.hairSkinGroup != nullptr && s_p.leftText != nullptr && s_p.hairAll != nullptr
        && s_p.hairIconB != nullptr && s_p.hairIconG != nullptr;
}

// 絵の差し替え（ゲームのアイコンの差し替え ItemIconWidget_SetItem 0x2B8EAC と同じ手順）:
//   GetTexture(出力 20 B, アクセサ = holder+12, 名前) → 材質+52 の TexMap の [0..3] に写し [4] の書式ビット（0xF00）だけ入れ替え
//   → nwlyt_TexMap_UpdateGpuRegs → 材質+0x4D bit2 を落とす。テクスチャは組んだときに全部登録済み（ArcResAccReader_RegisterTextures）
bool SetTexture(void *pic, const char *name) {
    void *mat = PicMaterial(pic);
    if (!IsHeapPointer(mat))
        return false;
    u32 info[5] = { 0, 0, 0, 0, 0 };
    GetTexture(info, s_holder + kHolderAccessor, name);
    u32 *t = reinterpret_cast<u32 *>(W(mat, kMatTexMaps));
    if (info[1] == 0 || !IsHeapPointer(t))
        return false;
    t[0] = info[0];
    t[1] = info[1];
    t[2] = info[2];
    t[3] = info[3];
    t[4] = ((info[4] & 0xFFu) << 8 & 0xF00u) | (t[4] & ~0xF00u);
    TexMapUpdate(t);
    B(mat, kMatFlags) &= ~4u;
    return true;
}

void MovePaneX(void *pane, float x) {
    *reinterpret_cast<float *>(reinterpret_cast<u8 *>(pane) + kPaneX) = x;
    B(pane, kPaneFlags) &= 0xCFu;
}

// 枠 f（0 左 / 1 中央 / 2 右）に出すページ。無ければ -1
s32 FramePage(s32 page, u32 f) {
    const s32 pg = page + (s32)f - 1;
    return pg >= 0 && pg < kHairPages ? pg : -1;
}

void SetHairPages(s8 page) {
    char name[32];
    for (u32 f = 0; f < 3; ++f) {
        const s32 pg = FramePage(page, f);
        SetVisible(s_p.hairPage[f], pg >= 0);
        if (pg < 0)
            continue;
        for (u32 k = 0; k < 8; ++k) {
            const s32 hair = pg * 8 + (s32)k;
            std::snprintf(name, sizeof(name), "sh_fce_%cHair_%02ld.bclim", hair < 16 ? 'B' : 'G', (long)(hair % 16));
            SetTexture(s_p.hairPic[f * 8 + k], name);
        }
    }
}

void PaintHairFrames(s8 page, s8 hair) {
    for (u32 f = 0; f < 3; ++f) {
        const s32 pg = FramePage(page, f);
        for (u32 k = 0; k < 8; ++k) {
            const bool sel = pg >= 0 && hair >= 0 && pg * 8 + (s32)k == hair;
            PaintWindow(s_p.hairFrame[f * 8 + k], sel ? HhdTables::kHairCell[k].b : HhdTables::kHairCell[k].a);
        }
    }
}

const u16 kTextHair[] = { 0x304B, 0x307F, 0x304C, 0x305F };    // かみがた
const u16 kTextFace[] = { 0x304B, 0x304A };                    // かお

// 欲しい状態と写した状態の差だけペインへ書く
void Apply(void) {
    State want;
    want.mode = s_state.mode;
    want.eyeShape = s_state.eyeShape;
    want.eyeColor = s_state.eyeColor;
    want.skin = s_state.skin;
    want.hair = s_state.hair;
    want.hairColor = s_state.hairColor;
    want.sex = s_state.sex;
    want.page = s_state.page;
    const bool all = !s_appliedValid;
    State &was = s_applied;
    if (all || want.sex != was.sex) {
        // 性別: 目の形・黒目の絵（HHD の CharaEyeBaseLayout_SetEyeIcons と同じく B / G の組）と、髪の色の行の左のアイコン
        char name[32];
        for (u32 k = 0; k < 12; ++k) {
            std::snprintf(name, sizeof(name), "sh_fce_%cEye_%02lu.bclim", want.sex == 1 ? 'G' : 'B', (unsigned long)k);
            SetTexture(s_p.eyePic[k], name);
            std::snprintf(name, sizeof(name), "sh_fce_%cEyeB_%02lu.bclim", want.sex == 1 ? 'G' : 'B', (unsigned long)k);
            SetTexture(s_p.iris[k], name);
        }
        SetVisible(s_p.hairIconB, want.sex != 1);
        SetVisible(s_p.hairIconG, want.sex == 1);
    }
    if (all || want.page != was.page)
        SetHairPages(want.page);
    if (all || want.mode != was.mode) {
        SetVisible(s_p.eyeColorGroup, want.mode == 0);
        SetVisible(s_p.hairSkinGroup, want.mode == 1);
        if (want.mode == 0)
            SetString(s_p.leftText, kTextHair, 0, 4);
        else
            SetString(s_p.leftText, kTextFace, 0, 2);
    }
    if (all || want.eyeShape != was.eyeShape)
        for (u32 k = 0; k < 12; ++k)
            PaintWindow(s_p.eyeFrame[k], (s32)k == want.eyeShape ? HhdTables::kEyeShape[k].b : HhdTables::kEyeShape[k].a);
    if (all || want.hair != was.hair || want.page != was.page)
        PaintHairFrames(want.page, want.hair);
    if (all || want.eyeColor != was.eyeColor) {
        for (u32 k = 0; k < 6; ++k) {
            SetVisible(s_p.ecFrame[k], (s32)k == want.eyeColor);
            if ((s32)k != want.eyeColor)
                Paint(PicMaterial(s_p.ecFrame[k]), HhdTables::kSwatchNormal);   // 外れた枠は通常の色へ（戻さないと押したとき縞が出る）
        }
        if (want.eyeColor >= 0) {
            Paint(PicMaterial(s_p.ecFrame[want.eyeColor]), HhdTables::kSwatchSelect);
            for (u32 k = 0; k < 12; ++k)
                Paint(PicMaterial(s_p.iris[k]), HhdTables::kEyeColor[want.eyeColor].a);
        }
    }
    if (all || want.skin != was.skin) {
        for (u32 k = 0; k < 8; ++k) {
            SetVisible(s_p.scFrame[k], (s32)k == want.skin);
            if ((s32)k != want.skin)
                Paint(PicMaterial(s_p.scFrame[k]), HhdTables::kSwatchNormal);   // 外れた枠は通常の色へ（戻さないと押したとき縞が出る）
        }
        if (want.skin >= 0) {
            Paint(PicMaterial(s_p.scFrame[want.skin]), HhdTables::kSwatchSelect);
            for (u32 k = 0; k < 12; ++k)
                Paint(PicMaterial(s_p.eyeBase[k]), HhdTables::kSkinColor[want.skin].a);
            for (u32 k = 0; k < 24; ++k)
                Paint(PicMaterial(s_p.hairSkin[k]), HhdTables::kSkinColor[want.skin].b);
        }
    }
    if (all || want.hairColor != was.hairColor) {
        for (u32 k = 0; k < 16; ++k) {
            SetVisible(s_p.hcFrame[k], (s32)k == want.hairColor);
            if ((s32)k != want.hairColor)
                Paint(PicMaterial(s_p.hcFrame[k]), HhdTables::kSwatchNormal);   // 外れた枠は通常の色へ（戻さないと押したとき縞が出る）
        }
        if (want.hairColor >= 0) {
            Paint(PicMaterial(s_p.hcFrame[want.hairColor]), HhdTables::kSwatchSelect);
            for (u32 k = 0; k < 24; ++k)
                Paint(PicMaterial(s_p.hairPic[k]), HhdTables::kHairColor[want.hairColor].a);
        }
    }
    s_applied = want;
    s_appliedValid = true;
}

// ---- 音 ----
void QueueSound(u32 id) {
    const u32 head = s_soundHead;
    if (head - s_soundTail >= 8)
        return;
    s_soundQ[head & 7] = id;
    s_soundHead = head + 1;
}

void FlushSounds(void) {
    while (s_soundTail != s_soundHead) {
        PlaySound(s_soundQ[s_soundTail & 7]);
        s_soundTail = s_soundTail + 1;
    }
}

// ---- 複製（ゲームのスレッド）: 複製ができてから Scene 1 にする。閉じ始めたら戻す ----
void StepMenuScene(void) {
    if (!s_menuScene) {
        PlayerClone::SetAlpha(0);           // 出す前は描かない
        if (!s_want || s_phase != Phase::Live)
            return;                         // 吹き出しの登場アニメが終わってから
        const PlayerClone::Status st = PlayerClone::Read();
        if (st.stage != 2 || !PlayerClone::ProjectionReady())
            return;                         // 複製が表示中で、世界のカメラの投影を一度取れてから
        if (*reinterpret_cast<const volatile u8 *>(kSceneModeByte) != 0)
            return;                         // ほかのメニューが使っている
        EnterMenuScene();
        s_menuScene = true;
        s_cloneFade = 0;
    } else if (!s_want || s_phase == Phase::Leaving) {
        LeaveMenuScene();
        s_menuScene = false;
    } else if (PlayerClone::Read().stage != 2) {
        PlayerClone::SetAlpha(0);           // 作り直し中（枠が足りないときだけ。ふつうは裏で作って即入れ替え）
    } else if (s_cloneFade < kCloneFadeFrames) {
        ++s_cloneFade;                      // フェードインは開いたときの 1 回だけ（利用者: 目の形の切り替えはフェードせず即）
        PlayerClone::SetAlpha(255u * s_cloneFade / kCloneFadeFrames);
    } else {
        PlayerClone::SetAlpha(255);
    }
}

void LeaveMenuSceneNow(void) {
    if (s_menuScene) {
        LeaveMenuScene();
        s_menuScene = false;
    }
}

// ---- 髪のページ（ゲームのスレッド）: スクロール位置から中央の枠のページと N_All の x を決める ----
//   中央のページが変われば Apply が 3 つの枠の絵を入れ替える
void PlacePages(void) {
    const float scroll = s_scrollPub;
    s32 page = (s32)(scroll / kPageStep + 0.5f);
    if (page < 0)
        page = 0;
    if (page >= kHairPages)
        page = kHairPages - 1;
    s_state.page = (s8)page;
    MovePaneX(s_p.hairAll, kPageStep * (float)page - scroll);
}

// ---- 押したときのアニメ（ゲームのスレッド）----
//   HHD の ButtonBaseActor は TouchSelect で touch、TouchDecide で touch_ok を流す（IDA-opus-5.5-HHD-F004）。
//   どちらもペインの位置 Y・回転 Z・拡大率・表示だけなので、HhdTables の kPress*（HHD の BFLAN のキー）をエルミートで補間して書く。
//   touch は 1 フレームの物で、触れている間は最後のフレーム（沈んだ形）のまま。touch_ok はフレーム 0 から最後まで 1 フレームずつ。
volatile s32 s_pressTarget = -1;            // メニューのスレッド: 触れている的（-1 = なし）
volatile s32 s_okTarget = -1;               // メニューのスレッド: 最後に決定した的
volatile u32 s_okSerial;                    // 決定のたびに 1 増える

const u32 kPaneY = 0x2C, kPaneRotZ = 0x3C, kPaneScaleX = 0x40, kPaneScaleY = 0x44;   // nw::lyt::Pane（translate +0x28 / rotate +0x34 / scale +0x40）
struct Posed { void *pane; u8 target; float base; };
// 押している間の色（「かみがた／かお」「けってい」だけ。HHD の select / select_ok）。obj = ペイン（kind 0）か材質（kind 1）
struct ColorPosed { void *obj; u8 kind; u8 index; u8 base; };
struct PressSlot {
    s32 target = -1;
    const HhdTables::PressAnim *anim = nullptr;   // [0] touch / [1] touch_ok
    u32 lay = 0;
    char prefix[8] = {};
    Posed posed[4];                         // 1 つの的の touch / touch_ok のペインと要素の組（export_gohan_tables.py が突き合わせる）
    u32 count = 0;
    const HhdTables::BakedColor *color = nullptr;  // select / select_ok を整数フレームで焼いた表（無ければ nullptr）
    u32 colorCount = 0;
    ColorPosed cposed[37];
    u32 ccount = 0;
    bool ok = false;                        // touch_ok を流している
    float frame = 0.0f;
};
PressSlot s_press;
u32 s_okSeen;

float EvalKeys(const HhdTables::Key *k, u32 n, float f) {
    if (f <= k[0].f)
        return k[0].v;
    u32 i = n - 1;
    while (i > 0 && k[i].f > f)
        --i;
    if (i == n - 1)
        return k[i].v;
    const float d = k[i + 1].f - k[i].f;
    if (d <= 0.0f)
        return k[i + 1].v;
    const float t = (f - k[i].f) / d, t2 = t * t, t3 = t2 * t;
    return k[i].v * (2.0f * t3 - 3.0f * t2 + 1.0f) + k[i + 1].v * (-2.0f * t3 + 3.0f * t2)
        + (k[i].s * (t3 - 2.0f * t2 + t) + k[i + 1].s * (t3 - t2)) * d;
}

float ReadTarget(void *pane, u8 target) {
    u8 *b = reinterpret_cast<u8 *>(pane);
    switch (target) {
    case 1: return *reinterpret_cast<float *>(b + kPaneY);
    case 5: return *reinterpret_cast<float *>(b + kPaneRotZ);
    case 6: return *reinterpret_cast<float *>(b + kPaneScaleX);
    case 7: return *reinterpret_cast<float *>(b + kPaneScaleY);
    default: return (B(pane, kPaneFlags) & 1u) ? 1.0f : 0.0f;
    }
}

void WriteTarget(void *pane, u8 target, float v) {
    u8 *b = reinterpret_cast<u8 *>(pane);
    switch (target) {
    case 1: *reinterpret_cast<float *>(b + kPaneY) = v; break;
    case 5: *reinterpret_cast<float *>(b + kPaneRotZ) = v; break;
    case 6: *reinterpret_cast<float *>(b + kPaneScaleX) = v; break;
    case 7: *reinterpret_cast<float *>(b + kPaneScaleY) = v; break;
    default: SetVisible(pane, v >= 0.5f); return;
    }
    B(pane, kPaneFlags) &= 0xCFu;           // 行列を作り直させる（MovePaneX と同じ）
}

// 色の 1 要素。ペインはゲームの仮想関数（+0x14 GetColorElement 0x73B508 / +0x18 nwlyt_Pane_SetColorElement 0x4B65E4: 16 = 不透明度、
//   ほかは頂点色。Picture +0x140 / TextBox +0xD8）、材質は +0x10 + 番号（黒 0..3 / 白 4..7。sub_4BCC7C と同じ）
typedef u8 (*GetColorFn)(void *pane, u32 index);
typedef void (*SetColorFn)(void *pane, u32 index, u8 value);
const u32 kVtGetColor = 0x14, kVtSetColor = 0x18;

u8 ReadColor(void *obj, u8 kind, u8 index) {
    if (kind != 0)
        return B(obj, kMatColor0 + index);
    return reinterpret_cast<GetColorFn>(W(reinterpret_cast<void *>(W(obj, 0)), kVtGetColor))(obj, index);
}

void WriteColor(void *obj, u8 kind, u8 index, u8 value) {
    if (kind != 0) {
        B(obj, kMatColor0 + index) = value;
        B(obj, kMatFlags) &= ~4u;           // GPU へ送り直させる（Paint と同じ）
        return;
    }
    reinterpret_cast<SetColorFn>(W(reinterpret_cast<void *>(W(obj, 0)), kVtSetColor))(obj, index, value);
}

void *ColorObject(const HhdTables::BakedColor &tr) {
    char name[24];
    std::snprintf(name, sizeof(name), "%s_%s", s_press.prefix, HhdTables::kSelectPanes[tr.pane]);
    void *pane = FindPane(s_lay[s_press.lay].obj, name);
    if (pane == nullptr || tr.kind == 0)
        return pane;
    void *mat = PicMaterial(pane);
    return IsHeapPointer(mat) ? mat : nullptr;
}

// 的の番号 → レイアウト・名前の頭・アニメ。今の画面に無い的は false
bool ResolvePress(s32 t, u32 &lay, char (&prefix)[8], const HhdTables::PressAnim *&anim) {
    const s8 mode = s_applied.mode;
    if (t >= 0 && t < 12 && mode == 0) {
        lay = kEye, anim = HhdTables::kPressEye;
        std::snprintf(prefix, sizeof(prefix), "e%02ld", (long)t);
    } else if (t >= 100 && t < 106 && mode == 0) {
        lay = kFace, anim = HhdTables::kPressColor;
        std::snprintf(prefix, sizeof(prefix), "ec%02ld", (long)(t - 100));
    } else if (t >= 200 && t < 208 && mode == 0) {
        lay = kFace, anim = HhdTables::kPressColor;
        std::snprintf(prefix, sizeof(prefix), "sc%02ld", (long)(t - 200));
    } else if (t >= 400 && t < 416 && mode == 1) {
        lay = kFace, anim = HhdTables::kPressColor;
        std::snprintf(prefix, sizeof(prefix), "hc%02ld", (long)(t - 400));
    } else if (t >= 300 && t < 332 && mode == 1) {
        const s32 hair = t - 300, f = hair / 8 - (s32)s_state.page + 1;
        if (f < 0 || f > 2)
            return false;
        lay = kHair, anim = HhdTables::kPressHair;
        std::snprintf(prefix, sizeof(prefix), "h%ld%ld", (long)f, (long)(hair % 8));
    } else if (t == 900 || t == 901) {
        lay = kFace, anim = t == 900 ? HhdTables::kPressBtn0 : HhdTables::kPressBtn1;
        std::snprintf(prefix, sizeof(prefix), "b%ld", (long)(t - 900));
    } else {
        return false;
    }
    return true;
}

void PressAdd(void *pane, u8 target) {
    for (u32 i = 0; i < s_press.count; ++i)
        if (s_press.posed[i].pane == pane && s_press.posed[i].target == target)
            return;
    if (s_press.count < sizeof(s_press.posed) / sizeof(s_press.posed[0]))
        s_press.posed[s_press.count++] = { pane, target, ReadTarget(pane, target) };
}

// 的 t の形を記録してから動かせるようにする（touch と touch_ok の全トラックのペインの元の値を取る）
bool PressBegin(s32 t) {
    s_press = PressSlot();
    if (!ResolvePress(t, s_press.lay, s_press.prefix, s_press.anim))
        return false;
    s_press.target = t;
    for (u32 a = 0; a < 2; ++a)
        for (u32 i = 0; i < s_press.anim[a].count; ++i) {
            const HhdTables::Track &tr = s_press.anim[a].tracks[i];
            char name[24];
            std::snprintf(name, sizeof(name), "%s_%s", s_press.prefix, tr.pane);
            void *pane = FindPane(s_lay[s_press.lay].obj, name);
            if (pane != nullptr)
                PressAdd(pane, tr.target);
        }
    if (t == 900 || t == 901) {
        s_press.color = t == 900 ? HhdTables::kSelectBtn0 : HhdTables::kSelectBtn1;
        s_press.colorCount = t == 900 ? HhdTables::kSelectBtn0Count : HhdTables::kSelectBtn1Count;
        for (u32 i = 0; i < s_press.colorCount; ++i) {
            const HhdTables::BakedColor &tr = s_press.color[i];   // 表は (ペイン, 種類, 番号) ごとに 1 行
            void *obj = ColorObject(tr);
            if (obj != nullptr && s_press.ccount < sizeof(s_press.cposed) / sizeof(s_press.cposed[0]))
                s_press.cposed[s_press.ccount++] = { obj, tr.kind, tr.index, ReadColor(obj, tr.kind, tr.index) };
        }
    }
    return true;
}

void PressPose(u32 which, float f) {
    for (u32 i = 0; i < s_press.colorCount; ++i) {
        const HhdTables::BakedColor &tr = s_press.color[i];
        if (!(tr.has & (which == 0 ? 1u : 2u)))
            continue;
        void *obj = ColorObject(tr);
        if (obj == nullptr)
            continue;
        u32 k = (u32)f;
        if (k > 8)
            k = 8;
        WriteColor(obj, tr.kind, tr.index, which == 0 ? tr.sel : tr.ok[k]);
    }
    const HhdTables::PressAnim &a = s_press.anim[which];
    for (u32 i = 0; i < a.count; ++i) {
        const HhdTables::Track &tr = a.tracks[i];
        char name[24];
        std::snprintf(name, sizeof(name), "%s_%s", s_press.prefix, tr.pane);
        void *pane = FindPane(s_lay[s_press.lay].obj, name);
        if (pane != nullptr)
            WriteTarget(pane, tr.target, EvalKeys(tr.keys, tr.count, f));
    }
}

// 元へ戻す。決定し終えたときの表示（色見本の枠）は Apply が選択に合わせて塗り直す
void PressEnd(bool decided) {
    bool vis = false;
    for (u32 i = 0; i < s_press.count; ++i) {
        const Posed &q = s_press.posed[i];
        if (q.target == 16) {
            vis = true;
            if (decided)
                continue;
        }
        WriteTarget(q.pane, q.target, q.base);
    }
    // 色: 離した（決定しなかった）ときは元へ。決定したときは select_ok の最後の色のまま（「かみがた／かお」は元の色に戻って終わり、
    //   「けってい」は選んだ色のまま閉じる。HHD と同じ）
    if (!decided)
        for (u32 i = 0; i < s_press.ccount; ++i)
            WriteColor(s_press.cposed[i].obj, s_press.cposed[i].kind, s_press.cposed[i].index, s_press.cposed[i].base);
    if (decided && vis)
        s_appliedValid = false;
    s_press = PressSlot();
}

void PressStep(void) {
    const u32 serial = s_okSerial;
    if (serial != s_okSeen) {
        s_okSeen = serial;
        const s32 t = s_okTarget;
        if (s_press.target != t) {
            if (s_press.target >= 0)
                PressEnd(false);
            PressBegin(t);
        }
        if (s_press.target == t && t >= 0) {
            s_press.ok = true;
            s_press.frame = 0.0f;
        }
    }
    if (s_press.ok) {
        PressPose(1, s_press.frame);
        s_press.frame += 1.0f;
        if (s_press.frame > s_press.anim[1].frames)
            PressEnd(true);
        return;
    }
    const s32 want = s_pressTarget;
    if (want != s_press.target) {
        if (s_press.target >= 0)
            PressEnd(false);
        if (want >= 0)
            PressBegin(want);
    }
    if (s_press.target >= 0)
        PressPose(0, s_press.anim[0].frames);   // 触れている間は沈んだ形・選んだ色のまま
}

// ---- プレイヤーへの反映（ゲームのスレッド）----

// HHD の髪の番号 k（0..31: 0〜15 男の子、16〜31 女の子）↔ ACNL の髪型（0..33: 0〜15 男の子、17〜32 女の子、16 / 33 は寝癖）。HHD-F008
s32 AcnlHairFromHhd(s32 k) { return k < 16 ? k : 17 + (k - 16); }
s32 HhdHairFromAcnl(s32 h) {
    if (h >= 0 && h < 16)
        return h;
    if (h >= 17 && h < 33)
        return 16 + (h - 17);
    return -1;                              // 寝癖（16 / 33）: HHD には無い
}

u8 *LookRecord(u32 &playerOut) {
    playerOut = LocalPlayerActor();
    if (!IsHeapPointer(reinterpret_cast<void *>(playerOut)))
        return nullptr;
    const u32 profile = PlayerProfile(*reinterpret_cast<const volatile u8 *>(playerOut + kActorPlayerIndex));
    // ★プロフィールは住人なら村データ（ヒープ）、訪問者（オンラインのゲスト自身）なら固定領域 0xA7E790 + 42,112×k（vc_PLAYER_1_PPOFFSET 0x2FB920）。
    //   ヒープの範囲だけを許すとゲストで弾いていた（hhd_t020b 実機。F092）
    if (profile < 0x00100000u || profile >= 0x40000000u)
        return nullptr;
    return reinterpret_cast<u8 *>(profile + kProfileLook);
}

// 開いたとき: 今の見た目を選択として出す
void LoadCurrentLook(void) {
    u32 player;
    const u8 *rec = LookRecord(player);
    if (rec == nullptr)
        return;
    const s32 hair = HhdHairFromAcnl(rec[kLookHair]);
    s_state.hair = (s8)hair;
    s_state.hairColor = rec[kLookHairColor] < 16 ? (s8)rec[kLookHairColor] : -1;
    s_state.eyeShape = rec[kLookFace] < 12 ? (s8)rec[kLookFace] : -1;
    s_state.eyeColor = rec[kLookEyeColor] < 6 ? (s8)rec[kLookEyeColor] : -1;
    s_state.skin = rec[kLookTan] < 16 ? (s8)(rec[kLookTan] >> 1) : -1;
    if (hair >= 0)
        s_state.page = (s8)(hair / 8);
}

// 状態パケットを 1 つ送る（通し番号はアクターのバッファの +8 を増やして使う。ゲームの送信と同じ）
void SendOnePacket(u32 player, u32 index, u8 *packet) {
    u8 &counter = *reinterpret_cast<u8 *>(player + kActorStatePacket + kPacketCounter);
    counter = (u8)(counter + 1);
    packet[kPacketCounter] = counter;
    SendStatePacket(index, packet);
}

// 上の 4 つを送る。ゲームの送信の前提（部屋の旗 0x10・オンライン番号・+2245 の取り込み中・今の状態が同期する状態か）は検査しない:
// 待つと送れないまま終わった（hhd_t017b 実機。立ち止まりの状態 6 はゲーム自身も送らない状態で、待ちが毎回 10 秒で打ち切られた）。
// 利用者: 状態を待たずに強制的に送る（2026-10-06）。送信関数 vc_SENDPACKETFUNC は自分で「複数人か」（Net_IsMultiplayer）だけ見る。
// 4 つ目はアクターの今のバッファ（ゲームが状態を変えるたびに必ず書く。同期しない状態でも中身は今の本当の状態）
void SendLookToPeers(u32 player) {
    const u32 index = *reinterpret_cast<const volatile u8 *>(player + kActorPlayerIndex);
    u8 *current = reinterpret_cast<u8 *>(player + kActorStatePacket);
    u8 packet[kStatePacketBytes];
    std::memcpy(packet, current, kStatePacketBytes);
    packet[kPacketState] = (u8)kStateAppearance;
    std::memset(packet + kPacketArgs, 0, kStatePacketBytes - kPacketArgs);
    packet[kPacketArgs + 0] = s_peerLook[0];
    packet[kPacketArgs + 1] = s_peerLook[1];
    packet[kPacketArgs + 2] = s_peerLook[2];
    std::memcpy(packet + kPacketArgs + 3, s_peerHat, 4);     // 帽子はそのまま（空にしない）
    SendOnePacket(player, index, packet);
    std::memcpy(packet, current, kStatePacketBytes);
    packet[kPacketState] = (u8)kStateHeadGoods;
    std::memset(packet + kPacketArgs, 0, kStatePacketBytes - kPacketArgs);
    std::memcpy(packet + kPacketArgs, s_peerHat, 4);
    std::memcpy(packet + kPacketArgs + 4, s_peerAccessory, 4);
    SendOnePacket(player, index, packet);
    std::memcpy(packet, current, kStatePacketBytes);
    packet[kPacketState] = (u8)kStateInitial;
    SendOnePacket(player, index, packet);
    // 今の状態: アクターのバッファそのもの（通し番号だけ進める）
    u8 &counter = current[kPacketCounter];
    counter = (u8)(counter + 1);
    SendStatePacket(index, current);
}

bool NetOnline(void) {
    const u32 mgr = *reinterpret_cast<const volatile u32 *>(kNetGameMgrPtr);
    return IsHeapPointer(reinterpret_cast<void *>(mgr)) && *reinterpret_cast<const volatile u8 *>(mgr + kNetOnline) != 0;
}

// けってい の後、送れる条件がそろうまで毎フレーム待って送る（FrameStep の先頭。画面を閉じた後も回る）
void StepPeerSync(void) {
    if (!s_peerWant)
        return;
    const u32 player = LocalPlayerActor();
    u32 reason = kPeerSent;
    if (!NetOnline())
        reason = kPeerOffline;
    else if (player != s_peerPlayer || !IsHeapPointer(reinterpret_cast<void *>(player)))
        reason = kPeerPlayer;
    if (reason == kPeerSent) {
        SendLookToPeers(player);
        s_peerWant = false;
        s_peerLastReason = kPeerSent;
        s_applyResult = u8"反映しました（通信相手へも送りました）";
        return;
    }
    s_peerLastReason = reason;
    s_peerWant = false;
    s_applyResult = reason == kPeerOffline ? u8"反映しました（通信相手へは送れませんでした 6）" : u8"反映しました（通信相手へは送れませんでした 5）";
}

// ---- 目の形を通信相手へ（T020、IDA-opus-5.5-F089）-----------------------------------------------------------------------------
// 相手の写しの目の形（記録 +2）を書けるのはプロフィール丸ごとの転送だけ。参加時の手順 9（sub_61F3DC）と同じ下位種別 11 を自分で送る:
//   片 = 自分のプロフィール（Save_GetCurrentPlayer、42,112 B）+ 通信管理 +78388 の 50 B。宛先は相手ごと（NetTransfer_SendSegments、引数は
//   (管理, 相手, 片, 大きさ, 片の数, 種別, 0, 1, 0) = 参加時と同じ）。送る前に sub_581FE0 で Mii の欄（+21656、状態 +168: 1 → 2）を送る形にし、
//   済んだら sub_6BC270 で戻す（ゲームは成功時だけ戻す。ここでは打ち切りでも必ず戻す）。
// 受け手は写しを上書きし、送り手がホスト以外なら住人の選出をし直す（sub_51D7E4。利用者: どうせ乱数なので構わない 2026-10-06）。
// 相手の画面で目が変わるのは、こちらのモデルを作り直したとき（相手が建物を出入りする。利用者了承）。★下位転送種別 0x0C は使わない。
typedef u32 (*SendSegmentsFn)(u32 mgr, u32 dest, const void *const *ptrs, const u32 *sizes, u32 count, u32 type, u32 a7, u32 enforceMasks, u32 preserveSequence);
typedef u32 (*ProfileOpFn)(u32 profile);
typedef u32 (*CurrentPlayerFn)(void);
const SendSegmentsFn SendSegments = reinterpret_cast<SendSegmentsFn>(0x00616E10);     // NetTransfer_SendSegments
const ProfileOpFn MiiToTransfer = reinterpret_cast<ProfileOpFn>(0x00581FE0);           // 手順 9 の前（sub_6BBE30 の中身）
const ProfileOpFn MiiFromTransfer = reinterpret_cast<ProfileOpFn>(0x006BC270);         // 手順 9 の後（→ sub_582154）
const CurrentPlayerFn SaveCurrentPlayer = reinterpret_cast<CurrentPlayerFn>(0x002FB900);   // Save_GetCurrentPlayer
const u32 kTransferMgrPtr = 0x0094D644;     // g_NetGameMgrPtr（転送の管理。kNetGameMgrPtr 0x94D648 とは別の欄）
const u32 kTransferOwnBlock = 78388, kTransferOwnBlockBytes = 50, kTransferProfile = 11;
const u32 kTransferOwnIndex = 78440, kTransferPeerMask = 78444;   // 自分の番号 / 登録済みの相手のマスク（NetTransfer_SendSegments が宛先 4 で使う）
const u32 kProfileBytes = 42112, kProfileSendFrames = 300;
volatile bool s_profileWant;
bool s_profileConverted;
u32 s_profileFrames, s_profilePtr, s_profilePending;   // 未送信の相手のビット
volatile u32 s_profileResult;               // 0 未 / 1 送った / 2 送れなかった

void ProfileSyncFinish(u32 result) {
    if (s_profileConverted)
        MiiFromTransfer(s_profilePtr);      // Mii の欄を元の形へ（状態 2 でなければ何もしない）
    s_profileConverted = false;
    s_profileWant = false;
    s_profileResult = result;
}

void StepProfileSync(void) {
    if (!s_profileWant)
        return;
    const u32 mgr = *reinterpret_cast<const volatile u32 *>(kTransferMgrPtr);
    const u32 profile = SaveCurrentPlayer();
    if (!NetOnline() || !IsHeapPointer(reinterpret_cast<void *>(mgr)) || profile == 0u || (s_profileConverted && profile != s_profilePtr))
        return ProfileSyncFinish(2);
    s_profilePtr = profile;
    MiiToTransfer(profile);                 // 手順 9 と同じく送る前に毎回（済んでいれば何もしない）
    s_profileConverted = true;
    if (s_profileFrames == 0u) {
        // 宛先は相手ごと（手順 9 と、実機で送れた座標同期と同じ。宛先 4 の一斉送信はやめた: hhd_t020a で反映しなかった）
        const u32 own = *reinterpret_cast<const volatile u8 *>(mgr + kTransferOwnIndex);
        s_profilePending = (*reinterpret_cast<const volatile u8 *>(mgr + kTransferPeerMask) & 0x0Fu) & ~(own < 4 ? 1u << own : 0u);
    }
    const void *ptrs[2] = { reinterpret_cast<const void *>(profile), reinterpret_cast<const void *>(mgr + kTransferOwnBlock) };
    const u32 sizes[2] = { kProfileBytes, kTransferOwnBlockBytes };
    for (u32 peer = 0; peer < 4; ++peer)
        if ((s_profilePending & (1u << peer)) != 0 && SendSegments(mgr, peer, ptrs, sizes, 2, kTransferProfile, 0, 1, 0) != 0u)
            s_profilePending &= ~(1u << peer);
    if (s_profilePending == 0u)
        return ProfileSyncFinish(1);
    if (++s_profileFrames > kProfileSendFrames)
        return ProfileSyncFinish(2);        // 送信が受け付けられない（未確認の送信が残っている等）: 打ち切る
}

// 「けってい」: 選んだものだけ記録へ書き、vc_UPDATE で読み直させる。通信中は相手へも状態パケットで送る（T017。目の形・肌は相手へは送れない）
void ApplyToPlayer(void) {
    const u32 mgr = *reinterpret_cast<const volatile u32 *>(kNetGameMgrPtr);
    const bool online = IsHeapPointer(reinterpret_cast<void *>(mgr)) && *reinterpret_cast<const volatile u8 *>(mgr + kNetOnline) != 0;
    u32 player;
    u8 *rec = LookRecord(player);
    if (rec == nullptr) {
        s_applyResult = u8"プレイヤーが見つからず反映できませんでした";
        return;
    }
    const s32 hair = s_state.hair, hairColor = s_state.hairColor, face = s_state.eyeShape, eyeColor = s_state.eyeColor, skin = s_state.skin;
    const u8 oldFace = rec[kLookFace];
    if (hair >= 0 && hair < 32)
        rec[kLookHair] = (u8)AcnlHairFromHhd(hair);
    if (hairColor >= 0 && hairColor < 16)
        rec[kLookHairColor] = (u8)hairColor;
    if (face >= 0 && face < 12)
        rec[kLookFace] = (u8)face;
    if (eyeColor >= 0 && eyeColor < 6)
        rec[kLookEyeColor] = (u8)eyeColor;
    if (skin >= 0 && skin < 8) {
        rec[kLookTan] = (u8)(skin * 2);
        const u32 pm = player + kActorModel;
        *reinterpret_cast<volatile u32 *>(pm + kModelTan) = (u32)skin;
    }
    PlayerUpdate(player);
    if (online) {
        // 送るのは記録に書いた後の値（選ばなかった欄は今の値）と、今の帽子・アクセサリー（相手側で消さない）
        const u32 profile = reinterpret_cast<u32>(rec) - kProfileLook;
        s_peerLook[0] = rec[kLookHair];
        s_peerLook[1] = rec[kLookHairColor];
        s_peerLook[2] = rec[kLookEyeColor];
        std::memcpy(s_peerHat, reinterpret_cast<const void *>(profile + kProfileHat), 4);
        std::memcpy(s_peerAccessory, reinterpret_cast<const void *>(profile + kProfileAccessory), 4);
        s_peerPlayer = player;
        s_peerWant = true;
    }
    // 目の形は vc_UPDATE では読み直されない（顔の枠は作るときにしか読まない）。本人の顔の枠を新しく読み、頭に読み直させる（F086）
    if (face >= 0 && (u8)face != oldFace)
        PlayerClone::ReloadRealFace();
    if (online && face >= 0 && (u8)face != oldFace) {
        s_profileFrames = 0;
        s_profileResult = 0;
        s_profileWant = true;               // 目の形は丸ごと転送で（相手が建物を出入りすると変わる。T020）
    }
    s_applyResult = !online ? u8"反映しました" : u8"反映しました（通信相手へ送ります）";
}

// ---- 出入りのアニメ（ゲームのスレッド）----

void BindAnim(Lay &l, void *anim) {
    if (l.bound != nullptr)
        GroupUnbind(l.obj, l.bound, l.group, 0);
    GroupBind(l.obj, anim, l.group, 0);
    AnimSetFrame(anim, 0.0f);
    l.bound = anim;
}

// 結んでいるアニメを 1 フレーム進める。終わっていれば外す（値は最後のフレームのまま残る。GameLabel と同じ）。戻り値: まだ動いている
bool StepAnim(Lay &l) {
    if (l.bound == nullptr)
        return false;
    if (AnimFinished(l.bound)) {
        GroupUnbind(l.obj, l.bound, l.group, 0);
        l.bound = nullptr;
        return false;
    }
    AnimStep(l.bound);
    return true;
}

// いま描いているレイアウト（地・顔・目か髪・上）
void DrawnLayouts(u32 (&out)[4]) {
    out[0] = kBg;
    out[1] = kFace;
    out[2] = s_applied.mode == 0 ? kEye : kHair;
    out[3] = kTop;
}

// レイアウトのコマンドリストの記録した長さ（見つからなければ 0xFFFFFFFF）
u32 RecordedBytes(void *layout) {
    const u32 id = W(layout, kLayoutListId);
    u32 m = reinterpret_cast<const volatile u32 *>(kCmdListBuckets)[(id & 0x1Fu) + 7u];
    for (u32 guard = 0; m != 0 && guard < 256; ++guard) {
        if (!IsHeapPointer(reinterpret_cast<void *>(m)))
            break;
        if (*reinterpret_cast<const volatile u32 *>(m) == id)
            return *reinterpret_cast<const volatile u32 *>(m + kCmdMgrUsed);
        m = *reinterpret_cast<const volatile u32 *>(m + kCmdMgrNext);
    }
    return 0xFFFFFFFFu;
}

void Release(void) {
    s_press = PressSlot();
    s_pressTarget = -1;
    for (u32 i = 0; i < kLayouts; ++i) {
        Lay &l = s_lay[i];
        if (l.made) {
            if (l.built && l.loopGroup != nullptr)
                GroupUnbind(l.obj, l.loop, l.loopGroup, 0);   // 繰り返しのアニメは結んだままなので、壊す前に外す（出入りのアニメは終わった時点で外れている）
            if (l.built)
                LayoutFinalize(l.obj);
            LayoutDtor(l.obj);
        }
        if (l.anims) {
            AnimDtor(l.in);
            AnimDtor(l.out);
        }
        if (l.hasLoop)
            AnimDtor(l.loop);
        l.made = l.built = l.anims = l.hasLoop = false;
        l.group = l.bound = l.loopGroup = nullptr;
    }
    std::memset(&s_p, 0, sizeof(s_p));
    s_appliedValid = false;
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = false;
    if (s_arc != nullptr && s_heap != nullptr)
        reinterpret_cast<HeapFreeFn>((*reinterpret_cast<u32 **>(s_heap))[kHeapFreeSlot])(s_heap, s_arc);
    s_arc = s_heap = nullptr;
}

// arc をゲームのヒープへ写し、ArcResAccReader に渡して書体・テクスチャを登録し、レイアウトを組む
void Build(void) {
    const u32 loader = *reinterpret_cast<const volatile u32 *>(kLoaderPtr);
    if (!IsHeapPointer(reinterpret_cast<void *>(loader)))
        return Fail("読み込み係が無い");
    void *heap = *reinterpret_cast<void **>(loader + kLoaderHeap);
    if (!IsHeapPointer(heap))
        return Fail("読み込みのヒープが無い");
    void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
    if (fontMgr == nullptr || FontGet(fontMgr, 0) == nullptr)
        return Fail("ゲームの書体が取れない");
    s_heapFreeBefore = HeapFreeSize(heap);
    const u32 align = *reinterpret_cast<const volatile u32 *>(kLoaderAlignPtr);
    void *arc = reinterpret_cast<HeapAllocFn>((*reinterpret_cast<u32 **>(heap))[kHeapAllocSlot])(heap, s_fileSize, align);
    if (!IsHeapPointer(arc))
        return Fail("ヒープが足りない");
    s_heap = heap;
    s_arc = arc;
    std::memcpy(arc, s_file, s_fileSize);
    FlushRange(arc, s_fileSize);

    ArcCtor(s_holder);
    s_holderMade = true;
    W(s_holder, kHolderArcLoaded) = reinterpret_cast<u32>(arc);
    W(s_holder, kHolderArc) = reinterpret_cast<u32>(arc);
    if (Attach(s_holder + kHolderAccessor, arc, ".") == 0)
        return Fail("DARC を解けない");
    // 書体（BsMenuCatalog_Init と同じ: 名前の器の vt[2] を呼んでから名前 = +4）
    u32 *name = reinterpret_cast<u32 *>(FontName(fontMgr, 0));
    reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(name[0])[2])(name);
    RegisterFont(s_holder + kHolderAccessor, reinterpret_cast<const char *>(name[1]), FontGet(fontMgr, 0));
    RegisterTex(s_holder);

    for (u32 i = 0; i < kLayouts; ++i) {
        Lay &l = s_lay[i];
        LayoutCtor(l.obj);
        l.made = true;
        W(l.obj, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
        if (LayoutBuild(l.obj, kDefs[i].name, nullptr, kDefs[i].cmdBytes) == 0)
            return Fail("レイアウトを組めない");
        l.built = true;
        l.obj[kLayoutPriority] = kPriority;
        s_size[i] = W(l.obj, kLayoutListSize);
        s_used[i] = 0xFFFFFFFFu;
        // 出入りのアニメ: anim/<名前>_in / _out.bclan（tools/hhd/make_acnl_layouts.py の convert_anims）
        char an[40];
        AnimCtor(l.in);
        AnimCtor(l.out);
        l.anims = true;
        const u32 stem = (u32)(std::strlen(kDefs[i].name) - std::strlen(".bclyt"));
        std::snprintf(an, sizeof(an), "%.*s_in.bclan", (int)stem, kDefs[i].name);
        if (AnimLoad(l.in, an, s_holder) == 0)
            return Fail("出入りのアニメを読めない");
        std::snprintf(an, sizeof(an), "%.*s_out.bclan", (int)stem, kDefs[i].name);
        if (AnimLoad(l.out, an, s_holder) == 0)
            return Fail("出入りのアニメを読めない");
        l.group = FindGroup(l.obj, HhdTables::kAnimGroup[i], 1);
        if (l.group == nullptr)
            return Fail("アニメのグループが無い");
        l.bound = nullptr;
        if (HhdTables::kHasLoop[i]) {
            AnimCtor(l.loop);
            l.hasLoop = true;
            std::snprintf(an, sizeof(an), "%.*s_loop.bclan", (int)stem, kDefs[i].name);
            if (AnimLoad(l.loop, an, s_holder) == 0)
                return Fail("繰り返しのアニメを読めない");
            l.loopGroup = FindGroup(l.obj, "G_Loop", 1);
            if (l.loopGroup == nullptr)
                return Fail("G_Loop が無い");
            // 結んだまま毎フレーム進める（UiAnim_Step 0x568964 は繰り返しのアニメを終わりで先頭へ戻す）
            GroupBind(l.obj, l.loop, l.loopGroup, 0);
            AnimSetFrame(l.loop, 0.0f);
        }
    }
    if (!FindAll())
        return Fail("ペインが見つからない");
    // 性別（プレイヤーのプロフィール。取れなければ男の子）
    s_state.sex = 0;
    s_sexByte = 0xFF;
    const u32 player = LocalPlayerActor();
    if (IsHeapPointer(reinterpret_cast<void *>(player))) {
        const u32 profile = PlayerProfile(*reinterpret_cast<const volatile u8 *>(player + kActorPlayerIndex));
        if (profile >= 0x00100000u && profile < 0x40000000u) {   // 訪問者の枠は固定領域（F092）
            s_sexByte = *reinterpret_cast<const volatile u8 *>(profile + kProfileSexByte);
            s_state.sex = (s8)(s_sexByte & 1u);
        }
    }
    s_state.page = (s8)(s_state.sex == 1 ? 2 : 0);    // 自分の性別の髪の最初のページから
    LoadCurrentLook();                                 // 今の見た目を選択として出す（髪があればそのページ）
    s_scrollPub = kPageStep * (float)s_state.page;
    s_scrollSync = true;
    PlacePages();
    s_appliedValid = false;
    s_heapFreeAfter = HeapFreeSize(heap);
    // 入場: 顔・目・上に in を結ぶ（HHD の CharaCreate と同じく in アニメで現れる）。
    //   地の in（HHD: 1 フレームで不透明度 0→255）は結ばず、地と上画面の N_All をコードで HhdTables::kBgFadeFrames かけて 0→255 にする
    //   （= 地の out の逆。利用者 2026-10-05: 背景がフェードで現れない）
    BindAnim(s_lay[kFace], s_lay[kFace].in);
    BindAnim(s_lay[kEye], s_lay[kEye].in);
    BindAnim(s_lay[kTop], s_lay[kTop].in);
    s_fadeFrame = 0;
    B(s_p.bgAll, kPaneAlpha) = 0;
    B(s_p.topAll, kPaneAlpha) = 0;
    s_phase = Phase::Entering;
    s_stage = Stage::Draw;
}

// ---- 入力（メニューのスレッド）----

bool Inside(const HhdTables::Rect &r, s32 x, s32 y) {
    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}

// 的の番号: 0..11 目の形 / 100+ 目の色 / 200+ 肌 / 300+ 髪 / 400+ 髪の色 / 900 左ボタン / 901 右ボタン / -1 なし
s32 HitTest(s32 x, s32 y, s8 mode) {
    if (Inside(HhdTables::kButtonLeft, x, y))
        return 900;
    if (Inside(HhdTables::kButtonRight, x, y))
        return 901;
    if (mode == 0) {
        for (u32 k = 0; k < 12; ++k)
            if (Inside(HhdTables::kEyeShape[k].hit, x, y))
                return (s32)k;
        for (u32 k = 0; k < 6; ++k)
            if (Inside(HhdTables::kEyeColor[k].hit, x, y))
                return 100 + (s32)k;
        for (u32 k = 0; k < 8; ++k)
            if (Inside(HhdTables::kSkinColor[k].hit, x, y))
                return 200 + (s32)k;
    } else {
        for (u32 k = 0; k < 16; ++k)
            if (Inside(HhdTables::kHairColor[k].hit, x, y))
                return 400 + (s32)k;
    }
    return -1;
}

// 髪のページの当たりの中: 800 / 801 左右の端、300 + 髪の番号（0..31）、-1 なし。髪のマスはスクロール位置だけずらして調べる
s32 HitPage(s32 x, s32 y, float scroll) {
    if (Inside(HhdTables::kPageLeft, x, y))
        return 800;
    if (Inside(HhdTables::kPageRight, x, y))
        return 801;
    const s32 center = (s32)(scroll / kPageStep + 0.5f);
    for (s32 page = center - 1; page <= center + 1; ++page) {
        if (page < 0 || page >= kHairPages)
            continue;
        const s32 shift = (s32)(kPageStep * (float)page - scroll + (kPageStep * (float)page - scroll < 0.0f ? -0.5f : 0.5f));
        for (u32 k = 0; k < 8; ++k)
            if (Inside(HhdTables::kHairCell[k].hit, x - shift, y))
                return 300 + page * 8 + (s32)k;
    }
    return -1;
}

// L / R・端のタップ: 隣のページの位置まで HHD の PageSlider の滑り方（Math_ApproachDamped 0x455EF8）を時間に直して滑らせる
void StepToward(float dt) {
    const float frames = dt * kFps;
    float &pos = s_scroller.scroll;
    const float d = s_stepTarget - pos;
    float step = d * (1.0f - powf(1.0f - kApproach, frames));
    const float minStep = kMinStep * frames, maxStep = kStepSpeed * frames;
    if (step < minStep && step > -minStep) {
        const float next = d > 0.0f ? pos + minStep : pos - minStep;
        pos = (d > 0.0f ? next >= s_stepTarget : next <= s_stepTarget) ? s_stepTarget : next;
    } else {
        if (step > maxStep)
            step = maxStep;
        if (step < -maxStep)
            step = -maxStep;
        pos += step;
    }
    if (pos == s_stepTarget)
        s_stepping = false;
}

// 的を「決定」したとき（HHD と同じく、指を置いた的の上で離したら決定）
void Decide(s32 target) {
    s_okTarget = target;                    // 押したときのアニメの touch_ok（ゲームのスレッド）
    s_okSerial = s_okSerial + 1;
    QueueSound(target == 900 ? (s_state.mode == 0 ? kSndToHair : kSndToFace) : target == 901 ? kSndDecide : target == 800 ? kSndPageDec : target == 801 ? kSndPageInc
               : target >= 400 ? kSndColor : target >= 300 ? kSndPick : target >= 100 ? kSndColor : kSndPick);
    if (target == 900)
        QueueSound(kSndModeExtra);
    if (target == 900) {
        s_state.mode = s_state.mode == 0 ? 1 : 0;
    } else if (target == 901) {
        s_applyReq = true;                  // けってい: 閉じ始めにプレイヤーへ反映（ゲームのスレッド）
        s_want = false;
    } else if (target == 800 || target == 801) {
        s_pageReq = target == 800 ? -1 : 1;
    } else if (target >= 400) {
        s_state.hairColor = (s8)(target - 400);
    } else if (target >= 300) {
        s_state.hair = (s8)(target - 300);  // 髪の番号（HitPage がスクロール位置込みで決めた）
    } else if (target >= 200) {
        s_state.skin = (s8)(target - 200);
    } else if (target >= 100) {
        s_state.eyeColor = (s8)(target - 100);
    } else if (target >= 0) {
        s_state.eyeShape = (s8)target;
    }
}

}  // namespace

bool Show(void) {
    if (s_stage != Stage::Idle && s_stage != Stage::Failed)
        return true;
    if (s_stage == Stage::Failed) {
        s_error = "前回の失敗を片付け中";
        return false;
    }
    if (s_file == nullptr) {
        File f;
        if (File::Open(f, kArcPath, File::READ) != File::SUCCESS) {
            s_error = "SD に /hhd_charcreate.arc が無い";
            return false;
        }
        const u64 size = f.GetSize();
        if (size < 0x20 || size > kMaxArcBytes) {
            f.Close();
            s_error = "arc の大きさがおかしい";
            return false;
        }
        u8 *buf = new u8[(u32)size];
        if (f.Read(buf, (u32)size) != File::SUCCESS || std::memcmp(buf, "darc", 4) != 0) {
            f.Close();
            delete[] buf;
            s_error = "arc を読めない";
            return false;
        }
        f.Close();
        s_fileSize = (u32)size;
        s_file = buf;
    }
    if (!s_hookReady) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep)) {
            s_error = "フレームフックを入れられない";
            return false;
        }
        s_hookReady = true;
    }
    // 元の下画面 UI（地図・タブ）をゲーム自身の命令で退場させておく（建物エディターと同じ GameList の手順）。
    //   隠れきってから組む（地図の arc が返ってから、こちらの arc を同じヒープに取る）
    if (!GameList::HoldField(true)) {
        s_error = "フレームフックを入れられない";
        return false;
    }
    s_state.mode = 0;
    s_state.eyeShape = s_state.eyeColor = s_state.skin = s_state.hair = s_state.hairColor = -1;
    s_state.page = 0;
    s_pageReq = 0;
    s_applyReq = false;
    s_applyResult = "";
    s_touchPrev = false;
    s_touchStart = -1;
    s_onPage = false;
    s_keysPrev = 0xFFFFFFFFu;               // 開いたときに押していたボタンは「押した」に数えない
    s_error = "";
    s_cloneYaw = 0.0f;
    s_hideHead = false;
    s_padTick = 0;
    s_soundTail = s_soundHead;
    PlayerClone::SetScreen(true, 0, kClonePitch, kCloneX, kCloneY, kCloneZoom);
    PlayerClone::SetAlpha(0);
    PlayerClone::Show();                    // 吹き出しの複製（作れなければ出ないだけ）
    s_want = true;
    s_stage = Stage::Copy;
    return true;
}

void Hide(void) {
    s_want = false;
}

bool Shown(void) {
    return s_want;
}

const char *LastError(void) {
    return s_error;
}

// 診断: 通信番号 0〜3 のプロフィール（自分は本物、相手は写し）の目の形を 16 進 1 字ずつ（無ければ -）。種別 11 が届いたかを相手側で見る（T020）
void PeerFaces(char *out, u32 size) {
    if (size < 5)
        return;
    for (u32 i = 0; i < 4; ++i) {
        const u32 profile = PlayerProfile(i);
        const u32 face = profile >= 0x00100000u && profile < 0x40000000u ? *reinterpret_cast<const volatile u8 *>(profile + kProfileLook + kLookFace) : 0xFFu;
        out[i] = face < 16u ? "0123456789abcdef"[face] : '-';
    }
    out[4] = 0;
}

u32 ProfileSyncResult(void) {
    return s_profileResult;
}

const char *ApplyResult(void) {
    return s_applyResult;
}

const char *StageName(void) {
    switch (s_stage) {
    case Stage::Idle: return u8"止まっている";
    case Stage::Copy: return u8"下画面の片付け待ち";
    case Stage::Draw: return u8"下画面に出している";
    case Stage::Teardown: return u8"片付け中";
    case Stage::Failed: return u8"失敗";
    }
    return u8"";
}

void Measure(char *out, u32 size) {
    // レイアウトごとに 記録した長さ/確保した大きさ（バイト。地・顔・目・髪・上）。ヒープは組む前と後の空き（KB）
    //   見出しは ASCII（通知はメニューの UI フォント = 美咲の一部 362 字で描くので、性・命令・空き・→ などが欠けた。利用者 2026-10-05）
    std::snprintf(out, size, "sex %u cmd %lu/%lu %lu/%lu %lu/%lu %lu/%lu top %lu/%lu heap %luK>%luK", (unsigned)s_sexByte,
                  (unsigned long)s_used[0], (unsigned long)s_size[0], (unsigned long)s_used[1], (unsigned long)s_size[1],
                  (unsigned long)s_used[2], (unsigned long)s_size[2], (unsigned long)s_used[3], (unsigned long)s_size[3],
                  (unsigned long)s_used[4], (unsigned long)s_size[4],
                  (unsigned long)(s_heapFreeBefore / 1024u), (unsigned long)(s_heapFreeAfter / 1024u));
    const u32 n = (u32)std::strlen(out);
    if (n + 1 < size) {
        out[n] = ' ';
        PlayerClone::RealFaceInfo(out + n + 1, size - n - 1);   // けってい の後の本人の目の形の差し替え（F086）
    }
}

void Tick(bool menuVisible) {
    if (!s_want && s_stage == Stage::Idle)
        return;
    // 開いている間はゲームへの入力を全部止める。BlockGameAll はボタンとスライドパッドだけで、タッチは BlockGameTouch が別に止める
    //   （GuiMenu.cpp SyncInputLock。2026-10-05 実機: BlockGameAll だけでは下の地図・タブが反応した）
    GuiMenu::BlockGameAll();
    GuiMenu::BlockGameTouch();
    if (!s_want || s_stage != Stage::Draw || s_phase != Phase::Live) {
        s_touchPrev = false;
        s_touchStart = -1;
        s_onPage = false;
        s_pressTarget = -1;
        return;                             // 組み立て待ち・出入りのアニメ中・片付け中は止めるだけ
    }
    if (menuVisible) {
        s_touchPrev = false;
        s_touchStart = -1;
        if (s_onPage)
            s_scroller.Release(svcGetSystemTick());
        s_onPage = false;
        s_pressTarget = -1;
        return;
    }
    const u32 keys = Controller::GetKeysDown(true);
    const u32 pressed = keys & ~s_keysPrev;
    s_keysPrev = keys;
    if (pressed & (u32)Key::B) {
        QueueSound(kSndCancel);
        s_want = false;                     // B: 閉じる
        return;
    }
    if (s_state.mode == 1 && (pressed & ((u32)Key::L | (u32)Key::R))) {
        s_pageReq = (pressed & (u32)Key::L) ? -1 : 1;   // 髪のページ送り（HHD-F007: L / R）
        QueueSound((pressed & (u32)Key::L) ? kSndPageDec : kSndPageInc);
    }
    if (pressed & (u32)Key::Y) {
        // Y: 頭の小物（帽子・アクセサリー）を外して見せる / 付ける（複製だけ。音は美容室の付け外しの音）
        s_hideHead = !s_hideHead;
        QueueSound(s_hideHead ? kSndHeadOff : kSndHeadOn);
    }
    {
        // スライドパッドで複製を回す（T019: 上限なし、倒した量に比例した速さ）
        const u64 t = svcGetSystemTick();
        const float dt = s_padTick != 0 ? (float)(t - s_padTick) / (float)SYSCLOCK_ARM11 : 0.0f;
        s_padTick = t;
        const shortVector pad = Controller::GetCirclePadPosition();
        s32 px = pad.x;
        if (px > -kPadDead && px < kPadDead)
            px = 0;
        if (px > kPadFull)
            px = kPadFull;
        if (px < -kPadFull)
            px = -kPadFull;
        if (dt > 0.0f && dt < 0.5f && px != 0) {
            s_cloneYaw += kYawPerSecond * dt * (float)px / (float)kPadFull;
            while (s_cloneYaw >= 360.0f)
                s_cloneYaw -= 360.0f;
            while (s_cloneYaw < 0.0f)
                s_cloneYaw += 360.0f;
        }
        PlayerClone::SetScreen(true, (s32)s_cloneYaw, kClonePitch, kCloneX, kCloneY, kCloneZoom);
        const s32 hair = s_state.hair;
        PlayerClone::SetHair(hair >= 0 && hair < 32 ? AcnlHairFromHhd(hair) : -1, s_state.hairColor);
        PlayerClone::SetLook(s_state.skin, s_hideHead);
        PlayerClone::SetEyes(s_state.eyeShape, s_state.eyeColor);   // 目の形を変えると複製は裏で作り直され、できた瞬間に入れ替わる
    }
    const bool down = Touch::IsDown();
    const s8 mode = s_state.mode;
    const u64 now = svcGetSystemTick();
    if (s_scrollSync) {
        s_scrollSync = false;
        s_scroller.scroll = s_scrollPub;
        s_scroller.Stop();
        s_stepping = false;
    }
    s_scroller.max = kPageStep * (float)(kHairPages - 1);
    // 髪のページ: 当たりの中で触れ始めた指はスクロールとタップ（漢字変換の候補欄と同じ: 6 画素を越えたらスクロール、慣性中の触れ方は止めるだけ）
    if (mode == 1 && down && !s_touchPrev && !s_onPage) {
        const UIntVector pos = Touch::GetPosition();
        if (Inside(HhdTables::kPageArea, (s32)pos.x, (s32)pos.y)) {
            const bool wasMoving = s_scroller.Press((float)pos.x, now) || s_stepping;
            s_stepping = false;
            s_onPage = true;
            s_touchStart = wasMoving ? -1 : HitPage((s32)pos.x, (s32)pos.y, s_scroller.scroll);
            if (s_touchStart >= 300 && s_touchStart < 400)
                QueueSound(kSndTouch);      // 髪のマスに触れた（端は送ったときに鳴らす）
        }
    }
    if (s_onPage) {
        if (down) {
            const UIntVector pos = Touch::GetPosition();
            s_scroller.Hold((float)pos.x, now);
            if (s_scroller.Scrolling() || HitPage((s32)pos.x, (s32)pos.y, s_scroller.scroll) != s_touchStart)
                s_touchStart = -1;          // スクロールになった・的から外れた（HHD の TouchUnSelect）
        } else {
            s_scroller.Release(now);
            if (s_touchStart >= 0)
                Decide(s_touchStart);
            s_touchStart = -1;
            s_onPage = false;
        }
        s_touchPrev = down;
        s_pressTarget = down ? s_touchStart : -1;
        s_scrollPub = s_scroller.scroll;
        return;
    }
    if (mode == 1) {
        const s8 req = s_pageReq;
        s_pageReq = 0;
        if (req != 0) {
            s32 next = (s32)(s_scroller.scroll / kPageStep + 0.5f) + req;
            if (next < 0)
                next = 0;
            if (next >= kHairPages)
                next = kHairPages - 1;
            s_scroller.Stop();
            s_stepTarget = kPageStep * (float)next;
            s_stepping = s_stepTarget != s_scroller.scroll;
            s_stepTick = now;
        }
        if (s_stepping) {
            const float dt = (float)(now - s_stepTick) / (float)SYSCLOCK_ARM11;
            s_stepTick = now;
            StepToward(dt);
        } else {
            s_scroller.Idle(now);           // 慣性
        }
        s_scrollPub = s_scroller.scroll;
    }
    // タッチ（ページの当たりの外）: 置いた的の上で離したら決定。途中で的から外れたら取り消し（HHD の TouchUnSelect）
    if (down) {
        const UIntVector pos = Touch::GetPosition();
        const s32 hit = HitTest((s32)pos.x, (s32)pos.y, mode);
        if (!s_touchPrev) {
            s_touchStart = hit;
            if (hit >= 0)
                QueueSound(kSndTouch);
        }
        else if (hit != s_touchStart)
            s_touchStart = -1;
    } else if (s_touchPrev) {
        if (s_touchStart >= 0)
            Decide(s_touchStart);
        s_touchStart = -1;
    }
    s_touchPrev = down;
    s_pressTarget = down ? s_touchStart : -1;
}

void FrameStep(void) {
    StepPeerSync();
    StepProfileSync();
    switch (s_stage) {
    case Stage::Idle:
        return;
    case Stage::Copy:
        if (!s_want) {
            PlayerClone::Hide();
            GameList::HoldField(false);
            s_stage = Stage::Idle;
            return;
        }
        if (!GameList::FieldHidden())
            return;                         // 元の下画面 UI が退場し終わるのを待つ
        Build();
        if (s_stage != Stage::Draw)
            return;
        PlaySound(kSndOpen);
        // fallthrough: 組めたフレームから描く
    case Stage::Draw: {
        u32 drawn[4];
        DrawnLayouts(drawn);
        if (!s_want && s_phase != Phase::Leaving) {
            PlayerClone::Hide();            // 閉じ始め: 複製を片付け、Scene 1 から戻す（StepMenuScene）
            if (s_applyReq) {
                s_applyReq = false;
                ApplyToPlayer();
            }
            // 退場: 描いている 4 枚に out を結ぶ（入場の途中でも out を頭から）
            for (u32 n = 0; n < 4; ++n)
                BindAnim(s_lay[drawn[n]], s_lay[drawn[n]].out);
            s_phase = Phase::Leaving;
        }
        bool moving = false;
        for (u32 n = 0; n < 4; ++n)
            moving = StepAnim(s_lay[drawn[n]]) || moving;
        for (u32 i = 0; i < kLayouts; ++i)
            if (s_lay[i].hasLoop)
                AnimStep(s_lay[i].loop);
        if (s_phase == Phase::Entering && s_fadeFrame < HhdTables::kBgFadeFrames) {
            ++s_fadeFrame;
            const u8 a = (u8)(255u * s_fadeFrame / HhdTables::kBgFadeFrames);
            B(s_p.bgAll, kPaneAlpha) = a;
            B(s_p.topAll, kPaneAlpha) = a;
            moving = true;
        }
        if (!moving) {
            if (s_phase == Phase::Entering)
                s_phase = Phase::Live;
            else if (s_phase == Phase::Leaving) {
                PlayerClone::SetHair(-1, -1);   // 普段の複製（チートの項目）に戻すとき本物のままになるように
                PlayerClone::SetLook(-1, false);
                PlayerClone::SetEyes(-1, -1);
                PlayerClone::SetAlpha(255);
                // 退場し終えた: このフレームから描かない。壊すのは数フレーム後
                s_stage = Stage::Teardown;
                s_wait = 0;
                return;
            }
        }
        // 前のフレームで記録した長さ（このフレームの記録より前に読む）
        for (u32 i = 0; i < kLayouts; ++i)
            s_used[i] = RecordedBytes(s_lay[i].obj);
        PlacePages();
        Apply();
        PressStep();
        StepMenuScene();
        FlushSounds();
        const u32 third = s_applied.mode == 0 ? kEye : kHair;
        const u32 order[3] = { kBg, kFace, third };
        void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
        for (u32 n = 0; n < 3; ++n) {
            LayoutCalc(s_lay[order[n]].obj);
            if (mgr != nullptr)
                AddLayout(mgr, s_lay[order[n]].obj, kScreenLower);
        }
        // 上画面: 最前面（0xFF）なので、ゲームの 3D と上画面の UI（時計など）より手前に全面を覆う
        LayoutCalc(s_lay[kTop].obj);
        if (mgr != nullptr)
            AddLayout(mgr, s_lay[kTop].obj, kScreenUpper);
        return;
    }
    case Stage::Teardown:
        LeaveMenuSceneNow();
        FlushSounds();
        // 描くのをやめてから数フレーム待って壊す（GPU がまだ読んでいるかもしれない。GameLabel と同じ）
        if (++s_wait < kTeardownWaitFrames)
            return;
        Release();
        GameList::HoldField(false);         // 全部返してから元の下画面 UI を戻させる
        s_stage = Stage::Idle;
        return;
    case Stage::Failed:
        // 途中まで作ったものを返す。描いていないので待たなくてよい
        LeaveMenuSceneNow();
        PlayerClone::Hide();
        Release();
        GameList::HoldField(false);
        s_want = false;
        s_stage = Stage::Idle;
        return;
    }
}

}  // namespace HhdScreen

// ---------------------------------------------------------------------------------------
// gohan のメニューとの結び付け（root/テスト/HHD キャラクリ）
// ---------------------------------------------------------------------------------------

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            // HHD式スタイル変更（アクション）: 開いていれば閉じ、閉じていれば開く（利用者 2026-10-06）
            void    HhdToggle(int index)
            {
                (void)index;
                if (HhdScreen::Shown())
                {
                    HhdScreen::Hide();
                    return;
                }
                if (!HhdScreen::Show())
                    GuiNotification::NotifyRed(kHhdStyle, HhdScreen::LastError());
            }

            // 入力と組み立ての進行（メニュースレッド）。アクション項目には OnTick が来ないので毎フレームの窓口から呼ぶ。
            //   閉じていれば Tick の先頭で何もせず戻る
            void    HhdFrameTick(u16 held)
            {
                (void)held;
                HhdScreen::Tick(GuiMenu::IsVisible());
            }

            void    HhdStatus(int index)
            {
                (void)index;
                static char message[192];
                static char measure[128];
                char faces[8];
                HhdScreen::PeerFaces(faces, sizeof(faces));   // 診断は先頭に（長いと末尾が画面に入らない。hhd_t020b 実機で pf が見えなかった）
                const char *err = HhdScreen::LastError();
                HhdScreen::Measure(measure, sizeof(measure));
                if (err[0] != 0)
                    std::snprintf(message, sizeof(message), u8"%s: %s", HhdScreen::StageName(), err);
                else if (HhdScreen::ApplyResult()[0] != 0)
                {
                    PlayerClone::RealFaceInfo(measure, sizeof(measure));   // けってい の後は本人の目の形の差し替えと、目の形の送信（pf）だけ出す
                    std::snprintf(message, sizeof(message), u8"pf%lu e%s %s %s", (unsigned long)HhdScreen::ProfileSyncResult(), faces, measure,
                                  HhdScreen::ApplyResult());
                }
                else
                    std::snprintf(message, sizeof(message), u8"e%s %s %s", faces, HhdScreen::StageName(), measure);
                GuiNotification::Notify(kHhdStat, message);
            }
        }

        void    WireHhdScreen(void)
        {
            const int style = GuiMenu::FindItem(kHhdStyle);
            const int stat = GuiMenu::FindItem(kHhdStat);
            if (style >= 0)
                GuiMenu::RegisterExecute(style, HhdToggle);
            GuiMenu::SetFrameTick(HhdFrameTick);
            if (stat >= 0)
                GuiMenu::RegisterExecute(stat, HhdStatus);
        }
    }
}
