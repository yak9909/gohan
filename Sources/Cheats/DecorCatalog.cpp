// DecorCatalog — 家の模様替えに HHD の家具リスト・壁紙/床紙リストを足す（T022 段 2、2026-10-07。未実機）
//
// 根拠（解析リポジトリ project_v2）:
//   HHD の動き IDA-gpt-6.1-sol-HHD-F001（格子 Tum15・タブ・吹き出し・音）、HHD-F016 / F018 / F019（置く順・上段タブ・品の並び）。
//   ACNL の置き方 IDA-gpt-6.1-sol-F001 / F002、IDA-opus-5.5-F103（かざるの判定 0x6920CC と生成 0x68FA84。試験 3 で実機の即反映を確認）。
//   壁紙・床 IDA-opus-5.5-F106（HouseRoom_SetWallpaper 0x2C5FA8 / SetFlooring 0x2C6CCC）。品番 F101 / F107。
//   設計 docs/topics/t022_decorate_port_requirements.md §7。
// 流れ（HHD の写し）:
//   模様替え UI が開いている間（DecorTrash と同じ EditorLive）、上段タブ 2 つを出す。押している間 select + touch、離して決定 touch_ok + select_ok。
//   決定で窓（家具 / 壁紙・床紙）を開く（in）。窓の上段タブ・小分類・戻る・格子（横送りは DecorSlider = HHD の PageSlider）。
//   マスに触れると select + touch と名前の吹き出し、離して決定 → 家具は空きマスへ置く（DecorPlace の順で 0x6920CC を試し、置けたら 0x68FA84）、
//   壁紙・床紙はゲーム自身の貼り替え。成功したら窓を閉じる（HHD の StateID_Ok_）。置けなければ無効音（HHD の EditError_ の代わり）。
//   B / 戻るで閉じる（out）。
//   出した家具のチップ（模様替え UI を開いたまま掴めるようにする）は IDA-gpt-6.1-sol-F002 の手順: 家具の完成（+0x66 bit1・状態 +0x5F8 == 2・破棄要求 +15 == 0）と
//   記録の対応を 2 フレーム続けて確かめ、窓の資源を返してから未使用枠へ Chip_Setup → Chip_RestoreOwnRoot（ChipStep）。
//   Neutral の通常の親は各チップ自身の root。共有ペインへ全チップを付け直すと既存チップも更新対象木から外れる（F???、fix3）。
// スレッド: SD を読むのと入力を止めるのはメニューのスレッド（Tick）。ゲームの関数は FrameStep（ゲームのスレッド）だけ。

#include "DecorCatalog.hpp"
#include "DecorCatalogLayout.h"
#include "DecorCatalogTable.h"
#include "DecorCatalogCro.h"
#include "DecorTrashTable.h"
#include "DecorIcons.hpp"
#include "DecorLayout.hpp"
#include "DecorPlace.hpp"
#include "DecorSlider.hpp"
#include "Cheats.hpp"
#include "GohanFiles.hpp"
#include "GridCursor.hpp"
#include "GridCursorGameApi.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"
#include "ItemNames.hpp"

#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>
#include <new>

namespace DecorCatalog {

namespace {

using namespace CTRPluginFramework;
namespace L = DecorLayout;
namespace CL = DecorCatalogLayout;
namespace CT = DecorCatalogTable;
using Cheats::kDecorCatalog;

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline bool IsHeapPointer(u32 v) { return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u; }

// ---- 模様替え UI（DecorTrash と同じ照合。DecorTrashTable.h の番地）----
u32 s_slotSeen = 0xFFFFFFFFu, s_indoor;

u32 IndoorBase(void) {
    using namespace DecorTrashTable;
    const u32 w = R32(kIndoorSlot);
    if (w != s_slotSeen) {
        s_slotSeen = w;
        s_indoor = 0;
        static const u32 words[][2] = {
            { kIndoorEditorInCalc, kIndoorEditorInCalcWord }, { kIndoorEditorNeutralCalc, kIndoorEditorNeutralCalcWord },
            { kIndoorEditorOutCalc, kIndoorEditorOutCalcWord }, { kIndoorSlotTarget, kIndoorSlotTargetWord },
            { kIndoorEditorCmnBtnInCalc, kIndoorEditorCmnBtnInCalcWord }, { kIndoorEditorCmnBtnOutCalc, kIndoorEditorCmnBtnOutCalcWord },
        };
        if (w != kUnloadedSlotWord && w > kIndoorSlotOffset) {
            const u32 base = w - kIndoorSlotOffset;
            bool ok = true;
            for (u32 i = 0; i < sizeof(words) / sizeof(words[0]) && ok; ++i)
                ok = Process::CheckAddress(base + words[i][0], MEMPERM_READ) && R32(base + words[i][0]) == words[i][1];
            if (ok)
                s_indoor = base;
        }
    }
    return s_indoor;
}

u32 EditorCalc(u32 indoor) {
    if (indoor == 0)
        return 0;
    const u32 at = R32(indoor + DecorTrashTable::kIndoorEditorPtrLiteral);
    if (!Process::CheckAddress(at, MEMPERM_READ))
        return 0;
    const u32 editor = R32(at);
    return IsHeapPointer(editor) ? R32(editor + 52) : 0;   // エディター +52 = 今の calc（F105）
}

bool EditorLive(u32 indoor, u32 calc) {
    using namespace DecorTrashTable;
    return indoor != 0 && (calc == indoor + kIndoorEditorInCalc || calc == indoor + kIndoorEditorCmnBtnInCalc
                           || calc == indoor + kIndoorEditorNeutralCalc || calc == indoor + kIndoorEditorCmnBtnOutCalc);
}

bool EditorNeutral(u32 indoor, u32 calc) { return indoor != 0 && calc == indoor + DecorTrashTable::kIndoorEditorNeutralCalc; }

u32 EditorPtr(u32 indoor) {
    if (indoor == 0)
        return 0;
    const u32 at = R32(indoor + DecorTrashTable::kIndoorEditorPtrLiteral);
    if (!Process::CheckAddress(at, MEMPERM_READ))
        return 0;
    const u32 editor = R32(at);
    return IsHeapPointer(editor) ? editor : 0;
}

void CancelCapturedRangeTouch(u32 indoor) {
    const u32 editor = EditorPtr(indoor);
    if (editor == 0 || !EditorNeutral(indoor, EditorCalc(indoor)))
        return;
    // ModuleIndoor .text先頭からのオフセット。入口と同関数の4欄クリア命令を照合してから触る。
    static const u32 checks[][2] = {
        { 0xAAF0, 0xE92D4010 }, { 0xAB30, 0xED848A6A }, { 0xAB34, 0xED848A6B },
        { 0xAB38, 0xED848A6C }, { 0xAB3C, 0xED848A6D },
    };
    for (u32 i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i)
        if (!Process::CheckAddress(indoor + checks[i][0], MEMPERM_READ) || R32(indoor + checks[i][0]) != checks[i][1])
            return;
    // Neutralから呼ぶ範囲選択器（+128200）。0xB0AAF0は開始点（+424/+428）が非0ならreleaseで選択確定する。
    // 先行するゲーム更新に最初のpressが届いていても、この指だけの開始/現在点を同じ経路で0へ戻し、
    // 次の遮断フレームがreleaseとして見えても範囲を確定させない。状態/選択vectorは変更しない。
    volatile u32 *points = reinterpret_cast<volatile u32 *>(editor + 128200 + 424);
    for (u32 i = 0; i < 4; ++i)
        points[i] = 0;
}

// ModuleFtr の先頭（DecorTrash と同じ照合）
u32 s_ftrSlotSeen = 0xFFFFFFFFu, s_ftr;

u32 FtrBase(void) {
    using namespace DecorTrashTable;
    const u32 w = R32(kFtrSlot);
    if (w != s_ftrSlotSeen) {
        s_ftrSlotSeen = w;
        s_ftr = 0;
        static const u32 words[][2] = { { kFtrRecGet, kFtrRecGetWord }, { kFtrRecIsAlive, kFtrRecIsAliveWord }, { kFtrSlotTarget, kFtrSlotTargetWord } };
        if (w != kUnloadedSlotWord && w > kFtrSlotOffset) {
            const u32 base = w - kFtrSlotOffset;
            bool ok = true;
            for (u32 i = 0; i < sizeof(words) / sizeof(words[0]) && ok; ++i)
                ok = Process::CheckAddress(base + words[i][0], MEMPERM_READ) && R32(base + words[i][0]) == words[i][1];
            if (ok)
                s_ftr = base;
        }
    }
    return s_ftr;
}

// ---- ゲームの関数 ----
struct Item { u16 id, flags; };
typedef int (*TryPutFn)(void *record, int x, int z, int rot, const Item *item, int layer, int px, int pz, int prot, int pattern);
typedef int (*SpawnFn)(void *record);
typedef void (*PosToCellFn)(int *x, int *z, const float *pos);
typedef void (*CellBoundsFn)(int *minX, int *minZ, int *maxX, int *maxZ);
typedef void *(*FtrTableFn)(void);
typedef int (*FtrFreeFn)(void *table);
typedef int (*SetBgFn)(const Item *item, int playSound);
typedef void (*PlaySoundFn)(u32 id);
const TryPutFn     TryPut      = reinterpret_cast<TryPutFn>(0x006920CC);       // Room_TryPutFurnitureAt（判定のみ）
const SpawnFn      Spawn       = reinterpret_cast<SpawnFn>(0x0068FA84);        // BsFtrMgr_SpawnFromRecord
const PosToCellFn  PosToCell   = reinterpret_cast<PosToCellFn>(0x00317090);    // Field_WorldToCellXZ
const CellBoundsFn CellBounds  = reinterpret_cast<CellBoundsFn>(0x002853A8);   // Room_GetInteriorCellBounds（両端を含む）
const FtrTableFn   FtrTable    = reinterpret_cast<FtrTableFn>(0x004E9314);     // FtrObjectTable_GetCurrent
const FtrFreeFn    FtrFree     = reinterpret_cast<FtrFreeFn>(0x007449D4);      // FtrObjectTable_CountFreeSlots（家の家具 48）
const SetBgFn      SetWall     = reinterpret_cast<SetBgFn>(0x002C5FA8);        // HouseRoom_SetWallpaper
const SetBgFn      SetFloor    = reinterpret_cast<SetBgFn>(0x002C6CCC);        // HouseRoom_SetFlooring
int (*const CanChangeBackground)(void) = reinterpret_cast<int (*)(void)>(0x005B39B4);   // F005。起動時・決定時とも本来の許可判定
const PlaySoundFn  PlaySound   = reinterpret_cast<PlaySoundFn>(0x0058C7D4);    // Game_PlaySound

// ---- 音（HHD の音は ACNL に無いので代用。要件書 §7.4。実機で利用者に決めてもらう）----
const u32 kSndCellTouch = 0x01000399;       // SE_SYS_BTN_ACTIVE_S
const u32 kSndCellDecide = 0x0100038F;      // SE_SYS_DECIDE_L
const u32 kSndTabTouch = 0x0100046B;        // SE_SYS_CTLG_TOP_BTN_ACTIVE
const u32 kSndTabDecide = 0x0100046C;       // SE_SYS_CTLG_TOP_BTN_SELECTED
const u32 kSndOpen = 0x010003C2;            // SE_SYS_WIN_SELECT_OPEN
const u32 kSndPageInc = 0x0100039C, kSndPageDec = 0x0100039D;   // SE_SYS_PAGE_INC / DEC
const u32 kSndSlide = 0x0100046A;           // SE_SYS_CTLG_PAGE_CHANGE（横スライド・小分類の境目）
const u32 kSndBack = 0x01000391;            // SE_SYS_DECIDE_QUIT
const u32 kSndInvalid = 0x0100039A;         // SE_SYS_BTN_ACT_INVALID

// ---- 描く順（Layout +12。昇順、同じ値なら後に足した方が手前）: チップ 6 / 掴んでいる 7（F105）。タブはゴミ箱と同じ 6、窓はその上 ----
const u8 kPriTop = 6, kPriWindow = 0x88;   // rom_lgt/rom_chip/rom_strg は 0x87（0x325950 / 0x325A80）。その次に描く
const u32 kScreenLower = 1;
const u32 kTeardownWaitFrames = 3;
// コマンドの器（未実測。HhdScreen の地は 784 B 実測。大きめに取り、実機で測って詰める）
const u32 kCmdTop = 0x2000, kCmdWindow = 0x8000, kCmdGrid = 0x18000, kCmdName = 0x2000;
const u32 kMaxTopArc = 0x40000, kMaxCatalogArc = 0x100000;
const u32 kWindowHeapReserve = 0x6000;          // 起動完了時に残す24KiB。実測6028Bの追加UI費用3件分＋6492B（fix3 report）

// アニメの最後のコマ。frames は ACNL の数え方（HHD の frameSize + 1。tools/hhd/clan_pack.py の ★）で、AnimStep 0x568964 は frames − 1 で止まる
float LastFrame(const CL::AnimRef &r) { return r.frames > 0 ? (float)(r.frames - 1) : 0.0f; }

// ---- 資源（メニューのスレッドが読み、ゲームのスレッドがヒープへ写す）----
u8 *s_topFile;
u32 s_topSize;
u8 *volatile s_catFile;
volatile u32 s_catSize;
volatile bool s_wantCatFile, s_catFileFailed, s_catUploaded;   // s_catUploaded: ゲームのヒープへ写し終えた（プラグインの写しを返してよい）
// 窓を組んだときの読み込みのヒープの空き（2026-10-08 実機: アイコンの枠 184,320 B が取れずに開かなかった。デバッガで読む）
//   [0] 組む前 [1] アイコン＋arc の後 [2] レイアウト 3 つの後 [3] アニメの後 [4] 失敗して返した後
volatile u32 s_heapLog[5];
volatile u32 s_heapMaxLog[5];                   // 同時点の0x80整列込み最大連続空き
volatile u32 s_heapWindowReady;                // Bind / 最初のボタンを含めた起動完了時の残量
bool s_canChangeBackground;                    // 模様替えを開いた時点のゲーム判定（家の外観から推測しない）
volatile u8 s_openFail;                         // 窓を組めなかった: 1 レイアウト / 2 アイコンの枠 / 3 ゲーム用の残量
u8 s_openStep;                                  // OpenWindow がどこまで進んだか（ゲームのスレッドだけ）
volatile u8 s_catFailReason;                    // 窓の arc が読めなかった理由（メニューのスレッドが知らせる）: 0 なし / 1 ファイルが無い / 2 読めない・大きすぎる・メモリ不足

// ---- ボタン ----
struct Btn {
    const CL::Button *def;
    L::Layout *lay;
    L::Anim an[5];
    u8 loaded;                                   // 読んだアニメのビット
    u8 unsel;                                    // 逆再生で戻している途中（BtnUnselect）: 0 なし / 1 戻すだけ / 2 戻したら loop
    bool keepSelect;                             // 戻すのは touch だけ（選ばれている小分類）
    L::Arc *arc;
};

bool BtnAnim(Btn &b, L::Arc &arc, u32 k) {
    if (b.def == nullptr || b.def->anim[k].name == nullptr)
        return false;
    if ((b.loaded & (1u << k)) == 0) {
        if (!L::LoadAnim(b.an[k], arc, b.def->anim[k].name))
            return false;
        b.loaded |= (u8)(1u << k);
    }
    return true;
}

bool BtnPlay(Btn &b, L::Arc &arc, u32 k, bool last) {
    if (!BtnAnim(b, arc, k))
        return false;
    const CL::AnimRef &r = b.def->anim[k];
    return L::Bind(b.an[k], *b.lay, r.group, last ? LastFrame(r) : 0.0f, r.group2);
}

void BtnStop(Btn &b, u32 k) {
    if (b.loaded & (1u << k))
        L::Unbind(b.an[k]);
}

// 結んでいるアニメを全部外す。★外してもペインは最後に当てた値のまま残る（色・位置が戻らない）。見た目を戻すのは BtnRest / BtnUnselect
void BtnReset(Btn &b) {
    for (u32 k = 0; k < 5; ++k)
        BtnStop(b, k);
    b.unsel = 0;
}

// 素の見た目へすぐ戻す: select と touch の 0 コマ目で止める（一度もアニメを読んでいなければレイアウトの値のまま）
void BtnRest(Btn &b, L::Arc &arc) {
    const bool touched = b.loaded != 0;
    BtnReset(b);
    if (!touched)
        return;
    for (u32 k : { (u32)CL::kSelect, (u32)CL::kTouch })
        if (BtnPlay(b, arc, k, false))
            L::Hold(b.an[k]);
}

// HHD の TouchUnSelect / UnSelect（HHD-F001 §2: select と touch を逆に再生し、両方 0 に着いたら Wait）。
//   keepSelect: 選ばれている小分類は touch だけ戻す。loopAfter: 戻し終えたら loop を流す（上段タブ）
void BtnUnselect(Btn &b, L::Arc &arc, bool keepSelect, bool loopAfter) {
    BtnStop(b, CL::kTouchOk);
    BtnStop(b, CL::kSelectOk);
    BtnStop(b, CL::kLoop);
    if (b.loaded & (1u << CL::kTouch))
        L::Reverse(b.an[CL::kTouch]);
    if (!keepSelect && (b.loaded & (1u << CL::kSelect)))
        L::Reverse(b.an[CL::kSelect]);
    b.unsel = loopAfter ? 2 : 1;
    b.keepSelect = keepSelect;
    b.arc = &arc;
}

void BtnFree(Btn &b) {
    for (u32 k = 0; k < 5; ++k)
        if (b.loaded & (1u << k))
            L::FreeAnim(b.an[k]);
    b.loaded = 0;
}

void BtnStep(Btn &b) {
    for (u32 k = 0; k < 5; ++k)
        if (b.loaded & (1u << k))
            L::Step(b.an[k]);
    if (b.unsel != 0 && L::Done(b.an[CL::kTouch]) && (b.keepSelect || L::Done(b.an[CL::kSelect]))) {
        const bool loop = b.unsel == 2;
        b.unsel = 0;                             // 0 コマ目で止まっている（結んだまま。外すと値が残るだけなので外さない）
        if (loop && b.arc != nullptr)
            BtnPlay(b, *b.arc, CL::kLoop, false);
    }
}

// HHD の ButtonBaseActor（HHD-F001 §2）: 触れた = select の最後のコマ + touch、外れた = 戻す、離した = touch_ok + select_ok
void BtnPress(Btn &b, L::Arc &arc) {
    BtnReset(b);
    BtnPlay(b, arc, CL::kSelect, true);
    BtnPlay(b, arc, CL::kTouch, false);
}

void BtnDecide(Btn &b, L::Arc &arc) {
    BtnStop(b, CL::kTouch);
    BtnStop(b, CL::kSelect);
    BtnPlay(b, arc, CL::kTouchOk, false);
    BtnPlay(b, arc, CL::kSelectOk, false);
}

// 選ばれている見た目（窓の上段タブ = select_ok の最後 + loop、小分類・マス = select の最後）
void BtnSelected(Btn &b, L::Arc &arc, bool tab) {
    BtnReset(b);
    if (tab) {
        BtnPlay(b, arc, CL::kSelectOk, true);
        BtnPlay(b, arc, CL::kLoop, false);
    } else {
        BtnPlay(b, arc, CL::kSelect, true);
    }
}

bool InRect(const CL::Rect &r, float x, float y, float dx = 0.0f) { return x >= r.l + dx && x < r.r + dx && y >= r.t && y < r.b; }

// ---- 状態 ----
enum class Stage : u8 { Idle, Live, Teardown, Failed };
enum class Win : u8 { Closed, Loading, Opening, Open, Closing };
enum class Hit : u8 { None, TopTab, Tab, Kind, Back, Cell, Grid };

volatile bool s_enabled;
bool s_hooked, s_toldFail;
int s_index = -1;
Stage s_stage = Stage::Idle;
u32 s_wait;
bool s_leaving;

L::Arc s_topArc, s_arc;
L::Layout s_top, s_win, s_grid, s_name;
L::Anim s_topIn, s_topOut, s_winIn, s_winOut, s_winLoop, s_winKind, s_gridIn, s_gridOut, s_gridKind, s_nameIn, s_nameOut;
L::Anim *s_topBound, *s_winBound;
Btn s_topBtn[2];
Btn s_tabBtn[8], s_kindBtn[10], s_backBtn, s_cellBtn[3][15];
u32 s_tabCount, s_kindSlots;

Win s_win_state = Win::Closed;
u32 s_main;                                     // 1 = 家具、4 = 壁紙・床紙
const CL::Catalog *s_cat;
u32 s_tab;                                      // 窓の上段タブ（CT の通し番号の中の何番目か）
s32 s_ctTab;                                    // CT::kTabs の番号
s32 s_kind = -1;                                // 今の小分類（CT のタブの中の何番目か）
DecorSlider::Slider s_slider;
float s_pageOrigX[3], s_pageY[3];
void *s_pageNode[3];
void *s_cellNode[3][15], *s_cellIcon[3][15];   // 開くときに 1 回だけ引く（FindPane は全ペインをたどる）
u32 s_iconSlots;                                // ヒープの枠（45 × 4096）
u32 s_slotsToFree, s_slotsTicket;              // 外したが、まだ返していない枠
u16 s_applied[3][15];                           // 枠に貼った絵の HHD 品番
bool s_namesLoaded;
bool s_nameShown;
s32 s_pressCell = -1;                           // 触れているマス（frame * 15 + slot）
Hit s_press = Hit::None;
u32 s_pressIdx;
volatile bool s_touchPrev;
UIntVector s_lastTouch;
volatile bool s_topTouchCaptured;
bool s_closeAfter;                              // 決めた後に閉じる（out へ）

// ---- 論理ページ ↔ 小分類 ----
u32 TabPages(s32 ctTab) {
    if (ctTab < 0)
        return 1;
    const CT::Tab &t = CT::kTabs[ctTab];
    u32 n = 0;
    for (u32 k = 0; k < t.kindCount; ++k)
        n += CT::kKinds[t.firstKind + k].pages;
    return n == 0 ? 1 : n;
}

// 論理ページ → (小分類, 小分類の中のページ)
bool PageKind(s32 page, s32 &kind, s32 &sub) {
    if (s_ctTab < 0 || page < 0)
        return false;
    const CT::Tab &t = CT::kTabs[s_ctTab];
    s32 p = page;
    for (u32 k = 0; k < t.kindCount; ++k) {
        const s32 pages = CT::kKinds[t.firstKind + k].pages;
        if (p < pages) {
            kind = (s32)k;
            sub = p;
            return true;
        }
        p -= pages;
    }
    return false;
}

s32 KindFirstPage(s32 kind) {
    const CT::Tab &t = CT::kTabs[s_ctTab];
    s32 p = 0;
    for (s32 k = 0; k < kind && k < (s32)t.kindCount; ++k)
        p += CT::kKinds[t.firstKind + k].pages;
    return p;
}

const CT::Item *CellItem(u32 frame, u32 slot) {
    s32 kind = 0, sub = 0;
    if (!PageKind(s_slider.framePage[frame], kind, sub))
        return nullptr;
    const CT::Kind &k = CT::kKinds[CT::kTabs[s_ctTab].firstKind + kind];
    const u32 i = (u32)sub * CT::kPerPage + slot;
    return i < k.count ? &CT::kItems[k.first + i] : nullptr;
}

s32 FindCtTab(u32 main, u32 tab) {
    for (u32 i = 0; i < CT::kTabCount; ++i)
        if (CT::kTabs[i].main == main && CT::kTabs[i].tab == tab)
            return (s32)i;
    return -1;
}

// ---- 文字 ----
u32 Digits(u16 *out, u32 v) {
    char buf[12];
    u32 n = 0;
    do {
        buf[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0 && n < sizeof(buf));
    for (u32 i = 0; i < n; ++i)
        out[i] = (u16)buf[n - 1 - i];
    return n;
}

// ゲームの品名（制御タグ 0x000E は除く: 0x000E, 群, 種類, 引数のバイト数, 引数…）
u32 CleanName(u16 acnl, u16 *out, u32 cap) {
    u32 len = 0, o = 0;
    const u16 *p = s_namesLoaded ? ItemNames::NormalName(acnl, len) : nullptr;
    for (u32 i = 0; p != nullptr && i < len && o + 1 < cap;) {
        if (p[i] == 0x000E && i + 3 < len) {
            i += 4 + p[i + 3] / 2;
            continue;
        }
        out[o++] = p[i++];
    }
    return o;
}

// ---- 窓 ----
void SetPageText(void) {
    if (s_cat == nullptr || s_cat->pageNode == nullptr)
        return;
    u16 buf[8];
    const u32 total = TabPages(s_ctTab);
    s32 cur = DecorSlider::CurrentPage(s_slider);
    if (cur < 0)
        cur = 0;
    L::SetText(L::Pane(s_win, s_cat->pageCurrent), buf, Digits(buf, (u32)cur + 1));
    L::SetText(L::Pane(s_win, s_cat->pageTotal), buf, Digits(buf, total));
}

void ShowKindSelection(void) {
    s32 kind = 0, sub = 0;
    if (!PageKind(DecorSlider::CurrentPage(s_slider), kind, sub))
        return;
    if (kind == s_kind)
        return;
    if (s_kind >= 0 && (u32)s_kind < s_kindSlots)
        BtnUnselect(s_kindBtn[s_kind], s_arc, false, false);   // HHD の UnSelect（select を逆に）
    s_kind = kind;
    if ((u32)kind < s_kindSlots)
        BtnSelected(s_kindBtn[kind], s_arc, false);
}

void HideName(void) {
    if (!s_nameShown)
        return;
    s_nameShown = false;
    L::Bind(s_nameOut, s_name, CL::kNameOut.group, 0.0f, CL::kNameOut.group2);
}

// 吹き出しの位置（HHD 0x3BB514）: N_All = (マス X, マス Y + マス高/2 + 窓高/2 − 2)、窓の N のローカル X は (窓幅 + 5)/2 が ±160 を越えないように
void ShowName(u32 frame, u32 slot) {
    const CT::Item *it = CellItem(frame, slot);
    if (it == nullptr)
        return;
    u16 text[64];
    const u32 n = CleanName(it->acnl, text, 64);
    void *tx = L::Pane(s_name, CL::kName[2]), *sh = L::Pane(s_name, CL::kName[3]);
    void *win = L::Pane(s_name, CL::kName[4]), *ws = L::Pane(s_name, CL::kName[5]);
    const float textWidth = L::MeasureText(tx, text, n);
    const float shadowWidth = L::MeasureText(sh, text, n);
    // TextBox +0x48 が折返し幅になる（0x4BAD78）。影も自分の字幅で測る。
    // 文字幅より少し広い箱を中央に置き、浮動小数点の端で最後の字が次行へ行くのを防ぐ。
    L::SetSize(tx, textWidth + 1.0f, L::Height(tx));
    L::SetSize(sh, shadowWidth + 1.0f, L::Height(sh));
    L::SetText(tx, text, n);
    L::SetText(sh, text, n);
    const float w = textWidth + 32.0f;
    const float h = L::Height(win);
    L::SetSize(win, w, h);
    L::SetSize(ws, w, L::Height(ws));
    const CL::Rect &r = CL::kPages[frame].cells[slot].rect;
    const float dx = s_slider.frameX[frame] - s_pageOrigX[frame];
    const float cx = (r.l + r.r) * 0.5f + dx - 160.0f, cy = 120.0f - (r.t + r.b) * 0.5f, ch = r.b - r.t;
    L::SetPos(L::Pane(s_name, CL::kName[0]), cx, cy + ch * 0.5f + h * 0.5f - 2.0f);
    const float half = (w + 5.0f) * 0.5f;
    float lx = 0.0f;
    if (cx + half > 160.0f)
        lx = 160.0f - half - cx;
    else if (cx - half < -160.0f)
        lx = -160.0f + half - cx;
    void *node = L::Pane(s_name, CL::kName[1]);
    L::SetPos(node, lx, L::PosY(node));
    if (!s_nameShown) {
        L::Bind(s_nameIn, s_name, CL::kNameIn.group, 0.0f, CL::kNameIn.group2);
        s_nameShown = true;
    }
}

// 3 枠の位置とマスの中身・アイコン
void UpdateGrid(void) {
    const s32 total = (s32)TabPages(s_ctTab);
    for (u32 f = 0; f < 3; ++f) {
        void *node = s_pageNode[f];
        const s32 page = s_slider.framePage[f];
        const bool show = page >= 0 && page < total;
        L::SetVisible(node, show);
        L::SetPos(node, s_slider.frameX[f], s_pageY[f]);
        for (u32 s = 0; s < 15; ++s) {
            const CT::Item *it = show ? CellItem(f, s) : nullptr;
            void *cell = s_cellNode[f][s];
            void *icon = s_cellIcon[f][s];
            L::SetVisible(cell, it != nullptr);
            const u32 slot = f * 15 + s;
            const u16 want = it != nullptr ? it->hhd : 0;
            DecorIcons::Want(slot, want);
            if (want == 0) {
                s_applied[f][s] = 0;
                continue;
            }
            if (s_applied[f][s] != want) {
                const u32 va = DecorIcons::Ready(slot, want);
                if (va != 0 && L::SetTexture(icon, va, 64, 64, 11)) {
                    s_applied[f][s] = want;
                } else {
                    L::SetVisible(icon, false);         // 絵が来るまで隠す（前のページの絵を見せない）
                    continue;
                }
            }
            L::SetVisible(icon, true);
        }
    }
}

void FreeWindow(void) {
    DecorIcons::SetSlots(0);
    for (u32 i = 0; i < 8; ++i)
        BtnFree(s_tabBtn[i]);
    for (u32 i = 0; i < 10; ++i)
        BtnFree(s_kindBtn[i]);
    BtnFree(s_backBtn);
    for (u32 f = 0; f < 3; ++f)
        for (u32 s = 0; s < 15; ++s)
            BtnFree(s_cellBtn[f][s]);
    L::Anim *anims[] = { &s_winIn, &s_winOut, &s_winLoop, &s_winKind, &s_gridIn, &s_gridOut, &s_gridKind, &s_nameIn, &s_nameOut };
    for (L::Anim *a : anims)
        if (a->made)
            L::FreeAnim(*a);
    s_winBound = nullptr;
    L::Free(s_name);
    L::Free(s_grid);
    L::Free(s_win);
    L::FreeArc(s_arc);
    if (s_iconSlots != 0) {
        s_slotsToFree = s_iconSlots;            // メニューのスレッドが書き終えてから返す（FreeIconSlots）
        s_slotsTicket = DecorIcons::ReleaseTicket();
    }
    s_iconSlots = 0;
    s_cat = nullptr;
    s_nameShown = false;
    s_press = Hit::None;
    s_pressCell = -1;
}

// 外した枠を返す（メニューのスレッドが読み込みの途中でなくなってから。ゲームのスレッド）
void FreeIconSlots(bool force) {
    if (s_slotsToFree != 0 && (force || DecorIcons::CanRelease(s_slotsTicket))) {
        L::HeapFree(reinterpret_cast<void *>(s_slotsToFree));
        s_slotsToFree = 0;
    }
}

bool LoadRef(L::Anim &an, L::Arc &arc, const CL::AnimRef &r) { return r.name == nullptr || L::LoadAnim(an, arc, r.name); }

void SelectTab(u32 tab) {
    if (s_tab < s_tabCount)
        BtnRest(s_tabBtn[s_tab], s_arc);
    s_tab = tab;
    s_ctTab = FindCtTab(s_main, tab);           // 窓の上段タブの並び = HHD のタブ番号（catalog_kinds.json の tab。生成器が同じ順で書く）
    BtnSelected(s_tabBtn[tab], s_arc, true);
    // 小分類の絵（窓の上段タブごとに違う）と数
    const CL::Tab &t = s_cat->tabs[tab];
    for (u32 k = 0; k < s_kindSlots; ++k) {
        BtnRest(s_kindBtn[k], s_arc);
        const bool used = k < t.kindCount;
        L::SetVisible(L::Pane(s_win, s_kindBtn[k].def->node), used);
        if (used)
            L::SetTextureByName(L::Pane(s_win, s_kindBtn[k].def->icon), s_arc, CL::kKinds[t.kindFirst + k].icon);
    }
    s_kind = -1;
    DecorSlider::Setup(s_slider, 288.0f, (s32)TabPages(s_ctTab), 0);
    for (u32 f = 0; f < 3; ++f)
        for (u32 s = 0; s < 15; ++s) {
            s_applied[f][s] = 0;
            BtnRest(s_cellBtn[f][s], s_arc);
        }
    HideName();
    ShowKindSelection();
    SetPageText();
}

bool OpenWindow(void) {
    s_cat = &CL::kCatalogs[s_main == 1 ? 0 : 1];
    s_openStep = 2;
    s_heapLog[0] = L::HeapFreeBytes();
    s_heapMaxLog[0] = L::HeapMaxAllocBytes(0x80);
    s_heapWindowReady = 0;
    // 最大の連続領域が残っているうちに45枠を取る。小確保/arcの後では合計が足りても失敗する。
    const u32 iconBytes = DecorIcons::kSlots * DecorIcons::kIconBytes;
    if (s_slotsToFree != 0 || s_heapMaxLog[0] < iconBytes)
        return false;
    void *slots = L::HeapAlloc(iconBytes, 0x80);
    if (slots == nullptr)
        return false;
    s_iconSlots = reinterpret_cast<u32>(slots);
    s_openStep = 1;
    if (!L::LoadArc(s_arc, s_catFile, s_catSize))
        return false;
    s_heapLog[1] = L::HeapFreeBytes();
    s_heapMaxLog[1] = L::HeapMaxAllocBytes(0x80);
    if (!L::Build(s_win, s_arc, s_cat->layout, kCmdWindow, kPriWindow) || !L::Build(s_grid, s_arc, CL::kGridLayout, kCmdGrid, kPriWindow)
        || !L::Build(s_name, s_arc, CL::kNameLayout, kCmdName, kPriWindow))
        return false;
    s_heapLog[2] = L::HeapFreeBytes();
    s_heapMaxLog[2] = L::HeapMaxAllocBytes(0x80);
    if (!LoadRef(s_winIn, s_arc, s_cat->in) || !LoadRef(s_winOut, s_arc, s_cat->out) || !LoadRef(s_winLoop, s_arc, s_cat->loop)
        || !LoadRef(s_winKind, s_arc, s_cat->kind) || !LoadRef(s_gridIn, s_arc, CL::kGridIn) || !LoadRef(s_gridOut, s_arc, CL::kGridOut)
        || !LoadRef(s_gridKind, s_arc, CL::kGridKind) || !LoadRef(s_nameIn, s_arc, CL::kNameIn) || !LoadRef(s_nameOut, s_arc, CL::kNameOut))
        return false;
    s_heapLog[3] = L::HeapFreeBytes();
    s_heapMaxLog[3] = L::HeapMaxAllocBytes(0x80);
    // ボタン
    s_tabCount = s_cat->tabCount < 8 ? s_cat->tabCount : 8;
    for (u32 i = 0; i < s_tabCount; ++i)
        s_tabBtn[i] = { &s_cat->tabs[i].btn, &s_win, {}, 0 };
    s_kindSlots = s_cat->kindSlotCount < 10 ? s_cat->kindSlotCount : 10;
    for (u32 i = 0; i < s_kindSlots; ++i)
        s_kindBtn[i] = { &s_cat->kindSlots[i], &s_win, {}, 0 };
    s_backBtn = { &s_cat->back, &s_win, {}, 0 };
    for (u32 f = 0; f < 3; ++f) {
        s_pageNode[f] = L::Pane(s_grid, CL::kPages[f].node);
        s_pageOrigX[f] = L::PosX(s_pageNode[f]);
        s_pageY[f] = L::PosY(s_pageNode[f]);
        for (u32 s = 0; s < 15; ++s) {
            s_cellBtn[f][s] = { &CL::kPages[f].cells[s], &s_grid, {}, 0 };
            s_cellNode[f][s] = L::Pane(s_grid, CL::kPages[f].cells[s].node);
            s_cellIcon[f][s] = L::Pane(s_grid, CL::kPages[f].cells[s].icon);
            if (s_cellNode[f][s] == nullptr || s_cellIcon[f][s] == nullptr || s_pageNode[f] == nullptr)
                return false;                       // 表と arc が食い違う（生成し直す）
            L::SetVisible(s_cellIcon[f][s], false);
        }
    }
    // 出入り・地の縞・小分類の段
    L::Bind(s_winIn, s_win, s_cat->in.group, 0.0f, s_cat->in.group2);
    s_winBound = &s_winIn;
    if (s_cat->loop.name != nullptr)
        L::Bind(s_winLoop, s_win, s_cat->loop.group, 0.0f, s_cat->loop.group2);
    if (s_cat->kind.name != nullptr)
        L::Bind(s_winKind, s_win, s_cat->kind.group, 0.0f, s_cat->kind.group2);
    L::Bind(s_gridIn, s_grid, CL::kGridIn.group, 0.0f, CL::kGridIn.group2);
    L::Bind(s_gridKind, s_grid, CL::kGridKind.group, 0.0f, CL::kGridKind.group2);
    L::Bind(s_nameOut, s_name, CL::kNameOut.group, LastFrame(CL::kNameOut), CL::kNameOut.group2);   // 吹き出しは閉じた形で始める
    s_tab = 0xFFFFFFFFu;
    SelectTab(0);
    s_heapWindowReady = L::HeapFreeBytes();
    if (s_heapWindowReady < kWindowHeapReserve) {
        s_openStep = 3;                             // 組めてもゲーム用の余白を食い切る窓は出さず、全て返す
        return false;
    }
    DecorIcons::SetSlots(s_iconSlots);              // 全工程が通ってからメニューのスレッドへ渡す
    s_openStep = 0;
    return true;
}

// ---- 置く ----
struct PlaceCtx { Item item; int px, pz, prot; u8 record[12]; };

struct PlaceOrigin { u32 scene; u8 room; int x, z, rot; bool valid; };
PlaceOrigin s_origin;

void TrackPlacementOrigin(void) {
    const u32 scene = reinterpret_cast<u32>(*GridCursor::Game::kSceneOwner);
    const u8 room = *GridCursor::Game::kRoomId;
    if (!s_enabled || scene != s_origin.scene || room != s_origin.room) {
        s_origin = { scene, room, 0, 0, 0, false };
    }
    if (!s_enabled || scene == 0)
        return;
    const u32 indoor = IndoorBase();
    if (s_origin.valid && EditorLive(indoor, EditorCalc(indoor)))
        return;                                    // エディターの間は入る直前の実位置。非表示化後の位置で上書きしない
    void *actor = GridCursor::Game::LocalPlayer();
    if (actor == nullptr || !IsHeapPointer(reinterpret_cast<u32>(actor)))
        return;                                    // 同じ部屋で観測した起点を保持。別の部屋の座標は使わない
    int x = 0, z = 0;
    const float *pos = reinterpret_cast<const float *>(reinterpret_cast<const u8 *>(actor) + GridCursor::Game::kPlayerPositionOffset);
    PosToCell(&x, &z, pos);
    const u16 angle = *reinterpret_cast<const u16 *>(reinterpret_cast<const u8 *>(actor) + 46);
    const int rot = (int)((((u32)angle >> 12 << 28) + 0x20000000u) >> 30);
    s_origin = { scene, room, x, z, rot, true };
}

bool TryCell(void *ctx, s32 x, s32 z) {
    PlaceCtx &c = *reinterpret_cast<PlaceCtx *>(ctx);
    if (x < 0 || z < 0)
        return false;
    std::memset(c.record, 0, sizeof(c.record));
    return TryPut(c.record, x, z, 0, &c.item, 0, c.px, c.pz, c.prot, 0) == 0;   // 向き 0（HHD の品の既定）・床の層 0・パターン 0（試験 3 と同じ）
}

u32 PlaceFurniture(u16 acnl) {
    void *table = FtrTable();
    if (table == nullptr || FtrFree(table) <= 0)
        return 0;                                   // 家の家具アクター 48 が満杯（F001 / F002）
    if (!s_origin.valid || s_origin.scene != reinterpret_cast<u32>(*GridCursor::Game::kSceneOwner)
                        || s_origin.room != *GridCursor::Game::kRoomId)
        return 0;
    PlaceCtx c;
    c.item = { acnl, 0 };
    // 探す起点と「プレイヤーが占有するマス」を分ける。0x6920CCは負値なら占有チェックを省く。
    // 模様替え中に実体が消えても、直前に実際に観測した位置からHHDの順で探せる。
    const bool playerPresent = GridCursor::Game::LocalPlayer() != nullptr;
    c.px = playerPresent ? s_origin.x : -1;
    c.pz = playerPresent ? s_origin.z : -1;
    c.prot = s_origin.rot;
    DecorPlace::Bounds b;
    int minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    CellBounds(&minX, &minZ, &maxX, &maxZ);
    b = { minX, minZ, maxX, maxZ };
    if (minX > maxX || minZ > maxZ)
        return 0;
    // 入口など範囲外の実位置は、DecorPlaceでHHD 0x4D0C24〜0x4D0C4Cと同じく内側へ収める。
    // 占有チェック用のc.px/c.pzは実位置のまま。探索の起点だけを補正する。
    s32 x = 0, z = 0;
    if (!DecorPlace::Search(s_origin.x, s_origin.z, b, 2, TryCell, &c, x, z))   // 押し引き中の家具は無いので HHD と同じく方向 2（F002）
        return 0;
    return (u32)Spawn(c.record);                    // 生成した家具（0 = 拒否。失敗したら同じ決定で試し直さない。F002）
}

// ---- 出した家具のチップ（IDA-gpt-6.1-sol-F002 §2 の手順）----
typedef void *(*RecGetFn)(const s16 *index);
typedef int (*RecAliveFn)(const s16 *index);
typedef void *(*FtrFromIndexFn)(u32 *index);       // sub_4E8650: 失敗すると *index = -1（写しを渡す）
typedef void (*ChipSetupFn)(void *chip, void *resource, void *record);
typedef void (*ChipRootFn)(void *chip);
const FtrFromIndexFn FtrFromIndex = reinterpret_cast<FtrFromIndexFn>(0x004E8650);
const u32 kEditorChips = 1176, kChipStride = 1120, kChipCount = 112, kChipInUse = 1117, kChipRecord = 1032, kRecordFtrIndex = 4;
const u32 kActorDestroy = 15, kActorCreateFlags = 0x66, kActorState = 0x5F8;   // 破棄要求 / 作成の旗（bit1 = 完成）/ 状態（2 = 通常）
const u32 kPendTimeout = 600;                   // 20 秒（30 fps）待っても完成しなければやめる

struct Pending { u32 actor, editor, frames, stable; bool active; };
Pending s_pend;
u32 s_chipEditor, s_chipNext;                   // 一度も使っていないチップの枠の先頭（開いた時点の使用中の最後 + 1 から）

void ChipTrackEditor(u32 editor) {
    if (editor == s_chipEditor)
        return;
    s_chipEditor = editor;                      // 新しいエディター（開き直した）: 枠の使い方を数え直す
    s_chipNext = 0;
    if (editor == 0)
        return;
    for (u32 i = 0; i < kChipCount; ++i)
        if (*reinterpret_cast<const volatile u8 *>(editor + kEditorChips + kChipStride * i + kChipInUse) != 0)
            s_chipNext = i + 1;
}

void ChipStep(u32 indoor) {
    if (!s_pend.active)
        return;
    const u32 editor = EditorPtr(indoor), ftr = FtrBase();
    if (editor == 0 || editor != s_pend.editor || ftr == 0 || ++s_pend.frames > kPendTimeout) {
        s_pend.active = false;                  // エディターが閉じた（開き直せばゲームがチップを作る）・時間切れ
        return;
    }
    if (!EditorNeutral(indoor, EditorCalc(indoor)))
        return;
    // 窓を壊した後で追加する。窓の確保の途中へchipの小領域を挟まず、約1MiBの資源を返してから使う。
    if (s_win_state != Win::Closed || s_arc.made || s_slotsToFree != 0 || Touch::IsDown())
        return;
    if (R32(editor + 129100) != 0 || R32(editor + 128640) != 0 || R32(editor + 126796) != 0 || R32(editor + 129288) != 0)
        return;                                   // Neutralでも選択・まとめ・共通ボタン処理が残っていれば追加しない（F002）
    const u32 actor = s_pend.actor;
    if (!Process::CheckAddress(actor, MEMPERM_READ) || *reinterpret_cast<const volatile u8 *>(actor + kActorDestroy) != 0) {
        s_pend.active = false;
        return;
    }
    if ((*reinterpret_cast<const volatile u8 *>(actor + kActorCreateFlags) & 2u) == 0
        || *reinterpret_cast<const volatile u8 *>(actor + kActorState) != 2) {
        s_pend.stable = 0;
        return;
    }
    // 記録（ModuleFtr）: 生きていて +4 の家具の番号がこの家具を指すもの
    const RecGetFn recGet = reinterpret_cast<RecGetFn>(ftr + DecorTrashTable::kFtrRecGet);
    const RecAliveFn recAlive = reinterpret_cast<RecAliveFn>(ftr + DecorTrashTable::kFtrRecIsAlive);
    s16 rec = -1;
    void *recPtr = nullptr;
    for (s16 r = 0; r < (s16)kChipCount; ++r) {
        if (!recAlive(&r))
            continue;
        void *p = recGet(&r);
        u32 index = R32(reinterpret_cast<u32>(p) + kRecordFtrIndex);
        if (reinterpret_cast<u32>(FtrFromIndex(&index)) == actor) {
            rec = r;
            recPtr = p;
            break;
        }
    }
    if (rec < 0) {
        s_pend.stable = 0;
        return;
    }
    if (++s_pend.stable < 2)                    // 2 フレーム続いた = 完成後の位置の更新（AcFtr_Update の vslot+208）を通った
        return;
    // 同じ記録番号を、片付け中の古いチップがまだ持っていたら待つ（F002）
    for (u32 i = 0; i < kChipCount; ++i) {
        const u32 chip = editor + kEditorChips + kChipStride * i;
        if (*reinterpret_cast<const volatile u8 *>(chip + kChipInUse) != 0 && *reinterpret_cast<const volatile s16 *>(chip + kChipRecord) == rec)
            return;
    }
    ChipTrackEditor(editor);
    if (s_chipNext >= kChipCount) {
        s_pend.active = false;                  // 一度も使っていない枠が無い（Out 済みの枠は使わない。F002）
        return;
    }
    const u32 chip = editor + kEditorChips + kChipStride * s_chipNext;
    if (*reinterpret_cast<const volatile u8 *>(chip + kChipInUse) != 0) {
        s_pend.active = false;
        return;
    }
    using namespace DecorCatalogCro;
    if (R32(indoor + kIndoorChipSetup) != kIndoorChipSetupWord || R32(indoor + kIndoorChipRestoreOwnRoot) != kIndoorChipRestoreOwnRootWord) {
        s_pend.active = false;
        return;
    }
    reinterpret_cast<ChipSetupFn>(indoor + kIndoorChipSetup)(reinterpret_cast<void *>(chip), reinterpret_cast<void *>(editor + kEditorChipResource), recPtr);
    ++s_chipNext;
    reinterpret_cast<ChipRootFn>(indoor + kIndoorChipRestoreOwnRoot)(reinterpret_cast<void *>(chip));
    s_pend.active = false;                      // 描画リストへは次の描画の巡回でゲームが載せる
}

bool ApplyBackground(u16 acnl, u32 ctTab) {
    const Item item = { acnl, 0 };
    const bool floor = CT::kTabs[ctTab].tab == 1;   // 上段 4 のタブ 0 = 壁紙、1 = 床紙（HHD-F019）
    return (floor ? SetFloor(&item, 1) : SetWall(&item, 1)) != 0;   // ゲーム自身の音が鳴る（F106）
}

void BeginClose(void) {
    HideName();
    L::Bind(s_winOut, s_win, s_cat->out.group, 0.0f, s_cat->out.group2);
    s_winBound = &s_winOut;
    L::Bind(s_gridOut, s_grid, CL::kGridOut.group, 0.0f, CL::kGridOut.group2);
    s_win_state = Win::Closing;
    s_press = Hit::None;
    s_pressCell = -1;
}

void Decide(u32 frame, u32 slot) {
    const CT::Item *it = CellItem(frame, slot);
    if (it == nullptr || s_ctTab < 0)
        return;
    bool ok;
    if (s_main == 4) {
        ok = ApplyBackground(it->acnl, (u32)s_ctTab);
    } else {
        const u32 indoor = IndoorBase();
        const u32 editor = EditorPtr(indoor);
        ok = !s_pend.active && editor != 0 && EditorNeutral(indoor, EditorCalc(indoor));   // 前の家具のチップを待っている間は出さない
        if (ok) {
            ChipTrackEditor(editor);
            const u32 actor = PlaceFurniture(it->acnl);
            ok = actor != 0;
            if (ok)
                s_pend = { actor, editor, 0, 0, true };
        }
        if (ok)
            PlaySound(kSndCellDecide);
    }
    if (!ok) {
        PlaySound(kSndInvalid);
        BtnUnselect(s_cellBtn[frame][slot], s_arc, false, false);
        return;
    }
    BtnDecide(s_cellBtn[frame][slot], s_arc);
    BeginClose();                                   // HHD の StateID_Ok_
}

// ---- 入力（ゲームのスレッド。CTRPF の Touch / Controller は HID を直接読む）----
Hit HitTest(float x, float y, u32 &idx) {
    if (s_win_state != Win::Open) {
        for (u32 i = 0; i < 2; ++i)
            if (InRect(CL::kTopTabs[i].rect, x, y)) {
                idx = i;
                return Hit::TopTab;
            }
        return Hit::None;
    }
    for (u32 i = 0; i < s_tabCount; ++i)
        if (InRect(s_cat->tabs[i].btn.rect, x, y)) {
            idx = i;
            return Hit::Tab;
        }
    if (InRect(s_cat->back.rect, x, y)) {
        idx = 0;
        return Hit::Back;
    }
    const CL::Tab &t = s_cat->tabs[s_tab];
    for (u32 i = 0; i < s_kindSlots && i < t.kindCount; ++i)
        if (InRect(s_cat->kindSlots[i].rect, x, y)) {
            idx = i;
            return Hit::Kind;
        }
    if (InRect(CL::kGridDrag, x, y)) {
        const u32 f = s_slider.center;
        const float dx = s_slider.frameX[f] - s_pageOrigX[f];
        for (u32 s = 0; s < 15; ++s)
            if (InRect(CL::kPages[f].cells[s].rect, x, y, dx) && CellItem(f, s) != nullptr) {
                idx = f * 15 + s;
                return Hit::Cell;
            }
        idx = 0;
        return Hit::Grid;
    }
    return Hit::None;
}

Btn *PressBtn(Hit h, u32 idx) {
    switch (h) {
    case Hit::TopTab: return &s_topBtn[idx];
    case Hit::Tab: return &s_tabBtn[idx];
    case Hit::Kind: return &s_kindBtn[idx];
    case Hit::Back: return &s_backBtn;
    case Hit::Cell: return &s_cellBtn[idx / 15][idx % 15];
    default: return nullptr;
    }
}

void OnDecide(Hit h, u32 idx) {
    switch (h) {
    case Hit::TopTab:
        if ((CL::kTopMain[idx] == 4 && (!s_canChangeBackground || CanChangeBackground() == 0)) || s_pend.active) {
            PlaySound(kSndInvalid);
            BtnUnselect(s_topBtn[idx], s_topArc, false, true);   // HHDの押した→戻す。不可では決定/窓を始めない
            return;
        }
        BtnDecide(s_topBtn[idx], s_topArc);
        PlaySound(kSndOpen);
        s_main = CL::kTopMain[idx];
        s_catUploaded = false;
        s_wantCatFile = true;                       // メニューのスレッドが arc を読む
        s_win_state = Win::Loading;
        return;
    case Hit::Tab:
        PlaySound(kSndTabDecide);
        if (idx != s_tab)
            SelectTab(idx);
        BtnDecide(s_tabBtn[idx], s_arc);
        return;
    case Hit::Kind: {
        PlaySound(kSndTabDecide);
        const s32 page = KindFirstPage((s32)idx);
        DecorSlider::Setup(s_slider, 288.0f, (s32)TabPages(s_ctTab), page);
        for (u32 f = 0; f < 3; ++f)
            for (u32 s = 0; s < 15; ++s)
                s_applied[f][s] = 0;
        HideName();
        ShowKindSelection();                        // 選ばれた見た目（select の最後のコマ）
        BtnPlay(s_kindBtn[idx], s_arc, CL::kTouchOk, false);
        SetPageText();
        return;
    }
    case Hit::Back:
        PlaySound(kSndBack);
        BtnDecide(s_backBtn, s_arc);
        BeginClose();
        return;
    case Hit::Cell:
        Decide(idx / 15, idx % 15);
        return;
    default:
        return;
    }
}

u32 s_keysPrev;

void InputStep(void) {
    if (s_win_state != Win::Closed && s_win_state != Win::Open) {
        s_touchPrev = Touch::IsDown();              // 読み込み・出入りのアニメの間は受け付けない
        s_press = Hit::None;
        return;
    }
    const bool down = Touch::IsDown();
    if (down)
        s_lastTouch = Touch::GetPosition();
    const UIntVector p = s_lastTouch;                // release は最後の有効な接触点（CTRPF EventManager と同じ）
    const float x = (float)p.x, y = (float)p.y;
    if (down && s_topTouchCaptured) {
        // メニュー更新との境界でも要求を保持する。捕捉した指が外へ滑っても範囲選択へ渡さない。
        GuiMenu::CaptureGameTouchUntilRelease();
        CancelCapturedRangeTouch(IndoorBase());
    }
    if (down && !s_touchPrev) {                     // 触れた
        u32 idx = 0;
        const Hit h = HitTest(x, y, idx);
        s_press = h;
        s_pressIdx = idx;
        if (h == Hit::TopTab) {
            s_topTouchCaptured = true;
            GuiMenu::CaptureGameTouchUntilRelease();
            CancelCapturedRangeTouch(IndoorBase());
        }
        if (s_win_state == Win::Open && (h == Hit::Cell || h == Hit::Grid))
            DecorSlider::TouchStart(s_slider, x, y);
        if (Btn *b = PressBtn(h, idx)) {
            BtnPress(*b, h == Hit::TopTab ? s_topArc : s_arc);
            PlaySound(h == Hit::Cell ? kSndCellTouch : kSndTabTouch);
            if (h == Hit::Cell)
                ShowName(idx / 15, idx % 15);
        }
    } else if (down && s_press != Hit::None) {      // 触れている
        if (s_win_state == Win::Open && (s_press == Hit::Cell || s_press == Hit::Grid)) {
            if (DecorSlider::TouchMove(s_slider, x, y) && s_press == Hit::Cell) {
                BtnUnselect(s_cellBtn[s_pressIdx / 15][s_pressIdx % 15], s_arc, false, false);   // ドラッグになった: 決定しない（HHD の TouchUnSelect）
                HideName();
                s_press = Hit::Grid;
            }
        } else {
            u32 idx = 0;
            if (HitTest(x, y, idx) != s_press || idx != s_pressIdx) {   // 外れた
                // HHD の TouchUnSelect: select と touch を逆に再生して戻す（2026-10-08 実機: 外へずらして離すと押した色のまま残った。
                //   アニメを外すだけではペインの値が戻らない）
                if (s_press == Hit::Tab && s_pressIdx == s_tab)
                    BtnSelected(s_tabBtn[s_tab], s_arc, true);      // 今の窓のタブは選ばれた見た目へ
                else if (Btn *b = PressBtn(s_press, s_pressIdx))
                    BtnUnselect(*b, s_press == Hit::TopTab ? s_topArc : s_arc, s_press == Hit::Kind && (s32)s_pressIdx == s_kind,
                                s_press == Hit::TopTab);
                s_press = Hit::None;
            }
        }
    } else if (!down && s_touchPrev) {              // 離した
        const Hit h = s_press;
        s_press = Hit::None;
        if (s_win_state == Win::Open && (h == Hit::Cell || h == Hit::Grid))
            DecorSlider::TouchEnd(s_slider);
        if (h == Hit::Cell) {
            u32 idx = 0;
            if (HitTest(x, y, idx) == Hit::Cell && idx == s_pressIdx)
                OnDecide(h, s_pressIdx);
            else {
                BtnUnselect(s_cellBtn[s_pressIdx / 15][s_pressIdx % 15], s_arc, false, false);
                HideName();
            }
        } else if (h != Hit::None && h != Hit::Grid) {
            OnDecide(h, s_pressIdx);
        }
    }
    s_touchPrev = down;
    if (!down)
        s_topTouchCaptured = false;
    if (s_win_state != Win::Open) {
        s_keysPrev = Controller::GetKeysDown();
        return;
    }
    // L / R / B（押されているかを 1 回読み、押した瞬間は自前で作る。IsKeyPressed は取りこぼし・二重が出る: GuiMenu F-321）
    const u32 keys = Controller::GetKeysDown();
    const u32 pressed = keys & ~s_keysPrev;
    s_keysPrev = keys;
    if ((pressed & (u32)Key::L) && DecorSlider::RequestStep(s_slider, true))
        PlaySound(kSndPageDec);
    if ((pressed & (u32)Key::R) && DecorSlider::RequestStep(s_slider, false))
        PlaySound(kSndPageInc);
    if (pressed & (u32)Key::B) {
        PlaySound(kSndBack);
        BeginClose();
    }
}

// ---- 上段タブ（模様替え画面）----
bool BuildTop(void) {
    if (!L::LoadArc(s_topArc, s_topFile, s_topSize) || !L::Build(s_top, s_topArc, CL::kTopLayout, kCmdTop, kPriTop))
        return false;
    if (!L::LoadAnim(s_topIn, s_topArc, CL::kTopIn.name) || !L::LoadAnim(s_topOut, s_topArc, CL::kTopOut.name))
        return false;
    for (u32 i = 0; i < 2; ++i)
        s_topBtn[i] = { &CL::kTopTabs[i], &s_top, {}, 0 };
    L::Bind(s_topIn, s_top, CL::kTopIn.group, 0.0f, CL::kTopIn.group2);
    s_topBound = &s_topIn;
    for (u32 i = 0; i < 2; ++i)
        BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
    return true;
}

void FreeTop(void) {
    for (u32 i = 0; i < 2; ++i)
        BtnFree(s_topBtn[i]);
    if (s_topIn.made)
        L::FreeAnim(s_topIn);
    if (s_topOut.made)
        L::FreeAnim(s_topOut);
    s_topBound = nullptr;
    L::Free(s_top);
    L::FreeArc(s_topArc);
}

void ReleaseAll(void) {
    FreeWindow();
    FreeTop();
    s_win_state = Win::Closed;
    s_chipEditor = s_chipNext = 0;                 // 同じ番地で開き直したエディターも新しい使用歴として数え直す
    s_pend.active = false;
    s_canChangeBackground = false;
}

u32 s_closedWait;

void WindowStep(void) {
    switch (s_win_state) {
    case Win::Loading:
        // outが終わった直後の再タップでも旧資源を上書きしない。GPUの待ちとSD書込み終了を経てから作り直す。
        if (s_arc.made) {
            if (++s_closedWait >= kTeardownWaitFrames) {
                FreeWindow();
                s_closedWait = 0;
            }
            return;
        }
        FreeIconSlots(false);
        if (s_slotsToFree != 0)
            return;
        if (s_catFileFailed) {
            s_catFileFailed = false;
            PlaySound(kSndInvalid);
            s_win_state = Win::Closed;
            for (u32 i = 0; i < 2; ++i) {
                BtnRest(s_topBtn[i], s_topArc);
                BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
            }
            return;
        }
        if (s_catFile == nullptr)
            return;
        if (!OpenWindow()) {                        // 組めなかった: 返して閉じたまま（模様替えの上段タブは残す。以前は Failed にして上段タブごと作り直していた）
            const u8 why = s_openStep;
            FreeWindow();
            s_heapLog[4] = L::HeapFreeBytes();
            s_heapMaxLog[4] = L::HeapMaxAllocBytes(0x80);
            s_openFail = why != 0 ? why : 1;
            s_catUploaded = true;
            PlaySound(kSndInvalid);
            s_win_state = Win::Closed;
            for (u32 i = 0; i < 2; ++i) {
                BtnRest(s_topBtn[i], s_topArc);
                BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
            }
            return;
        }
        s_catUploaded = true;                       // ゲームのヒープへ写した。プラグインの写し（518 KB）はメニューのスレッドが返す
        s_win_state = Win::Opening;
        return;
    case Win::Opening:
        if (L::Done(s_winIn) && L::Done(s_gridIn))
            s_win_state = Win::Open;
        return;
    case Win::Closing:
        if (L::Done(s_winOut) && L::Done(s_gridOut)) {
            s_win_state = Win::Closed;
            s_wait = 0;
            for (u32 i = 0; i < 2; ++i) {
                BtnRest(s_topBtn[i], s_topArc);
                BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
            }
        }
        return;
    default:
        return;
    }
}

void DrawAll(void) {
    L::Draw(s_top, kScreenLower);
    if (s_win_state == Win::Opening || s_win_state == Win::Open || s_win_state == Win::Closing) {
        L::Draw(s_win, kScreenLower);
        L::Draw(s_grid, kScreenLower);
        L::Draw(s_name, kScreenLower);
    }
}

void StepAnims(void) {
    if (s_topBound != nullptr)
        L::Step(*s_topBound);
    for (u32 i = 0; i < 2; ++i)
        BtnStep(s_topBtn[i]);
    if (s_win_state == Win::Opening || s_win_state == Win::Open || s_win_state == Win::Closing) {
        L::Anim *anims[] = { s_winBound, &s_winLoop, &s_gridIn, &s_gridOut, &s_nameIn, &s_nameOut };
        for (L::Anim *a : anims)
            if (a != nullptr && a->bound)
                L::Step(*a);
        for (u32 i = 0; i < s_tabCount; ++i)
            BtnStep(s_tabBtn[i]);
        for (u32 i = 0; i < s_kindSlots; ++i)
            BtnStep(s_kindBtn[i]);
        BtnStep(s_backBtn);
        for (u32 f = 0; f < 3; ++f)
            for (u32 s = 0; s < 15; ++s)
                BtnStep(s_cellBtn[f][s]);
    }
}

void FrameStep(void) {
    TrackPlacementOrigin();                        // エディターがプレイヤー実体を隠す前から実位置を保持する
    switch (s_stage) {
    case Stage::Idle: {
        FreeIconSlots(false);                       // 片付けの後でまだ返していない枠
        if (!s_enabled || s_topFile == nullptr)
            return;
        const u32 indoor = IndoorBase();
        if (!EditorLive(indoor, EditorCalc(indoor)))
            return;
        s_canChangeBackground = CanChangeBackground() != 0;
        if (!BuildTop()) {
            FreeTop();
            s_stage = Stage::Failed;
            return;
        }
        s_leaving = false;
        s_touchPrev = Touch::IsDown();              // 開いた瞬間に押していた指は使わない
        s_topTouchCaptured = false;                 // 前のエディター表示の捕捉を次の指へ持ち越さない
        s_stage = Stage::Live;
        [[fallthrough]];
    }
    case Stage::Live: {
        const u32 indoor = IndoorBase();
        const bool want = s_enabled && EditorLive(indoor, EditorCalc(indoor));
        if (!want && !s_leaving) {
            s_leaving = true;
            if (s_win_state == Win::Open || s_win_state == Win::Opening)
                BeginClose();
            L::Bind(s_topOut, s_top, CL::kTopOut.group, 0.0f, CL::kTopOut.group2);
            s_topBound = &s_topOut;
        }
        if (!s_leaving)
            InputStep();
        WindowStep();
        if (s_stage != Stage::Live)
            return;
        if (EditorNeutral(indoor, EditorCalc(indoor)))
            ChipTrackEditor(EditorPtr(indoor));     // 開いて最初の Neutral（ゲームの CreateChips の直後・ゴミ箱で Out する前）で未使用の枠を数える
        ChipStep(indoor);
        FreeIconSlots(false);
        if (s_win_state == Win::Open) {
            if (DecorSlider::Step(s_slider)) {          // 滑りが終わってページが決まった
                const s32 before = s_kind;
                ShowKindSelection();
                if (before != s_kind)
                    PlaySound(kSndSlide);
                SetPageText();
            }
        }
        if (s_win_state == Win::Opening || s_win_state == Win::Open || s_win_state == Win::Closing)
            UpdateGrid();
        StepAnims();
        if (s_leaving && L::Done(s_topOut) && (s_win_state == Win::Closed || s_win_state == Win::Loading)) {
            s_stage = Stage::Teardown;                  // このフレームから描かない。壊すのは数フレーム後
            s_wait = 0;
            return;
        }
        DrawAll();
        // 閉じ終わった窓の資源を返す（描くのをやめてから数フレーム後）
        if (s_win_state == Win::Closed && s_arc.made) {
            if (++s_closedWait >= kTeardownWaitFrames) {
                FreeWindow();
                s_closedWait = 0;
            }
        } else {
            s_closedWait = 0;
        }
        return;
    }
    case Stage::Teardown:
        if (++s_wait < kTeardownWaitFrames)
            return;
        ReleaseAll();
        s_wantCatFile = false;
        s_stage = Stage::Idle;
        return;
    case Stage::Failed:
        ReleaseAll();
        s_wantCatFile = false;
        s_enabled = false;
        s_stage = Stage::Idle;
        return;
    }
}

bool LoadTop(void) {
    if (s_topFile != nullptr)
        return true;
    char path[96];
    if (!GohanFiles::CommonPath(path, sizeof(path), CL::kTopArcName))
        return false;
    u32 size = 0;
    u8 *data = GohanFiles::ReadAll(path, kMaxTopArc, size);
    if (data == nullptr)
        return false;
    s_topSize = size;
    s_topFile = data;
    return true;
}

// メニューのスレッド: 窓の arc を読む・返す
void ServiceFiles(void) {
    if (s_wantCatFile && s_catFile == nullptr && !s_catFileFailed) {
        char path[96];
        u32 size = 0;
        u8 *data = GohanFiles::CommonPath(path, sizeof(path), CL::kArcName) ? GohanFiles::ReadAll(path, kMaxCatalogArc, size) : nullptr;
        if (data == nullptr) {
            s_catFailReason = File::Exists(path) == 1 ? 2 : 1;
            s_catFileFailed = true;
            s_wantCatFile = false;
            return;
        }
        s_catSize = size;
        s_catFile = data;
    } else if (s_catFile != nullptr && (s_catUploaded || (!s_wantCatFile && s_win_state != Win::Loading))) {
        u8 *p = s_catFile;                          // ヒープへ写し終えた・窓を開くのをやめた
        s_catFile = nullptr;
        s_catSize = 0;
        s_wantCatFile = false;
        s_catUploaded = false;
        delete[] p;
    }
}

}  // namespace

void Wire(void) {
    s_index = GuiMenu::FindItem(kDecorCatalog);
}

bool Tick(int index, unsigned short) {
    if (index != s_index || index < 0)
        return false;
    if (s_stage == Stage::Live && s_win_state != Win::Closed) {
        GuiMenu::BlockGameAll();                    // 窓が出ている間はゲームへの入力を止める（HhdScreen と同じ。タッチは別に止める）
        GuiMenu::BlockGameTouch();
    }
    // ゲームのFrameStepより早く検出できた場合にも遮断する。外へずらしても指を離すまで保持。
    if (s_stage == Stage::Live && !s_leaving && s_win_state == Win::Closed && Touch::IsDown()) {
        const UIntVector p = Touch::GetPosition();
        if (s_topTouchCaptured || (!s_touchPrev && (InRect(CL::kTopTabs[0].rect, (float)p.x, (float)p.y)
                                                  || InRect(CL::kTopTabs[1].rect, (float)p.x, (float)p.y))))
            GuiMenu::CaptureGameTouchUntilRelease();
    }
    ServiceFiles();
    if (const u8 why = s_catFailReason) {            // 窓が開かなかった理由を知らせる（2026-10-07 の実機で何も出ずに開かなかった）
        s_catFailReason = 0;
        GuiNotification::NotifyRed(kDecorCatalog, why == 1 ? u8"SD の gohan/common/hhd_catalog.arc がありません。"
                                                            : u8"hhd_catalog.arc を読めません（大きさ・メモリ）。");
    }
    if (const u8 why = s_openFail) {
        s_openFail = 0;
        char msg[160];
        if (why == 2)
            std::snprintf(msg, sizeof(msg), "ゲームのメモリが足りず窓を開けません（アイコン %lu B / 連続空き %lu B）。", (unsigned long)(DecorIcons::kSlots * DecorIcons::kIconBytes),
                          (unsigned long)s_heapMaxLog[0]);
        else if (why == 3)
            std::snprintf(msg, sizeof(msg), "ゲーム用の空きを残せず窓を開けません（残り %lu B）。", (unsigned long)s_heapWindowReady);
        else
            std::snprintf(msg, sizeof(msg), "窓を組めません（空き %lu B）。", (unsigned long)s_heapLog[0]);
        GuiNotification::NotifyRed(kDecorCatalog, msg);
    }
    DecorIcons::Service(6);
    if (s_enabled)
        return true;
    if (!LoadTop() || !DecorIcons::Open()) {
        if (!s_toldFail)
            GuiNotification::NotifyRed(kDecorCatalog, u8"SD の gohan/common/ に hhd_catalog_top.arc・hhd_catalog.arc・hhd_icons.bin が要ります。");
        s_toldFail = true;
        return true;
    }
    s_namesLoaded = ItemNames::LoadNormal();
    if (!s_hooked)
        s_hooked = GridCursor::InstallFrameHook() && GridCursor::AddExtraFrameStep(FrameStep);
    if (!s_hooked) {
        if (!s_toldFail)
            GuiNotification::NotifyRed(kDecorCatalog, u8"フレームの相乗りを入れられません。");
        s_toldFail = true;
        return true;
    }
    s_toldFail = false;
    s_enabled = true;
    return true;
}

bool Disable(int index) {
    if (index != s_index || index < 0)
        return false;
    s_enabled = false;                              // ゲームのスレッドが退場させて片付ける
    return true;
}

}  // namespace DecorCatalog
