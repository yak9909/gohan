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
// ---- 範囲選択（段階 3）----
const u32 kMaxCarry = 64;                   // 持ち上げる・写すアイテムの数（UnitCursor の最大と同じ）
const u32 kLiftFrames = 6;                  // 範囲の中の長押し（ゲームのチップ Select 状態と同じ 6 フレーム）
const float kGroupPad = 10.0f;              // 枠の大きさ = |差| + 10（CollectChip sub_B0ABF8 の flt_B8E240）
const u32 kGroupCmdBytes = 4096;            // CollectChip の組み立て（sub_B0AD88）が渡す大きさ
const u32 kListCmdBytes = 0x3700;           // ItemSelectWindow の組み立て（sub_2BA8B0）が渡す大きさ
const u8 kListPriority = 0x9F;              // ItemSelectWindow / ItemSelectNameWindow と同じ（-97）
const u32 kListRowCount = 4;
const float kListFontW = 14.4f;             // T_slct_cntnt_0X の文字の大きさ（横）。全角 1 字の送り
const float kListRowH = 20.0f;              // T_slct_cntnt_0X の高さ（sub_2B9A20 が行の高さに使う）
const u32 kTextAllocSlot = 28;              // TextBox vt[28] = 器の確保（GameList と同じ）
const u32 kTextDraw = 260;
typedef void (*AllocBufFn)(void *textBox, u32 chars, u32 flags);
typedef void (*SetStringFn)(void *textBox, const u16 *str, u32 start, u32 len);
const SetStringFn SetString = reinterpret_cast<SetStringFn>(0x004BACBC);    // nwlyt_TextBox_SetString（GameList と同じ）
const u32 kPlaySoundFn = 0x0058C7D4;        // Game_PlaySound
const u32 kSndListOpen = 0x010003C4;        // ItemSelectWindow を開いたとき（sub_2BA070）
const u32 kSndListRow = 0x010003C3;         // 行（sub_2BAC8C が各行に置く音）

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
// ---- 描画 → メニュー（持ち上げ中の行き先。メニュースレッドが GridCursor へ渡す）----
volatile u8 s_cursorX[kMaxCarry], s_cursorY[kMaxCarry];
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
    void *nAll;
    void *nRot;
    void *bBtn;
};
Chip s_chips[kMaxChips];
u32 s_chipsMade;

struct WantChip { u8 type; bool rotated; s16 tx, ty; u8 w, h; bool ghost; u8 gi; };
WantChip s_wantChips[kMaxChips];
u32 s_wantCount;
Chip *s_drawOrder[kMaxChips];
u32 s_drawCount;

// タッチの状態（描画スレッドだけ）
enum class TouchKind : u8 { None, Ignore, Paint, Erase, Hold, SelPendIn, SelPendOut, SelDrag, CarryDrag, ListTouch };
u32 s_touchTail;                            // 処理した点（通算）
bool s_touchPrevDown;
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
enum class Carry : u8 { None, Move, Copy, CopyArmed };
Carry s_carry = Carry::None;
struct CarryItem { s16 x, y; u32 value; u8 type; };
CarryItem s_carried[kMaxCarry];
u32 s_carriedCount;
s32 s_carryVx, s_carryVy;                   // 持ち上げたときの盤面
u16 s_carryPressX, s_carryPressY;           // 動かし始めた指
float s_carryBaseX, s_carryBaseY;           // 前のタッチまでの動き（複製は 2 回に分けて動かせる）
float s_carryPixX, s_carryPixY;             // 指の動き（画素。下が +）
s32 s_selStartTx, s_selStartTy;
s32 s_listRow = -1;
alignas(8) u8 s_group[332];
bool s_groupMade, s_groupBuilt;
void *s_gWin, *s_gStart, *s_gEnd;
alignas(8) u8 s_list[332];
alignas(8) u8 s_listIn[40];
alignas(8) u8 s_listOut[40];
bool s_listMade, s_listBuilt, s_listAnimsMade, s_listOpen, s_listClosing;
void *s_listAnim;
void *s_listAll, *s_listWin;
void *s_listRowPane[6], *s_listText[6], *s_listBound[6];
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
    if (!s_listBuilt) {                     // 一覧（持ち物の選択窓。資源は名前の吹き出しが読んだ itm_slct_win.arc）
        void *holder = s_name + 36;         // ItemSelectNameWindow の保持体（組み立てで書体も登録済み）
        if (!s_listMade) {
            LayoutCtor(s_list);
            s_listMade = true;
        }
        W(s_list, kLayoutHolder) = reinterpret_cast<u32>(holder);
        if (LayoutBuild(s_list, "itm_slct_win.bclyt", nullptr, kListCmdBytes) == 0) {
            s_error = u8"itm_slct_win.bclyt を組めません";
            return false;
        }
        s_listBuilt = true;
        B(s_list, kLayoutPriority) = kListPriority;
        AnimCtor(s_listIn);
        AnimCtor(s_listOut);
        s_listAnimsMade = true;
        AnimLoad(s_listIn, "itm_slct_win_in_slct_win.bclan", holder);
        AnimLoad(s_listOut, "itm_slct_win_out_slct_win.bclan", holder);
        SetVisible(s_list, "N_itm_nm_00", false);
        SetVisible(s_list, "N_slct_00", false);            // 十字キーのカーソル（タッチだけなので出さない）
        s_listAll = FindPane(s_list, "N_all");
        s_listWin = FindPane(s_list, "W_slct_00");
        if (s_listAll == nullptr || s_listWin == nullptr) {
            s_error = u8"itm_slct_win の部品がありません";
            return false;
        }
        B(s_listAll, kPaneFlagsByte) &= 0xFEu;
        char name[20];
        for (u32 i = 0; i < 6; ++i) {
            std::snprintf(name, sizeof(name), "N_slct_cntnt_%02u", (unsigned)i);
            s_listRowPane[i] = FindPane(s_list, name);
            std::snprintf(name, sizeof(name), "T_slct_cntnt_%02u", (unsigned)i);
            s_listText[i] = FindPane(s_list, name);
            std::snprintf(name, sizeof(name), "B_cntnts_%02u", (unsigned)i);
            s_listBound[i] = FindPane(s_list, name);
            if (s_listRowPane[i] == nullptr || s_listText[i] == nullptr || s_listBound[i] == nullptr) {
                s_error = u8"itm_slct_win の行がありません";
                return false;
            }
        }
        for (u32 i = 0; i < kListRowCount; ++i) {
            void *box = s_listText[i];
            const u32 draw = W(box, kTextDraw);
            const u32 flags = draw != 0 ? *reinterpret_cast<const u8 *>(draw + 9) : 0;
            u32 *vt = *reinterpret_cast<u32 **>(box);
            reinterpret_cast<AllocBufFn>(vt[kTextAllocSlot])(box, 8, flags);
            u32 n = 0;
            while (kListRows[i][n] != 0)
                ++n;
            SetString(box, reinterpret_cast<const u16 *>(kListRows[i]), 0, n);
        }
        return false;
    }
    return true;
}

void EndHold(void);

void DestroyAll(void) {
    EndHold();
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
            LayoutUnbindAll(s_list);
        LayoutDtor(s_list);
    }
    if (s_listAnimsMade) {
        AnimDtor(s_listIn);
        AnimDtor(s_listOut);
    }
    s_listMade = s_listBuilt = s_listAnimsMade = s_listOpen = s_listClosing = false;
    s_listAnim = nullptr;
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
}

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
        }
    }
    // 持ち上げたコマ（写しを含む）。受け持つマスは村の外の番号にして、盤面のコマと混ぜない
    if (s_carry != Carry::None) {
        for (u32 k = 0; k < s_carriedCount && s_wantCount < kMaxChips; ++k) {
            AddWant(s_carried[k].type, 20000 + (s32)k, 20000, 1, 1, false);
            s_wantChips[s_wantCount - 1].ghost = true;
            s_wantChips[s_wantCount - 1].gi = (u8)k;
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
void AssignChips(void) {
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
    const s32 vx = s_viewX, vy = s_viewY;
    const float half = (float)kView * 0.5f;
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
            if (w.ghost) {                  // 持ち上げたときの盤面での位置 + 指の動き（盤面を動かしても指の下に残る）
                const CarryItem &ci = s_carried[w.gi];
                const float gx = (float)(ci.x - s_carryVx) + 0.5f - half;
                const float gy = (float)(ci.y - s_carryVy) + 0.5f - half;
                SetTranslate(c->nAll, ox + gx * kTile + s_carryPixX, oy - gy * kTile - s_carryPixY);
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
// 範囲選択（段階 3。利用者の決定 2026-09-27）
//   スライド = どこから始めても範囲を引く（範囲の外から）。タップ = そのマスだけを選ぶ（アイテムなら名前も）。
//   範囲の中: 長押し（ゲームのチップと同じ 6 フレーム or 24 動く）で持ち上げて移動、タップで一覧（複製・削除・埋める・やめる）。
//   持ち上げている間は十字・スライドパッドで盤面を動かせる（コマは指の下に残り、行き先は盤面の動いた分だけずれる）。
//   行き先に物があれば上書き。盤面の外のタップと B で選択を解除
// ---------------------------------------------------------------------------------------------

bool InSel(s32 x, s32 y) {
    return s_sel.active && x >= s_sel.x0 && x <= s_sel.x1 && y >= s_sel.y0 && y <= s_sel.y1;
}

void ClearSel(void) {
    s_sel.active = false;
}

// 範囲の中のアイテムを写す（持ち上げ・複製）。戻り値: 数
u32 CaptureSel(void) {
    s_carriedCount = 0;
    for (s32 y = s_sel.y0; y <= s_sel.y1; ++y) {
        for (s32 x = s_sel.x0; x <= s_sel.x1; ++x) {
            const u32 *item = ItemAtTile(x, y);
            if (item == nullptr || IsEmpty(item) || s_carriedCount >= kMaxCarry)
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

// 持ち上げる。基準 = 今の指の位置と今の盤面
bool Lift(Carry mode, s32 vx, s32 vy, u16 px, u16 py) {
    if (CaptureSel() == 0)
        return false;
    HideName();
    s_carry = mode;
    s_carryVx = vx;
    s_carryVy = vy;
    s_carryPressX = px;
    s_carryPressY = py;
    s_carryBaseX = s_carryBaseY = 0.0f;
    s_carryPixX = s_carryPixY = 0.0f;
    return true;
}

// 行き先のずれ（マス）= 指の動き（20 画素で 1 マス、四捨五入）+ 盤面を動かした分
void CarryOffset(s32 vx, s32 vy, s32 &ox, s32 &oy) {
    const float fx = s_carryPixX / kTile, fy = s_carryPixY / kTile;
    ox = FloorI(fx + 0.5f) + (vx - s_carryVx);
    oy = FloorI(fy + 0.5f) + (vy - s_carryVy);
}

void CommitCarry(s32 vx, s32 vy) {
    s32 ox = 0, oy = 0;
    CarryOffset(vx, vy, ox, oy);
    const bool move = s_carry == Carry::Move;
    s_carry = Carry::None;
    if (ox == 0 && oy == 0)
        return;                             // 元の場所（移動なら何もしない・複製なら同じ物を上書きするだけ）
    if (move)
        for (u32 k = 0; k < s_carriedCount; ++k)
            DeleteItem(s_carried[k].x, s_carried[k].y, R32(kFieldPtr));
    for (u32 k = 0; k < s_carriedCount; ++k) {
        const s32 x = s_carried[k].x + ox, y = s_carried[k].y + oy;
        const u32 *item = ItemAtTile(x, y);
        if (item == nullptr)
            continue;                       // 村の外
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
}

void CancelCarry(void) {
    s_carry = Carry::None;
    s_carriedCount = 0;
}

// ---- 一覧（持ち物の選択窓 itm_slct_win の N_slct_win。ItemSelectWindow sub_2B9A20 の大きさの規則を写す）----
enum ListAction : u8 { kActCopy, kActDelete, kActFill, kActCancel };

void OpenList(u16 px, u16 py) {
    if (!s_listBuilt)
        return;
    HideName();
    // 大きさ: 文字の幅（全角 1 字 = 文字の大きさの横 14.4）の最大 + 15、枠はさらに + 30、高さ = 行の高さ × 行数 + 11
    float textW = 0.0f;
    for (u32 i = 0; i < kListRowCount; ++i) {
        u32 n = 0;
        while (kListRows[i][n] != 0)
            ++n;
        const float w = (float)n * kListFontW;
        if (w > textW)
            textW = w;
    }
    const float rowW = textW + 15.0f;
    const float winW = rowW + 30.0f;
    const float winH = kListRowH * (float)kListRowCount + 11.0f;
    F(s_listWin, 72) = winW;
    F(s_listWin, 76) = winH < 32.0f ? 32.0f : winH;
    const float winX = F(s_listWin, kPaneTranslateX);
    for (u32 i = 0; i < 6; ++i) {
        void *row = s_listRowPane[i];
        if (row == nullptr)
            continue;
        const bool on = i < kListRowCount;
        B(row, kPaneFlagsByte) = (u8)((B(row, kPaneFlagsByte) & 0xFEu) | (on ? 1u : 0u));
        if (!on)
            continue;
        F(s_listBound[i], 72) = rowW;
        F(s_listText[i], 72) = rowW;
        F(row, kPaneTranslateX) = -(rowW * 0.5f) - winX + 15.0f;
        B(row, kPaneFlagsByte) &= 0xCFu;
    }
    // 置き場所: 窓の上端（W_slct_00 の基準点は上端中央）を指の少し下に。画面からはみ出すなら上へ、横は画面に収める
    const float lx = (float)px - 160.0f, ly = 120.0f - (float)py;
    float x = lx, y = ly - 12.0f;
    const float h = F(s_listWin, 76);
    if (y - h < -120.0f)
        y = ly + 12.0f + h;
    if (y > 120.0f)
        y = 120.0f;
    if (x - winW * 0.5f < -160.0f)
        x = -160.0f + winW * 0.5f;
    if (x + winW * 0.5f > 160.0f)
        x = 160.0f - winW * 0.5f;
    SetTranslate(s_listAll, x, y);
    B(s_listAll, kPaneFlagsByte) |= 1u;
    if (s_listAnim != nullptr)
        AnimUnbind(s_list, s_listAnim);
    AnimBind(s_list, s_listIn);
    AnimSetFrame(s_listIn, 0.0f);
    s_listAnim = s_listIn;
    s_listOpen = true;
    s_listClosing = false;
    reinterpret_cast<void (*)(u32)>(kPlaySoundFn)(kSndListOpen);
}

void CloseList(void) {
    if (!s_listOpen || s_listClosing)
        return;
    if (s_listAnim != nullptr)
        AnimUnbind(s_list, s_listAnim);
    AnimBind(s_list, s_listOut);
    AnimSetFrame(s_listOut, 0.0f);
    s_listAnim = s_listOut;
    s_listClosing = true;
}

void StepListAnim(void) {
    if (s_listAnim == nullptr)
        return;
    if (AnimFinished(s_listAnim)) {
        AnimUnbind(s_list, s_listAnim);
        s_listAnim = nullptr;
        if (s_listClosing) {
            s_listOpen = false;
            s_listClosing = false;
            B(s_listAll, kPaneFlagsByte) &= 0xFEu;
        }
    } else {
        AnimStep(s_listAnim);
    }
}

// 一覧の行（B_cntnts_0X の大域位置と大きさ。基準点は左中央）。無ければ -1
s32 ListRowAt(u16 px, u16 py) {
    if (!s_listOpen || s_listClosing || s_listAnim != nullptr)
        return -1;
    const float lx = (float)px - 160.0f, ly = 120.0f - (float)py;
    for (u32 i = 0; i < kListRowCount; ++i) {
        void *b = s_listBound[i];
        const float gx = F(b, kPaneGlobalX), gy = F(b, kPaneGlobalY);
        const float w = F(b, 72), h = F(b, 76);
        if (lx >= gx && lx < gx + w && ly <= gy + h * 0.5f && ly > gy - h * 0.5f)
            return (s32)i;
    }
    return -1;
}

bool InListWindow(u16 px, u16 py) {
    if (!s_listOpen)
        return false;
    const float lx = (float)px - 160.0f, ly = 120.0f - (float)py;
    const float x = F(s_listAll, kPaneTranslateX), y = F(s_listAll, kPaneTranslateY);
    const float w = F(s_listWin, 72), h = F(s_listWin, 76);
    return lx >= x - w * 0.5f && lx < x + w * 0.5f && ly <= y && ly > y - h;
}

void RunListAction(u32 row, s32 vx, s32 vy) {
    reinterpret_cast<void (*)(u32)>(kPlaySoundFn)(kSndListRow);
    CloseList();
    switch (row) {
    case kActCopy:
        if (CaptureSel() != 0) {            // 写しが浮かび、次のタッチで動かして離した所に置く
            s_carry = Carry::CopyArmed;
            s_carryVx = vx;
            s_carryVy = vy;
            s_carryBaseX = s_carryBaseY = 0.0f;
            s_carryPixX = s_carryPixY = 0.0f;
        }
        break;
    case kActDelete:
        for (s32 y = s_sel.y0; y <= s_sel.y1; ++y)
            for (s32 x = s_sel.x0; x <= s_sel.x1; ++x)
                EraseAt(x, y);
        break;
    case kActFill:
        s_noItemTold = false;
        for (s32 y = s_sel.y0; y <= s_sel.y1; ++y)
            for (s32 x = s_sel.x0; x <= s_sel.x1; ++x)
                PlaceAt(x, y);
        break;
    default:
        break;
    }
}

// ---- 範囲選択のタッチ ----
void SelectPress(s32 vx, s32 vy, u16 px, u16 py, bool inside, s32 tx, s32 ty) {
    if (s_listOpen) {
        const s32 row = ListRowAt(px, py);
        if (row >= 0) {
            s_touchKind = TouchKind::ListTouch;
            s_listRow = row;
        } else {
            if (!InListWindow(px, py))
                CloseList();                // 窓の外を触ったら閉じる
            s_touchKind = TouchKind::Ignore;
        }
        return;
    }
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
            s_touchKind = TouchKind::Ignore;
        }
        return;
    }
    if (!inside) {
        HideName();
        ClearSel();
        s_touchKind = TouchKind::Ignore;
        return;
    }
    s_selStartTx = tx;
    s_selStartTy = ty;
    s_touchKind = InSel(tx, ty) ? TouchKind::SelPendIn : TouchKind::SelPendOut;
}

void SelectMove(s32 vx, s32 vy, u16 px, u16 py) {
    switch (s_touchKind) {
    case TouchKind::SelPendIn: {
        const float dx = (float)px - (float)s_touchStartX, dy = (float)py - (float)s_touchStartY;
        if (dx * dx + dy * dy >= kHoldSlop * kHoldSlop)
            s_touchKind = Lift(Carry::Move, vx, vy, s_touchStartX, s_touchStartY) ? TouchKind::CarryDrag : TouchKind::Ignore;
        if (s_touchKind == TouchKind::CarryDrag) {
            s_carryPixX = (float)px - (float)s_carryPressX;
            s_carryPixY = (float)py - (float)s_carryPressY;
        }
        break;
    }
    case TouchKind::SelPendOut: {
        s32 tx = -1, ty = -1;
        if (!TouchTile(vx, vy, px, py, tx, ty) || tx != s_selStartTx || ty != s_selStartTy) {
            HideName();                     // マスを出たら範囲を引く
            s_touchKind = TouchKind::SelDrag;
        }
        break;
    }
    case TouchKind::CarryDrag:
        s_carryPixX = s_carryBaseX + (float)px - (float)s_carryPressX;
        s_carryPixY = s_carryBaseY + (float)py - (float)s_carryPressY;
        break;
    default:
        break;
    }
}

void SelectRelease(s32 vx, s32 vy) {
    const u16 px = s_touchLastPx, py = s_touchLastPy;
    switch (s_touchKind) {
    case TouchKind::SelPendIn:
        OpenList(px, py);                   // 範囲の中のタップ
        break;
    case TouchKind::SelPendOut:             // 範囲の外のタップ: そのマスだけを選ぶ（アイテムなら名前も）
        s_sel.active = true;
        s_sel.x0 = s_sel.x1 = s_selStartTx;
        s_sel.y0 = s_sel.y1 = s_selStartTy;
        ShowNameAt(vx, vy, s_selStartTx, s_selStartTy);
        break;
    case TouchKind::SelDrag: {              // 範囲 = 始めたマスから離したマス（盤面の中に収める）
        float fx, fy;
        TileCoord(px, py, fx, fy);
        s32 ex = FloorI(fx), ey = FloorI(fy);
        ex = ex < 0 ? 0 : (ex >= kView ? kView - 1 : ex);
        ey = ey < 0 ? 0 : (ey >= kView ? kView - 1 : ey);
        ex += vx;
        ey += vy;
        const s32 sx = s_selStartTx, sy = s_selStartTy;    // 始めたマス（村のマス。途中で盤面を動かしても同じ）
        s_sel.active = true;
        s_sel.x0 = sx < ex ? sx : ex;
        s_sel.x1 = sx < ex ? ex : sx;
        s_sel.y0 = sy < ey ? sy : ey;
        s_sel.y1 = sy < ey ? ey : sy;
        break;
    }
    case TouchKind::CarryDrag:
        CommitCarry(vx, vy);
        break;
    case TouchKind::ListTouch:
        if (ListRowAt(px, py) == s_listRow)
            RunListAction((u32)s_listRow, vx, vy);
        break;
    default:
        break;
    }
}

// 毎フレーム: 範囲の中の長押し（6 フレーム）で持ち上げる
void SelectFrame(s32 vx, s32 vy) {
    if (s_touchKind == TouchKind::SelPendIn && s_touchPrevDown && ++s_holdFrames >= kLiftFrames) {
        if (Lift(Carry::Move, vx, vy, s_touchLastPx, s_touchLastPy))
            s_touchKind = TouchKind::CarryDrag;
        else
            s_touchKind = TouchKind::Ignore;
    }
}

// 範囲選択モードを離れた・B: 一覧・持ち上げ・範囲を順に 1 段ずつ解く
void SelectCancel(void) {
    if (s_listOpen) {
        CloseList();
        return;
    }
    if (s_carry != Carry::None) {
        CancelCarry();
        if (s_touchKind == TouchKind::CarryDrag)
            s_touchKind = TouchKind::Ignore;
        return;
    }
    ClearSel();
    HideName();
}

void SelectReset(void) {
    if (s_listOpen)
        CloseList();
    CancelCarry();
    ClearSel();
}

// ---- 枠（cip_group_00）の見た目。引いている間は指に付いて伸び、決まったら範囲のマスを囲む ----
bool UpdateGroup(s32 vx, s32 vy) {
    if (!s_groupBuilt)
        return false;
    const float ox = F(s_roomPane, kPaneGlobalX), oy = F(s_roomPane, kPaneGlobalY);
    const float half = (float)kView * 0.5f * kTile;
    float ax, ay, bx, by;
    bool markers = false;
    if (s_touchKind == TouchKind::SelDrag) {    // ゲームの CollectChip sub_B0ABF8: 始点と指の中点・大きさ |差| + 10
        ax = (float)s_touchStartX - 160.0f;
        ay = 120.0f - (float)s_touchStartY;
        bx = (float)s_touchLastPx - 160.0f;
        by = 120.0f - (float)s_touchLastPy;
        markers = true;
        const float dx = bx - ax, dy = by - ay;
        SetTranslate(s_gWin, ax + dx * 0.5f, ay + dy * 0.5f);
        F(s_gWin, 72) = (dx < 0 ? -dx : dx) + kGroupPad;
        F(s_gWin, 76) = (dy < 0 ? -dy : dy) + kGroupPad;
        SetTranslate(s_gStart, ax, ay);
        SetTranslate(s_gEnd, bx, by);
    } else if (s_sel.active) {
        // 決まった範囲: マスの四角（持ち上げ中は指と一緒に動く）
        s32 bvx = vx, bvy = vy;
        float px = 0.0f, py = 0.0f;
        if (s_carry != Carry::None && s_carry != Carry::CopyArmed) {
            bvx = s_carryVx;
            bvy = s_carryVy;
            px = s_carryPixX;
            py = s_carryPixY;
        } else if (s_carry == Carry::CopyArmed) {
            bvx = s_carryVx;
            bvy = s_carryVy;
        }
        const float left = ox - half + (float)(s_sel.x0 - bvx) * kTile + px;
        const float right = ox - half + (float)(s_sel.x1 + 1 - bvx) * kTile + px;
        const float top = oy + half - (float)(s_sel.y0 - bvy) * kTile - py;
        const float bottom = oy + half - (float)(s_sel.y1 + 1 - bvy) * kTile - py;
        SetTranslate(s_gWin, (left + right) * 0.5f, (top + bottom) * 0.5f);
        F(s_gWin, 72) = right - left + kGroupPad;
        F(s_gWin, 76) = top - bottom + kGroupPad;
    } else {
        return false;
    }
    B(s_gWin, kPaneFlagsByte) &= 0xCFu;
    B(s_gStart, kPaneFlagsByte) = (u8)((B(s_gStart, kPaneFlagsByte) & 0xFEu) | (markers ? 1u : 0u));
    B(s_gEnd, kPaneFlagsByte) = (u8)((B(s_gEnd, kPaneFlagsByte) & 0xFEu) | (markers ? 1u : 0u));
    LayoutCalc(s_group);
    return true;
}

// 持ち上げ中の行き先（上画面の UnitCursor。メニュースレッドが GridCursor へ渡す）
void PublishCursor(s32 vx, s32 vy) {
    u32 n = 0;
    if (s_carry == Carry::Move || s_carry == Carry::Copy || s_carry == Carry::CopyArmed) {
        s32 ox = 0, oy = 0;
        CarryOffset(vx, vy, ox, oy);
        for (u32 k = 0; k < s_carriedCount && n < kMaxCarry; ++k) {
            const s32 x = s_carried[k].x + ox, y = s_carried[k].y + oy;
            if (x < 0 || y < 0 || x >= kTilesX || y >= kTilesY)
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

void TouchRelease(void) {
    s_touchPrevDown = false;
    s_touchKind = TouchKind::None;
    s_touchLastX = s_touchLastY = -1;
    EndHold();
}

// 1 点。配置: 空きから始めたらなぞったマスに置く／アイテムから始めたら名前（長押しでスポイト）。削除: なぞったマスを消す
void TouchSample(s32 vx, s32 vy, bool down, u16 px, u16 py) {
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
    GridCursor::SetTint(0, 0);
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
    u8 xs[kMaxCarry], ys[kMaxCarry];
    u32 n = s_cursorCount;
    if (n > kMaxCarry)
        n = kMaxCarry;
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
        if (s_boardAnim == nullptr) {
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
    s_ghostStart = 0xFFFFFFFFu;
    CollectWants(vx, vy);
    AssignChips();
    for (u32 k = 0; k < s_drawCount; ++k)
        LayoutCalc(s_drawOrder[k]->layout);     // 吹き出しは基準のコマの大域位置を読むので先に計算する
    if (s_stage == Stage::Live) {
        if (s_mode != Mode::Select && (s_sel.active || s_carry != Carry::None || s_listOpen))
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
        if (s_listOpen)
            CloseList();
        CancelCarry();
    }
    PublishCursor(vx, vy);
    const bool group = UpdateGroup(vx, vy);
    if (s_listBuilt) {
        StepListAnim();
        LayoutCalc(s_list);
    }
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
        if (s_listOpen)
            AddLayout(mgr, s_list, 1);
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
    GuiMenu::BlockGameTouch();
    StepMove(keys);                         // メニュー表示中は keys = 0（押し続けが切れる。公共事業エディターと同じ）
    const u32 pressed = keys & ~s_prevKeys;
    s_prevKeys = keys;
    // L / R: モードを巡回（利用者の決定。範囲選択は段階 3 で足す）
    if (pressed & ((u32)Key::L | (u32)Key::R)) {
        const u32 count = kModesNow;
        const u32 now = (u32)s_mode;
        s_mode = (Mode)((pressed & (u32)Key::R) ? (now + 1) % count : (now + count - 1) % count);
        GuiMenu::Notify(Cheats::kMeOn, ModeName(s_mode));
    }
    if ((pressed & (u32)Key::B) && s_mode == Mode::Select)
        s_cancelSeq = s_cancelSeq + 1;      // 一覧・持ち上げ・範囲を 1 段ずつ解く
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
