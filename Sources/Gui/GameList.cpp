#include "GameList.hpp"
#include "FrameTrace.hpp"
#include "GameLabel.hpp"
#include "GridCursor.hpp"

#include <3ds.h>
#include <cstdio>
#include <cstring>

// ゲームのリスト UI を下画面だけ借りる（IDA-opus-5.5-F036、docs/topics/game_list.md）。
// 番地は JPN 無印の更新版。関数の意味は静的解析（逆アセンブル）と実機の欄の読み取りで照合した。

namespace GameList {

namespace {

// ---- ゲーム側 ------------------------------------------------------------------------------
const u32 kLayoutMgrPtr = 0x0096FC38;       // u32: ssys::ma::lyt::LayoutMgr
const u32 kLytAllocatorPtr = 0x0096FC3C;    // u32: ssys::ma::Allocator（+4 = sead::ExpHeap）
const u32 kMenuOpenState = 0x00949D1E;      // u8: g_MenuOpenState。1 = アイドル / 2 = 開き要求 / 3 = 開いている（旧 menu_system.md）
const u32 kMenuFlags = 0x00949D68;          // u32: vc_MENU_FLAGS。bit 0x08 = 通常の下メニューが出ている
// ---- 元の下画面 UI（地図・タブ）をゲームの手順で退場／復帰させる（IDA-opus-5.5-F036、実機で確認）----------------
//   BsMenuTab = *(BsMenuMgr+480)。命令バイト 0x949D24 を BsMenuTab_ConsumeCommand 0x6D3F04 が毎フレーム**状態に関係なく**
//   読んで 11 に戻す: 7 = 全タブ退場（→ タブ選択）、2 = 状態 18「地図同期タブ入場」（地図を作り直し → 全タブ入場 → タブ選択）。
//   地図は 0x949D25 = 0 で退場して画面ごと破棄される（sub_6D1428 → sub_6D0B54 → sub_2B3A98）。2 = アイドル。
//   実機: 7 + 0 で地図 +484 = 0・タブのマスク 0・フラグ 0x4、2 で地図が同じ番地に戻り マスク 0x81DF・フラグ 0x2。
//   ★命令は「タブ選択」で落ち着いているときだけ書く（ほかの状態でも強制的に切り替わってしまう）。
const u32 kMenuMgrPtr = 0x00949D4C;         // u32: BsMenuMgr
const u32 kMgrTab = 480;                    // BsMenuTab*
const u32 kMgrMap = 484;                    // 地図の画面（無ければ 0）
const u32 kMgrOther = 488;                  // もう 1 つの下画面（0x949D29 で出し入れ）
const u32 kTabCalc = 32;                    // 状態の calc
const u32 kTabCalcAdj = 36;
const u32 kTabShown = 6578;                 // u16: 出ているタブのマスク
const u32 kTabMoving = 6580;                // u16: 出入り中のマスク
const u32 kTabIdleCalc = 0x006D474C;        // 「タブ選択」
const u32 kTabCommand = 0x00949D24;         // u8: 11 = 何もしない
const u32 kMapCommand = 0x00949D25;         // u8: 2 = 何もしない
const u32 kOtherCommand = 0x00949D29;       // u8: 2 = 何もしない
const u8 kCmdNone = 11, kCmdAllTabsOut = 7, kCmdRestoreField = 2, kCmdMenuOut = 3, kMapIdle = 2, kMapOut = 0;
// 命令 3 = 状態 6「メニューアウト」（enter 0x6D4EE8 が MENU_FLAGS|=0x20 で下画面メニューへ閉じる通知 → 状態 18 で地図とタブを戻す）。
//   持ち物などの下画面メニューを開いたままエディターを起動したときに使う（利用者報告: メニューが退場せずリストも出ない）。
const u32 kMenuShownFlag = 0x10;            // MENU_FLAGS: 通常の下メニューの入場完了（BsMenuItem_OpenUpdate）
const u32 kSndListActive = 0x010003B0;     // SE_SYS_SCROLL_LIST_ACTIVE（行ボタンのフォーカス音。InstSetupRows が +208 に置く）
const u32 kPlaySoundFn = 0x0058C7D4;       // Game_PlaySound(id)
const u32 kRoomIdFn = 0x002F75CC;           // Room_GetCurrentId
// ---- 十字キーで一覧を動かす -----------------------------------------------------------------------
//   ★ゲームの一覧の十字操作は「手カーソル」（BsMenuMgr+500 の BsHandCursor）を動かし、その下の行にフォーカスして
//   A で決定する方式（ButtonActionControl vt[8] 0x2F62C8。一覧は +208=3 なので vt[7] の行送りは使わない）。
//   手カーソルはゲームの計算フェーズで自分で入力を読むので、入力を止めたまま描画フェーズから入れても届かない
//   （最初の版は BsMenuMgr の ControllerWrapper へ差し込んだが効かなかった。しかもビットの並びも違っていた:
//    写し側ビット i ← 物理側ビット 表[i]、十字は写し 0x400/0x800/0x1000/0x2000）。
//   そこで十字は自分で読み、一覧自身の関数で動かす: 選択の見た目 = vt[16]、スクロール = sub_2987A8。
//   上下 = 1 行、左右 = 8 行。押し続けると連続。スライドパッドは使わない。
const u32 kDpadUp = 1u, kDpadDown = 2u, kDpadLeft = 4u, kDpadRight = 8u;
// ★押し下げと押し続けの判定はメニューのスレッドで実時間で行う（利用者報告 2026-09-25: 単押しの連打がとても遅い・
//   長押しを離してもすぐ止まらない）。ゲームのフレームで数えていたので、プレビューのモデルを読む間（ゲームのフレームが
//   止まる）押下を取りこぼし、離しても次のフレームまで送りが続いた。移動量は溜めておき、ゲームのスレッドが
//   次のフレームでまとめて反映する（行の送り = s_dpadRowsSent、ページ送り = s_dpadPagesSent。書く側は 1 つだけ）。
const u32 kRepeatDelayMs = 400, kRepeatEveryMs = 100;  // 以前の 12 / 3 フレーム（30fps）と同じ長さ

typedef void *(*HeapAllocFn)(u32 size, void *heap, s32 align);
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
typedef int (*SetupStepFn)(void *list, void *holder, void *heap);
typedef void (*ScrollBindFn)(void *scroll, void *pane);
typedef void (*ScrollToFn)(void *scroll, s32 top);
typedef void (*ListFn)(void *list);
typedef void (*SetSelectFn)(void *list, s32 now, s32 before);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef void *(*FontSlotFn)(void *fontMgr, u32 kind);
typedef void (*RegisterFontFn)(void *accessor, const char *name, void *font);
typedef int (*RegisterTexFn)(void *holder);

const HeapAllocFn    HeapAlloc      = reinterpret_cast<HeapAllocFn>(0x002FD0CC);    // operator new(size, heap, align)
const CtorFn         InstSelectCtor = reinterpret_cast<CtorFn>(0x007C6EB0);         // InstSelect<8>（4,812 B）
const CtorFn         ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);         // ArcResAccReader（584 B）
const CtorFn         ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const ArcLoadStepFn  ArcLoadStep    = reinterpret_cast<ArcLoadStepFn>(0x00567244);  // 終わるまで 0
const CtorFn         LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);         // ssys::ma::lyt::Layout（332 B）
const CtorFn         LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutBuildFn  LayoutBuild    = reinterpret_cast<LayoutBuildFn>(0x005685A4);
const LayoutFn       LayoutFinalize = reinterpret_cast<LayoutFn>(0x00133A5C);
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
const SetupStepFn    SetupStep      = reinterpret_cast<SetupStepFn>(0x001C513C);    // 1 で完了
const ScrollBindFn   ScrollBindPane = reinterpret_cast<ScrollBindFn>(0x00298018);
const ScrollToFn     ScrollTo       = reinterpret_cast<ScrollToFn>(0x002987A8);
const ListFn         StartEnter     = reinterpret_cast<ListFn>(0x001C58F4);         // 状態「入場処理」へ
const ListFn         StartLeave     = reinterpret_cast<ListFn>(0x001C5A34);         // 状態「退場処理」へ
const AddLayoutFn    AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);
// ★書体とテクスチャの登録（BsMenuCatalog_Init の case 0 と同じ）。無いと行の TextBox の書体（+0xE0）が 0 のままで、
//   文字箱を結ぶ vc_ACMESSAGEBOX_FUNC1 0x5E20A8 が SIGSEGV（実機 2026-09-25）。
const u32            kFontMgrPtr    = 0x0094C9C8;                                    // u32: font::Mgr
const FontSlotFn     FontName       = reinterpret_cast<FontSlotFn>(0x00747528);     // 名前（SafeString。+4 = char*）
const FontSlotFn     FontGet        = reinterpret_cast<FontSlotFn>(0x0052D6A8);     // 読み込み済みなら ResFont、まだなら 0
const RegisterFontFn RegisterFont   = reinterpret_cast<RegisterFontFn>(0x004B3FD4); // (holder+12, 名前, 書体)
const RegisterTexFn  RegisterTex    = reinterpret_cast<RegisterTexFn>(0x00568CFC);  // arc の .bclim を全部
const u32            kHolderAccessor = 12;
// ゲームのメッセージから文字列を引く（vc_NPC_2_SETUP 0x75B8A8 の中身）。WordRes = {0x904954, UTF-16*, 0x3FFFFFFF}。
//   成功で 1、+4 はメッセージデータの中の 0 終端 UTF-16（失敗なら空文字）。ロックを取るのでゲームのスレッドで呼ぶ。
// ★名前はゲームの一覧と同じく script::WordFix<38>（100 B、ctor 0x56CE48）に vc_NPC_2_SETUP 0x75B8A8 で入れ、
//   その WordFix を行の語にする（PWorkList vt[13] 0x6EDEE0 → sub_56CDEC と同じ）。以前は WordRes の文字列を
//   0 で打ち切って写しており、メッセージの制御タグ（0x000E …、引数に 0 を含む）で切れて「モダンな」だけ・空欄になった。
typedef int (*MsgSetupFn)(u32 msgData, void *word, const char *label, u32 index);
const MsgSetupFn     MsgSetup       = reinterpret_cast<MsgSetupFn>(0x0075B8A8);     // 成功で真
const CtorFn         WordFixCtor    = reinterpret_cast<CtorFn>(0x0056CE48);         // script::WordFix<38>
const u32            kWordFixBytes  = 100;
const u32            kMsgDataPtr    = 0x00957ED4;                                    // u32: vc_DATAPOINTER

const u32 kInstSelectVtbl = 0x008E54B4;     // InstSelect<8> の vtable（26 語）
const u32 kVtblWords = 26;
const u32 kWordPtrVtbl = 0x0090491C;        // script::WordPtr（vt[3] = +4 を返す）
const u32 kInstSelectBytes = 4812;

// 一覧（SelectBase / InstSelect<8>）の欄
const u32 kListLayout = 44;                 // ssys::ma::lyt::Layout
const u32 kListAnimIn = 376;                // UiAnim
const u32 kListAnimOut = 416;
const u32 kListScroll = 548;
const u32 kListTop = 2368;                  // 先頭の行番号（スクロールの +1820）
const u32 kListCount = 2500;
const u32 kListTextCap = 2504;              // 文字箱の容量
const u32 kListAnimating = 2512;            // u8
const u32 kListInputLock = 2513;            // u8: 1 = ButtonActionControl の入力を止める
const u32 kListSelected = 2792;             // s32: -1 = なし
const u32 kListRows = 8;                    // 行の部品の数（見える行数ではない）
const u32 kScrollMaxTop = 1824;             // スクロール部品の欄: 先頭にできる最大の行番号
//   = 件数 - (+1868) + 1（sub_299910）。組み立ては +1868 に 行数-1 = 7 を渡すので、見える行は 6。
//   残りの 2 行はスクロールの途中を埋める余分（利用者報告「十字で下へ行くと 2 つ分はみ出す」）。
// Layout / UiAnim の欄
const u32 kLayoutPriority = 12;             // u8: 下画面の並び（枠と中身は 2）
const u32 kLayoutHolder = 236;              // ArcResAccReader*
const u32 kAnimTotal = 4;                   // float
const u32 kAnimCur = 8;                     // float
const u32 kAnimBound = 28;                  // u8

const char kArcPath[] = "Layout/catalog/catalogue.arc";
const u32 kFrameCmdBytes = 0x8000;          // BsMenuCatalog と同じ
// 枠の種類のアニメ（ctlg_list_kind）のフレーム。表 0x848B18[BsMenuCatalog+15264]。公共事業は 6 → 2.0（実機）。
const float kKindFrame = 2.0f;
const u32 kTeardownWaitFrames = 3;          // 描画登録をやめてから壊すまで（GPU がまだ読んでいるかもしれない）

// ---- 持ち物 --------------------------------------------------------------------------------
struct WordPtr { u32 vtbl; const u16 *text; u32 cap; };

alignas(8) u8 s_holder[584];
alignas(8) u8 s_frame[332];
alignas(8) u8 s_frameIn[40];
alignas(8) u8 s_frameOut[40];
alignas(8) u8 s_frameKind[40];
u32 s_vtblStore[1 + kVtblWords];           // [0] = TypeInfo（ゲームの vtable の 1 語手前と同じ）
u32 *const s_vtbl = s_vtblStore + 1;
u8 *s_list;                                 // ゲームのヒープ

// ---- 行の前置き（名前の左に小さく出す文字。例: アイテム ID「0x5C」。利用者指示 2026-09-25）----------------
// ★名前とは別の TextBox に描く（利用者指示: 同じ文字列にしない）。一覧の Layout（ctlg_cntnt_00）の中に置くので、
//   名前と同じ切り抜きがかかる（7 行目が半分隠れるのと同じ）。
//   切り抜き: 一覧は N_frame_00 の位置と大きさから Layout の切り抜き矩形（Layout+312.. = 一覧+356..）を作る
//   （sub_1C55F8。組み立て段 2）。名前の左の空きはこの矩形の外なので、そのままでは描いても見えない（利用者報告:
//   「リストの上にシェイプが描かれて隠れている」ように見えた。実際は切り抜き。IDA-opus-5.5-F039）。
//   作り方: 一覧の Layout をゲームより先に自前で組み立てる（ssys_ma_lyt_Layout_Build 0x5685A4 と同じ手順。
//   組み立て段 0 は Layout+32 が 0 でなければ組み立てを飛ばす）。そのとき arc の ctlg_cntnt_00.bclyt を写して
//     - 各行の T_itm_XX（txt1）の直後に、同じ物を名前 T_id_XX・位置・幅・文字の大きさだけ変えて足す（N_list_XX の子）
//     - N_frame_00（pan1）を左へ kClipGrow 広げる（右端は同じ。切り抜きが ID の場所まで届く）
//   にしたデータを nw::lyt::Layout::Build（sub_4B9474）へ渡す。マテリアルは T_itm_XX と同じ番号を使う（ペインごとに
//   別の Material ができる）。選択中の色（アニメ ctlg_cntnt_00_select = マテリアル T_itm_XX の色 CLMC）は
//   毎フレーム T_itm_XX の Material の色 7 個（+0x10..）を T_id_XX へ写して合わせる。
const u32 kPrefixChars = 6;
const float kIdScale = 0.65f;               // 名前の文字（14.4 x 19.2）に対する大きさ
// ID の欄は「枠の内側の左端 〜 名前の左端」いっぱいにして中央寄せ（利用者 2026-09-25: 項目側に寄りすぎ → 空きの真ん中に）。
//   Chokistream の下画面（work/evidence/screens/gamelist_20260925/list_id_v1.png、tools/dynamic/png_pixels.py で測定）:
//   枠の内側の左端 = 下画面 x 29、名前の字の左端 = x 76 → N_list_XX から -47 〜 0。
const float kIdRight = 0.0f;                // ID の欄の右端（N_list_XX から。名前の左端 = 0）
const float kIdWidth = 47.0f;
const u8 kIdTextPosition = 1 + 1 * 3;       // 横中央・縦中央（横 + 縦×3）
const float kClipGrow = 26.0f;              // 切り抜きを左へ広げる量（左端 62.5 → 36.5。枠の内側 ≒ 35）
const u32 kLytBytes = 12288;                // 写した bclyt の置き場（元は 5,780 B + 8 × 116 B）
const u32 kRows = 8;
const u32 kPaneTranslateX = 40, kPaneTranslateY = 44, kPaneScaleX = 64, kPaneScaleY = 68, kPaneFlagsByte = 183;
const u32 kTextColorTop = 216, kTextColorBottom = 220, kTextMaterial = 256, kTextDirty = 254, kTextDraw = 260;
const u32 kMatColors = 0x10, kMatColorBytes = 28, kMatFlags = 0x4D;
const u32 kTextBoxAllocSlot = 28;           // TextBox vt[28] 0x13B9A8 = 器の確保 (箱, 文字数, 旗)
typedef void (*AllocBufFn)(void *textBox, u32 chars, u32 flags);
typedef void (*SetStringFn)(void *textBox, const u16 *str, u32 start, u32 len);
const SetStringFn SetString = reinterpret_cast<SetStringFn>(0x004BACBC);    // nwlyt_TextBox_SetString
// Layout を組み立てる（0x5685A4 の中身）
typedef void *(*GetResourceFn)(void *accessor, u32 type, const char *name, u32 *size);
typedef void (*GetPropertyFn)(u32 id, u32 *out);
typedef void (*GenCmdlistsFn)(u32 count, void *out);
typedef void (*SelectListFn)(u32 list);
typedef void (*CmdlistStorageFn)(u32 bytes, u32 count);
typedef int (*NwLayoutBuildFn)(void *nwLayout, const void *data, void *accessor);
const GetPropertyFn    GetProperty    = reinterpret_cast<GetPropertyFn>(0x00127EDC);
const GenCmdlistsFn    GenCmdlists    = reinterpret_cast<GenCmdlistsFn>(0x00121B34);
const SelectListFn     SelectList     = reinterpret_cast<SelectListFn>(0x001216BC);
const CmdlistStorageFn CmdlistStorage = reinterpret_cast<CmdlistStorageFn>(0x00121988);
const NwLayoutBuildFn  NwLayoutBuild  = reinterpret_cast<NwLayoutBuildFn>(0x004B9474);
const u32 kResBlyt = 0x626C7974;            // 'blyt'（0x568698）
const u32 kPropCmdlist = 519;               // 0x56869C
const u32 kListCmdBytes = 0xB000;           // 組み立て段 0 が渡す大きさ（45,056）
const u32 kLytBuilt = 32, kLytCmdlist = 256, kLytCmdlistZero = 276, kLytCmdBytes = 280, kLytCmdFlag = 284, kLytNw = 16;

u16 s_text[kMaxItems][kMaxChars + 1];
alignas(4) u8 s_lyt[kLytBytes];             // 写して書き換えた ctlg_cntnt_00.bclyt（一覧がある間は Layout が参照する）
struct IdRow { void *box; void *name; float baseX, baseY, nameX, nameY; s32 shown; };
IdRow s_idRows[kRows];
bool s_idReady;                             // T_id_XX を見つけて器を取った
WordPtr s_words[kMaxItems];
alignas(8) u8 s_fix[kMaxItems][kWordFixBytes];     // ゲームの名前を入れた WordFix<38>
bool s_useFix[kMaxItems];
u32 s_count;

// プラグイン側の要求（メニューのスレッドが書き、FrameStep が読む）
u16 s_pendText[kMaxItems][kMaxChars + 1];
u16 s_pendPrefix[kMaxItems][kPrefixChars + 1];
volatile bool s_pendHasPrefix;
bool s_hasPrefix;                           // 組み立てた一覧が前置き（T_id_XX）を持つ
u16 s_prefix[kMaxItems][kPrefixChars + 1];  // 組み立てたときの前置き
volatile u32 s_pendCount;
const char *volatile s_pendLabel;           // メッセージのラベル（nullptr = 使わない）
s16 s_pendMsg[kMaxItems];                   // 行ごとの番号（負 = 使わない）
volatile u32 s_itemsSeq;                    // SetItems のたびに増える
u32 s_builtSeq;
volatile bool s_want;
volatile s32 s_wantSelect = -1;
volatile u32 s_selectSeq;
u32 s_selectDone;
volatile s32 s_decided = -1;
volatile bool s_shutdown;
const char *volatile s_error = "";

enum class Stage : u8 { Off, Loading, Building, Live, Waiting };
volatile Stage s_stage = Stage::Off;
bool s_holderMade;
bool s_frameMade;       // Layout を ctor 済み
bool s_frameBuilt;      // LayoutBuild 済み
bool s_animsMade;
bool s_listSetup;       // SetupStep が 1 を返した
enum class Dir : u8 { None, In, Out };
enum class Field : u8 { Shown, Exiting, Hidden, Restoring };
volatile Field s_field = Field::Shown;
u32 s_fieldFrames;          // 今の段に入ってからのフレーム数
u32 s_fieldRoom;            // 退場させたときの部屋
bool s_menuCloseSent;      // 開いていた下画面メニューに閉じる命令を出した
u32 s_dpadPrev;             // メニューのスレッド: 前回の kDpad* のビット
u32 s_dpadNextMs;           // メニューのスレッド: 次に送る時刻
volatile s32 s_dpadRowsSent;    // メニューのスレッドだけが書く（通算）
volatile s32 s_dpadPagesSent;
s32 s_dpadRowsDone;             // ゲームのスレッドだけが書く（反映した通算）
s32 s_dpadPagesDone;
Dir s_frameDir = Dir::None;
Dir s_listDir = Dir::None;
bool s_hookReady;
u32 s_waitFrames;
s32 s_lastSelected = -1;

inline u8 *P(void *p, u32 off) { return reinterpret_cast<u8 *>(p) + off; }
inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(P(p, off)); }
inline s32 &S(void *p, u32 off) { return *reinterpret_cast<s32 *>(P(p, off)); }
inline float &F(void *p, u32 off) { return *reinterpret_cast<float *>(P(p, off)); }
inline u8 &B(void *p, u32 off) { return *P(p, off); }
inline u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }
inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline void W8(u32 a, u8 v) { *reinterpret_cast<volatile u8 *>(a) = v; }

// ---- 自前の仮想関数（ゲームが BLX で呼ぶ）-----------------------------------------------------
// vt[13]: 一覧を作る。件数を +2500 に置いて 1 を返す（0 だと組み立てが次の段へ進まない）。
int ListBuildItems(u8 *self) {
    S(self, kListCount) = (s32)s_count;
    return 1;
}

// vt[25]: i 行目の「語」。vc_ACMESSAGEBOX_FUNC2 がその vt[3] の UTF-16 を流す。
void *ListWordAt(u8 *self, s32 index) {
    (void)self;
    if (index < 0 || (u32)index >= s_count)
        index = 0;
    return s_useFix[index] ? static_cast<void *>(s_fix[index]) : static_cast<void *>(&s_words[index]);
}

// ---- 行の前置き（上の説明）--------------------------------------------------------------------
inline u16 RdU16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 RdU32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
inline void WrU32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
inline float &SecF(u8 *sec, u32 off) { return *reinterpret_cast<float *>(sec + off); }

// bclyt の節（txt1 / pan1）: +12 名前 16 B、+36 位置 x,y,z、+48 回転、+60 拡大、+68 幅、+72 高さ。
//   txt1: +84 文字の配置（横 + 縦×3）、+100 文字の大きさ x,y（tools/layout/lyt_dump.py と同じ読み方）
const u32 kSecName = 12, kSecX = 36, kSecW = 68, kTxtPosition = 84, kTxtFontX = 100, kTxtFontY = 104;

bool SecNameIs(const u8 *sec, const char *name) {
    return std::strncmp(reinterpret_cast<const char *>(sec + kSecName), name, 16) == 0;
}

// 元の bclyt から T_id_XX を足した物を s_lyt に作る。戻り値: 大きさ（失敗で 0）
u32 MakeIdLayout(const u8 *src) {
    if (src == nullptr || std::memcmp(src, "CLYT", 4) != 0)
        return 0;
    const u32 headerLen = RdU16(src + 6);
    const u32 total = RdU32(src + 12);
    const u32 sections = RdU32(src + 16);
    if (headerLen < 20 || total > kLytBytes || headerLen > total)
        return 0;
    std::memcpy(s_lyt, src, headerLen);
    u32 in = headerLen, out = headerLen, added = 0;
    for (u32 k = 0; k < sections; ++k) {
        if (in + 8 > total)
            return 0;
        const u32 size = RdU32(src + in + 4);
        if (size < 8 || in + size > total || out + size > kLytBytes)
            return 0;
        u8 *sec = s_lyt + out;
        std::memcpy(sec, src + in, size);
        out += size;
        if (std::memcmp(sec, "pan1", 4) == 0 && size >= 76 && SecNameIs(sec, "N_frame_00")) {
            SecF(sec, kSecX) -= kClipGrow * 0.5f;       // 基準は中央（origin 0x4）。右端を変えずに左へ広げる
            SecF(sec, kSecW) += kClipGrow;
        }
        if (std::memcmp(sec, "txt1", 4) == 0 && size >= 108
            && std::strncmp(reinterpret_cast<const char *>(sec + kSecName), "T_itm_", 6) == 0) {
            if (out + size > kLytBytes)
                return 0;
            u8 *id = s_lyt + out;
            std::memcpy(id, sec, size);
            out += size;
            char name[16] = {};
            std::snprintf(name, sizeof(name), "T_id_%.2s", reinterpret_cast<const char *>(sec + kSecName) + 6);
            std::memcpy(id + kSecName, name, 16);
            SecF(id, kSecX) = kIdRight - kIdWidth;      // 基準は左端・上下中央（origin 0x3。T_itm と同じ）
            SecF(id, kSecW) = kIdWidth;
            id[kTxtPosition] = kIdTextPosition;
            SecF(id, kTxtFontX) *= kIdScale;
            SecF(id, kTxtFontY) *= kIdScale;
            ++added;
        }
        in += size;
    }
    if (added != kRows)
        return 0;
    WrU32(s_lyt + 12, out);
    WrU32(s_lyt + 16, sections + added);
    return out;
}

// 一覧の Layout（一覧+44）を ssys_ma_lyt_Layout_Build 0x5685A4 と同じ手順で組み立てる。データだけ s_lyt に差し替える
bool PrebuildListLayout(void) {
    u8 *layout = P(s_list, kListLayout);
    if (W(layout, kLytBuilt) != 0)
        return false;
    void *accessor = reinterpret_cast<void *>(W(s_holder, 4));
    if (accessor == nullptr)
        return false;
    const u32 *avt = *reinterpret_cast<u32 *const *>(accessor);
    const u8 *src = reinterpret_cast<const u8 *>(
        reinterpret_cast<GetResourceFn>(avt[2])(accessor, kResBlyt, "ctlg_cntnt_00.bclyt", nullptr));
    if (MakeIdLayout(src) == 0)
        return false;
    W(layout, kLytCmdlistZero) = 0;
    B(layout, kLytCmdFlag) = 1;
    u32 current = 0;
    GetProperty(kPropCmdlist, &current);
    GenCmdlists(1, P(layout, kLytCmdlist));
    SelectList(W(layout, kLytCmdlist));
    W(layout, kLytCmdBytes) = kListCmdBytes;
    CmdlistStorage(kListCmdBytes, 1);
    SelectList(current);
    if (NwLayoutBuild(P(layout, kLytNw), s_lyt, accessor) == 0)
        return false;
    const u32 *lvt = *reinterpret_cast<u32 *const *>(layout);
    reinterpret_cast<void (*)(void *)>(lvt[3])(layout);
    return true;
}

// 組み立てが終わったあと: T_id_XX に文字の器を取り、行の名前の欄と組にする
void SetupIdRows(void) {
    void *layout = P(s_list, kListLayout);
    char name[16];
    for (u32 i = 0; i < kRows; ++i) {
        IdRow &r = s_idRows[i];
        std::snprintf(name, sizeof(name), "T_id_%02u", (unsigned)i);
        r.box = FindPane(layout, name);
        std::snprintf(name, sizeof(name), "T_itm_%02u", (unsigned)i);
        r.name = FindPane(layout, name);
        if (r.box == nullptr || r.name == nullptr) {
            s_idReady = false;
            return;
        }
        const u32 draw = W(r.box, kTextDraw);
        const u32 flags = draw != 0 ? *reinterpret_cast<const u8 *>(draw + 9) : 0;
        u32 *vt = *reinterpret_cast<u32 **>(r.box);
        reinterpret_cast<AllocBufFn>(vt[kTextBoxAllocSlot])(r.box, kPrefixChars, flags);
        r.baseX = F(r.box, kPaneTranslateX);
        r.baseY = F(r.box, kPaneTranslateY);
        r.nameX = F(r.name, kPaneTranslateX);
        r.nameY = F(r.name, kPaneTranslateY);
        r.shown = -1;
    }
    s_idReady = true;
}

// 毎フレーム: 行 i に出ている項目の ID を書き、名前の欄の色・動き（選択・タッチのアニメ）を写す
void StepIdRows(void) {
    if (!s_idReady || s_list == nullptr)
        return;
    const s32 top = S(s_list, kListTop);
    for (u32 i = 0; i < kRows; ++i) {
        IdRow &r = s_idRows[i];
        const s32 index = top + (s32)i;
        if (index != r.shown) {
            r.shown = index;
            const bool valid = index >= 0 && (u32)index < s_count;
            const u16 *text = valid ? s_prefix[index] : s_prefix[0];
            u32 len = 0;
            if (valid)
                while (len < kPrefixChars && text[len] != 0)
                    ++len;
            SetString(r.box, text, 0, len);
        }
        // 色: 文字色 2 つと Material の色 7 個
        bool dirty = false;
        for (u32 k = 0; k < 4; ++k) {
            if (B(r.box, kTextColorTop + k) != B(r.name, kTextColorTop + k)
                || B(r.box, kTextColorBottom + k) != B(r.name, kTextColorBottom + k)) {
                B(r.box, kTextColorTop + k) = B(r.name, kTextColorTop + k);
                B(r.box, kTextColorBottom + k) = B(r.name, kTextColorBottom + k);
                dirty = true;
            }
        }
        if (dirty)
            B(r.box, kTextDirty) |= 1u;
        const u32 idMat = W(r.box, kTextMaterial), nameMat = W(r.name, kTextMaterial);
        if (idMat != 0 && nameMat != 0
            && std::memcmp(reinterpret_cast<void *>(idMat + kMatColors), reinterpret_cast<void *>(nameMat + kMatColors),
                           kMatColorBytes) != 0) {
            std::memcpy(reinterpret_cast<void *>(idMat + kMatColors), reinterpret_cast<void *>(nameMat + kMatColors),
                        kMatColorBytes);
            *reinterpret_cast<u8 *>(idMat + kMatFlags) &= ~4u;     // GPU へ送り直させる
        }
        // 動き（タッチのアニメは T_itm_XX の位置・拡大を動かす）
        const float x = r.baseX + (F(r.name, kPaneTranslateX) - r.nameX);
        const float y = r.baseY + (F(r.name, kPaneTranslateY) - r.nameY);
        if (x != F(r.box, kPaneTranslateX) || y != F(r.box, kPaneTranslateY)
            || F(r.box, kPaneScaleX) != F(r.name, kPaneScaleX) || F(r.box, kPaneScaleY) != F(r.name, kPaneScaleY)) {
            F(r.box, kPaneTranslateX) = x;
            F(r.box, kPaneTranslateY) = y;
            F(r.box, kPaneScaleX) = F(r.name, kPaneScaleX);
            F(r.box, kPaneScaleY) = F(r.name, kPaneScaleY);
            B(r.box, kPaneFlagsByte) &= 0xCFu;
        }
    }
}

// ---- UTF-8 → UTF-16（BMP だけ。範囲外は '?'）--------------------------------------------------
void Utf8To16(const char *src, u16 *dst, u32 cap) {
    u32 n = 0;
    const u8 *s = reinterpret_cast<const u8 *>(src != nullptr ? src : "");
    while (*s != 0 && n < cap) {
        u32 c = *s++;
        if (c >= 0xF0) {                    // 4 バイト（BMP 外）
            for (u32 k = 0; k < 3 && (*s & 0xC0) == 0x80; ++k)
                ++s;
            c = '?';
        } else if (c >= 0xE0) {
            u32 c1 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            u32 c2 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            c = ((c & 0x0F) << 12) | (c1 << 6) | c2;
        } else if (c >= 0xC0) {
            u32 c1 = (*s & 0xC0) == 0x80 ? *s++ & 0x3F : 0;
            c = ((c & 0x1F) << 6) | c1;
        } else if (c >= 0x80) {
            c = '?';
        }
        dst[n++] = (u16)c;
    }
    dst[n] = 0;
}

// ---- アニメ（枠と中身で同じ扱い）-------------------------------------------------------------
// 今の位置の割合 [0,1]。登場の途中で退場へ切り替えるとき、見た目が飛ばないように逆側の位置へ写す。
float Progress(void *anim) {
    const float total = F(anim, kAnimTotal) - 1.0f;
    if (total <= 0.0f)
        return 1.0f;
    float p = F(anim, kAnimCur) / total;
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

// from（結合中なら）→ to へ。to のフレーム = (1 - from の進み) * (総 - 1)。from が結合されていなければ 0 から。
void SwitchAnim(void *layout, void *from, void *to) {
    float start = 0.0f;
    if (B(from, kAnimBound) != 0) {
        start = (1.0f - Progress(from)) * (F(to, kAnimTotal) - 1.0f);
        AnimUnbind(layout, from);
    }
    if (B(to, kAnimBound) == 0)
        AnimBind(layout, to);
    AnimSetFrame(to, start);
}

// ---- 片付け ---------------------------------------------------------------------------------
void DestroyAll(void) {
    FrameTrace::Mark(FrameTrace::ListDestroy, reinterpret_cast<u32>(s_list));
    s_idReady = false;
    if (s_list != nullptr) {
        if (s_listSetup) {
            u32 *vt = *reinterpret_cast<u32 **>(s_list);
            reinterpret_cast<ListFn>(vt[3])(s_list);    // SelectBase_Finalize
        }
        // ★組み立て途中の一覧は vt[3] が 0 の欄を辿って落ちる（+540 の文字箱）。
        //   組み立ては完了まで回してから片付ける（FrameStep がそうする）ので、ここへは来ない。
        u32 *vt = *reinterpret_cast<u32 **>(s_list);
        reinterpret_cast<ListFn>(vt[1])(s_list);        // delete 付きデストラクタ（ゲームのヒープへ返す）
        s_list = nullptr;
    }
    s_listSetup = false;
    if (s_frameMade) {
        if (s_frameBuilt)
            LayoutFinalize(s_frame);
        LayoutDtor(s_frame);
    }
    s_frameMade = s_frameBuilt = false;
    if (s_animsMade) {
        AnimDtor(s_frameIn);
        AnimDtor(s_frameOut);
        AnimDtor(s_frameKind);
    }
    s_animsMade = false;
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = false;
    s_frameDir = s_listDir = Dir::None;
    s_stage = Stage::Off;
}

// ★0 ではなく 1 がアイドル。村の屋外で 1 / フラグ 0x2（実機）。以前は「0 以外 = 開いている」と取り違え、一度も組み立てなかった。

u32 RoomId(void) {
    return reinterpret_cast<u32 (*)(void)>(kRoomIdFn)();
}

bool MenuOpen(void);

// タブが「タブ選択」で、命令バイトがどれも消費済み
bool FieldIdle(void) {
    const u32 mgr = R32(kMenuMgrPtr);
    const u32 tab = mgr != 0 ? R32(mgr + kMgrTab) : 0;
    if (tab == 0)
        return false;
    return R32(tab + kTabCalc) == kTabIdleCalc && R32(tab + kTabCalcAdj) == 0
        && *reinterpret_cast<const volatile u16 *>(tab + kTabMoving) == 0
        && R8(kTabCommand) == kCmdNone && R8(kMapCommand) == kMapIdle;
}

// ---- 開いていた下画面メニューを閉じさせる（ゲームの状態 6「メニューアウト」と 7 の後始末を、地図とタブの出し直しを抜いて行う）----
//   状態 6 enter 0x6D4EE8: 選択中のタブボタンを外す（+217 = 1、vt+28(ボタン, 0) = sub_2E016C）、+6427 = 10、
//     手カーソルを隠す、MENU_FLAGS |= 0x20（下メニューへ閉じる通知）、+6576 |= 1、+6422 = 0。
//   状態 6 calc 0x6D4E5C: 閉じるボタンの退場（sub_6D5110）とメニューが消える（sub_6D1074）のを待ち、byte_949D88 = 0、
//     sub_693EE8(dword_949718, 10) → 状態 18（地図を作り直す）。
//   状態 7 calc 0x6D67F0: +4493 = +4881 = 1、音の再開、MENU_FLAGS の 0x20 を消す、+6422 = 1、+6427 = -1 → タブ選択。
//   ★持ち物を X で開いたときは +6427 が -1 のままで、状態 6 がタブボタンを外さず、次に開くと「閉じる音」
//   （SE_SYS_MAIN_TAB_SELECTED_OFF）が鳴った（利用者報告。実機の差分: タブボタン 0 の +204 が 0x10003F3、+384/+385 = 1）。
//   → 選択されたまま（+384 が 0 でない）のボタンは番号に関係なく外す。
const u32 kTabButtons = 1948, kTabButtonStride = 388, kTabButtonCount = 11;
const u32 kTabButtonSelected = 384, kTabButtonDirty = 217, kTabButtonSound = 204, kTabButtonSound218 = 218;
const u32 kSndTabSelectedOn = 0x010003F2;   // SE_SYS_MAIN_TAB_SELECTED_ON（開いていないタブを押したときの音）
const u32 kTabSelectedIndex = 6427, kTabFlags6576 = 6576, kTabActive6422 = 6422, kTab4493 = 4493, kTab4881 = 4881;
const u32 kTabStateMachine = 20;
const u32 kTabWaitCalc = 0x006D4444;        // 状態 0「待機」の calc（何もしない）
const u32 kChangeStateFn = 0x0081B41C;      // BsMenuTab の状態切替 (sm, calc, 0)。enter をその場で呼ぶ
const u32 kInvMenuPtr = 0x00986500;         // vc_INVMENU（0 = 持ち物などの画面が無い）
const u32 kMenuBgPtr = 0x00949718, kMenuBgFn = 0x00693EE8;
const u32 kMenuFlagByte88 = 0x00949D88;
const u32 kSoundStateFn = 0x00316E7C, kSoundResumeA = 0x0052B678, kSoundResumeB = 0x0052B644;
const u32 kMgrCursorOn = 64, kMgrHand = 500, kHandHideFn = 0x001FA86C;

void ChangeTabState(u32 tab, u32 calc) {
    reinterpret_cast<void (*)(u32, u32, u32)>(kChangeStateFn)(tab + kTabStateMachine, calc, 0);
}

// ゲームがタブボタンの選択を外す手順（BsMenuTab_TabSelectUpdate 0x6D4980〜0x6D49C8）の写し:
//   +204 = 押したときの音 SELECTED_ON、+218 = 1、+384 = +385 = 0（選択中）、vt+28(ボタン, 0) で見た目を +384 に合わせる。
//   ★+217 と vt+28 だけ（状態 6 enter の形）では +384 が 1 のまま残り、次に開くと閉じる音が鳴った（実機 2026-09-25）。
void DeselectTabButton(u32 tab, u32 index) {
    const u32 b = tab + kTabButtons + kTabButtonStride * index;
    *reinterpret_cast<volatile u8 *>(b + kTabButtonDirty) = 1;
    *reinterpret_cast<volatile u32 *>(b + kTabButtonSound) = kSndTabSelectedOn;
    *reinterpret_cast<volatile u8 *>(b + kTabButtonSound218) = 1;
    *reinterpret_cast<volatile u8 *>(b + kTabButtonSelected) = 0;
    *reinterpret_cast<volatile u8 *>(b + kTabButtonSelected + 1) = 0;
    const u32 vt = R32(b);
    reinterpret_cast<void (*)(u32, u32)>(R32(vt + 28))(b, 0);
}

void BeginMenuClose(u32 mgr, u32 tab) {
    FrameTrace::Mark(FrameTrace::ListFieldCmd, 0x100 | kCmdMenuOut);
    const s8 sel = (s8)R8(tab + kTabSelectedIndex);
    for (u32 i = 0; i < kTabButtonCount; ++i) {
        const u32 b = tab + kTabButtons + kTabButtonStride * i;
        if ((s32)i == sel || R8(b + kTabButtonSelected) != 0)
            DeselectTabButton(tab, i);
    }
    W8(tab + kTabSelectedIndex, 10);
    if (R8(mgr + kMgrCursorOn) == 1) {
        const u32 hand = R32(mgr + kMgrHand);
        if (hand != 0)
            reinterpret_cast<void (*)(u32)>(kHandHideFn)(hand);
        W8(mgr + kMgrCursorOn, 0);
    }
    *reinterpret_cast<volatile u32 *>(kMenuFlags) = R32(kMenuFlags) | 0x20u;
    *reinterpret_cast<volatile u16 *>(tab + kTabFlags6576) = (u16)(*reinterpret_cast<volatile u16 *>(tab + kTabFlags6576) | 1u);
    W8(tab + kTabActive6422, 0);
    ChangeTabState(tab, kTabWaitCalc);      // 地図同期へ進まないよう、閉じ終わるまで何もしない状態に置く
}

bool MenuGone(void) {
    return R8(kMenuOpenState) == 1 && R32(kInvMenuPtr) == 0 && (R32(kMenuFlags) & 0x08u) == 0;
}

void FinishMenuClose(u32 mgr, u32 tab) {
    (void)mgr;
    FrameTrace::Mark(FrameTrace::ListFieldCmd, 0x200 | kCmdMenuOut);
    W8(kMenuFlagByte88, 0);
    reinterpret_cast<void (*)(u32, u32)>(kMenuBgFn)(R32(kMenuBgPtr), 10);
    W8(tab + kTab4493, 1);
    W8(tab + kTab4881, 1);
    if (reinterpret_cast<u32 (*)(u32, u32)>(kSoundStateFn)(7, 0))
        reinterpret_cast<void (*)(void)>(kSoundResumeA)();
    else if (reinterpret_cast<u32 (*)(u32, u32)>(kSoundStateFn)(13, 0))
        reinterpret_cast<void (*)(void)>(kSoundResumeB)();
    // 0x20（閉じる通知）を消し、地図なし（持ち物を開いたとき「メニューイン」が地図を片付けている）の印 0x4 にする
    *reinterpret_cast<volatile u32 *>(kMenuFlags) = (R32(kMenuFlags) & ~0x27u) | 0x04u;
    W8(tab + kTabActive6422, 1);
    W8(tab + kTabSelectedIndex, 0xFF);
    ChangeTabState(tab, kTabIdleCalc);      // タブ選択へ（出ているタブは続く全タブ退場で引っ込める）
}

// 元の下画面 UI を hide に合わせて動かす。戻り値: 隠れきっている
bool StepField(bool hide) {
    const u32 mgr = R32(kMenuMgrPtr);
    const u32 tab = mgr != 0 ? R32(mgr + kMgrTab) : 0;
    ++s_fieldFrames;
    if (s_field != Field::Shown && RoomId() != s_fieldRoom) {
        s_field = Field::Shown;             // 場面が変わった: 下画面はゲームが作り直す。命令は出さない
        s_fieldFrames = 0;
    }
    switch (s_field) {
    case Field::Shown:
        // ★開いていた下画面メニュー（持ち物など）を、地図とタブを出し直さずに閉じさせる（利用者指示 2026-09-25:
        //   閉じたら即リスト。以前は命令 3 = 状態 6「メニューアウト」→ 18「地図同期」で地図とタブを出し直していた）。
        //   閉じ始めたら、途中でエディターが切られても最後（後始末）まで行う。
        if (s_menuCloseSent) {
            if (MenuGone()) {
                FinishMenuClose(mgr, tab);
                s_menuCloseSent = false;
            }
            return false;
        }
        if (hide && MenuOpen()) {
            // 下メニューが入場し終えた（MENU_FLAGS 0x08|0x10）ときだけ閉じ始める。★g_MenuOpenState は条件にしない（1 に戻ることがある）
            if ((R32(kMenuFlags) & (0x08u | kMenuShownFlag)) == (0x08u | kMenuShownFlag) && R8(kTabCommand) == kCmdNone
                && tab != 0) {
                BeginMenuClose(mgr, tab);
                s_menuCloseSent = true;
            }
            return false;
        }
        if (hide && FieldIdle()) {
            s_fieldRoom = RoomId();
            if (R32(mgr + kMgrMap) != 0)
                W8(kMapCommand, kMapOut);
            if (R32(mgr + kMgrOther) != 0)
                W8(kOtherCommand, 0);
            W8(kTabCommand, kCmdAllTabsOut);
            FrameTrace::Mark(FrameTrace::ListFieldCmd, kCmdAllTabsOut);
            s_field = Field::Exiting;
            s_fieldFrames = 0;
        }
        return false;
    case Field::Exiting:
        // 全タブが引っ込み、地図の画面が無くなった（命令は消費済み）
        if (s_fieldFrames > 1 && FieldIdle() && *reinterpret_cast<const volatile u16 *>(tab + kTabShown) == 0
            && R32(mgr + kMgrMap) == 0) {
            s_field = Field::Hidden;
            s_fieldFrames = 0;
        }
        return false;
    case Field::Hidden:
        // ★地図を戻す（ゲームが map_village.arc の 596 KB の塊を取り直す）のは、リストを片付け、箱の arc も返してから
        if (!hide && FieldIdle() && s_stage == Stage::Off && !GameLabel::Present()) {
            W8(kTabCommand, kCmdRestoreField);
            FrameTrace::Mark(FrameTrace::ListFieldCmd, kCmdRestoreField);
            s_field = Field::Restoring;
            s_fieldFrames = 0;
            return false;
        }
        return true;
    case Field::Restoring:
        // 状態 18 → 7 → タブ選択。地図が作り直され、タブが出そろった
        if (s_fieldFrames > 1 && FieldIdle() && (R32(kMenuFlags) & 0x02u) != 0
            && *reinterpret_cast<const volatile u16 *>(tab + kTabShown) != 0) {
            s_field = Field::Shown;
            s_fieldFrames = 0;
        }
        return false;
    }
    return false;
}

bool MenuOpen(void) {
    return *reinterpret_cast<const volatile u8 *>(kMenuOpenState) != 1
        || (*reinterpret_cast<const volatile u32 *>(kMenuFlags) & 0x08u) != 0;
}

void *LytHeap(void) {
    const u32 alloc = *reinterpret_cast<const volatile u32 *>(kLytAllocatorPtr);
    return alloc != 0 ? *reinterpret_cast<void *const *>(alloc + 4) : nullptr;
}

// ---- 組み立て（1 フレームに 1 段）-------------------------------------------------------------
// 戻り値: 真 = 完了。偽 = まだ（s_error が空でなければ失敗）。
bool BuildStep(void) {
    if (!s_holderMade) {
        ArcCtor(s_holder);
        s_holderMade = true;
    }
    if (s_stage == Stage::Loading) {
        if (ArcLoadStep(s_holder, kArcPath) == 0)
            return false;
        // 書体 0 番（実機: "Garden_msg_size16.bcfnt"）とテクスチャを保持体へ登録する
        void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
        void *font = fontMgr != nullptr ? FontGet(fontMgr, 0) : nullptr;
        if (font == nullptr) {
            s_error = "ゲームの書体が取れない";
            return false;
        }
        u32 *nameObj = reinterpret_cast<u32 *>(FontName(fontMgr, 0));
        reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(nameObj[0])[2])(nameObj);   // 終端をそろえる（ゲームと同じ）
        RegisterFont(P(s_holder, kHolderAccessor), reinterpret_cast<const char *>(nameObj[1]), font);
        RegisterTex(s_holder);
        s_stage = Stage::Building;
        return false;
    }
    if (!s_frameBuilt) {
        if (!s_frameMade) {
            LayoutCtor(s_frame);
            s_frameMade = true;
        }
        W(s_frame, kLayoutHolder) = reinterpret_cast<u32>(s_holder);
        if (LayoutBuild(s_frame, "ctlg_list.bclyt", nullptr, kFrameCmdBytes) == 0) {
            s_error = "ctlg_list.bclyt を組めない";
            return false;
        }
        s_frameBuilt = true;
        B(s_frame, kLayoutPriority) = 2;
        AnimCtor(s_frameIn);
        AnimCtor(s_frameOut);
        AnimCtor(s_frameKind);
        s_animsMade = true;
        AnimLoad(s_frameIn, "ctlg_list_in.bclan", s_holder);
        AnimLoad(s_frameOut, "ctlg_list_out.bclan", s_holder);
        AnimLoad(s_frameKind, "ctlg_list_kind.bclan", s_holder);
        // 種類の見た目を焼く（BsMenuCatalog_StartListEnter と同じ: 結合 → フレーム → 計算 → 解除）
        AnimBind(s_frame, s_frameKind);
        AnimSetFrame(s_frameKind, kKindFrame);
        LayoutCalc(s_frame);
        AnimUnbind(s_frame, s_frameKind);
        return false;
    }
    if (s_list == nullptr) {
        void *heap = LytHeap();
        if (heap == nullptr) {
            s_error = "nw::lyt のヒープが無い";
            return false;
        }
        // 一覧の文字をここで確定する（組み立て中に差し替えられても混ざらない）
        const u32 count = s_pendCount;
        s_builtSeq = s_itemsSeq;
        s_count = count;
        const char *label = s_pendLabel;
        const u32 msgData = label != nullptr ? *reinterpret_cast<const volatile u32 *>(kMsgDataPtr) : 0;
        s_hasPrefix = s_pendHasPrefix;
        if (s_hasPrefix)
            std::memcpy(s_prefix, s_pendPrefix, sizeof(s_prefix));
        for (u32 i = 0; i < count; ++i) {
            s_useFix[i] = false;
            if (msgData != 0 && s_pendMsg[i] >= 0) {
                // ゲームの名前があればそちら（STR_Fobj_name など）。引けなければ渡された文字列のまま
                WordFixCtor(s_fix[i]);
                s_useFix[i] = MsgSetup(msgData, s_fix[i], label, (u32)s_pendMsg[i]) != 0;
            }
            std::memcpy(s_text[i], s_pendText[i], sizeof(s_pendText[i]));
            s_words[i].vtbl = kWordPtrVtbl;
            s_words[i].text = s_text[i];
            s_words[i].cap = kMaxChars + 1;
        }
        void *mem = HeapAlloc(kInstSelectBytes, heap, 4);
        if (mem == nullptr) {
            s_error = "一覧の確保に失敗";
            return false;
        }
        s_list = reinterpret_cast<u8 *>(InstSelectCtor(mem));
        s_dpadRowsDone = s_dpadRowsSent;    // 出す前の十字は反映しない
        s_dpadPagesDone = s_dpadPagesSent;
        const u32 *base = reinterpret_cast<const u32 *>(kInstSelectVtbl);
        s_vtblStore[0] = base[-1];
        for (u32 i = 0; i < kVtblWords; ++i)
            s_vtbl[i] = base[i];
        s_vtbl[13] = reinterpret_cast<u32>(&ListBuildItems);
        s_vtbl[25] = reinterpret_cast<u32>(&ListWordAt);
        W(s_list, 0) = reinterpret_cast<u32>(s_vtbl);
        W(s_list, kListTextCap) = kMaxChars;
        s_idReady = false;
        if (s_hasPrefix && !PrebuildListLayout()) {
            s_error = "ID 付きの一覧を組み立てられない";
            s_hasPrefix = false;            // 素の組み立てに任せる（Layout+32 が 0 のまま）
        }
        return false;
    }
    if (!s_listSetup) {
        if (SetupStep(s_list, s_holder, LytHeap()) == 0)
            return false;
        s_listSetup = true;
        if (s_hasPrefix)
            SetupIdRows();
        void *pane = FindPane(s_frame, "N_scrl_pos_00");
        if (pane != nullptr)
            ScrollBindPane(P(s_list, kListScroll), pane);
        return false;
    }
    return true;
}

// 見える行数 = 件数 - スクロールの最大値（件数が少なくスクロールしないときは件数）
s32 VisibleRows(void) {
    const s32 maxTop = S(s_list, kListScroll + kScrollMaxTop);
    s32 v = maxTop > 0 ? (s32)s_count - maxTop : (s32)s_count;
    if (v < 1) v = 1;
    if (v > (s32)kListRows) v = (s32)kListRows;
    return v;
}

void ApplySelect(s32 index) {
    if (s_list == nullptr || !s_listSetup || s_count == 0)
        return;
    if (index < 0)
        index = 0;
    if ((u32)index >= s_count)
        index = (s32)s_count - 1;
    // 見えていなければスクロールする（先頭 = index、ただし末尾で 8 行が埋まるように）
    const s32 top = S(s_list, kListTop);
    if (index < top || index >= top + VisibleRows()) {
        s32 want = index;
        const s32 maxTop = S(s_list, kListScroll + kScrollMaxTop);
        if (want > maxTop)
            want = maxTop > 0 ? maxTop : 0;
        ScrollTo(P(s_list, kListScroll), want);
    }
    u32 *vt = *reinterpret_cast<u32 **>(s_list);
    reinterpret_cast<SetSelectFn>(vt[16])(s_list, index, S(s_list, kListSelected));
    s_lastSelected = S(s_list, kListSelected);
}

// 選択を index へ動かす。見えていなければ 1 行ずつスクロールして端に入れる（上なら先頭、下なら末尾の行）
void MoveSelect(s32 index) {
    FrameTrace::Mark(FrameTrace::ListMove, (u32)index);
    const s32 top = S(s_list, kListTop);
    const s32 rows = VisibleRows();
    s32 want = top;
    if (index < top)
        want = index;
    else if (index >= top + rows)
        want = index - (rows - 1);
    if (want != top)
        ScrollTo(P(s_list, kListScroll), want);
    u32 *vt = *reinterpret_cast<u32 **>(s_list);
    reinterpret_cast<SetSelectFn>(vt[16])(s_list, index, S(s_list, kListSelected));
    s_lastSelected = S(s_list, kListSelected);
    s_decided = index;
}

void StepDpad(void) {
    const s32 rows = s_dpadRowsSent, pages = s_dpadPagesSent;
    const s32 dRows = rows - s_dpadRowsDone, dPages = pages - s_dpadPagesDone;
    s_dpadRowsDone = rows;
    s_dpadPagesDone = pages;
    if (B(s_list, kListInputLock) != 0 || s_count == 0 || (dRows == 0 && dPages == 0))
        return;                             // 止めている間の押下は捨てる
    const s32 cur = S(s_list, kListSelected) < 0 ? 0 : S(s_list, kListSelected);
    s32 next = cur + dRows + dPages * VisibleRows();
    if (next < 0) next = 0;
    if (next >= (s32)s_count) next = (s32)s_count - 1;
    if (next != cur) {
        MoveSelect(next);
        reinterpret_cast<void (*)(u32)>(kPlaySoundFn)(kSndListActive);
    }
}

void EnterBoth(void) {
    FrameTrace::Mark(FrameTrace::ListEnter);
    SwitchAnim(s_frame, s_frameOut, s_frameIn);
    s_frameDir = Dir::In;
    // 中身は状態機械の「入場処理」に任せる（終われば「待機処理」へ）。enter が in を 0 から結合するので、
    // 退場の途中なら out を外して in の位置を直す。
    const bool wasOut = B(P(s_list, kListAnimOut), kAnimBound) != 0;
    const float from = wasOut ? 1.0f - Progress(P(s_list, kListAnimOut)) : 0.0f;
    if (wasOut)
        AnimUnbind(P(s_list, kListLayout), P(s_list, kListAnimOut));
    StartEnter(s_list);
    AnimSetFrame(P(s_list, kListAnimIn), from * (F(P(s_list, kListAnimIn), kAnimTotal) - 1.0f));
    s_listDir = Dir::In;
}

void LeaveBoth(void) {
    FrameTrace::Mark(FrameTrace::ListLeave);
    SwitchAnim(s_frame, s_frameIn, s_frameOut);
    s_frameDir = Dir::Out;
    const bool wasIn = B(P(s_list, kListAnimIn), kAnimBound) != 0;
    const float from = wasIn ? 1.0f - Progress(P(s_list, kListAnimIn)) : 0.0f;
    if (wasIn)
        AnimUnbind(P(s_list, kListLayout), P(s_list, kListAnimIn));
    StartLeave(s_list);
    AnimSetFrame(P(s_list, kListAnimOut), from * (F(P(s_list, kListAnimOut), kAnimTotal) - 1.0f));
    s_listDir = Dir::Out;
}

// ★枠は毎フレーム計算する（BsMenuCatalog の一覧操作中の状態 sub_21B26C と同じ）。スクロールバーは一覧の別の
//   Layout（スクロール部品 +588）のペイン木を枠の N_scrl_pos_00 へ付け替えたもので、枠を計算しないと
//   つまみの位置が画面に反映されない（以前はアニメ中だけ計算していて、利用者報告「スクロールバーが同期していない」）。
void StepFrameAnim(void) {
    void *anim = s_frameDir == Dir::In ? s_frameIn : s_frameDir == Dir::Out ? s_frameOut : nullptr;
    if (anim != nullptr) {
        if (AnimFinished(anim)) {
            AnimUnbind(s_frame, anim);
            s_frameDir = Dir::None;
        } else {
            AnimStep(anim);
        }
    }
    LayoutCalc(s_frame);
}

}  // namespace

// ---- メニューのスレッド -----------------------------------------------------------------------
bool SetItems(const char *const *items, u32 count) {
    if (count > kMaxItems)
        count = kMaxItems;
    for (u32 i = 0; i < count; ++i)
        Utf8To16(items[i], s_pendText[i], kMaxChars);
    s_pendCount = count;
    s_pendLabel = nullptr;                  // メッセージは SetItemMessages で改めて渡す
    s_pendHasPrefix = false;                // 前置きも SetItemPrefixes で改めて渡す
    s_itemsSeq = s_itemsSeq + 1;
    return count > 0;
}

void SetItemPrefixes(const char *const *prefixes, u32 count) {
    if (count > kMaxItems)
        count = kMaxItems;
    bool any = false;
    for (u32 i = 0; i < kMaxItems; ++i) {
        Utf8To16(prefixes != nullptr && i < count ? prefixes[i] : "", s_pendPrefix[i], kPrefixChars);
        any = any || s_pendPrefix[i][0] != 0;
    }
    s_pendHasPrefix = any;
    s_itemsSeq = s_itemsSeq + 1;
}

void FeedDpad(u32 heldKeys) {
    // CTRPF の Key: DPadRight 0x10 / DPadLeft 0x20 / DPadUp 0x40 / DPadDown 0x80（HID と同じ並び）
    u32 s = 0;
    if (heldKeys & 0x40u) s |= kDpadUp;
    if (heldKeys & 0x80u) s |= kDpadDown;
    if (heldKeys & 0x20u) s |= kDpadLeft;
    if (heldKeys & 0x10u) s |= kDpadRight;
    // 押し下げ・押し続けをここ（メニューのスレッド）で実時間で判定し、移動量を溜める（上の説明）
    const u32 now = (u32)(svcGetSystemTick() / (u64)(SYSCLOCK_ARM11 / 1000));
    const u32 trig = s & ~s_dpadPrev;
    bool send = false;
    if (s == 0) {
        s_dpadPrev = 0;
        return;
    }
    if (trig != 0 || s != s_dpadPrev) {
        s_dpadNextMs = now + kRepeatDelayMs;
        send = trig != 0;
    } else if ((s32)(now - s_dpadNextMs) >= 0) {
        s_dpadNextMs = s_dpadNextMs + kRepeatEveryMs;
        if ((s32)(now - s_dpadNextMs) >= 0)     // 大きく遅れたら追いつかせない（1 回だけ送る）
            s_dpadNextMs = now + kRepeatEveryMs;
        send = true;
    }
    s_dpadPrev = s;
    if (!send)
        return;
    if (s & kDpadUp) s_dpadRowsSent = s_dpadRowsSent - 1;
    else if (s & kDpadDown) s_dpadRowsSent = s_dpadRowsSent + 1;
    else if (s & kDpadLeft) s_dpadPagesSent = s_dpadPagesSent - 1;
    else if (s & kDpadRight) s_dpadPagesSent = s_dpadPagesSent + 1;
}

void SetItemMessages(const char *label, const s16 *indices, u32 count) {
    if (count > kMaxItems)
        count = kMaxItems;
    for (u32 i = 0; i < kMaxItems; ++i)
        s_pendMsg[i] = (indices != nullptr && i < count) ? indices[i] : (s16)-1;
    s_pendLabel = label;
    s_itemsSeq = s_itemsSeq + 1;
}

void Show(s32 selected) {
    if (!s_hookReady) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep)) {
            s_error = "フレームフックを入れられない";
            return;
        }
        s_hookReady = true;
    }
    s_wantSelect = selected;
    s_selectSeq = s_selectSeq + 1;
    s_error = "";
    s_want = true;
}

void Hide(void) {
    s_want = false;
}

bool Wanted(void) {
    return s_want;
}

bool Present(void) {
    return s_stage != Stage::Off || s_field != Field::Shown;
}

bool FieldTransition(void) {
    return s_field == Field::Exiting || s_field == Field::Restoring || s_menuCloseSent;
}

void Select(s32 index) {
    s_wantSelect = index;
    s_selectSeq = s_selectSeq + 1;
}

s32 TakeDecided(void) {
    const s32 v = s_decided;
    if (v >= 0)
        s_decided = -1;
    return v;
}

const char *LastError(void) {
    return s_error;
}

void Shutdown(void) {
    s_want = false;
    if (!s_hookReady)
        return;
    s_shutdown = true;
    for (u32 i = 0; i < 90 && (s_stage != Stage::Off || s_field != Field::Shown); ++i)
        svcSleepThread(16666667LL);
}

// ---- ゲームのスレッド -------------------------------------------------------------------------
void FrameStep(void) {
    const bool want = s_want && !s_shutdown && !MenuOpen() && s_pendCount > 0;
    void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
    // 元の下画面 UI: 出したいあいだ、またはリストが描かれているあいだは退場させておく。リストが消えてから戻す。
    //   ★メニューが開いていても「出したい」は変えない（StepField がメニューを閉じさせる）
    const bool wantRaw = s_want && !s_shutdown && s_pendCount > 0 && s_error[0] == 0;
    const bool fieldHidden = StepField(wantRaw || s_stage == Stage::Live);

    switch (s_stage) {
    case Stage::Off:
        // ★組み立て（catalogue.arc の読み込み）は元の UI が退場し終えてから（地図の作り直しと重ねない）
        if (!want || s_error[0] != 0 || !fieldHidden)
            return;
        s_stage = Stage::Loading;
        // 続けて組み立てへ
    case Stage::Loading:
    case Stage::Building:
        if (!BuildStep()) {
            if (s_error[0] != 0) {
                // 失敗: 組み立てを最後まで回せない。一覧が組み立て途中なら壊せないので残す（漏れより落ちる方が悪い）。
                if (s_list == nullptr || s_listSetup)
                    DestroyAll();
            }
            return;
        }
        if (want && !fieldHidden)
            return;                         // 元の UI が退場しきるまで待つ（組み上がったまま描かない）
        s_stage = Stage::Live;
        FrameTrace::Mark(FrameTrace::ListBuildDone, s_count);
        s_selectDone = s_selectSeq;
        ApplySelect(s_wantSelect);
        if (want)
            EnterBoth();
        else
            LeaveBoth();                    // 組み立て中に Hide された: 出さずに片付けへ
        break;
    case Stage::Live:
        break;
    case Stage::Waiting:
        if (++s_waitFrames >= kTeardownWaitFrames)
            DestroyAll();
        return;
    }

    // ---- Live ----
    // メニューが開いた・終了: アニメを待たずに描くのをやめ、数フレーム後に壊す
    if (s_shutdown || MenuOpen()) {
        s_stage = Stage::Waiting;
        s_waitFrames = 0;
        return;
    }
    // 一覧が差し替えられた: 退場させて組み直す
    const bool stale = s_builtSeq != s_itemsSeq;
    const bool show = want && !stale && fieldHidden;
    if (show && s_frameDir != Dir::In && s_listDir != Dir::In && (s_frameDir == Dir::Out || s_listDir == Dir::Out))
        EnterBoth();
    else if (!show && s_frameDir != Dir::Out && s_listDir != Dir::Out)
        LeaveBoth();

    if (s_selectDone != s_selectSeq) {
        s_selectDone = s_selectSeq;
        ApplySelect(s_wantSelect);
    }

    // 入力はアニメ中は止める。中身の状態機械は vt[4] が進める（入場・退場・待機）。
    B(s_list, kListInputLock) = (s_frameDir != Dir::None || B(s_list, kListAnimating) != 0 || !show) ? 1 : 0;
    u32 *vt = *reinterpret_cast<u32 **>(s_list);
    reinterpret_cast<ListFn>(vt[4])(s_list);            // SelectBase_Update
    StepDpad();
    StepIdRows();                                       // ID の欄（行の前置き）を名前の欄に合わせる
    if (s_listDir != Dir::None && B(s_list, kListAnimating) == 0)
        s_listDir = Dir::None;
    StepFrameAnim();

    // タッチで決定された行（待機処理が +2792 に置く）
    const s32 sel = S(s_list, kListSelected);
    if (sel != s_lastSelected) {
        s_lastSelected = sel;
        if (sel >= 0 && (u32)sel < s_count)
            s_decided = sel;
    }

    // 退場が両方終わったら片付けへ（この フレームから描かない）
    if (!show && s_frameDir == Dir::None && s_listDir == Dir::None) {
        s_stage = Stage::Waiting;
        s_waitFrames = 0;
        return;
    }

    // 描画登録（リストは RenderBottom のあと毎フレーム空になる）。枠 → 中身（どちらも優先度 2、入れた順）
    if (mgr != nullptr) {
        AddLayout(mgr, s_frame, 1);
        reinterpret_cast<ListFn>(vt[5])(s_list);        // SelectBase_Draw: 計算 + AddLayout(+44, 1)
    }
}

}  // namespace GameList
