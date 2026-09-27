#include "MapEditor.hpp"

#include "BuildingEditor.hpp"
#include "Cheats.hpp"
#include "CursorRepeat.hpp"
#include "FieldCamera.hpp"
#include "GameList.hpp"
#include "GridCursor.hpp"
#include "GridCursorGameApi.hpp"
#include "GuiDialog.hpp"
#include "GuiMenu.hpp"
#include "ItemNames.hpp"
#include "MapEditor3D.hpp"
#include "PublicWorks.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

using namespace CTRPluginFramework;

// 設計と根拠: MapEditor.hpp の説明、FINDINGS IDA-opus-5.5-F050（模様替え UI・名前の吹き出し）、
// GameList.cpp（Layout の組み立て手順。IDA-opus-5.5-F036）、FieldCamera.cpp（カメラ）。

namespace MapEditor {

namespace {

// ---- ゲーム側（Layout の部品。番地と使い方は GameList.cpp と同じ）----------------------------------------
typedef void *(*CtorFn)(void *self);
typedef int (*ArcLoadStepFn)(void *holder, const char *path);
typedef int (*LayoutBuildFn)(void *layout, const char *name, void *holder, u32 cmdBytes);
typedef void (*LayoutFn)(void *layout);
typedef void *(*FindPaneFn)(void *layout, const char *name);
typedef int (*AnimLoadFn)(void *anim, const char *name, void *holder);
typedef void (*AnimBindFn)(void *layout, void *anim);
typedef void (*AnimSetFrameFn)(void *anim, float frame);       // 値は VFP s0（hard-float でそのまま）
typedef void (*AnimFn)(void *anim);
typedef int (*AnimFinishedFn)(void *anim);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef int (*RegisterTexFn)(void *holder);

const CtorFn         ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);         // ArcResAccReader（584 B）
const CtorFn         ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const ArcLoadStepFn  ArcLoadStep    = reinterpret_cast<ArcLoadStepFn>(0x00567244);  // 終わるまで 0
const RegisterTexFn  RegisterTex    = reinterpret_cast<RegisterTexFn>(0x00568CFC);  // arc の .bclim を全部
const CtorFn         LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);         // ssys::ma::lyt::Layout（332 B）
const CtorFn         LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutBuildFn  LayoutBuild    = reinterpret_cast<LayoutBuildFn>(0x005685A4);
const LayoutFn       LayoutUnbindAll = reinterpret_cast<LayoutFn>(0x00133A5C);      // nw::lyt::Layout vt+32(0)（GameList の LayoutFinalize）
const LayoutFn       LayoutCalc     = reinterpret_cast<LayoutFn>(0x00568030);       // vc_ACSYSTEM_DATA2
const FindPaneFn     FindPane       = reinterpret_cast<FindPaneFn>(0x00567BAC);
const CtorFn         AnimCtor       = reinterpret_cast<CtorFn>(0x001261EC);         // UiAnim（40 B）
const CtorFn         AnimDtor       = reinterpret_cast<CtorFn>(0x00568CAC);
const AnimLoadFn     AnimLoad       = reinterpret_cast<AnimLoadFn>(0x00568A04);
const AnimBindFn     AnimBind       = reinterpret_cast<AnimBindFn>(0x004B8ECC);
const AnimBindFn     AnimUnbind     = reinterpret_cast<AnimBindFn>(0x00568840);
const AnimSetFrameFn AnimSetFrame   = reinterpret_cast<AnimSetFrameFn>(0x00568C00);
const AnimFn         AnimStep       = reinterpret_cast<AnimFn>(0x00568964);
const AnimFinishedFn AnimFinished   = reinterpret_cast<AnimFinishedFn>(0x0074F58C);
const AddLayoutFn    AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);
const u32 kLayoutMgrPtr = 0x0096FC38;       // u32: ssys::ma::lyt::LayoutMgr
const u32 kLytAllocatorPtr = 0x0096FC3C;    // u32: ssys::ma::Allocator（+4 = sead::ExpHeap）
const u32 kRoomIdFn = 0x002F75CC;           // Room_GetCurrentId

// Layout / Pane の欄（GameList.cpp と同じ）
const u32 kLayoutPriority = 12;             // u8: 下画面の並び（同じ値なら入れた順に描く）
const u32 kLayoutHolder = 236;              // ArcResAccReader*
const u32 kPaneTranslateX = 40, kPaneTranslateY = 44, kPaneTranslateZ = 48;
const u32 kPaneRotateX = 52, kPaneRotateY = 56, kPaneRotateZ = 60;
const u32 kPaneFlagsByte = 183;             // bit0 = 表示。SRT を書いたら 0x30 を落とす（ゲームと同じ: & 0xCF）
const u32 kPaneGlobalX = 140, kPaneGlobalY = 156;   // 大域行列の平行移動（ItemSelectNameWindow_UpdatePosition 0x31D908 が読む欄）

// ---- 盤面・コマ（chip.arc。IDA-opus-5.5-F050）----
const char kChipArc[] = "Layout/chip/chip.arc";
const u32 kBoardCmdBytes = 24576;           // RoomLayoutWindow（sub_B3DD6C）が渡す大きさ
const u32 kChipCmdBytes = 1280;             // Chip の組み立て（sub_B420C4）が渡す大きさ
// chip_room_00_kind: 大きさのキーはフレーム 0/1/2 = 80/120(128)/160（4x4/6x6/8x8）で終わり、3/4 は扉の位置だけが違う 8x8。
//   ゲーム（RoomLayoutWindow sub_B21FA8）が使うのは 0/1/3/4 で、2 は使わない（2 は同じフレームにキーが 2 つある境目）。
//   8x8 のうちゲームが使う 4.0 にする（扉は隠す）
const float kKindFrame = 4.0f;
const float kTile = 20.0f;                  // 盤面の 1 マス（エディター +129560）
// 利用者指示 2026-09-27: fgobj は通常アイテムと同じ C（色だけ濃い緑）、建物は全部 1x1 の P（色はオレンジ）
enum ChipType : u8 { kItemC, kFgobjC, kBuild11, kChipTypes };
const char *const kChipLayouts[kChipTypes] = {
    "cip_01C_02x02.bclyt", "cip_01C_02x02.bclyt", "cip_01P_02x02.bclyt",
};
const u32 kMaxChips = 160;                  // 8x8 のアイテム 64 + 建物のコマ（1x1 に割っても 64）+ 余裕
const u32 kBuildsPerFrame = 8;              // 1 フレームで組み立てるコマの数（組み立ての山を平らにする）
const u32 kLytReserve = 192 * 1024;         // nw::lyt ヒープにこれだけは残す（ほかの UI の分）
const u32 kTeardownWaitFrames = 3;          // 描画登録をやめてから壊すまで（GPU がまだ読んでいるかもしれない）

// ---- 名前の吹き出し（ItemSelectNameWindow。IDA-opus-5.5-F050）----
typedef int (*NameStepFn)(void *win);
typedef int (*NameBuildFn)(void *win, void *holder);
typedef void (*NameShowFn)(void *win, void *word, void *pane, const float *offset);
typedef void (*NameFn)(void *win);
typedef void *(*ItemWordFn)(void *buf, const u16 *item, u32 zero);
const CtorFn         NameCtor       = reinterpret_cast<CtorFn>(0x0031E2B0);
const CtorFn         NameDtor       = reinterpret_cast<CtorFn>(0x0031E384);         // vt[0]（delete しない方）
const NameStepFn     NameLoadStep   = reinterpret_cast<NameStepFn>(0x0031E26C);     // itm_slct_win.arc。済めば非 0
const NameBuildFn    NameBuild      = reinterpret_cast<NameBuildFn>(0x0031DECC);    // (窓, 0 = 自前の保持体)
const NameShowFn     NameShow       = reinterpret_cast<NameShowFn>(0x0031DB88);
const NameFn         NameHide       = reinterpret_cast<NameFn>(0x0031DB64);
const ItemWordFn     ItemWord       = reinterpret_cast<ItemWordFn>(0x002FE9FC);     // script::WordFix<33>（96 B）にアイテム名
const u32 kNameBytes = 1280;                // 1,262 B 以上（ctor が +1261 まで書く）
const u32 kNameLayout = 624;                // 窓の Layout（描画 = AddLayout(mgr, 窓 + 624, 1)。sub_31DEB0）
const u32 kNameStateFn = 12, kNameStateAdj = 16;    // 状態の calc（メンバ関数ポインタ）

// ---- 村 ----
typedef u32 *(*ItemAtFn)(u32 field, s32 x, s32 y, u32 zero);
typedef float (*GroundHeightFn)(const float *pos, u32 zero);
const ItemAtFn       ItemAt         = reinterpret_cast<ItemAtFn>(0x002FEE38);       // Field_GetItemAtXY（無ければ 0）
// 置く・消す（IDA-opus-5.5-F051）。消すはゲームの Field_DeleteItemAt（空へ書き、記録を残し、fgobj があれば Field_MarkDirty）。
//   置くは同じ形で: Field_SetItemAtWorldXY で書いて、fgobj（off_948E70）があれば Field_MarkDirty(x, y, 1)
typedef int (*SetItemFn)(u32 field, const u32 *item, s32 x, s32 y, u32 zero);
typedef void (*DeleteItemFn)(s32 x, s32 y, u32 field);
typedef void (*MarkDirtyFn)(u32 x, u32 y, u32 destroy);
const SetItemFn      SetItem        = reinterpret_cast<SetItemFn>(0x002FC950);      // Field_SetItemAtWorldXY
const DeleteItemFn   DeleteItem     = reinterpret_cast<DeleteItemFn>(0x006FA828);   // Field_DeleteItemAt(x, y, 部屋データ)
const MarkDirtyFn    MarkDirty      = reinterpret_cast<MarkDirtyFn>(0x0059DA7C);    // Field_MarkDirty（PublicWorks と同じ）
const u32 kFgobjPtr = 0x00948E70;           // off_948E70: fgobj が居るか（sub_59CEC0）
const GroundHeightFn GroundHeight   = reinterpret_cast<GroundHeightFn>(0x006C69C0);
const u32 kFieldPtr = 0x009AEA04;           // u32: Field_GetMapManager 0x6A53DC が返す（+0/+4/+8/+C = エーカーの最小 x, y / 最大 x, y）
const u16 kEmptyItem = 0x7FFE;              // Item_IsEmpty 0x2FCB24
const u16 kFgobjMax = 0xFD;                 // Item_IsFieldObj 0x2FCCBC
const s32 kTilesX = 112, kTilesY = 96;      // 村のマス（7 x 6 エーカー）
// タッチ（ゲームの模様替えのチップ Select 状態 0xB429BC と同じ閾値: 押した点から 24.0 動いたら長押しをやめる）
const float kHoldSlop = 24.0f;
const u32 kPickFrames = 15;                 // スポイトの長押し（30fps で 0.5 秒）
const u32 kPickShowFrames = 6;              // 進捗バーはタッチから 200ms（6 フレーム）で出す（利用者指示 2026-09-27）
const u32 kNoItem = 0xFFFFFFFFu;
// カメラの目標は盤面の中心より 1 マス南（利用者指示 2026-09-27: 盤面の下一列が上画面から外れていた）
const float kCameraSouthTiles = 1.0f;
// コマの色（利用者指示 2026-09-27）。テクスチャは灰色（LA4）で、色はマテリアルの黒色・白色（Material +0x10 / +0x14。F-291）。
//   元は C = #3F930F/#B4FF14（黄緑）、N・P = #1B7348/#00CA79（青緑）。fgobj = 通常アイテムより濃い緑、建物 = オレンジ
const u8 kFgobjBlack[4] = { 0x1C, 0x4F, 0x07, 0x00 };
const u8 kFgobjWhite[4] = { 0x5D, 0xAE, 0x12, 0xFF };
const u8 kBuildBlack[4] = { 0x7A, 0x3E, 0x10, 0x00 };
const u8 kBuildWhite[4] = { 0xFF, 0xA0, 0x3C, 0xFF };
const u32 kPictureMaterial = 316;           // nw::lyt::Picture +0x13C = Material*（ctor 0x4BAA68。F-291）
const u32 kMatColors = 0x10;                // Material +0x10.. 色 7 個（[0] 黒色 / [1] 白色）
const u32 kMatFlags = 0x4D;                 // bit2 を落とすと GPU へ送り直す（GameList と同じ）
// ---- 範囲選択（段階 3）。ゲームの模様替え（ModuleIndoor の CollectChip / Chip、ModuleFtr の移動）に合わせる（IDA-opus-5.5-F053）----
// 持ち上げる・写すアイテムの数。以前は 64（UnitCursor の最大）で、大きな範囲は 65 個目から掴めなかった（利用者報告 2026-09-27）。
//   上画面のカーソル・複製と下画面のコマは盤面の周りだけ作るので、ここは村の物の数だけ持てればよい
const u32 kMaxCarry = 2048;
const u32 kMaxCursorTiles = 128;            // 上画面の UnitCursor。盤面 8x8 の周り 1 マスで最大 100 か所（GridCursor::kMaxCursors 256 以下）
const s32 kNearMargin = 1;                  // 盤面の周り何マスまで上画面に出すか（カーソル・複製）
const u32 kCursorBlue = 0x00FFB060u;        // BuildingHighlight::kBlue（公共事業エディターの移動の色）
const u8 kCursorTint = 0xB0;                // 公共事業エディターと同じ強さ
const u32 kLiftFrames = 6;                  // 範囲の中の長押し（ゲームのチップ Select 状態と同じ 6 フレーム）
const float kGroupPad = 10.0f;              // 枠の大きさ = |差| + 10（CollectChip sub_B0ABF8 の flt_B8E240）
const u32 kGroupCmdBytes = 4096;            // CollectChip の組み立て（sub_B0AD88）が渡す大きさ
// 範囲の点の吸着（CollectChip sub_B359D4）: u = 指 − 部屋の中心 に (1 マス × 0.5 − 1) を u の符号つきで足し、
//   1 マスで割って 0 へ切り捨て、1 マスを掛ける = 最寄りの格子線（ちょうど半分はやや 0 寄り）
const float kSnapBias = kTile * 0.5f - 1.0f;
// 持ち上げたコマの行き先（ModuleFtr sub_B147EC）: 単位の境目から 2.0（世界の長さ。1 マス = 32）以内なら前の単位のまま
const float kCarryHysteresis = 2.0f / 32.0f;
const float kMoveSoundScale = 0.00625f;     // 移動の音の引数 = 行き先の世界 x × 0.00625（ModuleFtr 0xB070A8 の VLDR 0x3BCCCCCD）
const float kWorldTile = 32.0f;
// 音（名前は reference/old_project/SOUND/index/sounds.csv）。
//   ゲームの模様替えの音 SE_SYS_FUR_*（POLY_START 0x100051D / POLY_MOVE_FOR_SLT 0x100051E / POLY_ON 0x100051F / POLY_CANCEL 0x1000520 /
//   FUR_ON 0x1000521 / PUT 0x1000525 / BACK 0x1000526 / POLY_MOVE 0x1000527 / POLY_MOVE_OUT 0x1000528）は全部 GROUP_HOUSE
//   （BANK_SE_SYS_HOUSE_PLUS・波形 WARC005 = 668,792 B）にあり、家の中でしか読まれていないので屋外では鳴らない（利用者の実機確認 2026-09-27）。
//   ゲームの音のヒープへ読み足すのはやめ、屋外で常に読まれている GROUP_STATIC から同じ役割の音を使う（利用者の指示 2026-09-27: 鳴らない所は直す）:
//   持ち物欄のアイテムを掴む・置く・マーク、持ち物のドラッグで枠を移るとき、スクロールのつまみ、デザインの線を引き始めるとき
const u32 kSndRangeStart = 0x01000452;      // SE_SYS_EDIT_LINE_START（範囲を引き始めた ← POLY_START）
const u32 kSndRangeStep = 0x010003A6;       // SE_SYS_SCROLL_BAR_DRAG（引いている間、終点が格子 1 本動くたび ← POLY_MOVE_FOR_SLT。
                                            //   ゲームはスクロールのつまみが 1 行動くたびに sub_5827D0 で鳴らす: sub_2993E4）
const u32 kSndSelectOn = 0x01000403;        // SE_SYS_ITEM_ICON_MARKING_ON（アイテムを選んだ ← POLY_ON / FUR_ON）
const u32 kSndSelectOff = 0x01000404;       // SE_SYS_ITEM_ICON_MARKING_OFF（選択を解いた ← POLY_CANCEL）
const u32 kSndPickUp = 0x01000401;          // SE_SYS_ITEM_ICON_PICK_UP（持ち上げた ← FUR_ON）
const u32 kSndPut = 0x01000402;             // SE_SYS_ITEM_ICON_SET（置いた ← PUT）
const u32 kSndInvalid = 0x0100039A;         // SE_SYS_BTN_ACT_INVALID（置けない ← POLY_MOVE_OUT / BACK）
const u32 kSndCarryStep = 0x01000405;       // SE_SYS_ITEM_SLOT_ACTIVE（行き先が変わった ← POLY_MOVE。持ち物のドラッグと同じく sub_5827D0: sub_1FFA80）
const u32 kSndListClose = 0x010003C6;       // SE_SYS_WIN_SELECT_ITEM_CLOSE（一覧を窓の外のタッチで閉じたときと同じ。sub_2BA608）
typedef void (*MoveSoundFn)(u32 id, float x);
// BGM に合わせた音程で鳴らす（sub_6B1530: +0xC5C != 0 / +0xC5D == 0 / +0xCB8 == 2 のときだけ鳴る）
const MoveSoundFn MoveSoundGame = reinterpret_cast<MoveSoundFn>(0x005827D0);
const u32 kSoundMgrPtr = 0x00947080;
const u32 kPlaySoundFn = 0x0058C7D4;        // Game_PlaySound
// コマのアニメ（cip_01C_02x02。アニメの名前表 CRO 0xB8E1F4 の 5 touch / 6 select / 2 drag / 3 drop2 / 7 loop）。
//   建物（P）は触れないので C だけ読む。値は全部 P_Btn_00 / P_Btn_01 / N_All に効き、マテリアルの色（fgobj の濃い緑）は触らない
enum ChipAnim : u8 { kAnimTouch, kAnimSelect, kAnimDrag, kAnimDrop2, kAnimLoop, kChipAnims };
const char *const kChipAnimNames[kChipAnims] = {
    "cip_01C_02x02_touch.bclan", "cip_01C_02x02_select.bclan", "cip_01C_02x02_drag.bclan",
    "cip_01C_02x02_drop2.bclan", "cip_01C_02x02_loop.bclan",
};
const float kAnimTapEnd = 1.0f;             // touch / select の終わり（2 フレーム）
const u32 kAnimPulseEnd = 3;                // drag / drop2 の終わり（4 フレーム）
const u32 kAnimLoopLen = 40;                // loop（縞の流れ。40 フレームで一周）
enum ChipLook : u8 { kLookPlain, kLookSelected, kLookCarried, kLookDropped };
// ---- 一覧 = ゲームの ItemSelectWindow（持ち物の選択窓。使い方は script::ChoiceStandardItem と持ち物欄 sub_23FB5C を写す）----
typedef int (*ListStepFn)(void *win);
typedef int (*ListBuildFn)(void *win, void *holder);
typedef void (*ListOpenFn)(void *win, s32 cancelRow);
typedef void (*ChangeStateFn)(void *obj, u32 calc, u32 adj);
typedef void (*ListFn)(void *win);
typedef void (*ListNameFn)(void *win, u32 visible);
const u32 kListBytes = 2880;                // 2,832 B 以上（ctor 0x2BAFC0: +1488 からボタンの節 224 B × 6）
const CtorFn         ListCtor       = reinterpret_cast<CtorFn>(0x002BAFC0);
const CtorFn         ListDtor       = reinterpret_cast<CtorFn>(0x002BB13C);         // vt[0]（delete しない方）
const ListStepFn     ListLoadStep   = reinterpret_cast<ListStepFn>(0x002BAC48);     // itm_slct_win.arc。済めば非 0
const ListBuildFn    ListBuild      = reinterpret_cast<ListBuildFn>(0x002BA8B0);    // (窓, 0 = 自前の保持体)
const ListOpenFn     ListOpen       = reinterpret_cast<ListOpenFn>(0x002BAF48);     // 待機なら +1224 = 取消の行、Select In へ
const ChangeStateFn  ChangeState    = reinterpret_cast<ChangeStateFn>(0x00818528);  // 状態表から enter を引いて呼ぶ
const ListFn         ListUpdate     = reinterpret_cast<ListFn>(0x002BABAC);         // 状態の calc → ボタン → Layout 計算
const ListNameFn     ListNameVisible = reinterpret_cast<ListNameFn>(0x002B9F64);    // 名前の欄（N_itm_nm_00）の表示
const u32 kListIdle = 0x002B9F5C, kListDecided = 0x002BA29C, kListClose = 0x002BA2A0;  // 状態の calc（表 0x8736F8）
const u32 kListLayout = 624;                // Layout（描画 = AddLayout(mgr, 窓 + 624, 1)）
const u32 kListState = 12;                  // 状態の calc（+16 = 調整）
const u32 kListRowBase = 956, kListRowStride = 12;  // 行 i: +0 文字箱 T_slct_cntnt / +4 下線 P_undrLine / +8 決定音
const u32 kListAnchorPane = 1204;           // 基準のペイン（sub_2B9D1C が +72/+76 の大きさだけ読む）
const u32 kListResult = 1212, kListCancelled = 1228;
const u32 kListAnchor = 1236;               // 基準の位置（持ち物欄は項目のペインの大域位置 +140/+156/+172。sub_71A250）
// u8: 閉じ終わりに手カーソルを戻すか（sub_2BA2A0: 真かつ開く前に出ていた（+1249）なら BsHandCursor_Show、偽なら隠す。ゲームも行によって 0 にする: sub_2BA608）
const u32 kListRestoreCursor = 1251;
const u32 kListRowCount = 4;
const s32 kListCancelRow = 3;               // 「やめる」
// ---- 一覧のキー操作（IDA-opus-5.5-F060。利用者指示 2026-09-28: ゲームと同じく十字で選択・A で決定・B でやめる）----
//   窓の部品（窓 +1256。vtable 0x8F0F64、ctor 0x33E4A4）は窓の更新（ItemSelectWindow_Update → sub_2F7744 → sub_2F7768）の中で
//   BsMenuMgr（*0x949D4C）+68 の sead::ControllerWrapper の +72（押した瞬間）/ +80（リピート）を読む:
//     ・vt+24 = 0x2F6B1C ショートカット: 部品 +208 & 8 のとき、各ノードの +196 と sub_6D33F0(mgr, mask)（+72 & mask）を照合し、当たったノードを決定。
//       窓は取消の行（+1224）のノード +196 に 8 = B を書く（ItemSelectWindow_LayoutRows 0x2B9C70）→ B は「やめる」のショートカット。
//     ・vt+28 = 0x33DEE0 キー方式（BsMenuMgr +508 == 部品 +204 = 11 のとき）: 次へ sub_722A6C / 前へ sub_722B40 は部品 +196 = 0x14C04 で
//       下 0x800 / 上 0x400（左右 0x2000 / 0x1000 は外れる）、決定 = sub_6D33F0(mgr, 4)（A）。
//   メニュー側のビットは BsMenuMgr_Init 0x6D3274 → sub_54752C(+68, 19, 表 0x84B4FC): 0x1 / 0x4 = A、0x2 / 0x8 = B、0x400〜0x2000 = 十字。
//   ゲームの入力は止めたまま（BlockGameAll）、自前で呼ぶ窓の更新の間だけこの 2 語に実機のボタンと同じビットを入れて戻す。判断・音・決定は窓の部品がする。
//   リピートはゲームの設定そのまま: BsMenuMgr_Init が sub_542B58(+68, 0x77F00, 20, 5)、sub_542E88 がボタンごとの押し続け c（押した最初 = 0）で
//   c == 20、または c > 20 かつ (c − 20) % 5 == 0 のとき立てる。A・B は対象外（0x77F00 に無い）。
const u32 kMenuMgrPtr = 0x00949D4C;         // BsMenuMgr
const u32 kMenuPadTrig = 72, kMenuPadRepeat = 80;
const u32 kMenuKeyA = 0x1u | 0x4u, kMenuKeyB = 0x2u | 0x8u;
const u32 kMenuKeyUp = 0x400u, kMenuKeyDown = 0x800u, kMenuKeyLeft = 0x1000u, kMenuKeyRight = 0x2000u;
const u32 kMenuRepeatMask = 0x77F00u, kMenuRepeatDelay = 20, kMenuRepeatEvery = 5;
// 行の決定音はゲームの表（dword_88BA80: 選択肢の種類 → 音）から: 複製 = 9 COPY / 削除 = 8 ERASE / 埋める = 0 DECIDE / やめる = 1 CANCEL。
//   ※7 WIN_SELECT_ITEM_DECIDE は CSEQ が大域変数 31 を鳴らすたびに +1 して 3 段階に音程を上げる（利用者報告 2026-09-27: 選ぶたびに上がる）ので使わない
const u32 kListRowSounds[kListRowCount] = { 0x01000395, 0x01000393, 0x0100038E, 0x01000392 };
const u32 kTextAllocSlot = 28;              // TextBox vt[28] = 器の確保（GameList と同じ）
const u32 kTextDraw = 260;
typedef void (*AllocBufFn)(void *textBox, u32 chars, u32 flags);
typedef void (*SetStringFn)(void *textBox, const u16 *str, u32 start, u32 len);
const SetStringFn SetString = reinterpret_cast<SetStringFn>(0x004BACBC);    // nwlyt_TextBox_SetString（GameList と同じ）
// 文字の幅（ChoiceStandardItem sub_5E7838 は行ごとに文字箱と下線の幅 +72 を測った幅にする。測り方は sub_5E9430 と同じ）
typedef void (*WriterScaleFn)(void *writer, float w, float h);
typedef float (*MeasureFn)(void *writer, const u16 *str, u32 len);
const CtorFn         WriterCtor     = reinterpret_cast<CtorFn>(0x007E8488);         // nw::font::TextWriter（100 B）
const CtorFn         WriterDtor     = reinterpret_cast<CtorFn>(0x004D5BCC);
const WriterScaleFn  WriterScale    = reinterpret_cast<WriterScaleFn>(0x004D51B0);  // nwfont_CharWriter_RecomputeScale（s0, s1 = 文字の大きさ）
const MeasureFn      MeasureWidth   = reinterpret_cast<MeasureFn>(0x008268E0);      // 写しで CalcStringRect → 右 − 左（s0）
const u32 kWriterBytes = 0x74;              // sub_5E9430 が積む大きさ（使うのは 100 B）

inline u8 *P(void *p, u32 off) { return reinterpret_cast<u8 *>(p) + off; }
inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(P(p, off)); }
inline float &F(void *p, u32 off) { return *reinterpret_cast<float *>(P(p, off)); }
inline u8 &B(void *p, u32 off) { return *P(p, off); }
inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline s32 S32(u32 a) { return *reinterpret_cast<const volatile s32 *>(a); }

// ---- 建物のコマ（メニュースレッドが開始時に作り、描画スレッドは読むだけ）----
struct BuildChip { u8 x, y, w, h; };        // 村のマスで左上と大きさ
const u32 kMaxBuildChips = 512;
BuildChip s_buildChips[kMaxBuildChips];
volatile u32 s_buildChipCount;

// ---- メニュー → 描画 ----
volatile bool s_want;
volatile s32 s_viewX, s_viewY;              // 盤面の左上のマス
// タッチはメニュースレッドが毎ティック（約 16ms）読んだ点を全部リングに積み、描画スレッド（30fps）がまとめて処理する。
//   以前は最後の 1 点だけを見ていて、速くなぞると間のマスを飛ばした（利用者報告 2026-09-27）。
//   1 語 = bit31 触れている / bit0-8 x / bit9-16 y（1 語で書くので途中の値を読まない）
const u32 kTouchRing = 64;
volatile u32 s_touchRing[kTouchRing];
volatile u32 s_touchHead;                   // メニュースレッドだけが書く（通算）
volatile Mode s_mode = Mode::Place;
volatile u32 s_placeId = kNoItem;           // 配置するアイテム（kNoItem = 未設定）
// ---- 描画 → メニュー（スポイト）----
volatile float s_pickProgress = -1.0f;      // 長押しの進み（0〜1、負 = 出さない）
volatile u16 s_pickX, s_pickY;              // 長押しを始めた画素
volatile u32 s_pickedSeq;                   // スポイトで取れたら増える（メニュースレッドが通知）
volatile u32 s_noItemSeq;                   // 配置するアイテムが無いまま置こうとしたら増える
volatile u32 s_cancelSeq;                   // B（範囲選択の取り消し）ごとに増える
volatile u32 s_listKeys;                    // 一覧が開いている間の十字・A・B（メニューのスレッドが書く。CTRPF の Key）
u32 s_listPrevKeys;
u32 s_listHold[32];                         // メニュー側のビットごとの押し続け（sead の +44 + 4i と同じ数え方）
// ---- 描画 → メニュー（持ち上げ中の行き先。メニュースレッドが GridCursor へ渡す）----
volatile u8 s_cursorX[kMaxCursorTiles], s_cursorY[kMaxCursorTiles];
volatile u32 s_cursorCount, s_cursorSeq;
// ---- 描画 → メニュー ----
const char *volatile s_error = "";

// ---- 描画スレッドだけ ----
enum class Stage : u8 { Off, Loading, Building, Live, Leaving, Waiting };
volatile Stage s_stage = Stage::Off;
alignas(8) u8 s_holder[584];
alignas(8) u8 s_board[332];
alignas(8) u8 s_boardIn[40];
alignas(8) u8 s_boardOut[40];
alignas(8) u8 s_boardKind[40];
alignas(8) u8 s_name[kNameBytes];
alignas(8) u8 s_word[96];
bool s_holderMade, s_arcLoaded, s_boardMade, s_boardBuilt, s_animsMade, s_nameMade, s_nameLoaded, s_nameBuilt;
void *s_boardAnim;                          // 再生中の盤面のアニメ（in / out）
void *s_roomPane;                           // N_Room_00（盤面の中心）
u32 s_waitFrames;
u32 s_startRoom;

struct alignas(8) Chip {
    u8 layout[332];
    u8 type;
    bool made;                              // ctor 済み
    bool built;                             // 組み立て済み
    bool used;                              // このフレームで使う
    bool rotated;
    s16 tx, ty;                             // 受け持つマス（左上）
    u8 look;                                // 焼いてある見た目（ChipLook）
    void *nAll;
    void *nRot;
    void *bBtn;
};
Chip s_chips[kMaxChips];
u32 s_chipsMade;
u8 s_chipLook[kMaxChips];                   // このフレームの見た目（AssignChips が決め、CalcChip が焼く）
alignas(8) u8 s_chipAnim[kChipAnims][40];   // コマのアニメ（UiAnim。全部のコマで順に結んで焼いて外す）
bool s_chipAnimsMade;
u32 s_frameNo;                              // 縞の流れ（loop）の拍

struct WantChip { u8 type; bool rotated; s16 tx, ty; u8 w, h; bool ghost; u8 gi; u8 look; };
WantChip s_wantChips[kMaxChips];
u32 s_wantCount;
Chip *s_drawOrder[kMaxChips];
u32 s_drawCount;

// タッチの状態（描画スレッドだけ）
enum class TouchKind : u8 { None, Ignore, Paint, Erase, Hold, SelPendIn, SelPendOut, SelDrag, CarryDrag };
u32 s_touchTail;                            // 処理した点（通算）
bool s_touchPrevDown;
bool s_touchWaitUp;                         // 一覧が指を使っていた: 離すまで点を使わない
s32 s_fingerX = -1, s_fingerY = -1;         // いま指が触れているマス（盤面の外・離している = -1）
u16 s_touchLastPx, s_touchLastPy;           // 直前の点（画素）
TouchKind s_touchKind = TouchKind::None;
s32 s_touchLastX = -1, s_touchLastY = -1;   // 直前のマス（盤面の外は -1）
u16 s_touchStartX, s_touchStartY;
s32 s_holdTileX, s_holdTileY;               // 長押しを始めたマス（スポイトで取るアイテム）
bool s_noItemTold;                          // このタッチで「配置するアイテムが無い」を知らせた
u32 s_holdFrames;
s32 s_nameTileX = -1, s_nameTileY = -1;
Chip *s_nameChip;                           // 吹き出しの基準のコマ
s32 s_nameViewX, s_nameViewY;

// ---- 範囲選択（描画スレッドだけ）----
struct SelRect { bool active; s32 x0, y0, x1, y1; };      // 村のマス（両端を含む）
SelRect s_sel;
// 選択のマスごとの印（利用者指示 2026-09-28）。範囲を動かしたあとは四角の中でも「運んだ物が着いたマス」と「空きのマス」だけが選択。
//   印が無い（s_selMasked == false）ときは四角の中が全部選択。
u8 s_selMask[kTilesX * kTilesY / 8];
bool s_selMasked;
enum class Carry : u8 { None, Move, Copy, CopyArmed };
Carry s_carry = Carry::None;
struct CarryItem { s16 x, y; u32 value; u8 type; };
CarryItem s_carried[kMaxCarry];
u32 s_carriedCount;
s32 s_carryVx, s_carryVy;                   // 持ち上げたときの盤面
u16 s_carryPressX, s_carryPressY;           // 動かし始めた指
float s_carryBaseX, s_carryBaseY;           // 前のタッチまでの動き（複製は 2 回に分けて動かせる）
float s_carryPixX, s_carryPixY;             // 指の動き（画素。下が +）
s32 s_carrySnapX, s_carrySnapY;             // 指の動きをマスに吸着したもの（境目の揺れはゲームと同じ 2.0 の遊びで抑える）
s32 s_carrySoundX, s_carrySoundY;           // 最後に音を鳴らした行き先のずれ
u32 s_carryFrames;                          // 持ち上げてからのフレーム（drag アニメ）
u32 s_dropFrames = 0xFFFFFFFFu;             // 置いてからのフレーム（drop2 アニメ）
s32 s_selStartTx, s_selStartTy;             // 押したマス（タップの判定）
s32 s_selGridX0, s_selGridY0;               // 範囲の始点 = 押した点を吸着した格子線（村のマスの番号。左上の角 = そのマスの番号）
s32 s_selGridX1, s_selGridY1;               // 範囲の終点（指）
s32 s_selStepX, s_selStepY;                 // 引いている間の音を最後に鳴らした終点
alignas(8) u8 s_group[332];
bool s_groupMade, s_groupBuilt;
void *s_gWin, *s_gStart, *s_gEnd;
alignas(8) u8 s_listWin[kListBytes];        // ゲームの ItemSelectWindow
alignas(8) u8 s_listAnchorPane[80];         // 一覧の基準のペインの代わり（+72/+76 = 1 マスの大きさだけ使われる）
bool s_listMade, s_listLoaded, s_listBuilt;
volatile bool s_listActive;                 // 描画 → メニュー: 一覧が出ている（ゲームのタッチを止めない）
u32 s_ghostStart;                           // s_drawOrder の中で持ち上げたコマが始まる位置
u32 s_cancelDone;

u32 RoomId(void) {
    return reinterpret_cast<u32 (*)(void)>(kRoomIdFn)();
}

void *LytHeap(void) {
    const u32 alloc = R32(kLytAllocatorPtr);
    return alloc != 0 ? *reinterpret_cast<void *const *>(alloc + 4) : nullptr;
}

bool LytRoom(void) {
    void *heap = LytHeap();
    return heap != nullptr && GridCursor::Game::HeapGetFreeSize(heap) >= kLytReserve;
}

void SetVisible(void *layout, const char *pane, bool on) {
    void *p = FindPane(layout, pane);
    if (p != nullptr)
        B(p, kPaneFlagsByte) = (u8)((B(p, kPaneFlagsByte) & 0xFEu) | (on ? 1u : 0u));
}

void SetTranslate(void *pane, float x, float y) {
    F(pane, kPaneTranslateX) = x;
    F(pane, kPaneTranslateY) = y;
    F(pane, kPaneTranslateZ) = 0.0f;
    B(pane, kPaneFlagsByte) &= 0xCFu;
}

void SetRotateZ(void *pane, float deg) {
    F(pane, kPaneRotateX) = 0.0f;
    F(pane, kPaneRotateY) = 0.0f;
    F(pane, kPaneRotateZ) = deg;
    B(pane, kPaneFlagsByte) &= 0xCFu;
}

// ---- 名前の吹き出し ----
void NameCalc(void) {
    u32 fn = W(s_name, kNameStateFn);
    const u32 adj = W(s_name, kNameStateAdj);
    if (fn == 0 && (adj & 1u) == 0)
        return;
    u8 *obj = s_name + ((s32)adj >> 1);
    if ((adj & 1u) != 0)
        fn = *reinterpret_cast<u32 *>(*reinterpret_cast<u32 *>(obj) + fn);
    reinterpret_cast<void (*)(void *)>(fn)(obj);
}

void HideName(void) {
    if (s_nameBuilt && s_nameChip != nullptr)
        NameHide(s_name);
    s_nameChip = nullptr;
    s_nameTileX = s_nameTileY = -1;
}

const char16_t *const kListRows[kListRowCount] = { u"複製", u"削除", u"埋める", u"やめる" };

// ---- 組み立て（1 フレームに 1 段）。戻り値: 完了 ----
bool LoadStep(void) {
    if (!s_holderMade) {
        ArcCtor(s_holder);
        s_holderMade = true;
    }
    if (!s_arcLoaded) {
        if (ArcLoadStep(s_holder, kChipArc) == 0)
            return false;
        RegisterTex(s_holder);
        s_arcLoaded = true;
        return false;
    }
    if (!s_nameMade) {
        NameCtor(s_name);
        s_nameMade = true;
    }
    if (!s_nameLoaded) {
        if (NameLoadStep(s_name) == 0)
            return false;
        s_nameLoaded = true;
    }
    if (!s_listMade) {                      // 一覧（持ち物の選択窓）。ゲームと同じく自分の保持体に itm_slct_win.arc を読む
        ListCtor(s_listWin);
        s_listMade = true;
    }
    if (!s_listLoaded) {
        if (ListLoadStep(s_listWin) == 0)
            return false;
        s_listLoaded = true;
    }
    return true;
}

bool BuildStep(void) {
    if (!s_boardBuilt) {
        if (!LytRoom()) {
            s_error = u8"nw::lyt のヒープが足りません";
            return false;
        }
        if (!s_boardMade) {
            LayoutCtor(s_board);
            s_boardMade = true;
        }
        W(s_board, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
        if (LayoutBuild(s_board, "chip_room_00.bclyt", nullptr, kBoardCmdBytes) == 0) {
            s_error = u8"chip_room_00.bclyt を組めません";
            return false;
        }
        s_boardBuilt = true;
        B(s_board, kLayoutPriority) = 2;
        AnimCtor(s_boardIn);
        AnimCtor(s_boardOut);
        AnimCtor(s_boardKind);
        s_animsMade = true;
        AnimLoad(s_boardIn, "chip_room_00_in.bclan", s_holder);
        AnimLoad(s_boardOut, "chip_room_00_out.bclan", s_holder);
        AnimLoad(s_boardKind, "chip_room_00_kind.bclan", s_holder);
        // 部屋の大きさを最大（8x8）に焼く（結合 → フレーム → 計算 → 解除。GameList の枠の種類と同じ）
        AnimBind(s_board, s_boardKind);
        AnimSetFrame(s_boardKind, kKindFrame);
        LayoutCalc(s_board);
        AnimUnbind(s_board, s_boardKind);
        // 本体以外は出さない: 扉・窓（部屋の壁の飾り）と投函ボタン。「できあがり」ボタンは組まない（ComButton を作らない）
        SetVisible(s_board, "N_door_00", false);
        SetVisible(s_board, "N_Window_00", false);
        SetVisible(s_board, "N_post_00", false);
        s_roomPane = FindPane(s_board, "N_Room_00");
        if (s_roomPane == nullptr) {
            s_error = u8"N_Room_00 がありません";
            return false;
        }
        return false;
    }
    if (!s_nameBuilt) {
        NameBuild(s_name, nullptr);
        s_nameBuilt = true;
        return false;
    }
    if (!s_groupBuilt) {                    // 範囲の枠（ゲームの CollectChip と同じ cip_group_00）
        if (!s_groupMade) {
            LayoutCtor(s_group);
            s_groupMade = true;
        }
        W(s_group, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
        if (LayoutBuild(s_group, "cip_group_00.bclyt", nullptr, kGroupCmdBytes) == 0) {
            s_error = u8"cip_group_00.bclyt を組めません";
            return false;
        }
        s_groupBuilt = true;
        B(s_group, kLayoutPriority) = 2;
        s_gWin = FindPane(s_group, "W_Group_00");
        s_gStart = FindPane(s_group, "P_Start_00");
        s_gEnd = FindPane(s_group, "P_End_00");
        if (s_gWin == nullptr || s_gStart == nullptr || s_gEnd == nullptr) {
            s_error = u8"cip_group_00 の部品がありません";
            return false;
        }
        return false;
    }
    if (!s_listBuilt) {                     // 一覧 = ゲームの ItemSelectWindow をそのまま組む（アニメ・音・行のボタンは窓が持つ）
        ListBuild(s_listWin, nullptr);
        s_listBuilt = true;
        for (u32 i = 0; i < 6; ++i) {       // 行の文字箱に器（8 字）。文字は開くたびに入れる
            void *box = reinterpret_cast<void *>(W(s_listWin, kListRowBase + kListRowStride * i));
            if (box == nullptr) {
                s_error = u8"itm_slct_win の行がありません";
                return false;
            }
            const u32 draw = W(box, kTextDraw);
            const u32 flags = draw != 0 ? *reinterpret_cast<const u8 *>(draw + 9) : 0;
            u32 *vt = *reinterpret_cast<u32 **>(box);
            reinterpret_cast<AllocBufFn>(vt[kTextAllocSlot])(box, 8, flags);
        }
        return false;
    }
    if (!s_chipAnimsMade) {                 // コマの見た目（選択・持ち上げ・置いた）のアニメ
        for (u32 i = 0; i < kChipAnims; ++i) {
            AnimCtor(s_chipAnim[i]);
            AnimLoad(s_chipAnim[i], kChipAnimNames[i], s_holder);
        }
        s_chipAnimsMade = true;
    }
    return true;
}

void EndHold(void);

void DestroyAll(void) {
    EndHold();
    MapEditor3D::Abandon();                 // 場面が同じなら赤を戻して複製を壊す。変わっていれば何も書かずにヒープだけ返す
    s_nameChip = nullptr;                   // 壊すだけなので退場の状態へは進めない
    s_nameTileX = s_nameTileY = -1;
    for (u32 i = 0; i < s_chipsMade; ++i) {
        Chip &c = s_chips[i];
        if (c.built)
            LayoutUnbindAll(c.layout);
        if (c.made)
            LayoutDtor(c.layout);
        c.made = c.built = c.used = false;
    }
    s_chipsMade = 0;
    if (s_chipAnimsMade)
        for (u32 i = 0; i < kChipAnims; ++i)
            AnimDtor(s_chipAnim[i]);
    s_chipAnimsMade = false;
    if (s_boardMade) {
        if (s_boardBuilt)
            LayoutUnbindAll(s_board);
        LayoutDtor(s_board);
    }
    s_boardMade = s_boardBuilt = false;
    if (s_animsMade) {
        AnimDtor(s_boardIn);
        AnimDtor(s_boardOut);
        AnimDtor(s_boardKind);
    }
    s_animsMade = false;
    s_boardAnim = nullptr;
    s_roomPane = nullptr;
    if (s_nameMade) {
        if (s_nameBuilt)
            LayoutUnbindAll(s_name + kNameLayout);
        NameDtor(s_name);
    }
    s_nameMade = s_nameLoaded = s_nameBuilt = false;
    if (s_groupMade) {
        if (s_groupBuilt)
            LayoutUnbindAll(s_group);
        LayoutDtor(s_group);
    }
    s_groupMade = s_groupBuilt = false;
    if (s_listMade) {
        if (s_listBuilt)
            LayoutUnbindAll(s_listWin + kListLayout);
        ListDtor(s_listWin);
    }
    s_listMade = s_listLoaded = s_listBuilt = false;
    s_listActive = false;
    s_touchWaitUp = false;
    s_sel.active = false;
    s_carry = Carry::None;
    s_carriedCount = 0;
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = s_arcLoaded = false;
    s_stage = Stage::Off;
    GameList::HoldField(false);             // 元の下画面 UI を戻してよい（地図の arc を取り直す）
}

void PlayBoard(void *anim) {
    if (s_boardAnim != nullptr)
        AnimUnbind(s_board, s_boardAnim);
    AnimBind(s_board, anim);
    AnimSetFrame(anim, 0.0f);
    s_boardAnim = anim;
}

void StepBoardAnim(void) {
    if (s_boardAnim == nullptr)
        return;
    if (AnimFinished(s_boardAnim)) {
        AnimUnbind(s_board, s_boardAnim);
        s_boardAnim = nullptr;
    } else {
        AnimStep(s_boardAnim);
    }
}

// ---- 盤面に出すコマ ----
void AddWant(u8 type, s32 tx, s32 ty, u8 w, u8 h, bool rotated) {
    if (s_wantCount >= kMaxChips)
        return;
    WantChip &c = s_wantChips[s_wantCount++];
    c.type = type;
    c.rotated = rotated;
    c.tx = (s16)tx;
    c.ty = (s16)ty;
    c.w = w;
    c.h = h;
    c.ghost = false;
    c.gi = 0;
    c.look = kLookPlain;
}

u8 ItemLook(s32 x, s32 y);
void CarryOffset(s32 vx, s32 vy, s32 &ox, s32 &oy);

bool Carried(s32 x, s32 y) {
    for (u32 k = 0; k < s_carriedCount; ++k)
        if (s_carried[k].x == x && s_carried[k].y == y)
            return true;
    return false;
}

void CollectWants(s32 vx, s32 vy) {
    s_wantCount = 0;
    // 建物（P）。衝突判定のマスごとに 1x1
    const u32 n = s_buildChipCount;
    for (u32 k = 0; k < n; ++k) {
        const BuildChip &b = s_buildChips[k];
        if (b.x >= vx && b.y >= vy && b.x < vx + kView && b.y < vy + kView)
            AddWant(kBuild11, b.x, b.y, 1, 1, false);
    }
    // アイテム（C。fgobj は色だけ変える）。建物の上に載っていても出す（上に置ける家具に載る扱い）
    const u32 field = R32(kFieldPtr);
    if (field == 0)
        return;
    for (s32 j = 0; j < kView; ++j) {
        for (s32 i = 0; i < kView; ++i) {
            const u32 *item = ItemAt(field, vx + i, vy + j, 0);
            if (item == nullptr)
                continue;
            const u16 id = (u16)(*item & 0x7FFFu);
            if (id == kEmptyItem)
                continue;
            if (s_carry == Carry::Move && Carried(vx + i, vy + j))
                continue;                   // 持ち上げている間は元の場所に出さない
            AddWant(id <= kFgobjMax ? kFgobjC : kItemC, vx + i, vy + j, 1, 1, false);
            s_wantChips[s_wantCount - 1].look = ItemLook(vx + i, vy + j);
        }
    }
    // 持ち上げたコマ（写しを含む）。行き先のマスに吸着して描く（ゲームも下画面のコマは単位の位置）。盤面の外は出さない。
    //   受け持つマスは村の外の番号にして、盤面のコマと混ぜない
    if (s_carry != Carry::None) {
        s32 ox = 0, oy = 0;
        CarryOffset(vx, vy, ox, oy);
        for (u32 k = 0; k < s_carriedCount && s_wantCount < kMaxChips; ++k) {
            const s32 x = s_carried[k].x + ox, y = s_carried[k].y + oy;
            if (x < vx || y < vy || x >= vx + kView || y >= vy + kView)
                continue;
            AddWant(s_carried[k].type, 20000 + (s32)k, 20000, 1, 1, false);
            s_wantChips[s_wantCount - 1].ghost = true;
            s_wantChips[s_wantCount - 1].gi = (u8)k;
            s_wantChips[s_wantCount - 1].look = kLookCarried;
        }
    }
}

bool BuildChipLayout(Chip &c, u8 type) {
    if (!c.made) {
        LayoutCtor(c.layout);
        c.made = true;
    }
    W(c.layout, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
    if (LayoutBuild(c.layout, kChipLayouts[type], nullptr, kChipCmdBytes) == 0)
        return false;
    c.built = true;
    c.type = type;
    c.look = kLookPlain;
    B(c.layout, kLayoutPriority) = 2;
    if (type == kFgobjC || type == kBuild11) {
        void *pic = FindPane(c.layout, "P_Btn_00");
        const u32 mat = pic != nullptr ? W(pic, kPictureMaterial) : 0u;
        if (mat != 0) {
            std::memcpy(reinterpret_cast<void *>(mat + kMatColors), type == kFgobjC ? kFgobjBlack : kBuildBlack, 4);
            std::memcpy(reinterpret_cast<void *>(mat + kMatColors + 4), type == kFgobjC ? kFgobjWhite : kBuildWhite, 4);
            *reinterpret_cast<u8 *>(mat + kMatFlags) &= ~4u;
        }
    }
    c.nAll = FindPane(c.layout, "N_All");
    c.nRot = FindPane(c.layout, "N_Rotate_00");
    c.bBtn = FindPane(c.layout, "B_Btn_00");
    return c.nAll != nullptr && c.nRot != nullptr && c.bBtn != nullptr;
}

// 欲しいコマに、組み立て済みのコマを割り当てる（同じマスを受け持っていたものを優先。足りなければ組み立てる）
void AssignChips(s32 vx, s32 vy) {
    for (u32 i = 0; i < s_chipsMade; ++i)
        s_chips[i].used = false;
    s_drawCount = 0;
    Chip *assigned[kMaxChips];
    for (u32 k = 0; k < s_wantCount; ++k) {
        assigned[k] = nullptr;
        const WantChip &w = s_wantChips[k];
        for (u32 i = 0; i < s_chipsMade; ++i) {
            Chip &c = s_chips[i];
            if (c.built && !c.used && c.type == w.type && c.tx == w.tx && c.ty == w.ty && c.rotated == w.rotated) {
                c.used = true;
                assigned[k] = &c;
                break;
            }
        }
    }
    u32 builds = 0;
    for (u32 k = 0; k < s_wantCount; ++k) {
        if (assigned[k] != nullptr)
            continue;
        const WantChip &w = s_wantChips[k];
        for (u32 i = 0; i < s_chipsMade && assigned[k] == nullptr; ++i) {
            Chip &c = s_chips[i];
            if (c.built && !c.used && c.type == w.type) {
                c.used = true;
                assigned[k] = &c;
            }
        }
        if (assigned[k] == nullptr && builds < kBuildsPerFrame && s_chipsMade < kMaxChips && LytRoom()) {
            Chip &c = s_chips[s_chipsMade];
            ++builds;
            const bool ok = BuildChipLayout(c, w.type);
            ++s_chipsMade;                  // ctor 済みなら片付けの対象（組み立てに失敗しても）
            if (ok) {
                c.used = true;
                assigned[k] = &c;
            } else {
                c.built = false;
            }
        }
    }
    // 位置。盤面の中心 = N_Room_00 の大域位置、1 マス 20、y は下へ減る（ModuleFtr 0xB0F758）
    const float ox = F(s_roomPane, kPaneGlobalX);
    const float oy = F(s_roomPane, kPaneGlobalY);
    const float half = (float)kView * 0.5f;
    s32 carryOx = 0, carryOy = 0;
    if (s_carry != Carry::None)
        CarryOffset(vx, vy, carryOx, carryOy);
    // 描く順: 建物 → アイテム → 持ち上げたコマ（一番上）
    for (u32 pass = 0; pass < 3; ++pass) {
        if (pass == 2)
            s_ghostStart = s_drawCount;
        for (u32 k = 0; k < s_wantCount; ++k) {
            Chip *c = assigned[k];
            const WantChip &w = s_wantChips[k];
            const u32 wantPass = w.ghost ? 2u : (w.type >= kBuild11 ? 0u : 1u);
            if (c == nullptr || wantPass != pass)
                continue;
            c->tx = w.tx;
            c->ty = w.ty;
            c->rotated = w.rotated;
            s_chipLook[c - s_chips] = w.look;
            if (w.ghost) {                  // 行き先のマス（CollectWants で盤面の中だけにしてある）
                const CarryItem &ci = s_carried[w.gi];
                const float gx = (float)(ci.x + carryOx - vx) + 0.5f - half;
                const float gy = (float)(ci.y + carryOy - vy) + 0.5f - half;
                SetTranslate(c->nAll, ox + gx * kTile, oy - gy * kTile);
                SetRotateZ(c->nRot, 0.0f);
                s_drawOrder[s_drawCount++] = c;
                continue;
            }
            const float cx = (float)(w.tx - vx) + (float)w.w * 0.5f - half;
            const float cy = (float)(w.ty - vy) + (float)w.h * 0.5f - half;
            SetTranslate(c->nAll, ox + cx * kTile, oy - cy * kTile);
            SetRotateZ(c->nRot, w.rotated ? 90.0f : 0.0f);
            s_drawOrder[s_drawCount++] = c;
        }
    }
}

// ---- 音 ----
void Sound(u32 id) {
    reinterpret_cast<void (*)(u32)>(kPlaySoundFn)(id);
}

// 移動の音はゲームと同じ sub_5827D0（BGM に合わせた音程）。その経路が鳴らない状態のときだけ Game_PlaySound で補う（自前）
void MoveSound(u32 id, float worldX) {
    const u32 mgr = R32(kSoundMgrPtr);
    if (mgr != 0 && *reinterpret_cast<const u8 *>(mgr + 0xC5C) != 0 && *reinterpret_cast<const u8 *>(mgr + 0xC5D) == 0
        && R32(mgr + 0xCB8) == 2)
        MoveSoundGame(id, worldX * kMoveSoundScale);
    else
        Sound(id);
}

// ---- コマの見た目を焼いて計算する（アニメを結ぶ → フレーム → 計算 → 外す。値はペインに残る）----
//   選択 = touch と select の終わり（sub_B43330）、解除 = 同じ 2 本の先頭（sub_B2EB04）、縞の流れ = loop（ゲームは常に結んでいる）、
//   持ち上げ = drag、置いた = drop2（まだ選択のまま）
void CalcChip(Chip &c, u8 look) {
    if (!s_chipAnimsMade || c.type >= kBuild11 || (look == kLookPlain && c.look == kLookPlain)) {
        LayoutCalc(c.layout);
        return;
    }
    u8 ids[3];
    float frames[3];
    u32 n = 0;
    const float loop = (float)(s_frameNo % kAnimLoopLen);
    switch (look) {
    case kLookPlain:
        ids[n] = kAnimTouch, frames[n++] = 0.0f;
        ids[n] = kAnimSelect, frames[n++] = 0.0f;
        break;
    case kLookSelected:
        if (c.look != kLookSelected) {
            ids[n] = kAnimTouch, frames[n++] = kAnimTapEnd;
            ids[n] = kAnimSelect, frames[n++] = kAnimTapEnd;
        }
        ids[n] = kAnimLoop, frames[n++] = loop;
        break;
    case kLookCarried:
        ids[n] = kAnimDrag, frames[n++] = (float)(s_carryFrames < kAnimPulseEnd ? s_carryFrames : kAnimPulseEnd);
        ids[n] = kAnimLoop, frames[n++] = loop;
        break;
    default:
        ids[n] = kAnimDrop2, frames[n++] = (float)(s_dropFrames < kAnimPulseEnd ? s_dropFrames : kAnimPulseEnd);
        ids[n] = kAnimLoop, frames[n++] = loop;
        break;
    }
    for (u32 i = 0; i < n; ++i) {
        AnimBind(c.layout, s_chipAnim[ids[i]]);
        AnimSetFrame(s_chipAnim[ids[i]], frames[i]);
    }
    LayoutCalc(c.layout);
    for (u32 i = 0; i < n; ++i)
        AnimUnbind(c.layout, s_chipAnim[ids[i]]);
    c.look = look;
}

// ---- 一覧（ゲームの ItemSelectWindow）----
bool ListBusy(void) {
    return s_listBuilt && !(W(s_listWin, kListState) == kListIdle && W(s_listWin, kListState + 4) == 0);
}

// 文字箱の書体で文字列の幅を測る（sub_5E9430 と同じ: 折り返しなし）
float MeasureText(void *box, const u16 *str, u32 len) {
    alignas(8) u8 writer[kWriterBytes];
    WriterCtor(writer);
    W(writer, 60) = W(box, 224);            // 書体
    WriterScale(writer, F(box, 228), F(box, 232));
    W(writer, 84) = W(box, 236);
    W(writer, 80) = W(box, 240);
    W(writer, 76) = 0x7F7FFFFFu;            // 折り返しの幅 = FLT_MAX
    const float w = MeasureWidth(writer, str, len);
    WriterDtor(writer);
    return w;
}

// 開く。行の文字・幅・決定音を入れ（ChoiceStandardItem sub_5E7838）、基準を置いて名前の欄を隠し（持ち物欄 sub_23FB5C）、sub_2BAF48 で開く
void OpenList(s32 vx, s32 vy, s32 tx, s32 ty) {
    if (!s_listBuilt || ListBusy())
        return;
    HideName();
    static const u16 kEmpty[1] = { 0 };
    for (u32 i = 0; i < 6; ++i) {
        u8 *row = s_listWin + kListRowBase + kListRowStride * i;
        void *box = reinterpret_cast<void *>(W(row, 0));
        void *line = reinterpret_cast<void *>(W(row, 4));
        if (i >= kListRowCount) {
            SetString(box, kEmpty, 0, 0);   // 文字が無い行は出ない（sub_2B9A20 が +250 を見る）
            continue;
        }
        const u16 *str = reinterpret_cast<const u16 *>(kListRows[i]);
        u32 n = 0;
        while (str[n] != 0)
            ++n;
        SetString(box, str, 0, n);
        const float w = MeasureText(box, str, n);
        F(box, 72) = w;                     // 文字箱と下線の幅 = 文字の幅（高さはそのまま）
        if (line != nullptr)
            F(line, 72) = w;
        W(row, 8) = kListRowSounds[i];
    }
    // 基準 = タップしたマスの中心（大域の位置）、大きさ = 1 マス
    const float half = (float)kView * 0.5f;
    F(s_listWin, kListAnchor) = F(s_roomPane, kPaneGlobalX) + ((float)(tx - vx) + 0.5f - half) * kTile;
    F(s_listWin, kListAnchor + 4) = F(s_roomPane, kPaneGlobalY) - ((float)(ty - vy) + 0.5f - half) * kTile;
    F(s_listWin, kListAnchor + 8) = 0.0f;
    F(s_listAnchorPane, 72) = kTile;
    F(s_listAnchorPane, 76) = kTile;
    W(s_listWin, kListAnchorPane) = reinterpret_cast<u32>(s_listAnchorPane);
    ListNameVisible(s_listWin, 0);
    B(s_listWin, kListRestoreCursor) = 0;   // 閉じたあと手カーソルを出さない（エディターでは使わない。利用者指示 2026-09-27）
    ListOpen(s_listWin, kListCancelRow);
    s_listActive = true;
}

// 閉じる（sub_2BAFA0 と同じ状態へ）。閉じ始めたら真
bool CloseList(void) {
    if (!ListBusy() || W(s_listWin, kListState) == kListClose)
        return false;
    ChangeState(s_listWin, kListClose, 0);
    return true;
}

Chip *ChipAt(s32 tx, s32 ty, bool itemsOnly) {
    for (u32 k = s_drawCount; k-- > 0;) {       // 後に描いた（上の）ものから
        Chip *c = s_drawOrder[k];
        if (itemsOnly && c->type >= kBuild11)
            continue;
        if (c->tx == tx && c->ty == ty)
            return c;
    }
    return nullptr;
}

// 画素 → 盤面のマス。盤面の外なら偽
bool TouchTile(s32 vx, s32 vy, u16 px, u16 py, s32 &tx, s32 &ty) {
    const float lx = (float)px - 160.0f;
    const float ly = 120.0f - (float)py;
    const float ox = F(s_roomPane, kPaneGlobalX);
    const float oy = F(s_roomPane, kPaneGlobalY);
    const float half = (float)kView * 0.5f * kTile;
    const float fx = (lx - (ox - half)) / kTile;
    const float fy = ((oy + half) - ly) / kTile;
    if (fx < 0.0f || fy < 0.0f || fx >= (float)kView || fy >= (float)kView)
        return false;
    tx = vx + (s32)fx;
    ty = vy + (s32)fy;
    return true;
}

u32 *ItemAtTile(s32 tx, s32 ty) {
    const u32 field = R32(kFieldPtr);
    return field != 0 ? ItemAt(field, tx, ty, 0) : nullptr;
}

bool IsEmpty(const u32 *item) {
    return (u16)(*item & 0x7FFFu) == kEmptyItem;
}

// アイテムのマスなら名前（建物は無視）。出せたら真
bool ShowNameAt(s32 vx, s32 vy, s32 tx, s32 ty) {
    Chip *c = ChipAt(tx, ty, true);
    const u32 *item = ItemAtTile(tx, ty);
    if (c == nullptr || item == nullptr || IsEmpty(item)) {
        HideName();
        return false;
    }
    // 語はアイテムそのもの（上位の旗つき）から作る。没アイテムの自前の名前（ItemNames のフック）もここで効く
    void *word = ItemWord(s_word, reinterpret_cast<const u16 *>(item), 0);
    const float offset[3] = { 0.0f, 0.0f, 0.0f };
    NameShow(s_name, word, c->bBtn, offset);
    s_nameChip = c;
    s_nameTileX = tx;
    s_nameTileY = ty;
    s_nameViewX = vx;
    s_nameViewY = vy;
    return true;
}

// 配置: 空いているマスにだけ置く（建物のマスにも置ける。利用者指示 2026-09-27 で fgobj も）
void PlaceAt(s32 tx, s32 ty) {
    u32 *item = ItemAtTile(tx, ty);
    if (item == nullptr || !IsEmpty(item))
        return;
    const u32 id = s_placeId;
    if (id == kNoItem) {
        if (!s_noItemTold) {                // 1 回のタッチで 1 回だけ知らせる
            s_noItemTold = true;
            s_noItemSeq = s_noItemSeq + 1;
        }
        return;
    }
    const u32 value = id;                   // 上位（旗）は 0
    if (SetItem(R32(kFieldPtr), &value, tx, ty, 0) != 0 && R32(kFgobjPtr) != 0)
        MarkDirty((u32)tx, (u32)ty, 1);
}

// 削除: アイテムだけ（建物は消さない）。ゲームの消し方そのもの
void EraseAt(s32 tx, s32 ty) {
    const u32 *item = ItemAtTile(tx, ty);
    if (item == nullptr || IsEmpty(item))
        return;
    DeleteItem(tx, ty, R32(kFieldPtr));
}

void EndHold(void) {
    s_pickProgress = -1.0f;
}

// 画素 → 盤面のマス座標（小数。盤面の外でもそのまま）
void TileCoord(u16 px, u16 py, float &fx, float &fy) {
    const float lx = (float)px - 160.0f;
    const float ly = 120.0f - (float)py;
    const float half = (float)kView * 0.5f * kTile;
    fx = (lx - (F(s_roomPane, kPaneGlobalX) - half)) / kTile;
    fy = ((F(s_roomPane, kPaneGlobalY) + half) - ly) / kTile;
}

s32 FloorI(float v) {
    const s32 i = (s32)v;
    return (float)i > v ? i - 1 : i;
}

void TraceVisit(s32 vx, s32 vy, s32 cx, s32 cy) {
    const bool inside = cx >= 0 && cy >= 0 && cx < kView && cy < kView;
    const s32 tx = inside ? vx + cx : -1, ty = inside ? vy + cy : -1;
    if (tx == s_touchLastX && ty == s_touchLastY)
        return;
    s_touchLastX = tx;
    s_touchLastY = ty;
    if (!inside)
        return;
    if (s_touchKind == TouchKind::Paint)
        PlaceAt(tx, ty);
    else if (s_touchKind == TouchKind::Erase)
        EraseAt(tx, ty);
}

// なぞった線（直前の点 → 今の点）がまたぐマスを、通る順に全部処理する（格子の走査。角をかすめるマスも拾う）。
//   以前は最後の点のマスだけで、速くなぞると間のマスを飛ばした（利用者報告 2026-09-27）。
void Trace(s32 vx, s32 vy, u16 px, u16 py) {
    float ax, ay, bx, by;
    TileCoord(s_touchLastPx, s_touchLastPy, ax, ay);
    TileCoord(px, py, bx, by);
    s32 cx = FloorI(ax), cy = FloorI(ay);
    const s32 ex = FloorI(bx), ey = FloorI(by);
    const float dx = bx - ax, dy = by - ay;
    const s32 sx = dx > 0.0f ? 1 : -1, sy = dy > 0.0f ? 1 : -1;
    const float kFar = 1.0e30f;
    const float tdx = dx != 0.0f ? (dx > 0.0f ? 1.0f / dx : -1.0f / dx) : kFar;
    const float tdy = dy != 0.0f ? (dy > 0.0f ? 1.0f / dy : -1.0f / dy) : kFar;
    float tmx = dx != 0.0f ? (dx > 0.0f ? (float)(cx + 1) - ax : ax - (float)cx) * tdx : kFar;
    float tmy = dy != 0.0f ? (dy > 0.0f ? (float)(cy + 1) - ay : ay - (float)cy) * tdy : kFar;
    TraceVisit(vx, vy, cx, cy);
    for (u32 guard = 0; guard < 64 && (cx != ex || cy != ey); ++guard) {
        if (tmx < tmy) {
            cx += sx;
            tmx += tdx;
        } else {
            cy += sy;
            tmy += tdy;
        }
        TraceVisit(vx, vy, cx, cy);
    }
}

// ---------------------------------------------------------------------------------------------
// 範囲選択（段階 3）。操作は利用者の決定 2026-09-27、見た目・音・吸着はゲームの模様替え（IDA-opus-5.5-F053）
//   スライド = 範囲を引く: 押した点と指を最寄りの格子線へ吸着し（CollectChip sub_B359D4）、その四角に丸ごと入るマスを選ぶ（sub_B5923C）。
//   タップ = そのマスだけを選ぶ（アイテムなら名前も）。範囲の中: 長押し（6 フレーム or 24 動く）で持ち上げて移動、タップで一覧。
//   持ち上げたコマは行き先のマスに吸着して描き、行き先が変わるたびに音（ModuleFtr sub_B06E44）。
//   持ち上げている間は十字・スライドパッドで盤面を動かせる（行き先は盤面の動いた分だけずれる）。行き先に物があれば上書き。
//   盤面の外のタップと B で選択を解く
// ---------------------------------------------------------------------------------------------

bool SelMaskBit(s32 x, s32 y) {
    if (x < 0 || y < 0 || x >= kTilesX || y >= kTilesY)
        return false;
    const u32 i = (u32)y * (u32)kTilesX + (u32)x;
    return ((s_selMask[i >> 3] >> (i & 7u)) & 1u) != 0u;
}

void SetSelMaskBit(s32 x, s32 y) {
    if (x < 0 || y < 0 || x >= kTilesX || y >= kTilesY)
        return;
    const u32 i = (u32)y * (u32)kTilesX + (u32)x;
    s_selMask[i >> 3] = (u8)(s_selMask[i >> 3] | (1u << (i & 7u)));
}

bool InSel(s32 x, s32 y) {
    return s_sel.active && x >= s_sel.x0 && x <= s_sel.x1 && y >= s_sel.y0 && y <= s_sel.y1 && (!s_selMasked || SelMaskBit(x, y));
}

void ClearSel(void) {
    s_sel.active = false;
    s_selMasked = false;
}

// 選択を解く（ゲームの sub_B3D5B0 は選んだチップがあれば POLY_CANCEL。こちらは空きマスだけの範囲も選択なので、範囲があれば鳴らす）
void CancelSel(void) {
    if (s_sel.active)
        Sound(kSndSelectOff);
    ClearSel();
}

// 盤面のアイテムのコマの見た目: 範囲の中 = 選択（置いた直後の 4 フレームは drop2）
u8 ItemLook(s32 x, s32 y) {
    if (!InSel(x, y))
        return kLookPlain;
    return s_dropFrames <= kAnimPulseEnd ? kLookDropped : kLookSelected;
}

// CollectChip sub_B359D4 の吸着。u = 部屋の中心からの位置（ゲームの座標では部屋の中心が 0。原点 +129568 は ctor の 0.0 のまま）
float SnapLine(float u) {
    const float bias = u < 0.0f ? -kSnapBias : kSnapBias;
    return (float)(s32)((u + bias) / kTile) * kTile;
}

// 画素 → 吸着した格子線の番号（村のマス。格子線 i = マス i の左・上の辺）。盤面の辺までに収める
void GridAt(s32 vx, s32 vy, u16 px, u16 py, s32 &gx, s32 &gy) {
    const float half = (float)kView * 0.5f;
    const float ux = SnapLine(((float)px - 160.0f) - F(s_roomPane, kPaneGlobalX));
    const float uy = SnapLine((120.0f - (float)py) - F(s_roomPane, kPaneGlobalY));
    s32 ix = (s32)(ux / kTile + half), iy = (s32)(half - uy / kTile);      // 20 の倍数 / 20 なので割り切れる
    ix = ix < 0 ? 0 : (ix > kView ? kView : ix);
    iy = iy < 0 ? 0 : (iy > kView ? kView : iy);
    gx = vx + ix;
    gy = vy + iy;
}

// 範囲の中のアイテムを写す（持ち上げ・複製）。戻り値: 数
u32 CaptureSel(void) {
    s_carriedCount = 0;
    for (s32 y = s_sel.y0; y <= s_sel.y1; ++y) {
        for (s32 x = s_sel.x0; x <= s_sel.x1; ++x) {
            const u32 *item = ItemAtTile(x, y);
            if (!InSel(x, y) || item == nullptr || IsEmpty(item) || s_carriedCount >= kMaxCarry)
                continue;
            CarryItem &c = s_carried[s_carriedCount++];
            c.x = (s16)x;
            c.y = (s16)y;
            c.value = *item;                // 旗ごとそのまま運ぶ
            c.type = (u16)(*item & 0x7FFFu) <= kFgobjMax ? kFgobjC : kItemC;
        }
    }
    return s_carriedCount;
}

void StartCarry(Carry mode, s32 vx, s32 vy, u16 px, u16 py) {
    s_carry = mode;
    s_carryVx = vx;
    s_carryVy = vy;
    s_carryPressX = px;
    s_carryPressY = py;
    s_carryBaseX = s_carryBaseY = 0.0f;
    s_carryPixX = s_carryPixY = 0.0f;
    s_carrySnapX = s_carrySnapY = 0;
    s_carrySoundX = s_carrySoundY = 0;
    s_carryFrames = 0;
    s_dropFrames = 0xFFFFFFFFu;
    Sound(kSndPickUp);                      // 持ち上げた
}

// 持ち上げる。基準 = 今の指の位置と今の盤面
bool Lift(Carry mode, s32 vx, s32 vy, u16 px, u16 py) {
    if (CaptureSel() == 0)
        return false;
    HideName();
    StartCarry(mode, vx, vy, px, py);
    return true;
}

// 指の動き（画素）→ マスのずれ。四捨五入で、前のずれとの境目から 2.0（世界の長さ）以内なら前のまま（ModuleFtr sub_B147EC）
s32 SnapAxis(float pix, s32 prev) {
    const float f = pix / kTile;
    const s32 r = FloorI(f + 0.5f);
    if (r == prev)
        return prev;
    if (r == prev + 1 || r == prev - 1) {
        const float d = f - ((float)(r < prev ? r : prev) + 0.5f);
        if (d < kCarryHysteresis && d > -kCarryHysteresis)
            return prev;
    }
    return r;
}

void MoveCarry(u16 px, u16 py) {
    s_carryPixX = s_carryBaseX + (float)px - (float)s_carryPressX;
    s_carryPixY = s_carryBaseY + (float)py - (float)s_carryPressY;
    s_carrySnapX = SnapAxis(s_carryPixX, s_carrySnapX);
    s_carrySnapY = SnapAxis(s_carryPixY, s_carrySnapY);
}

// 行き先のずれ（マス）= 吸着した指の動き + 盤面を動かした分
void CarryOffset(s32 vx, s32 vy, s32 &ox, s32 &oy) {
    ox = s_carrySnapX + (vx - s_carryVx);
    oy = s_carrySnapY + (vy - s_carryVy);
}

// 置けるか: 行き先が全部村の中（物があれば上書きするので、ほかに置けない場所は無い）
bool CarryPlaceable(s32 ox, s32 oy) {
    for (u32 k = 0; k < s_carriedCount; ++k)
        if (ItemAtTile(s_carried[k].x + ox, s_carried[k].y + oy) == nullptr)
            return false;
    return true;
}

// 移動の音の引数（ゲームは行き先の世界 x × 0.00625）。持ち上げた先頭のアイテムの行き先の中心
float CarryWorldX(s32 ox) {
    return s_carriedCount != 0 ? ((float)(s_carried[0].x + ox) + 0.5f) * kWorldTile : 0.0f;
}

void CommitCarry(s32 vx, s32 vy) {
    s32 ox = 0, oy = 0;
    CarryOffset(vx, vy, ox, oy);
    const bool move = s_carry == Carry::Move;
    s_carry = Carry::None;
    if (!CarryPlaceable(ox, oy)) {
        Sound(kSndInvalid);                 // 置けないので元へ戻す（ModuleFtr sub_B07108 と同じ動き）
        return;
    }
    Sound(kSndPut);
    s_dropFrames = 0;
    if (ox == 0 && oy == 0)
        return;                             // 元の場所（移動なら何もしない・複製なら同じ物を上書きするだけ）
    if (move)
        for (u32 k = 0; k < s_carriedCount; ++k)
            DeleteItem(s_carried[k].x, s_carried[k].y, R32(kFieldPtr));
    for (u32 k = 0; k < s_carriedCount; ++k) {
        const s32 x = s_carried[k].x + ox, y = s_carried[k].y + oy;
        const u32 *item = ItemAtTile(x, y);
        if (item == nullptr)
            continue;
        if (!IsEmpty(item))
            DeleteItem(x, y, R32(kFieldPtr));   // 上書き（利用者の決定）
        const u32 value = s_carried[k].value;
        if (SetItem(R32(kFieldPtr), &value, x, y, 0) != 0 && R32(kFgobjPtr) != 0)
            MarkDirty((u32)x, (u32)y, 1);
    }
    s_sel.x0 += ox;                         // 範囲も行き先へ
    s_sel.x1 += ox;
    s_sel.y0 += oy;
    s_sel.y1 += oy;
    // 移動後の選択 = 運んだ物が着いたマス + 空きのマス。行き先に元からあって運んでいない物は選ばない（利用者指示 2026-09-28）
    for (u32 i = 0; i < sizeof(s_selMask); ++i)
        s_selMask[i] = 0;
    for (s32 y = s_sel.y0; y <= s_sel.y1; ++y)
        for (s32 x = s_sel.x0; x <= s_sel.x1; ++x) {
            const u32 *item = ItemAtTile(x, y);
            if (item != nullptr && IsEmpty(item))
                SetSelMaskBit(x, y);
        }
    for (u32 k = 0; k < s_carriedCount; ++k)
        SetSelMaskBit(s_carried[k].x + ox, s_carried[k].y + oy);
    s_selMasked = true;
}

void CancelCarry(void) {
    s_carry = Carry::None;
    s_carriedCount = 0;
}

// ---- 一覧の行を選んだ（StepList が呼ぶ。決定音は窓が行ごとの音で鳴らす）----
enum ListAction : u8 { kActCopy, kActDelete, kActFill, kActCancel };

void RunListAction(u32 row, s32 vx, s32 vy) {
    switch (row) {
    case kActCopy:
        if (CaptureSel() != 0)              // 写しが浮かび、次のタッチで動かして離した所に置く
            StartCarry(Carry::CopyArmed, vx, vy, 0, 0);
        break;
    case kActDelete:
        for (s32 y = s_sel.y0; y <= s_sel.y1; ++y)
            for (s32 x = s_sel.x0; x <= s_sel.x1; ++x)
                if (InSel(x, y))
                    EraseAt(x, y);
        break;
    case kActFill:
        s_noItemTold = false;
        for (s32 y = s_sel.y0; y <= s_sel.y1; ++y)
            for (s32 x = s_sel.x0; x <= s_sel.x1; ++x)
                if (InSel(x, y))
                    PlaceAt(x, y);
        break;
    default:
        break;
    }
}

// ---- 範囲選択のタッチ ----
void SelectPress(s32 vx, s32 vy, u16 px, u16 py, bool inside, s32 tx, s32 ty) {
    if (s_carry == Carry::CopyArmed) {
        if (inside) {                       // 次のタッチで写しを動かす
            s_carry = Carry::Copy;
            s_carryPressX = px;
            s_carryPressY = py;
            s_carryBaseX = s_carryPixX;
            s_carryBaseY = s_carryPixY;
            s_touchKind = TouchKind::CarryDrag;
        } else {
            CancelCarry();
            Sound(kSndInvalid);
            s_touchKind = TouchKind::Ignore;
        }
        return;
    }
    if (!inside) {
        HideName();
        CancelSel();
        s_touchKind = TouchKind::Ignore;
        return;
    }
    s_selStartTx = tx;
    s_selStartTy = ty;
    if (InSel(tx, ty)) {
        s_touchKind = TouchKind::SelPendIn;
        return;
    }
    CancelSel();                            // 範囲の外を押した = 前の選択を解く（ゲームも押した瞬間に解く）
    GridAt(vx, vy, px, py, s_selGridX0, s_selGridY0);   // 始点 = 押した点を吸着（CollectChip も押した瞬間の点）
    s_selGridX1 = s_selGridX0;
    s_selGridY1 = s_selGridY0;
    s_touchKind = TouchKind::SelPendOut;
}

void SelectMove(s32 vx, s32 vy, u16 px, u16 py) {
    switch (s_touchKind) {
    case TouchKind::SelPendIn: {
        const float dx = (float)px - (float)s_touchStartX, dy = (float)py - (float)s_touchStartY;
        if (dx * dx + dy * dy >= kHoldSlop * kHoldSlop) {
            if (Lift(Carry::Move, vx, vy, s_touchStartX, s_touchStartY)) {
                s_touchKind = TouchKind::CarryDrag;
                MoveCarry(px, py);
            } else {
                s_touchKind = TouchKind::Ignore;
            }
        }
        break;
    }
    case TouchKind::SelPendOut: {
        s32 tx = -1, ty = -1;
        if (!TouchTile(vx, vy, px, py, tx, ty) || tx != s_selStartTx || ty != s_selStartTy) {
            HideName();                     // マスを出たら範囲を引く
            s_touchKind = TouchKind::SelDrag;
            Sound(kSndRangeStart);
            GridAt(vx, vy, px, py, s_selGridX1, s_selGridY1);
            s_selStepX = s_selGridX1;
            s_selStepY = s_selGridY1;
        }
        break;
    }
    case TouchKind::SelDrag:
        GridAt(vx, vy, px, py, s_selGridX1, s_selGridY1);
        break;
    case TouchKind::CarryDrag:
        MoveCarry(px, py);
        break;
    default:
        break;
    }
}

void SelectRelease(s32 vx, s32 vy) {
    switch (s_touchKind) {
    case TouchKind::SelPendIn:
        OpenList(vx, vy, s_selStartTx, s_selStartTy);   // 範囲の中のタップ
        break;
    case TouchKind::SelPendOut: {           // 範囲の外のタップ: そのマスだけを選ぶ（アイテムなら名前も）
        s_sel.active = true;
        s_selMasked = false;
        s_sel.x0 = s_sel.x1 = s_selStartTx;
        s_sel.y0 = s_sel.y1 = s_selStartTy;
        s_dropFrames = 0xFFFFFFFFu;
        Sound(kSndSelectOn);                // 空きマスも選択（埋めるに使う）なので必ず鳴らす
        ShowNameAt(vx, vy, s_selStartTx, s_selStartTy);
        break;
    }
    case TouchKind::SelDrag: {              // 吸着した 2 点の四角に丸ごと入るマス。幅か高さが 0 なら何も選ばない
        const s32 gx0 = s_selGridX0 < s_selGridX1 ? s_selGridX0 : s_selGridX1;
        const s32 gx1 = s_selGridX0 < s_selGridX1 ? s_selGridX1 : s_selGridX0;
        const s32 gy0 = s_selGridY0 < s_selGridY1 ? s_selGridY0 : s_selGridY1;
        const s32 gy1 = s_selGridY0 < s_selGridY1 ? s_selGridY1 : s_selGridY0;
        if (gx0 == gx1 || gy0 == gy1) {
            ClearSel();
            break;
        }
        s_sel.active = true;
        s_selMasked = false;
        s_sel.x0 = gx0;
        s_sel.x1 = gx1 - 1;
        s_sel.y0 = gy0;
        s_sel.y1 = gy1 - 1;
        s_dropFrames = 0xFFFFFFFFu;
        Sound(kSndSelectOn);                // 選んだ（ゲームは 1 個以上のとき sub_B3D014 で POLY_ON。こちらは空きマスだけの範囲も選択）
        break;
    }
    case TouchKind::CarryDrag:
        CommitCarry(vx, vy);
        break;
    default:
        break;
    }
}

// 毎フレーム: 範囲の中の長押し（6 フレーム）で持ち上げる・範囲を引いている間の音・行き先が変わった音・置いたアニメの拍
void SelectFrame(s32 vx, s32 vy) {
    if (s_touchKind == TouchKind::SelPendIn && s_touchPrevDown && ++s_holdFrames >= kLiftFrames) {
        if (Lift(Carry::Move, vx, vy, s_touchLastPx, s_touchLastPy))
            s_touchKind = TouchKind::CarryDrag;
        else
            s_touchKind = TouchKind::Ignore;
    }
    if (s_touchKind == TouchKind::SelDrag && (s_selGridX1 != s_selStepX || s_selGridY1 != s_selStepY)) {
        s_selStepX = s_selGridX1;           // 終点が格子 1 本動いた
        s_selStepY = s_selGridY1;
        MoveSound(kSndRangeStep, ((float)s_selGridX1) * kWorldTile);
    }
    if (s_carry != Carry::None) {
        ++s_carryFrames;
        s32 ox = 0, oy = 0;
        CarryOffset(vx, vy, ox, oy);
        if (ox != s_carrySoundX || oy != s_carrySoundY) {
            s_carrySoundX = ox;
            s_carrySoundY = oy;
            if (CarryPlaceable(ox, oy))
                MoveSound(kSndCarryStep, CarryWorldX(ox));
            else
                Sound(kSndInvalid);
        }
    }
    if (s_dropFrames <= kAnimPulseEnd)
        ++s_dropFrames;
}

// 範囲選択モードの B: 一覧・持ち上げ・範囲を順に 1 段ずつ解く
void SelectCancel(void) {
    if (ListBusy())
        return;                             // 一覧の B は窓の部品のショートカット（取消の行 = 「やめる」）が受ける（StepList）
    if (s_carry != Carry::None) {
        CancelCarry();
        Sound(kSndInvalid);
        if (s_touchKind == TouchKind::CarryDrag)
            s_touchKind = TouchKind::Ignore;
        return;
    }
    HideName();
    CancelSel();
}

// 範囲選択モードを離れた: 音を出さずに全部解く
void SelectReset(void) {
    CloseList();
    CancelCarry();
    ClearSel();
}

// ---- 枠（cip_group_00）。引いている間 = ゲームの CollectChip と同じ（吸着した始点・終点、中点・|差| + 10）。
//   ゲームは離すと枠を消してコマの縞で見せるが、空きマスも選べる（埋める）ので、離したあとも選んだマスの四角に W_Group だけ残す（自前）----
bool UpdateGroup(s32 vx, s32 vy) {
    if (!s_groupBuilt)
        return false;
    const float ox = F(s_roomPane, kPaneGlobalX), oy = F(s_roomPane, kPaneGlobalY);
    const float half = (float)kView * 0.5f;
    float ax, ay, bx, by;
    bool markers = false;
    if (s_touchKind == TouchKind::SelDrag) {
        ax = ox + ((float)(s_selGridX0 - vx) - half) * kTile;
        ay = oy - ((float)(s_selGridY0 - vy) - half) * kTile;
        bx = ox + ((float)(s_selGridX1 - vx) - half) * kTile;
        by = oy - ((float)(s_selGridY1 - vy) - half) * kTile;
        markers = true;
    } else if (s_sel.active) {
        s32 cx = 0, cy = 0;                 // 持ち上げている間は行き先に吸着して動く
        if (s_carry != Carry::None)
            CarryOffset(vx, vy, cx, cy);
        ax = ox + ((float)(s_sel.x0 + cx - vx) - half) * kTile;
        ay = oy - ((float)(s_sel.y0 + cy - vy) - half) * kTile;
        bx = ox + ((float)(s_sel.x1 + 1 + cx - vx) - half) * kTile;
        by = oy - ((float)(s_sel.y1 + 1 + cy - vy) - half) * kTile;
    } else {
        return false;
    }
    const float dx = bx - ax, dy = by - ay;
    SetTranslate(s_gWin, ax + dx * 0.5f, ay + dy * 0.5f);
    F(s_gWin, 72) = (dx < 0 ? -dx : dx) + kGroupPad;
    F(s_gWin, 76) = (dy < 0 ? -dy : dy) + kGroupPad;
    SetTranslate(s_gStart, ax, ay);
    SetTranslate(s_gEnd, bx, by);
    B(s_gStart, kPaneFlagsByte) = (u8)((B(s_gStart, kPaneFlagsByte) & 0xFEu) | (markers ? 1u : 0u));
    B(s_gEnd, kPaneFlagsByte) = (u8)((B(s_gEnd, kPaneFlagsByte) & 0xFEu) | (markers ? 1u : 0u));
    LayoutCalc(s_group);
    return true;
}

// 上画面に見せる選択: 範囲を引いている途中はいまの吸着した四角（離したときに選ばれる物と同じ計算。SelectRelease）、
//   そうでなければ確定した範囲（利用者指示 2026-09-28: 引いている途中もカーソルと赤）
bool ShownSel(SelRect &r) {
    if (s_touchKind == TouchKind::SelDrag) {
        const s32 gx0 = s_selGridX0 < s_selGridX1 ? s_selGridX0 : s_selGridX1;
        const s32 gx1 = s_selGridX0 < s_selGridX1 ? s_selGridX1 : s_selGridX0;
        const s32 gy0 = s_selGridY0 < s_selGridY1 ? s_selGridY0 : s_selGridY1;
        const s32 gy1 = s_selGridY0 < s_selGridY1 ? s_selGridY1 : s_selGridY0;
        if (gx0 == gx1 || gy0 == gy1)
            return false;
        r = { true, gx0, gy0, gx1 - 1, gy1 - 1 };
        return true;
    }
    r = s_sel;
    return s_sel.active;
}

// 見せる選択のマスか（引いている途中は四角の全部、確定した範囲は印も見る）
bool ShownSelHas(const SelRect &r, s32 x, s32 y) {
    if (x < r.x0 || x > r.x1 || y < r.y0 || y > r.y1)
        return false;
    return s_touchKind == TouchKind::SelDrag || !s_selMasked || SelMaskBit(x, y);
}

bool NearView(s32 vx, s32 vy, s32 x, s32 y) {
    return x >= vx - kNearMargin && y >= vy - kNearMargin && x < vx + kView + kNearMargin && y < vy + kView + kNearMargin
        && x >= 0 && y >= 0 && x < kTilesX && y < kTilesY;
}

// 上画面の UnitCursor（青。メニュースレッドが GridCursor へ渡す）: 持ち上げ中は行き先のうち盤面の周りのマス、
//   そうでなければ指が触れているマスと範囲選択の中のマス（利用者指示 2026-09-27 / 28）
void PublishCursor(s32 vx, s32 vy) {
    u32 n = 0;
    if (s_carry != Carry::None) {
        s32 ox = 0, oy = 0;
        CarryOffset(vx, vy, ox, oy);
        for (u32 k = 0; k < s_carriedCount && n < kMaxCursorTiles; ++k) {
            const s32 x = s_carried[k].x + ox, y = s_carried[k].y + oy;
            if (!NearView(vx, vy, x, y))
                continue;
            s_cursorX[n] = (u8)x;
            s_cursorY[n] = (u8)y;
            ++n;
        }
    } else {
        // 指のマス。範囲を引いている間は出さない（範囲だけを見せる。利用者指示 2026-09-28）
        const bool finger = s_fingerX >= 0 && s_fingerY >= 0 && s_touchKind != TouchKind::SelDrag;
        if (finger) {
            s_cursorX[0] = (u8)s_fingerX;
            s_cursorY[0] = (u8)s_fingerY;
            n = 1;
        }
        // 配置モード: 名前を出しているアイテムのマスは出し続ける（利用者指示 2026-09-28）
        if (s_mode == Mode::Place && s_nameChip != nullptr && s_nameTileX >= 0 && s_nameTileY >= 0
            && !(finger && s_nameTileX == s_fingerX && s_nameTileY == s_fingerY) && NearView(vx, vy, s_nameTileX, s_nameTileY)) {
            s_cursorX[n] = (u8)s_nameTileX;
            s_cursorY[n] = (u8)s_nameTileY;
            ++n;
        }
        // 範囲選択の中の全部のマス（利用者指示 2026-09-28。引いている途中も）。盤面の周りだけ・最大 64
        SelRect r;
        if (s_mode == Mode::Select && ShownSel(r))
            for (s32 y = r.y0; y <= r.y1 && n < kMaxCursorTiles; ++y)
                for (s32 x = r.x0; x <= r.x1 && n < kMaxCursorTiles; ++x) {
                    if (!NearView(vx, vy, x, y) || (finger && x == s_fingerX && y == s_fingerY) || !ShownSelHas(r, x, y))
                        continue;
                    s_cursorX[n] = (u8)x;
                    s_cursorY[n] = (u8)y;
                    ++n;
                }
    }
    if (n != s_cursorCount || n != 0) {
        s_cursorCount = n;
        s_cursorSeq = s_cursorSeq + 1;
    }
}

// 赤くするマス（MapEditor3D）: 持ち上げ中は無し（元の実体の赤を戻す。利用者指示）。指が触れているアイテムと、範囲選択の中
bool HighlightTile(s32 x, s32 y) {
    if (s_carry != Carry::None)
        return false;
    if (x == s_fingerX && y == s_fingerY && s_touchKind != TouchKind::SelDrag)
        return true;                        // 指のマス（範囲を引いている間は範囲だけ）
    if (s_mode == Mode::Place && s_nameChip != nullptr && x == s_nameTileX && y == s_nameTileY)
        return true;                        // 配置モードで名前を出しているアイテム
    SelRect r;
    return s_mode == Mode::Select && ShownSel(r) && ShownSelHas(r, x, y);
}

// 移動の複製（MapEditor3D）: 行き先が盤面の周りにある物だけ
MapEditor3D::Clone s_clones[MapEditor3D::kMaxClones];

u32 CollectClones(s32 vx, s32 vy) {
    if (s_carry == Carry::None)
        return 0;
    s32 ox = 0, oy = 0;
    CarryOffset(vx, vy, ox, oy);
    u32 n = 0;
    for (u32 k = 0; k < s_carriedCount && n < MapEditor3D::kMaxClones; ++k) {
        const s32 x = s_carried[k].x + ox, y = s_carried[k].y + oy;
        if (!NearView(vx, vy, x, y))
            continue;
        s_clones[n].item = s_carried[k].value;
        s_clones[n].x = (u8)x;
        s_clones[n].y = (u8)y;
        s_clones[n].srcX = (u8)s_carried[k].x;
        s_clones[n].srcY = (u8)s_carried[k].y;
        ++n;
    }
    return n;
}

void TouchRelease(void) {
    s_touchPrevDown = false;
    s_fingerX = s_fingerY = -1;
    s_touchKind = TouchKind::None;
    s_touchLastX = s_touchLastY = -1;
    EndHold();
}

// 1 点。配置: 空きから始めたらなぞったマスに置く／アイテムから始めたら名前（長押しでスポイト）。削除: なぞったマスを消す
void TouchSample(s32 vx, s32 vy, bool down, u16 px, u16 py) {
    // 一覧が出ている間はゲームがタッチを使う。閉じたあとも、その指を離すまでは使わない
    if (s_touchWaitUp) {
        if (!down)
            s_touchWaitUp = false;
        return;
    }
    if (ListBusy()) {
        if (down) {
            TouchRelease();
            s_touchWaitUp = true;
        }
        return;
    }
    if (!down) {
        if (s_touchPrevDown && s_mode == Mode::Select)
            SelectRelease(vx, vy);
        TouchRelease();
        return;
    }
    if (!s_touchPrevDown) {                 // 押し始め
        s_touchPrevDown = true;
        s_touchStartX = s_touchLastPx = px;
        s_touchStartY = s_touchLastPy = py;
        s_holdFrames = 0;
        s_noItemTold = false;
        s32 tx = -1, ty = -1;
        const bool inside = TouchTile(vx, vy, px, py, tx, ty);
        s_touchLastX = inside ? tx : -1;
        s_touchLastY = inside ? ty : -1;
        s_fingerX = s_touchLastX;
        s_fingerY = s_touchLastY;
        if (s_mode == Mode::Select) {
            SelectPress(vx, vy, px, py, inside, tx, ty);
        } else if (!inside) {
            HideName();
            s_touchKind = TouchKind::Ignore;
        } else if (s_mode == Mode::Remove) {
            HideName();
            s_touchKind = TouchKind::Erase;
            EraseAt(tx, ty);
        } else {
            const u32 *item = ItemAtTile(tx, ty);
            if (item != nullptr && !IsEmpty(item)) {
                ShowNameAt(vx, vy, tx, ty);
                s_touchKind = TouchKind::Hold;
                s_holdTileX = tx;
                s_holdTileY = ty;
                s_pickX = px;
                s_pickY = py;
            } else {
                HideName();
                s_touchKind = TouchKind::Paint;
                PlaceAt(tx, ty);
            }
        }
        return;
    }
    switch (s_touchKind) {
    case TouchKind::Paint:
    case TouchKind::Erase:
        Trace(vx, vy, px, py);
        break;
    case TouchKind::Hold: {
        // 指が長押しのマスを出たら配置に切り替え、なぞった先に置く（利用者指示 2026-09-27。長押しのマス自体は物があるので置かない）
        s32 tx = -1, ty = -1;
        if (!TouchTile(vx, vy, px, py, tx, ty) || tx != s_holdTileX || ty != s_holdTileY) {
            EndHold();
            HideName();
            s_touchKind = TouchKind::Paint;
            s_touchLastX = s_holdTileX;
            s_touchLastY = s_holdTileY;
            Trace(vx, vy, px, py);
            break;
        }
        const float dx = (float)px - (float)s_touchStartX, dy = (float)py - (float)s_touchStartY;
        if (dx * dx + dy * dy >= kHoldSlop * kHoldSlop) {
            s_touchKind = TouchKind::Ignore;    // 同じマスの中で大きく動かしたら長押しだけやめる
            EndHold();
        }
        break;
    }
    default:
        if (s_mode == Mode::Select)
            SelectMove(vx, vy, px, py);
        break;
    }
    s_touchLastPx = px;
    s_touchLastPy = py;
    s32 fx = -1, fy = -1;                   // 上画面の青いカーソル・赤いハイライトの「触れているマス」
    if (!TouchTile(vx, vy, px, py, fx, fy))
        fx = fy = -1;
    s_fingerX = fx;
    s_fingerY = fy;
}

// 毎フレーム: 溜まった点を順に処理し、長押しを 1 フレーム進める
void StepTouch(s32 vx, s32 vy) {
    const u32 head = s_touchHead;
    if (head - s_touchTail > kTouchRing)
        s_touchTail = head - kTouchRing;    // 溢れた分（ゲームが止まっていた間）は捨てる
    const bool busy = s_boardAnim != nullptr;   // 出入りの途中は受け付けない
    while (s_touchTail != head) {
        const u32 w = s_touchRing[s_touchTail % kTouchRing];
        ++s_touchTail;
        TouchSample(vx, vy, (w >> 31) != 0 && !busy, (u16)(w & 0x1FFu), (u16)((w >> 9) & 0xFFu));
    }
    if (s_mode == Mode::Select)
        SelectFrame(vx, vy);
    if (s_touchKind != TouchKind::Hold || !s_touchPrevDown)
        return;
    ++s_holdFrames;
    if (s_holdFrames >= kPickFrames) {
        const u32 *item = ItemAtTile(s_holdTileX, s_holdTileY);
        if (item != nullptr && !IsEmpty(item)) {
            s_placeId = *item & 0x7FFFu;    // 埋めた印（0x8000）と上位の旗は落とす
            s_pickedSeq = s_pickedSeq + 1;
        }
        s_touchKind = TouchKind::Ignore;
        EndHold();
    } else if (s_holdFrames >= kPickShowFrames) {
        // バーは出た時点で空から伸び始める（全体は 0.5 秒のまま）
        s_pickProgress = (float)(s_holdFrames - kPickShowFrames) / (float)(kPickFrames - kPickShowFrames);
    }
}

void StepName(s32 vx, s32 vy) {
    if (s_nameChip == nullptr)
        return;
    // 盤面を動かした・そのマスのアイテムが無くなった（基準のコマがほかへ回った）ら消す
    if (vx != s_nameViewX || vy != s_nameViewY || ChipAt(s_nameTileX, s_nameTileY, true) != s_nameChip)
        HideName();
}

// 毎フレーム: 一覧を進め（状態の calc → 行のボタン → Layout 計算。ChoiceStandardItem sub_5E7B08 と同じ sub_2BABAC）、
//   行が決まったら閉じて実行する。窓の外のタッチ（+1228）は取り消し
void StepList(s32 vx, s32 vy) {
    if (!ListBusy()) {
        s_listActive = false;
        return;
    }
    // 十字・A・B をこの呼び出しの間だけ窓の部品が読む欄に入れる（上の説明）
    const u32 menu = R32(kMenuMgrPtr);
    u32 trig = 0, repeat = 0;
    {
        const u32 keys = s_listKeys;
        u32 held = 0;
        if (keys & (u32)Key::A) held |= kMenuKeyA;
        if (keys & (u32)Key::B) held |= kMenuKeyB;
        if (keys & (u32)Key::DPadUp) held |= kMenuKeyUp;
        if (keys & (u32)Key::DPadDown) held |= kMenuKeyDown;
        if (keys & (u32)Key::DPadLeft) held |= kMenuKeyLeft;
        if (keys & (u32)Key::DPadRight) held |= kMenuKeyRight;
        trig = held & ~s_listPrevKeys;
        for (u32 i = 0; i < 32; ++i) {      // sub_542E88 と同じ: 押している間 c を数え、c == 遅れ か 遅れを越えて間隔ごとに立てる
            const u32 bit = 1u << i;
            if ((held & bit) == 0) {
                s_listHold[i] = 0;
                continue;
            }
            const u32 c = s_listHold[i];
            if ((kMenuRepeatMask & bit) != 0 && (c == kMenuRepeatDelay || (c > kMenuRepeatDelay && (c - kMenuRepeatDelay) % kMenuRepeatEvery == 0)))
                repeat |= bit;
            s_listHold[i] = c + 1;
        }
        s_listPrevKeys = held;
    }
    const bool inject = menu != 0 && (trig | repeat) != 0;
    u32 oldTrig = 0, oldRepeat = 0;
    if (inject) {
        oldTrig = R32(menu + kMenuPadTrig);
        oldRepeat = R32(menu + kMenuPadRepeat);
        W(reinterpret_cast<void *>(menu), kMenuPadTrig) = trig;
        W(reinterpret_cast<void *>(menu), kMenuPadRepeat) = repeat;
    }
    ListUpdate(s_listWin);
    if (inject) {
        W(reinterpret_cast<void *>(menu), kMenuPadTrig) = oldTrig;
        W(reinterpret_cast<void *>(menu), kMenuPadRepeat) = oldRepeat;
    }
    if (W(s_listWin, kListState) == kListDecided && W(s_listWin, kListState + 4) == 0) {
        const s32 row = (s32)W(s_listWin, kListResult);
        const bool cancelled = B(s_listWin, kListCancelled) != 0;
        ChangeState(s_listWin, kListClose, 0);
        if (!cancelled && row >= 0 && row < (s32)kListRowCount)
            RunListAction((u32)row, vx, vy);
    }
    s_listActive = ListBusy();
}

float ViewCenter(s32 v) {
    return 32.0f * (float)(v + kView / 2);
}

// カメラの目標 = 盤面の中心（FieldCamera が描画スレッドで呼ぶ）
void CameraTarget(float out[3]) {
    out[0] = ViewCenter(s_viewX);
    out[1] = 0.0f;
    out[2] = ViewCenter(s_viewY) + 32.0f * kCameraSouthTiles;
    out[1] = GroundHeight(out, 0);
}

// ---------------------------------------------------------------------------------------------
// メニュースレッド
// ---------------------------------------------------------------------------------------------

bool s_running;
bool s_failed;
CursorRepeat s_repeat;                      // 十字キーとスライドパッドの押し続け（公共事業エディターと同じ規則）
bool s_touchHeldAtStart;
u32 s_cursorShown = 0xFFFFFFFFu;           // GridCursor へ渡した行き先の番号
bool s_touchPushedDown;                     // 最後に積んだ点が「触れている」
void PushTouch(bool down, u32 x, u32 y);
u32 s_prevKeys;
u32 s_pickedShown, s_noItemShown;
s32 s_minX, s_minY, s_maxX, s_maxY;         // 盤面の左上にできる範囲

// 盤面の左上にできる範囲 = アイテムのあるエーカー（部屋データ +0..+C）。取れなければ村全体
void ReadBounds(void) {
    s_minX = 0;
    s_minY = 0;
    s_maxX = kTilesX - kView;
    s_maxY = kTilesY - kView;
    const u32 field = R32(kFieldPtr);
    if (field < 0x08000000u || field >= 0x40000000u || (field & 3u) != 0)
        return;
    const s32 ax0 = S32(field), ay0 = S32(field + 4), ax1 = S32(field + 8), ay1 = S32(field + 12);
    if (ax0 < 0 || ay0 < 0 || ax1 < ax0 || ay1 < ay0 || ax1 >= kTilesX / 16 || ay1 >= kTilesY / 16)
        return;
    s_minX = ax0 * 16;
    s_minY = ay0 * 16;
    s_maxX = (ax1 + 1) * 16 - kView;
    s_maxY = (ay1 + 1) * 16 - kView;
}

void MoveView(s32 dx, s32 dy) {
    s32 x = s_viewX + dx, y = s_viewY + dy;
    if (x < s_minX) x = s_minX;
    if (y < s_minY) y = s_minY;
    if (x > s_maxX) x = s_maxX;
    if (y > s_maxY) y = s_maxY;
    s_viewX = x;
    s_viewY = y;
}

// 建物の衝突判定（公共事業エディターと同じ形）のマスをコマにする
void CollectBuildings(void) {
    u32 n = 0;
    for (u32 i = 0; i < PublicWorks::kSlots; ++i) {
        PublicWorks::Slot slot;
        if (!PublicWorks::ReadSlot(i, slot) || slot.id >= PublicWorks::kEmptyId)
            continue;
        s8 dx[64], dy[64];
        const u32 cells = BuildingEditor::CollisionCells(slot.id, dx, dy, 64);
        if (cells == 0)
            continue;
        for (u32 k = 0; k < cells && n < kMaxBuildChips; ++k) {      // 衝突判定のマスごとに 1x1（利用者指示 2026-09-27）
            const s32 x = (s32)slot.x + dx[k], y = (s32)slot.y + dy[k];
            if (x >= 0 && y >= 0 && x < kTilesX && y < kTilesY)
                s_buildChips[n++] = { (u8)x, (u8)y, 1, 1 };
        }
    }
    s_buildChipCount = n;
}

bool Start(void) {
    if (BuildingEditor::Running()) {
        GuiDialog::ShowMessage(Cheats::kMeOn, u8"公共事業エディターを先に止めてください");
        return false;
    }
    if (s_stage != Stage::Off) {
        GuiDialog::ShowMessage(Cheats::kMeOn, u8"前回の片付けが終わっていません。もう一度入れ直してください");
        return false;
    }
    if (!PublicWorks::StartFrameHook()) {
        GuiDialog::ShowMessage(Cheats::kMeOn, u8"フックが入れられません");
        return false;
    }
    u32 px = 0, py = 0;
    if (!FieldCamera::Available() || !PublicWorks::PlayerTile(px, py)) {
        GuiDialog::ShowMessage(Cheats::kMeOn, u8"村の屋外で使ってください");
        return false;
    }
    if (!GridCursor::ShowTiles()) {         // 持ち上げ中の行き先に UnitCursor（公共事業エディターと同じ出し方）
        GuiDialog::ShowMessage(Cheats::kMeOn, u8"グリッドカーソルを先に止めてください");
        return false;
    }
    GridCursor::SetTint(kCursorBlue, kCursorTint);     // 青（利用者指示 2026-09-27）
    GridCursor::SetTiles(nullptr, nullptr, 0, -1, 0, 0);
    s_cursorShown = 0xFFFFFFFFu;
    if (!GameList::HoldField(true)) {
        GuiDialog::ShowMessage(Cheats::kMeOn, u8"下画面を使えません");
        return false;
    }
    CollectBuildings();
    ReadBounds();
    s_viewX = (s32)px - kView / 2;
    s_viewY = (s32)py - kView / 2;
    MoveView(0, 0);
    s_error = "";
    PushTouch(false, 0, 0);
    s_touchHeldAtStart = true;              // 押したまま始めたタッチは押し始めにしない
    s_prevKeys = 0xFFFFFFFFu;               // 押しっぱなしのボタンを最初の押下にしない
    s_mode = Mode::Place;
    s_pickedShown = s_pickedSeq;
    s_noItemShown = s_noItemSeq;
    s_repeat.Reset();
    s_startRoom = RoomId();
    FieldCamera::Want(CameraTarget, false);
    s_want = true;
    s_running = true;
    return true;
}

// 十字キーとスライドパッドのどちらでも動かす（利用者指示 2026-09-27。押し続けは公共事業エディターと同じ）
void StepMove(u32 keys) {
    u32 held = 0;
    if (keys & ((u32)Key::DPadUp | (u32)Key::CPadUp)) held |= CursorRepeat::kUp;
    if (keys & ((u32)Key::DPadDown | (u32)Key::CPadDown)) held |= CursorRepeat::kDown;
    if (keys & ((u32)Key::DPadLeft | (u32)Key::CPadLeft)) held |= CursorRepeat::kLeft;
    if (keys & ((u32)Key::DPadRight | (u32)Key::CPadRight)) held |= CursorRepeat::kRight;
    const u32 fire = s_repeat.Step(held);
    s32 dx = 0, dy = 0;
    if (fire & CursorRepeat::kUp) dy -= 1;
    if (fire & CursorRepeat::kDown) dy += 1;
    if (fire & CursorRepeat::kLeft) dx -= 1;
    if (fire & CursorRepeat::kRight) dx += 1;
    if (dx != 0 || dy != 0)
        MoveView(dx, dy);
}

const char *ModeName(Mode m) {
    return m == Mode::Remove ? u8"削除モード" : m == Mode::Select ? u8"範囲選択モード" : u8"配置モード";
}

// 描画スレッドからの出来事を通知する（スポイトで取れた・配置するアイテムが無い）
void NotifyEvents(void) {
    if (s_pickedSeq != s_pickedShown) {
        s_pickedShown = s_pickedSeq;
        const u32 id = s_placeId;
        char name[64];
        char msg[96];
        if (!ItemNames::NameUtf8((u16)id, name, sizeof(name)))
            name[0] = '\0';
        std::snprintf(msg, sizeof(msg), u8"配置するアイテム: %04X %s", (unsigned)id, name);
        GuiMenu::Notify(Cheats::kMeOn, msg);
    }
    if (s_noItemSeq != s_noItemShown) {
        s_noItemShown = s_noItemSeq;
        GuiMenu::NotifyRed(Cheats::kMeOn, u8"配置するアイテムをメニューかスポイトで選んでください");
    }
}

void PushTouch(bool down, u32 x, u32 y) {
    if (!down && !s_touchPushedDown)
        return;                             // 離れている間は積まない（離した瞬間の 1 点だけ）
    if (x > 319) x = 319;
    if (y > 239) y = 239;
    const u32 head = s_touchHead;
    s_touchRing[head % kTouchRing] = (down ? 0x80000000u : 0u) | x | (y << 9);
    __sync_synchronize();                   // 点を書いてから通算を進める
    s_touchHead = head + 1;
    s_touchPushedDown = down;
}

// 持ち上げ中の行き先を上画面の UnitCursor へ（GridCursor はメニュースレッドから頼む）

void ForwardCursor(void) {
    const u32 seq = s_cursorSeq;
    if (seq == s_cursorShown)
        return;
    s_cursorShown = seq;
    u8 xs[kMaxCursorTiles], ys[kMaxCursorTiles];
    u32 n = s_cursorCount;
    if (n > kMaxCursorTiles)
        n = kMaxCursorTiles;
    for (u32 i = 0; i < n; ++i) {
        xs[i] = s_cursorX[i];
        ys[i] = s_cursorY[i];
    }
    GridCursor::SetTiles(xs, ys, n, -1, 0, 0);
}

void StepTouchInput(void) {
    const bool down = Touch::IsDown();
    // 押したまま始めたタッチは、一度離すまで使わない
    if (s_touchHeldAtStart && !down)
        s_touchHeldAtStart = false;
    if (!down || s_touchHeldAtStart) {
        PushTouch(false, 0, 0);
        return;
    }
    const UIntVector pos = Touch::GetPosition();
    PushTouch(true, pos.x, pos.y);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// 描画スレッド
// ---------------------------------------------------------------------------------------------

void FrameStep(void) {
    const bool want = s_want;
    void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
    // 場面が変わった: アニメを待たずに描くのをやめ、数フレーム後に壊す
    if ((s_stage == Stage::Live || s_stage == Stage::Leaving) && RoomId() != s_startRoom) {
        s_stage = Stage::Waiting;
        s_waitFrames = 0;
        return;
    }
    switch (s_stage) {
    case Stage::Off:
        if (!want || s_error[0] != 0 || !GameList::FieldHidden())
            return;
        s_startRoom = RoomId();
        s_stage = Stage::Loading;
        // fallthrough
    case Stage::Loading:
        // 読み込みの途中では壊さない（終わってから片付ける）
        if (!LoadStep())
            return;
        s_stage = want ? Stage::Building : Stage::Waiting;
        s_waitFrames = 0;
        return;
    case Stage::Building:
        if (!want || !BuildStep()) {
            if (!want || s_error[0] != 0) {
                s_stage = Stage::Waiting;
                s_waitFrames = 0;
            }
            return;
        }
        s_stage = Stage::Live;
        PlayBoard(s_boardIn);
        break;
    case Stage::Live:
        if (!want) {
            HideName();
            PlayBoard(s_boardOut);
            s_stage = Stage::Leaving;
        }
        break;
    case Stage::Leaving:
        // 一覧も閉じ終わってから（閉じ終わりで手カーソルの状態をゲームが戻す）。上画面の赤・複製も戻し終わってから
        if (s_boardAnim == nullptr && !ListBusy() && MapEditor3D::Release()) {
            s_stage = Stage::Waiting;
            s_waitFrames = 0;
            return;
        }
        break;
    case Stage::Waiting:
        if (++s_waitFrames >= kTeardownWaitFrames)
            DestroyAll();
        return;
    }

    // ---- Live / Leaving ----
    const s32 vx = s_viewX, vy = s_viewY;
    StepBoardAnim();
    LayoutCalc(s_board);                    // N_Room_00 の大域位置（コマの基準）を今のフレームにする
    ++s_frameNo;
    s_ghostStart = 0xFFFFFFFFu;
    CollectWants(vx, vy);
    AssignChips(vx, vy);
    for (u32 k = 0; k < s_drawCount; ++k) {     // 吹き出しは基準のコマの大域位置を読むので先に計算する
        Chip *c = s_drawOrder[k];
        CalcChip(*c, s_chipLook[c - s_chips]);
    }
    if (s_stage == Stage::Live) {
        if (s_mode != Mode::Select && (s_sel.active || s_carry != Carry::None || ListBusy()))
            SelectReset();                  // 範囲選択モードを離れた
        if (s_cancelSeq != s_cancelDone) {
            s_cancelDone = s_cancelSeq;
            if (s_mode == Mode::Select)
                SelectCancel();
        }
        StepName(vx, vy);
        StepTouch(vx, vy);
    } else {
        EndHold();
        CloseList();
        CancelCarry();
    }
    PublishCursor(vx, vy);
    if (s_stage == Stage::Live)             // 上画面: 赤いハイライトと移動の複製（Leaving は Release が戻す）
        MapEditor3D::Frame(HighlightTile, s_clones, CollectClones(vx, vy));
    const bool group = UpdateGroup(vx, vy);
    StepList(vx, vy);
    NameCalc();
    // 描画登録（リストは毎フレーム空になる）。盤面 → 建物 → アイテム → 名前
    if (mgr != nullptr) {
        AddLayout(mgr, s_board, 1);
        const u32 ghosts = s_ghostStart < s_drawCount ? s_ghostStart : s_drawCount;
        for (u32 k = 0; k < ghosts; ++k)
            AddLayout(mgr, s_drawOrder[k]->layout, 1);
        if (group)
            AddLayout(mgr, s_group, 1);     // 枠は盤面のコマの上、持ち上げたコマの下
        for (u32 k = ghosts; k < s_drawCount; ++k)
            AddLayout(mgr, s_drawOrder[k]->layout, 1);
        AddLayout(mgr, s_name + kNameLayout, 1);
        if (ListBusy())
            AddLayout(mgr, s_listWin + kListLayout, 1);
    }
}

// ---------------------------------------------------------------------------------------------
// メニュースレッド（公開）
// ---------------------------------------------------------------------------------------------

bool Running(void) {
    return s_running;
}

bool PickProgress(float &progress, int &x, int &y) {
    const float p = s_pickProgress;
    if (!s_running || p < 0.0f)
        return false;
    progress = p;
    x = s_pickX;
    y = s_pickY;
    return true;
}

u32 PlaceItem(void) {
    return s_placeId;
}

void SetPlaceItem(u32 id) {
    s_placeId = id;
}

void Reset(void) {
    s_failed = false;
}

void Stop(void) {
    if (!s_running)
        return;
    s_running = false;
    s_want = false;
    GridCursor::Hide();
    FieldCamera::Release();
    // カメラは描画スレッドが戻す。盤面は退場アニメのあと描画スレッドが片付け、元の下画面 UI を戻す（DestroyAll）。
    for (u32 i = 0; i < 18 && FieldCamera::Patched(); ++i)
        svcSleepThread(16666667LL);
}

void Tick(u32 keys) {
    if (s_failed)
        return;
    if (!s_running) {
        if (!Start())
            s_failed = true;
        return;
    }
    u32 lostReason = 0;
    if (FieldCamera::Lost(lostReason) || s_error[0] != 0) {
        const char *why = s_error[0] != 0 ? s_error
                        : lostReason == 1 ? u8"村の屋外を離れたので止めました"
                        : lostReason == 3 ? u8"カメラの関数がほかの改造で書き換わっています"
                                          : u8"カメラが取れません";
        Stop();
        GuiDialog::ShowMessage(Cheats::kMeOn, why);
        s_failed = true;
        return;
    }
    // プレイヤーとゲームのタッチを止める（毎ティック頼み続けている間だけ効く）
    GuiMenu::BlockGameAll();
    if (!s_listActive)                      // 一覧（ゲームの ItemSelectWindow）はゲームのタッチで動く
        GuiMenu::BlockGameTouch();
    // 一覧が開いている間の十字・A は一覧へ（描画スレッドが窓の更新の間だけ渡す）。盤面は動かさない
    s_listKeys = s_listActive ? (keys & ((u32)Key::A | (u32)Key::B | (u32)Key::DPadUp | (u32)Key::DPadDown | (u32)Key::DPadLeft | (u32)Key::DPadRight)) : 0u;
    // 一覧が開いている間も、スライドパッドはカメラ（盤面の移動）に使う（利用者指示 2026-09-28。ゲームの一覧はスライドパッドでも
    //   選べるが、一覧へ渡すのは十字・A・B だけ）。十字は一覧が受けるので盤面は動かさない
    const u32 kPad = (u32)Key::CPadUp | (u32)Key::CPadDown | (u32)Key::CPadLeft | (u32)Key::CPadRight;
    StepMove(s_listActive ? (keys & kPad) : keys);  // メニュー表示中は keys = 0（押し続けが切れる。公共事業エディターと同じ）
    const u32 pressed = keys & ~s_prevKeys;
    s_prevKeys = keys;
    // L / R: モードを巡回（利用者の決定。範囲選択は段階 3 で足す）
    if (pressed & ((u32)Key::L | (u32)Key::R)) {
        const u32 count = kModesNow;
        const u32 now = (u32)s_mode;
        s_mode = (Mode)((pressed & (u32)Key::R) ? (now + 1) % count : (now + count - 1) % count);
        GuiMenu::Notify(Cheats::kMeOn, ModeName(s_mode));
    }
    if ((pressed & (u32)Key::B) && s_mode == Mode::Select && !s_listActive)
        s_cancelSeq = s_cancelSeq + 1;      // 持ち上げ・範囲を 1 段ずつ解く（一覧の B は一覧が受ける）
    ForwardCursor();
    NotifyEvents();
    if (GuiMenu::IsVisible()) {
        PushTouch(false, 0, 0);
        return;
    }
    StepTouchInput();
}

}  // namespace MapEditor

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            int     g_mapEditorIndex = -1;
            int     g_placeItemIndex = -1;

            // 配置するアイテム（連動型: エディターの値を読み書きする。スポイトで変わる）。0xFFFF = 未設定
            bool    PlaceItemRead(int index, s32 *value)
            {
                (void)index;
                const u32 id = MapEditor::PlaceItem();
                *value = id == 0xFFFFFFFFu ? 0xFFFF : (s32)id;
                return true;
            }

            void    PlaceItemWrite(int index, s32 value)
            {
                (void)index;
                MapEditor::SetPlaceItem(value < 0 || value > 0x7FFF ? 0xFFFFFFFFu : (u32)value);
            }
            bool    g_mapEditorActive;              // チェック項目の効果（ホットキーで入れ切りする）

            bool    MapEditorIsActive(int index)
            {
                (void)index;
                return g_mapEditorActive;
            }

            void    MapEditorSetActive(int index, bool active)
            {
                (void)index;
                g_mapEditorActive = active;
                if (!active)
                {
                    MapEditor::Stop();
                    MapEditor::Reset();
                }
            }

            const GuiMenu::ToggleEffectFuncs kMapEditorFuncs = { MapEditorIsActive, MapEditorSetActive };
        }

        bool    MapEditorTick(int index, u16 held)
        {
            (void)held;
            if (g_mapEditorIndex < 0 || index != g_mapEditorIndex)
                return false;
            if (!g_mapEditorActive)                 // ホットキーで切られている（項目は ON のまま）
                return true;
            const u32 keys = GuiMenu::IsVisible() ? 0u : Controller::GetKeysDown(true);
            MapEditor::Tick(keys);
            return true;
        }

        bool    MapEditorDisable(int index)
        {
            if (g_mapEditorIndex < 0 || index != g_mapEditorIndex)
                return false;
            g_mapEditorActive = false;
            MapEditor::Stop();
            MapEditor::Reset();
            return true;
        }

        void    WireMapEditor(void)
        {
            g_mapEditorIndex = GuiMenu::FindItem(kMeOn);
            if (g_mapEditorIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_mapEditorIndex, &kMapEditorFuncs);
            g_placeItemIndex = GuiMenu::FindItem(kMePlaceItem);
            if (g_placeItemIndex >= 0)
                GuiMenu::RegisterLinked(g_placeItemIndex, PlaceItemRead, PlaceItemWrite);
        }
    }
}
