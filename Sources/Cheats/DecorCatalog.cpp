// DecorCatalog — 家の模様替えに HHD の家具・壁かけ家具・壁紙/床紙リストを足す（T022）。
// hhd_room_camera1まで利用者実機PASS。F015の操作/フェード追加は未実機。
//
// 根拠（解析リポジトリ project_v2）:
//   HHD の動き IDA-gpt-6.1-sol-HHD-F001（格子 Tum15・タブ・吹き出し・音）、HHD-F016 / F018 / F019（置く順・上段タブ・品の並び）。
//   ACNL の置き方 IDA-gpt-6.1-sol-F001 / F002、IDA-opus-5.5-F103（かざるの判定 0x6920CC と生成 0x68FA84。試験 3 で実機の即反映を確認）。
//   壁紙・床 IDA-opus-5.5-F106（HouseRoom_SetWallpaper 0x2C5FA8 / SetFlooring 0x2C6CCC）。品番 F101 / F107。
//   設計 docs/topics/t022_decorate_port_requirements.md §7。
// 流れ（HHD の写し）:
//   模様替え UI が開いている間（DecorTrash と同じ EditorLive）、上段タブを出す。押している間 select + touch、離して決定 touch_ok + select_ok。
//   決定で窓（家具 / 壁かけ家具 / 壁紙・床紙）を開く（in）。窓の上段タブ・小分類・戻る・格子（横送りは DecorSlider = HHD の PageSlider）。
//   マスに触れると select + touch と名前の吹き出し、離して決定 → 家具は空きマスへ置く（DecorPlace の順で 0x6920CC を試し、置けたら 0x68FA84）、
//   壁紙・床紙はゲーム自身の貼り替え。成功したら窓を閉じる（HHD の StateID_Ok_）。置けなければ無効音（HHD の EditError_ の代わり）。
//   B / 戻るで閉じる（out）。
//   出した家具のチップは資源のFinalize（+0x66 bit1）と記録を確認し、層0/親なしのanchorを計算して未使用枠へSetup→自身root。
//   利用者指定の差（IDA-gpt-6.1-sol-F010）: 配置演出（状態+0x5F8が0）の完了と2フレーム安定は待たない。
//   fix6: 家具生成の前に窓を隠し、GPU待ちとSDの借用終了を経て窓を返す。再開のタップは待ち中も保持する。
//   Neutral の通常の親は各チップ自身の root。共有ペインへ全チップを付け直すと既存チップも更新対象木から外れる（IDA-gpt-6.1-sol-F006、fix3）。
// スレッド: SD を読むのと入力を止めるのはメニューのスレッド（Tick）。ゲームの関数は FrameStep（ゲームのスレッド）だけ。

#include "DecorCatalog.hpp"
#include "CatalogBackdrop.hpp"
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
#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <cmath>

namespace DecorCatalog {

namespace {

using namespace CTRPluginFramework;
namespace L = DecorLayout;
namespace CL = DecorCatalogLayout;
namespace CT = DecorCatalogTable;
using Cheats::kDecorCatalog;
const char *const kNoticeTitle = u8"模様替えの品リスト"; // メニュー識別子は変えず、通知は既存字形だけで書く。

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
const PlaySoundFn  PlaySound   = reinterpret_cast<PlaySoundFn>(0x0058C7D4);    // Game_PlaySound

bool BackgroundAbiReady(void) {
    static const u32 checks[][2] = {
        { 0x00300914, 0xE59F0004 }, { 0x002F75E4, 0xE35100A5 }, { 0x002FF92C, 0xE3500004 },
        { 0x006F9728, 0xE92D4010 }, { 0x006F8F24, 0xE92D4070 }, { 0x005B3438, 0xE35000A5 },
        { 0x002FCB64, 0xE92D4010 }, { 0x002D5728, 0xE2800C02 }, { 0x002D52D4, 0xE2800C02 },
        { 0x002C55B8, 0xE92D43F0 }, { 0x002C6350, 0xE59F100C },
        { 0x004F05F4, 0xE92D4070 }, { 0x004A90FC, 0xE92D40F8 },
    };
    for (const auto &c : checks)
        if (R32(c[0]) != c[1])
            return false;
    return true;
}

// IDA-gpt-6.1-sol-F013: 書込み先の部屋と所有権を確認。家の外観は参照しない。
u32 EditableRoomData(void) {
    if (!BackgroundAbiReady())
        return 0;
    const int room = *GridCursor::Game::kRoomId;
    const u32 player = reinterpret_cast<u32 (*)(u32)>(0x002FF92C)(4);
    if (player >= 4)
        return 0;
    if (reinterpret_cast<int (*)(u32, int)>(0x002F75E4)(0x80, room)) {
        const u32 owner = reinterpret_cast<u32 (*)(int)>(0x006F9728)(room);
        if (owner != player)
            return 0;
    } else if (!reinterpret_cast<int (*)(int)>(0x005B3438)(room)) {
        return 0;                                   // ゲーム自身の別室の所有権判定（外観を読まない）
    }
    const u32 data = reinterpret_cast<u32 (*)(int)>(0x006F8F24)(room);
    return IsHeapPointer(data) ? data : 0;
}

bool OfflineBackgroundEdit(void) {
    if (reinterpret_cast<int (*)(void)>(0x00300914)() != 0)
        return false;
    // 0x300914だけでホストの通信状態を推測しない。F007の登録相手マスクも確認。
    const u32 net = R32(0x0094D644);
    if (!IsHeapPointer(net))
        return false;
    const u32 own = *reinterpret_cast<const volatile u8 *>(net + 78440);
    const u32 peers = *reinterpret_cast<const volatile u8 *>(net + 78444) & 0x0F;
    if (peers != 0 && (own >= 4 || (peers & ~(1u << own)) != 0))
        return false;
    return true;
}

bool BackgroundModelHasMaterial(u32 holder, const char *material) {
    if (!Process::CheckAddress(holder + 4, MEMPERM_READ) || !IsHeapPointer(R32(holder + 4)))
        return false;
    // TransformNodeHolderのModel型を確認してから、資源の材質辞書を引く。どちらも読取りだけ。
    const u32 model = reinterpret_cast<u32 (*)(u32)>(0x004F05F4)(holder);
    if (!IsHeapPointer(model) || !Process::CheckAddress(model + 8, MEMPERM_READ))
        return false;
    const u32 resource = R32(model + 8);
    if (!Process::CheckAddress(resource, MEMPERM_READ))
        return false;
    return reinterpret_cast<u32 (*)(const u32 *, const char *)>(0x004A90FC)(&resource, material) != 0;
}

bool BackgroundSurfaceReady(bool floor) {
    const u32 view = R32(0x00948DEC);               // BsCharRoomViewMgrの稼働中インスタンス
    if (!IsHeapPointer(view) || !Process::CheckAddress(view, MEMPERM_READ)
        || R32(view) != 0x008EC71C)
        return false;
    // Init→Applyの両経路で照合した実モデル。室内種別や外観の番号から機能を推測しない。
    if (floor)
        return BackgroundModelHasMaterial(view + 0x3834, "m_carpet");
    for (u32 side = 0; side < 4; ++side)
        if (BackgroundModelHasMaterial(view + 0x3AE8 + 0x40 * side, "m_wall"))
            return true;
    return false;
}

bool BackgroundAvailable(void) {
    return EditableRoomData() != 0 && (BackgroundSurfaceReady(false) || BackgroundSurfaceReady(true));
}

// ---- 音（HHD の音は ACNL に無いので代用。要件書 §7.4。実機で利用者に決めてもらう）----
const u32 kSndCellTouch = 0x01000399;       // SE_SYS_BTN_ACTIVE_S
const u32 kSndCellDecide = 0x0100038F;      // SE_SYS_DECIDE_L
const u32 kSndTabTouch = 0x0100046B;        // SE_SYS_CTLG_TOP_BTN_ACTIVE
const u32 kSndTabDecide = 0x0100046C;       // SE_SYS_CTLG_TOP_BTN_SELECTED
const u32 kSndKindDecide = 0x01000413;      // SE_SYS_BOOK_ICON_SELECTED (GROUP_STATIC)
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
// IDA-gpt-6.1-sol-F011: 観測73→112チップの窓後モデル173,852 B。新チップは窓返却後に作る。
// 開窓中の家具/チップ費用は重ねて積まない。アイコン枠は余裕のあるgraphicHeapを優先する。
const u32 kHeapTotalReserve = 0x20000;          // 総空き128 KiB。開窓中の遅延アニメは確保後にも再検査。
const u32 kHeapContiguousReserve = 0x20000;     // 連続128 KiB。単品Setup実測全量とUIの小さな器より大きく取る。
volatile u32 s_heapBaseline;                   // 初回の模様替え空き。診断のみ、基準の計算には使わない。
volatile u32 s_heapReserve = kHeapTotalReserve;
volatile u32 s_heapContiguousReserve = kHeapContiguousReserve;
volatile u32 s_heapFailFree, s_heapFailMax, s_heapFailRequiredFree, s_heapFailRequiredMax, s_heapFailMask, s_heapFailChips, s_heapFailKind;
u32 s_chipEditor, s_chipNext;                   // チップの確保履歴。Out後も保持するので使用中の数と区別する。
u32 HeapReserve(void) { return s_heapReserve; }
bool HeapRoom(void) { return L::HeapFreeBytes() >= HeapReserve() && L::HeapMaxAllocBytes(0x80) >= s_heapContiguousReserve; }
void RecordHeapFailure(u32 freeBytes, u32 maxBytes, u32 requiredFree, u32 requiredMax, bool graphics = false) {
    s_heapFailFree = freeBytes;
    s_heapFailMax = maxBytes;
    s_heapFailRequiredFree = requiredFree;
    s_heapFailRequiredMax = requiredMax;
    s_heapFailMask = (freeBytes < requiredFree ? 1u : 0u) | (maxBytes < requiredMax ? 2u : 0u);
    s_heapFailChips = s_chipNext <= 112 ? s_chipNext : 112;
    s_heapFailKind = graphics ? 1u : 0u;        // 0 読み込み / 1 描画命令用。通知も原因を分ける。
}

// アニメの最後のコマ。frames は ACNL の数え方（HHD の frameSize + 1。tools/hhd/clan_pack.py の ★）で、AnimStep 0x568964 は frames − 1 で止まる
float LastFrame(const CL::AnimRef &r) { return r.frames > 0 ? (float)(r.frames - 1) : 0.0f; }

// ---- 資源（メニューのスレッドが読み、ゲームのスレッドがヒープへ写す）----
u8 *s_topFile;
u32 s_topSize;
u8 *s_catFile;
u32 s_catSize;
u32 s_catRequestMain, s_catFileMain;            // 要求の公開より先にmainを公開。読む側はその要求を保持。
bool s_wantCatFile;                            // atomic。読込みを希望するか
enum CatalogFileState : u32 { FileEmpty, FileReading, FileReady, FileCopying, FileUploaded, FileFailed, FileDiscarding };
u32 s_catFileState;                            // atomic。Ready→Copyingの所有権を取ってからコピーする
volatile u32 s_frameSeq, s_frameCall, s_tickSeq; // 停止時の診断。ゲーム/メニューそれぞれの単独writer
volatile u32 s_lastPlaceActor, s_placeReject;   // 拒否: 1 pending/状態、2 未使用chip、3 actor/配置、4 ヒープ余白
volatile u32 s_heapChipLog[4];                 // Setupの前後: 合計空き/0x80整列最大空き
volatile u32 s_heapPlaceLog[4];                // PlaceFurniture前後: 合計空き/最大空き。非同期生成のピークは含めない。
struct HeapSample { u32 seq, frame, ticksLo, win, freeBytes, maxBytes, chipNext, call, graphicHeap, graphicFree, graphicMax; };
volatile HeapSample s_heapRing[16];
volatile u32 s_heapRingSeq;
u64 s_heapSampleTick;
struct ChipSample {
    u32 seq, frame, item, chip, index, beforeFree, beforeMax, setupFree, setupMax, rootFree, rootMax;
    s32 setupUsed;
};
volatile ChipSample s_heapChipSamples[16];
volatile u32 s_heapChipSeq;

u32 CatalogFileStatus(void) { return __atomic_load_n(&s_catFileState, __ATOMIC_ACQUIRE); }
void RequestCatalogFile(bool want) { __atomic_store_n(&s_wantCatFile, want, __ATOMIC_RELEASE); }
// 窓を組んだときの読み込みのヒープの空き（2026-10-08 実機: アイコンの枠 184,320 B が取れずに開かなかった。デバッガで読む）
//   [0] 組む前 [1] アイコン＋arc の後 [2] レイアウト 3 つの後 [3] アニメの後 [4] 失敗して返した後
volatile u32 s_heapLog[5];
volatile u32 s_heapMaxLog[5];                   // 同時点の0x80整列込み最大連続空き
volatile u32 s_graphicHeapLog[5], s_graphicHeapFreeLog[5], s_graphicHeapMaxLog[5]; // 前/arc後/構築後/完成/返却後
void RecordGraphicsHeap(u32 index) {
    u32 freeBytes, maxBytes;
    s_graphicHeapLog[index] = reinterpret_cast<u32>(L::GraphicsHeapStatus(freeBytes, maxBytes));
    s_graphicHeapFreeLog[index] = freeBytes;
    s_graphicHeapMaxLog[index] = maxBytes;
}
volatile u32 s_heapWindowReady;                // Bind / 最初のボタンを含めた起動完了時の残量
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
    bool releaseIdle;                            // 窓のボタン。絵の差替えなしの戻し終えたアニメを返す
};

bool BtnAnim(Btn &b, L::Arc &arc, u32 k) {
    if (b.def == nullptr || b.def->anim[k].name == nullptr)
        return false;
    if ((b.loaded & (1u << k)) == 0) {
        // 窓完成時だけの余白判定では、遅延ロードするボタンが後で食い切れる。
        if (L::HeapFreeBytes() < HeapReserve() + 0x1000)
            return false;
        if (!L::LoadAnim(b.an[k], arc, b.def->anim[k].name))
            return false;
        if (!HeapRoom()) {
            L::FreeAnim(b.an[k]);              // 未Bindの確保を返す。遅延ロードでも同じ余白を保つ。
            return false;
        }
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
    if (b.loaded & (1u << k)) {
        if (b.releaseIdle) {
            L::FreeAnim(b.an[k]);
            b.loaded &= (u8)~(1u << k);
        } else {
            L::Unbind(b.an[k]);
        }
    }
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
        if (BtnPlay(b, arc, k, false)) {
            L::Hold(b.an[k]);
            if (b.releaseIdle) {
                L::ApplyFrame(b.an[k]);          // SetFrameだけでは値の適用が未済。LayoutCalc先頭の評価だけを行う
                BtnStop(b, k);                  // CLPA/CLVC/CLMCの値はUnbind後も残る
            }
        }
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
        if (b.releaseIdle) {
            L::ApplyFrame(b.an[CL::kTouch]);
            BtnStop(b, CL::kTouch);
            if (!b.keepSelect) {
                L::ApplyFrame(b.an[CL::kSelect]);
                BtnStop(b, CL::kSelect);
            }
        }
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

bool s_enabled;                               // atomic。SD初期化済みの公開も兼ねる
bool Enabled(void) { return __atomic_load_n(&s_enabled, __ATOMIC_ACQUIRE); }
void SetEnabled(bool on) { __atomic_store_n(&s_enabled, on, __ATOMIC_RELEASE); }
bool s_hooked, s_toldFail;
int s_index = -1;
Stage s_stage = Stage::Idle;
u32 s_wait, s_closedWait;
bool s_leaving;

L::Arc s_topArc, s_arc;
L::Layout s_top, s_win, s_grid, s_name;
L::Anim s_topIn, s_topOut, s_winIn, s_winOut, s_winLoop, s_winKind, s_gridIn, s_gridOut, s_gridKind, s_nameIn, s_nameOut;
L::Anim *s_topBound, *s_winBound;
Btn s_topBtn[CL::kTopCount];
Btn s_tabBtn[8], s_kindBtn[10], s_backBtn, s_cellBtn[3][15];
u32 s_tabCount, s_kindSlots;

Win s_win_state = Win::Closed;
u32 s_main;                                     // 1 = 家具、2 = 壁にかける、4 = 壁紙・床紙
u32 s_lastMain = 1;                             // Last successfully opened list; survives editor exit.
u32 s_backdropFade = 6;                         // 0 hidden .. 6 fully visible.
const CL::Catalog *s_cat;
u32 s_tab;                                      // 窓の上段タブ（CT の通し番号の中の何番目か）
s32 s_ctTab;                                    // CT::kTabs の番号
s32 s_kind = -1;                                // 今の小分類（CT のタブの中の何番目か）
DecorSlider::Slider s_slider;
struct WindowPosition { u32 tab; s32 kind; float pos; bool valid; };
WindowPosition s_position[CL::kCatalogCount];    // 各mainの位置。窓は共有せず、退場で位置を消す

u32 CatalogIndex(u32 main) {
    for (u32 i = 0; i < CL::kCatalogCount; ++i)
        if (CL::kCatalogs[i].main == main)
            return i;
    return CL::kCatalogCount;
}

void SavePosition(void) {
    if (s_cat != nullptr && s_ctTab >= 0 && CatalogIndex(s_main) < CL::kCatalogCount)
        s_position[CatalogIndex(s_main)] = { s_tab, s_kind, s_slider.pos, true };
}
float s_pageOrigX[3], s_pageY[3];
void *s_pageNode[3];
void *s_cellNode[3][15], *s_cellIcon[3][15];   // 開くときに 1 回だけ引く（FindPane は全ペインをたどる）
u32 s_iconSlots;                                // ヒープの可視枠プール（DecorIcons::kSlots × kIconBytes）
void *s_iconHeap, *s_slotsHeapToFree;            // 確保したヒープを保持。遅延返却も同じ所有者へ。
u32 s_slotsToFree, s_slotsTicket;              // 外したが、まだ返していない枠
u16 s_applied[3][15];                           // 枠に貼った絵の HHD 品番
s32 s_iconOwner[DecorIcons::kSlots];             // プールの所有論理セル。-1は空き。
u16 s_iconId[DecorIcons::kSlots];                // 読込み中の要求も所有する（applied==0で返さない）。
u32 s_iconRetired[DecorIcons::kSlots];           // 最後に非表示にしたフレーム（GPU待ち後に再利用）。
bool s_namesLoaded;
bool s_nameShown;
s32 s_pressCell = -1;                           // 触れているマス（frame * 15 + slot）
Hit s_press = Hit::None;
u32 s_pressIdx;
volatile bool s_touchPrev;
UIntVector s_lastTouch;
volatile bool s_topTouchCaptured;
bool s_closeAfter;                              // 決めた後に閉じる（out へ）
bool s_reclaimWindow;                           // 既存の余白を失ったら、非表示の窓をGPU待ち後に返す

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
    // TextBox +0x48 が折返し幅になる（0x4BAD78）。現行で非表示だった文字影は生成しない。
    // 文字幅より少し広い箱を中央に置き、浮動小数点の端で最後の字が次行へ行くのを防ぐ。
    L::SetSize(tx, textWidth + 1.0f, L::Height(tx));
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
    u16 wanted[45] = {};
    // 先に全マスの可視性を確定し、非表示にした枠はGPU待ち後にだけ再利用する。
    for (u32 f = 0; f < 3; ++f)
        for (u32 s = 0; s < 15; ++s) {
            const CL::Rect &r = CL::kPages[f].cells[s].rect;
            const float dx = s_slider.frameX[f] - s_pageOrigX[f];
            const CT::Item *it = CellItem(f, s);
            if (s_slider.framePage[f] >= 0 && s_slider.framePage[f] < total && it != nullptr
                && r.r + dx + CL::kGridIconOverhang > CL::kGridDrag.l
                && r.l + dx - CL::kGridIconOverhang < CL::kGridDrag.r)
                wanted[f * 15 + s] = it->hhd;
        }
    for (u32 i = 0; i < DecorIcons::kSlots; ++i) {
        const s32 cell = s_iconOwner[i];
        if (cell >= 0 && (wanted[cell] == 0 || s_iconId[i] != wanted[cell])) {
            L::SetVisible(s_cellIcon[cell / 15][cell % 15], false);
            s_applied[cell / 15][cell % 15] = 0;
            s_iconOwner[i] = -1;
            s_iconRetired[i] = s_frameSeq;
            DecorIcons::Want(i, 0);
        }
    }
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
            const s32 logical = (s32)(f * 15 + s);
            const u16 want = wanted[logical];
            if (want == 0) {
                s_applied[f][s] = 0;
                L::SetVisible(icon, false);
                continue;
            }
            u32 pool = DecorIcons::kSlots;
            for (u32 i = 0; i < DecorIcons::kSlots; ++i)
                if (s_iconOwner[i] == logical) { pool = i; break; }
            if (pool == DecorIcons::kSlots)
                for (u32 i = 0; i < DecorIcons::kSlots; ++i)
                    if (s_iconOwner[i] < 0 && (u32)(s_frameSeq - s_iconRetired[i]) >= kTeardownWaitFrames) {
                        pool = i;
                        s_iconOwner[i] = logical;
                        s_iconId[i] = want;
                        break;
                    }
            if (pool == DecorIcons::kSlots) { L::SetVisible(icon, false); continue; }
            DecorIcons::Want(pool, want);
            if (s_applied[f][s] != want) {
                const u32 va = DecorIcons::Ready(pool, want);
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
        s_slotsHeapToFree = s_iconHeap;
        s_slotsTicket = DecorIcons::ReleaseTicket();
    }
    s_iconSlots = 0;
    s_iconHeap = nullptr;
    s_cat = nullptr;
    s_nameShown = false;
    s_reclaimWindow = false;
    s_press = Hit::None;
    s_pressCell = -1;
    RecordGraphicsHeap(4);                     // アイコンは遅延返却前。返した時に同じ欄を更新する。
}

// 外した枠を返す（メニューのスレッドが読み込みの途中でなくなってから。ゲームのスレッド）
void FreeIconSlots(bool force) {
    if (s_slotsToFree != 0 && (force || DecorIcons::CanRelease(s_slotsTicket))) {
        L::HeapFreeFrom(s_slotsHeapToFree, reinterpret_cast<void *>(s_slotsToFree));
        s_slotsToFree = 0;
        s_slotsHeapToFree = nullptr;
        s_heapLog[4] = L::HeapFreeBytes();
        s_heapMaxLog[4] = L::HeapMaxAllocBytes(0x80);
        RecordGraphicsHeap(4);
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
    DecorSlider::SetupFree(s_slider, 288.0f, (s32)TabPages(s_ctTab), 0.0f);
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
    const u32 catalog = CatalogIndex(s_main);
    if (catalog == CL::kCatalogCount)
        return false;
    s_cat = &CL::kCatalogs[catalog];
    s_openStep = 2;
    s_heapLog[0] = L::HeapFreeBytes();
    s_heapMaxLog[0] = L::HeapMaxAllocBytes(0x80);
    RecordGraphicsHeap(0);
    s_heapFailChips = s_chipNext <= 112 ? s_chipNext : 112;
    s_heapWindowReady = 0;
    // 閉窓で返す大きな塊を末尾側へ集める。ゲーム側の小さな器の確保先は変えない。
    const u32 iconBytes = DecorIcons::kSlots * DecorIcons::kIconBytes;
    if (s_slotsToFree != 0)
        return false;
    // 命令器3つ（各queue 28 B）と管理/整列用4 KiBを残せる時だけ、アイコンを別FCRAMへ。
    void *slots = L::GraphicsAllocTail(iconBytes, 0x80, kCmdWindow + kCmdGrid + kCmdName + 0x1000, s_iconHeap);
    const u32 requiredFree = HeapReserve() + (slots == nullptr ? iconBytes : 0) + s_catSize;
    if (s_heapLog[0] < requiredFree || s_heapMaxLog[0] < s_heapContiguousReserve) {
        s_openStep = 3;
        s_heapWindowReady = s_heapLog[0];
        RecordHeapFailure(s_heapLog[0], s_heapMaxLog[0], requiredFree, s_heapContiguousReserve);
        L::HeapFreeFrom(s_iconHeap, slots);
        s_iconHeap = nullptr;
        return false;
    }
    if (slots == nullptr && s_heapMaxLog[0] >= iconBytes) {
        s_iconHeap = L::HeapOwner();
        slots = L::HeapAllocTail(iconBytes, 0x80);
    }
    if (slots == nullptr)
        return false;
    s_iconSlots = reinterpret_cast<u32>(slots);
    for (u32 i = 0; i < DecorIcons::kSlots; ++i) {
        s_iconOwner[i] = -1;
        s_iconRetired[i] = s_frameSeq - kTeardownWaitFrames;
    }
    s_openStep = 1;
    if (!L::LoadArc(s_arc, s_catFile, s_catSize, true))
        return false;
    s_heapLog[1] = L::HeapFreeBytes();
    s_heapMaxLog[1] = L::HeapMaxAllocBytes(0x80);
    RecordGraphicsHeap(1);
    if (!L::Build(s_win, s_arc, s_cat->layout, kCmdWindow, kPriWindow) || !L::Build(s_grid, s_arc, CL::kGridLayout, kCmdGrid, kPriWindow)
        || !L::Build(s_name, s_arc, CL::kNameLayout, kCmdName, kPriWindow))
        return false;
    s_heapLog[2] = L::HeapFreeBytes();
    s_heapMaxLog[2] = L::HeapMaxAllocBytes(0x80);
    RecordGraphicsHeap(2);
    if (!LoadRef(s_winIn, s_arc, s_cat->in) || !LoadRef(s_winOut, s_arc, s_cat->out) || !LoadRef(s_winLoop, s_arc, s_cat->loop)
        || !LoadRef(s_winKind, s_arc, s_cat->kind) || !LoadRef(s_gridIn, s_arc, CL::kGridIn) || !LoadRef(s_gridOut, s_arc, CL::kGridOut)
        || !LoadRef(s_gridKind, s_arc, CL::kGridKind) || !LoadRef(s_nameIn, s_arc, CL::kNameIn) || !LoadRef(s_nameOut, s_arc, CL::kNameOut))
        return false;
    s_heapLog[3] = L::HeapFreeBytes();
    s_heapMaxLog[3] = L::HeapMaxAllocBytes(0x80);
    // ボタン
    s_tabCount = s_cat->tabCount < 8 ? s_cat->tabCount : 8;
    for (u32 i = 0; i < s_tabCount; ++i)
        s_tabBtn[i] = { &s_cat->tabs[i].btn, &s_win, {}, 0, 0, false, nullptr, true };
    s_kindSlots = s_cat->kindSlotCount < 10 ? s_cat->kindSlotCount : 10;
    for (u32 i = 0; i < s_kindSlots; ++i)
        s_kindBtn[i] = { &s_cat->kindSlots[i], &s_win, {}, 0, 0, false, nullptr, true };
    s_backBtn = { &s_cat->back, &s_win, {}, 0, 0, false, nullptr, true };
    for (u32 f = 0; f < 3; ++f) {
        s_pageNode[f] = L::Pane(s_grid, CL::kPages[f].node);
        s_pageOrigX[f] = L::PosX(s_pageNode[f]);
        s_pageY[f] = L::PosY(s_pageNode[f]);
        for (u32 s = 0; s < 15; ++s) {
            s_cellBtn[f][s] = { &CL::kPages[f].cells[s], &s_grid, {}, 0 };
            s_cellBtn[f][s].releaseIdle = true;
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
    const WindowPosition &saved = s_position[catalog];
    SelectTab(saved.valid && saved.tab < s_tabCount ? saved.tab : 0);
    if (saved.valid) {
        DecorSlider::RestorePosition(s_slider, saved.pos);
        ShowKindSelection();
        SetPageText();
    }
    s_heapWindowReady = L::HeapFreeBytes();
    const u32 readyMax = L::HeapMaxAllocBytes(0x80);
    RecordGraphicsHeap(3);
    if (s_iconHeap != L::HeapOwner() && !L::GraphicsHeapRoom(s_iconHeap)) {
        s_openStep = 3;                         // 命令器を組んだ後に描画余白を割ったら窓を返す。
        RecordHeapFailure(s_graphicHeapFreeLog[3], s_graphicHeapMaxLog[3], L::kGraphicsTotalReserve, L::kGraphicsContiguousReserve, true);
        return false;
    }
    if (s_heapWindowReady < HeapReserve() || readyMax < s_heapContiguousReserve) {
        s_openStep = 3;                             // 組めてもゲーム用の余白を食い切る窓は出さず、全て返す
        RecordHeapFailure(s_heapWindowReady, readyMax, HeapReserve(), s_heapContiguousReserve);
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
    if (!Enabled() || scene != s_origin.scene || room != s_origin.room) {
        s_origin = { scene, room, 0, 0, 0, false };
    }
    if (!Enabled() || scene == 0)
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

// HHD 0x4F3850→0x15330CとACNL 0x765620→0x1A1910は同じ向き別の壁alphaを読む。
// S0戻りの関数をsoftfp ABIで誤呼出しせず、確認済みのcamera欄を読む。
u32 PreferredWalls(void) {
    const u32 camera = R32(0x0094A880);
    if (!IsHeapPointer(camera) || !Process::CheckAddress(camera + 0x68, MEMPERM_READ))
        return 0;                                   // HHDのcamera無しと同じ: 先に残り8候補を有効にする
    const u32 offsets[4] = { 0x68, 0x60, 0x58, 0x50 }; // rot0/1/2/3。元の角度0/0x4000/0x8000/0xC000に対応
    u32 mask = 0;
    for (u32 rot = 0; rot < 4; ++rot)
        if (*reinterpret_cast<const volatile float *>(camera + offsets[rot]) > 0.0f)
            mask |= 1u << rot;
    return mask;
}

bool TryWallCell(void *ctx, s32 x, s32 z, u32 rot) {
    PlaceCtx &c = *reinterpret_cast<PlaceCtx *>(ctx);
    if (x < 0 || z < 0)
        return false;
    std::memset(c.record, 0, sizeof(c.record));
    // 壁も層0。0x692230..0x692640が壁属性・向き・空き・隣接家具を検査。
    return TryPut(c.record, x, z, rot, &c.item, 0, c.px, c.pz, c.prot, 0) == 0;
}

u32 PlaceFurniture(u16 acnl, bool wall) {
    void *table = FtrTable();
    if (table == nullptr || FtrFree(table) <= 0)
        return 0;                                   // 家の家具アクター 48 が満杯（F001 / F002）
    if (!s_origin.valid || s_origin.scene != reinterpret_cast<u32>(*GridCursor::Game::kSceneOwner)
                        || s_origin.room != *GridCursor::Game::kRoomId)
        return 0;
    PlaceCtx c;
    c.item = { acnl, 0 };
    // IDA-gpt-6.1-sol-F013: 壁配置の機能はTryWallCell→TryPutの実マップ属性で判定する。
    // 0x5B3AC4の外観による事前拒否を使わず、壁・向き・空き・全footprintをゲームに確認する。
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
    u32 rot = 0;
    const bool found = wall ? DecorPlace::SearchWall(s_origin.x, s_origin.z, b, PreferredWalls(), TryWallCell, &c, x, z, rot)
                            : DecorPlace::Search(s_origin.x, s_origin.z, b, 2, TryCell, &c, x, z); // 床の順はF002のまま
    if (!found)
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
u16 s_chipItem;
struct Decision { u16 item; u32 main, ctTab, editor, frames; bool active; };
Decision s_decision;

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
    if (R32(editor + 129100) != 0 || R32(editor + 128640) != 0 || R32(editor + 126796) != 0 || R32(editor + 129288) != 0)
        return;                                   // Neutralでも選択・まとめ・共通ボタン処理が残っていれば追加しない（F002）
    const u32 actor = s_pend.actor;
    if (!Process::CheckAddress(actor, MEMPERM_READ) || *reinterpret_cast<const volatile u8 *>(actor + kActorDestroy) != 0) {
        s_pend.active = false;
        return;
    }
    if ((*reinterpret_cast<const volatile u8 *>(actor + kActorCreateFlags) & 2u) == 0) {
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
    // 資源のFinalize完了と配置演出（状態0）は別。壁もrecord+7→actor+1972の層0、親なし。
    // actor状態2や窓のout完了を待たず、次の通常Submitからチップを描かせる。
    if (*reinterpret_cast<const volatile u8 *>(actor + 1972) != 0
        || *reinterpret_cast<const volatile s16 *>(actor + 1548) != -1)
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
    if (R32(indoor + kIndoorChipSetup) != kIndoorChipSetupWord || R32(indoor + kIndoorChipRestoreOwnRoot) != kIndoorChipRestoreOwnRootWord
        || R32(0x00751220) != 0xE92D43F0) {
        s_pend.active = false;
        return;
    }
    if (!HeapRoom()) {
        s_reclaimWindow = s_arc.made;           // Chip_SetupにはOOM戻り契約がない。隠した窓を先に返す
        s_placeReject = 4;
        // 非同期生成の一時ピークなら既存のpending期限内で回復を待つ。
        // 窓を返し終えたという理由だけで、出した家具のチップ待ちを取り消さない。
        return;
    }
    s_placeReject = 0;
    s_frameCall = 32;
    u16 yawDelta = 0;
    reinterpret_cast<void (*)(void *, void *, const float *, u16 *, int)>(0x00751220)
        (reinterpret_cast<void *>(actor), reinterpret_cast<void *>(actor + 1352), nullptr, &yawDelta, 0);
    s_heapChipLog[0] = L::HeapFreeBytes();
    s_heapChipLog[1] = L::HeapMaxAllocBytes(0x80);
    s_heapChipLog[2] = s_heapChipLog[3] = 0;
    volatile ChipSample &sample = s_heapChipSamples[s_heapChipSeq % 16];
    sample.seq = 0;                             // seqを最後に入れた完了レコードだけ読む。
    sample.frame = s_frameSeq;
    sample.item = s_chipItem;
    sample.chip = chip;
    sample.index = s_chipNext;
    sample.beforeFree = s_heapChipLog[0];
    sample.beforeMax = s_heapChipLog[1];
    s_frameCall = 33;
    reinterpret_cast<ChipSetupFn>(indoor + kIndoorChipSetup)(reinterpret_cast<void *>(chip), reinterpret_cast<void *>(editor + kEditorChipResource), recPtr);
    sample.setupFree = L::HeapFreeBytes();
    sample.setupMax = L::HeapMaxAllocBytes(0x80);
    sample.setupUsed = (s32)sample.beforeFree - (s32)sample.setupFree;
    ++s_chipNext;
    s_frameCall = 34;
    reinterpret_cast<ChipRootFn>(indoor + kIndoorChipRestoreOwnRoot)(reinterpret_cast<void *>(chip));
    s_heapChipLog[2] = L::HeapFreeBytes();
    s_heapChipLog[3] = L::HeapMaxAllocBytes(0x80);
    sample.rootFree = s_heapChipLog[2];
    sample.rootMax = s_heapChipLog[3];
    sample.seq = s_heapChipSeq + 1;
    ++s_heapChipSeq;
    s_frameCall = 35;
    s_pend.active = false;                      // 描画リストへは次の描画の巡回でゲームが載せる
}

bool ApplyBackground(u16 acnl, u32 ctTab) {
    const Item item = { acnl, 0 };
    const bool floor = CT::kTabs[ctTab].tab == 1;   // 上段 4 のタブ 0 = 壁紙、1 = 床紙（HHD-F019）
    const u32 data = EditableRoomData();
    if (data == 0 || !BackgroundSurfaceReady(floor)
        || reinterpret_cast<int (*)(const Item *)>(0x002FCB64)(&item) != (floor ? 4 : 3))
        return false;
    // 通常setterの通信・音を先に使う。外観の制約で拒否されても、機能がある室内は下位setterへ。
    if ((floor ? SetFloor(&item, 1) : SetWall(&item, 1)) != 0)
        return true;
    if (!OfflineBackgroundEdit())
        return false;
    // HouseRoom setterの自宅枝と同じItem/variant書込み。オフライン限定なのでNet片は送らない。
    reinterpret_cast<void (*)(void *, const Item *)>(floor ? 0x002D5728 : 0x002D52D4)(reinterpret_cast<void *>(data), &item);
    *reinterpret_cast<volatile u8 *>(data + (floor ? 33 : 32)) = 0;
    PlaySound(floor ? 0x0100094F : 0x0100094E);
    return true;                                   // F106の毎フレーム読取り/Applyが画面へ反映
}

void BeginClose(bool immediate = false) {
    SavePosition();
    DecorSlider::RestorePosition(s_slider, s_slider.pos); // 慣性も止め、閉じた瞬間の位置を保持
    HideName();
    L::Bind(s_winOut, s_win, s_cat->out.group, 0.0f, s_cat->out.group2);
    s_winBound = &s_winOut;
    L::Bind(s_gridOut, s_grid, CL::kGridOut.group, 0.0f, CL::kGridOut.group2);
    s_win_state = Win::Closing;
    s_press = Hit::None;
    s_pressCell = -1;
    if (immediate) {
        // 家具決定時は次のSubmitからチップを見せて掴ませる。資源はGPU待ち後に返す。
        L::SetFrame(s_winOut, LastFrame(s_cat->out));
        L::SetFrame(s_gridOut, LastFrame(CL::kGridOut));
        L::Hold(s_winOut);
        L::Hold(s_gridOut);
        s_win_state = Win::Closed;
        for (u32 f = 0; f < 3; ++f)
            for (u32 s = 0; s < 15; ++s)
                BtnRest(s_cellBtn[f][s], s_arc);
        for (u32 i = 0; i < CL::kTopCount; ++i) {
            BtnRest(s_topBtn[i], s_topArc);
            BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
        }
    }
}

void Decide(u32 frame, u32 slot) {
    const CT::Item *it = CellItem(frame, slot);
    if (it == nullptr || s_ctTab < 0)
        return;
    if (s_decision.active || s_pend.active) {
        PlaySound(kSndInvalid);
        return;
    }
    // 家具/壁床の生成は、窓とGPU/SDの借用を返し終えた後に進める。
    // arc上の品ポインタを保持せずIDとtabを写す。再開要求はLoadingで保持する。
    s_decision = { it->acnl, s_main, (u32)s_ctTab, EditorPtr(IndoorBase()), 0, true };
    BtnDecide(s_cellBtn[frame][slot], s_arc);
    BeginClose(s_main == 1 || s_main == 2);
}

void DecisionStep(void) {
    if (!s_decision.active || s_arc.made || s_slotsToFree != 0)
        return;
    const u32 indoor = IndoorBase(), editor = EditorPtr(indoor);
    if (s_leaving || editor == 0 || editor != s_decision.editor || ++s_decision.frames > kPendTimeout) {
        s_decision.active = false;
        return;
    }
    if (!EditorNeutral(indoor, EditorCalc(indoor)))
        return;
    s_decision.active = false;
    bool ok;
    if (!HeapRoom()) {
        s_placeReject = 4;
        PlaySound(kSndInvalid);
        return;
    }
    if (s_decision.main == 4) {
        ok = ApplyBackground(s_decision.item, s_decision.ctTab);
    } else {
        ok = !s_pend.active && editor != 0 && EditorNeutral(indoor, EditorCalc(indoor));   // 前の家具のチップを待っている間は出さない
        s_placeReject = ok ? 0 : 1;
        if (ok) {
            ChipTrackEditor(editor);
            // Out済み枠を再利用できないため、生成前に未使用枠を確保できることを確かめる。
            // 枠が尽きてからSpawnすると、チップの無い家具を1件増やしてしまう。
            ok = s_chipNext < kChipCount
                && *reinterpret_cast<const volatile u8 *>(editor + kEditorChips + kChipStride * s_chipNext + kChipInUse) == 0;
            if (!ok)
                s_placeReject = 2;
        }
        if (ok) {
            s_heapPlaceLog[0] = L::HeapFreeBytes();
            s_heapPlaceLog[1] = L::HeapMaxAllocBytes(0x80);
            s_frameCall = 21;
            const u32 actor = PlaceFurniture(s_decision.item, s_decision.main == 2);
            s_heapPlaceLog[2] = L::HeapFreeBytes();
            s_heapPlaceLog[3] = L::HeapMaxAllocBytes(0x80);
            s_lastPlaceActor = actor;
            ok = actor != 0;
            s_placeReject = ok ? 0 : 3;
            if (ok)
                s_pend = { actor, editor, 0, 0, true };
            s_chipItem = s_decision.item;
            if (ok)
                ChipStep(indoor);              // 同期Finalize済みなら、この配置のフレームから追加
        }
        if (ok)
            PlaySound(kSndCellDecide);
    }
    if (!ok) {
        PlaySound(kSndInvalid);
        return;
    }
}

// ---- 入力（ゲームのスレッド。CTRPF の Touch / Controller は HID を直接読む）----
Hit HitTest(float x, float y, u32 &idx) {
    if (s_win_state != Win::Open) {
        if (s_win_state != Win::Closed || s_backdropFade == 0)
            return Hit::None;
        for (u32 i = 0; i < CL::kTopCount; ++i)
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
        for (u32 f = 0; f < 3; ++f) {
            const float dx = s_slider.frameX[f] - s_pageOrigX[f];
            for (u32 s = 0; s < 15; ++s)
                if (InRect(CL::kPages[f].cells[s].rect, x, y, dx) && CellItem(f, s) != nullptr) {
                    idx = f * 15 + s;
                    return Hit::Cell;
                }
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
        if (CL::kTopMain[idx] == 4 && !BackgroundAvailable()) {
            PlaySound(kSndInvalid);
            BtnUnselect(s_topBtn[idx], s_topArc, false, true);   // HHDの押した→戻す。不可では決定/窓を始めない
            return;
        }
        BtnDecide(s_topBtn[idx], s_topArc);
        PlaySound(kSndOpen);
        s_main = CL::kTopMain[idx];
        if (s_win_state == Win::Closing)
            s_closedWait = 0;                      // このフレームから描かない。ここからGPU待ち。
        __atomic_store_n(&s_catRequestMain, s_main, __ATOMIC_RELEASE);
        RequestCatalogFile(true);                   // 前回Uploadedの返却が終わるまでは再利用しない
        s_win_state = Win::Loading;
        return;
    case Hit::Tab:
        PlaySound(kSndTabDecide);
        if (idx != s_tab)
            SelectTab(idx);
        BtnDecide(s_tabBtn[idx], s_arc);
        return;
    case Hit::Kind: {
        PlaySound(kSndKindDecide);
        const s32 page = KindFirstPage((s32)idx);
        DecorSlider::SetupFree(s_slider, 288.0f, (s32)TabPages(s_ctTab), -288.0f * (float)page);
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
    const u32 keys = Controller::GetKeysDown();
    const u32 pressed = keys & ~s_keysPrev;
    s_keysPrev = keys;
    if (GuiMenu::IsVisible()) {
        s_touchPrev = Touch::IsDown();
        s_press = Hit::None;
        return;
    }
    if (s_win_state == Win::Closed && (pressed & (u32)Key::X)) {
        for (u32 i = 0; i < CL::kTopCount; ++i)
            if (CL::kTopMain[i] == s_lastMain) {
                OnDecide(Hit::TopTab, i);
                break;
            }
    }
    if (s_win_state != Win::Closed && s_win_state != Win::Open && s_win_state != Win::Closing) {
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
        return;
    }
    // L / R / B（押されているかを 1 回読み、押した瞬間は自前で作る。IsKeyPressed は取りこぼし・二重が出る: GuiMenu F-321）
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
    for (u32 i = 0; i < CL::kTopCount; ++i)
        s_topBtn[i] = { &CL::kTopTabs[i], &s_top, {}, 0 };
    L::Bind(s_topIn, s_top, CL::kTopIn.group, 0.0f, CL::kTopIn.group2);
    s_topBound = &s_topIn;
    for (u32 i = 0; i < CL::kTopCount; ++i)
        BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
    return true;
}

void FreeTop(void) {
    for (u32 i = 0; i < CL::kTopCount; ++i)
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
    CatalogBackdrop::Reset();
    s_backdropFade = 6;
    FreeWindow();
    FreeTop();
    s_win_state = Win::Closed;
    s_chipEditor = s_chipNext = 0;                 // 同じ番地で開き直したエディターも新しい使用歴として数え直す
    s_pend.active = false;
    s_decision.active = false;
    std::memset(s_position, 0, sizeof(s_position));
}

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
        if (s_leaving)
            return;
        if (s_decision.active || s_pend.active)
            return;                               // ゲームの家具/チップ確保を先に終える。
        if (CatalogFileStatus() == FileEmpty)
            RequestCatalogFile(true);
        if (CatalogFileStatus() == FileFailed) {
            RequestCatalogFile(false);
            __atomic_store_n(&s_catFileState, (u32)FileEmpty, __ATOMIC_RELEASE);
            PlaySound(kSndInvalid);
            s_win_state = Win::Closed;
            for (u32 i = 0; i < CL::kTopCount; ++i) {
                BtnRest(s_topBtn[i], s_topArc);
                BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
            }
            return;
        }
        u32 expected;
        expected = FileReady;
        if (!__atomic_compare_exchange_n(&s_catFileState, &expected, (u32)FileCopying, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
        if (s_catFileMain != s_main) {
            __atomic_store_n(&s_catFileState, (u32)FileUploaded, __ATOMIC_RELEASE);
            return;                               // 前のmainの読込み。返却して今回の要求を読み直す。
        }
        s_frameCall = 31;
        if (!OpenWindow()) {                        // 組めなかった: 返して閉じたまま（模様替えの上段タブは残す。以前は Failed にして上段タブごと作り直していた）
            const u8 why = s_openStep;
            FreeWindow();
            s_heapLog[4] = L::HeapFreeBytes();
            s_heapMaxLog[4] = L::HeapMaxAllocBytes(0x80);
            s_openFail = why != 0 ? why : 1;
            RequestCatalogFile(false);
            __atomic_store_n(&s_catFileState, (u32)FileUploaded, __ATOMIC_RELEASE);
            PlaySound(kSndInvalid);
            s_win_state = Win::Closed;
            for (u32 i = 0; i < CL::kTopCount; ++i) {
                BtnRest(s_topBtn[i], s_topArc);
                BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
            }
            return;
        }
        RequestCatalogFile(false);
        __atomic_store_n(&s_catFileState, (u32)FileUploaded, __ATOMIC_RELEASE);
        s_win_state = Win::Opening;
        s_lastMain = s_main;
        return;
    case Win::Opening:
        if (L::Done(s_winIn) && L::Done(s_gridIn))
            s_win_state = Win::Open;
        return;
    case Win::Closing:
        if (L::Done(s_winOut) && L::Done(s_gridOut)) {
            s_win_state = Win::Closed;
            s_wait = 0;
            for (u32 i = 0; i < CL::kTopCount; ++i) {
                BtnRest(s_topBtn[i], s_topArc);
                BtnPlay(s_topBtn[i], s_topArc, CL::kLoop, false);
            }
        }
        return;
    case Win::Closed:
        // 閉じた窓は毎回返す。家具/B/戻る後も位置だけを保持しSDから開き直す。
        if (s_arc.made) {
            s_reclaimWindow = true;
            if (++s_closedWait >= kTeardownWaitFrames) {
                FreeWindow();
                s_closedWait = 0;
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
    for (u32 i = 0; i < CL::kTopCount; ++i)
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

void SampleHeap(void) {
    if (s_stage != Stage::Live)
        return;
    const u64 now = svcGetSystemTick();
    if (s_heapSampleTick != 0 && now - s_heapSampleTick < SYSCLOCK_ARM11)
        return;
    s_heapSampleTick = now;
    volatile HeapSample &sample = s_heapRing[s_heapRingSeq % 16];
    sample.seq = 0;
    sample.frame = s_frameSeq;
    sample.ticksLo = (u32)now;
    sample.win = (u32)s_win_state;
    sample.freeBytes = L::HeapFreeBytes();
    sample.maxBytes = L::HeapMaxAllocBytes(0x80);
    sample.chipNext = s_chipNext;
    sample.call = s_frameCall;
    u32 graphicFree, graphicMax;
    sample.graphicHeap = reinterpret_cast<u32>(L::GraphicsHeapStatus(graphicFree, graphicMax));
    sample.graphicFree = graphicFree;
    sample.graphicMax = graphicMax;
    sample.seq = s_heapRingSeq + 1;
    ++s_heapRingSeq;
}

void FrameStep(void) {
    SampleHeap();
    ++s_frameSeq;
    s_frameCall = 1;
    TrackPlacementOrigin();                        // エディターがプレイヤー実体を隠す前から実位置を保持する
    switch (s_stage) {
    case Stage::Idle: {
        FreeIconSlots(false);                       // 片付けの後でまだ返していない枠
        if (!Enabled() || s_topFile == nullptr)
            return;
        const u32 indoor = IndoorBase();
        if (!EditorLive(indoor, EditorCalc(indoor)))
            return;
        if (!BuildTop()) {
            FreeTop();
            s_stage = Stage::Failed;
            return;
        }
        s_heapBaseline = L::HeapFreeBytes();
        s_heapSampleTick = 0;
        s_leaving = false;
        s_touchPrev = Touch::IsDown();              // 開いた瞬間に押していた指は使わない
        s_topTouchCaptured = false;                 // 前のエディター表示の捕捉を次の指へ持ち越さない
        s_keysPrev = Controller::GetKeysDown();
        s_backdropFade = 6;
        s_stage = Stage::Live;
        [[fallthrough]];
    }
    case Stage::Live: {
        const u32 indoor = IndoorBase();
        const bool want = Enabled() && EditorLive(indoor, EditorCalc(indoor));
        if (!want && !s_leaving) {
            s_leaving = true;
            if (s_win_state == Win::Open || s_win_state == Win::Opening)
                BeginClose();
            L::Bind(s_topOut, s_top, CL::kTopOut.group, 0.0f, CL::kTopOut.group2);
            s_topBound = &s_topOut;
        }
        if (!s_leaving) {
            s_frameCall = 2;
            InputStep();
        }
        if (s_stage != Stage::Live)
            return;
        if (EditorNeutral(indoor, EditorCalc(indoor)))
            ChipTrackEditor(EditorPtr(indoor));     // 開いて最初の Neutral（ゲームの CreateChips の直後・ゴミ箱で Out する前）で未使用の枠を数える
        s_frameCall = 3;
        ChipStep(indoor);
        s_frameCall = 4;
        WindowStep();                              // チップ確保を先に完了。窓の構築途中へ割り込ませない
        FreeIconSlots(false);
        DecisionStep();
        if (s_win_state == Win::Open) {
            DecorSlider::Step(s_slider);
            {                                          // 自由送り中も現在位置から小分類/ページを決める
                const s32 before = s_kind;
                ShowKindSelection();
                if (before != s_kind)
                    PlaySound(s_main == 4 ? kSndSlide : kSndKindDecide);
                SetPageText();
            }
        }
        s_frameCall = 5;
        if (s_win_state == Win::Open && !HeapRoom())
            BeginClose(true);                      // 遅延資源/ゲーム側消費で目標割れした窓は返す。
        if (s_win_state == Win::Opening || s_win_state == Win::Open || s_win_state == Win::Closing)
            UpdateGrid();
        s_frameCall = 6;
        StepAnims();
        if (s_leaving) {
            CatalogBackdrop::Reset();
            s_backdropFade = 6;
        } else {
            if (s_win_state != Win::Closed) {
                if (s_backdropFade) --s_backdropFade;
            } else if (s_backdropFade < 6) {
                ++s_backdropFade;
            }
            CatalogBackdrop::Set(EditorPtr(indoor), reinterpret_cast<u32>(s_top.obj),
                static_cast<u8>(s_backdropFade * 255u / 6u),
                indoor ? R32(indoor + DecorTrashTable::kIndoorEditorPtrLiteral) : 0);
        }
        if (s_leaving && L::Done(s_topOut) && (s_win_state == Win::Closed || s_win_state == Win::Loading)) {
            s_stage = Stage::Teardown;                  // このフレームから描かない。壊すのは数フレーム後
            s_wait = 0;
            return;
        }
        s_frameCall = 7;
        DrawAll();
        s_frameCall = 8;
        // 非表示の待ちはリセットしない。閉窓/Loadingの旧資源を毎回返す。
        if (s_win_state == Win::Open || s_win_state == Win::Opening || s_win_state == Win::Closing)
            s_closedWait = 0;
        return;
    }
    case Stage::Teardown:
        if (++s_wait < kTeardownWaitFrames)
            return;
        ReleaseAll();
        RequestCatalogFile(false);
        s_stage = Stage::Idle;
        return;
    case Stage::Failed:
        ReleaseAll();
        RequestCatalogFile(false);
        SetEnabled(false);
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
    const u32 state = CatalogFileStatus();
    if (state == FileEmpty && __atomic_load_n(&s_wantCatFile, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&s_catFileState, (u32)FileReading, __ATOMIC_RELEASE);
        char path[96] = {};
        u32 size = 0;
        const u32 main = __atomic_load_n(&s_catRequestMain, __ATOMIC_ACQUIRE);
        const u32 catalog = CatalogIndex(main);
        if (catalog == CL::kCatalogCount) {
            s_catFailReason = 2;
            __atomic_store_n(&s_catFileState, (u32)FileFailed, __ATOMIC_RELEASE);
            return;
        }
        const char *arc = CL::kCatalogs[catalog].arc;
        u8 *data = GohanFiles::CommonPath(path, sizeof(path), arc) ? GohanFiles::ReadAll(path, kMaxCatalogArc, size) : nullptr;
        if (data == nullptr) {
            s_catFailReason = File::Exists(path) == 1 ? 2 : 1;
            __atomic_store_n(&s_catFileState, (u32)FileFailed, __ATOMIC_RELEASE);
            return;
        }
        s_catSize = size;
        s_catFileMain = main;
        s_catFile = data;
        __atomic_store_n(&s_catFileState, (u32)FileReady, __ATOMIC_RELEASE);
    } else if (state == FileUploaded || (state == FileReady && !__atomic_load_n(&s_wantCatFile, __ATOMIC_ACQUIRE))) {
        u32 expected = state;
        if (!__atomic_compare_exchange_n(&s_catFileState, &expected, (u32)FileDiscarding, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;                                // ゲームが先にCopyingを取った場合は返さない
        u8 *p = s_catFile;                          // ヒープへ写し終えた・窓を開くのをやめた
        s_catFile = nullptr;
        s_catSize = 0;
        delete[] p;
        __atomic_store_n(&s_catFileState, (u32)FileEmpty, __ATOMIC_RELEASE);
    } else if (state == FileFailed && !__atomic_load_n(&s_wantCatFile, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&s_catFileState, (u32)FileEmpty, __ATOMIC_RELEASE);
    }
}

}  // namespace

void Wire(void) {
    s_index = GuiMenu::FindItem(kDecorCatalog);
}

bool Tick(int index, unsigned short) {
    if (index != s_index || index < 0)
        return false;
    ++s_tickSeq;
    if (s_stage == Stage::Live && (s_win_state != Win::Closed
        || (!s_leaving && !GuiMenu::IsVisible() && (Controller::GetKeysDown() & (u32)Key::X)))) {
        GuiMenu::BlockGameAll();                    // 窓が出ている間はゲームへの入力を止める（HhdScreen と同じ。タッチは別に止める）
        GuiMenu::BlockGameTouch();
    }
    // ゲームのFrameStepより早く検出できた場合にも遮断する。外へずらしても指を離すまで保持。
    if (s_stage == Stage::Live && !s_leaving && (s_win_state == Win::Closed || s_win_state == Win::Closing) && Touch::IsDown()) {
        const UIntVector p = Touch::GetPosition();
        bool overTop = false;
        for (u32 i = 0; i < CL::kTopCount; ++i)
            overTop = overTop || InRect(CL::kTopTabs[i].rect, (float)p.x, (float)p.y);
        if (s_topTouchCaptured || (!s_touchPrev && overTop))
            GuiMenu::CaptureGameTouchUntilRelease();
    }
    ServiceFiles();
    if (const u8 why = s_catFailReason) {            // 窓が開かなかった理由を知らせる（2026-10-07 の実機で何も出ずに開かなかった）
        s_catFailReason = 0;
        GuiNotification::NotifyRed(kNoticeTitle, why == 1 ? u8"SD の gohan/common/ に品・かべ・ゆかの arc がありません。"
                                                        : u8"品・かべ・ゆかの arc を読めません（大きさ・メモリ）。");
    }
    if (const u8 why = s_openFail) {
        s_openFail = 0;
        char msg[160];
        if (why == 2)
            std::snprintf(msg, sizeof(msg), u8"品を出して不足\nチップ%lu\nB 残/要\n続%lu/%lu",
                          (unsigned long)s_heapFailChips, (unsigned long)s_heapMaxLog[0], (unsigned long)(DecorIcons::kSlots * DecorIcons::kIconBytes));
        else if (why == 3)
            // 本文欄は96 B。最大値でも両通知94 B。チップはOut済みを含む履歴。
            std::snprintf(msg, sizeof(msg), u8"%s\nチップ%lu\nB 残/要\n計%lu/%lu\n続%lu/%lu",
                          s_heapFailKind == 1 ? u8"描画の空き不足" : u8"品を出して不足", (unsigned long)s_heapFailChips,
                          (unsigned long)s_heapFailFree, (unsigned long)s_heapFailRequiredFree,
                          (unsigned long)s_heapFailMax, (unsigned long)s_heapFailRequiredMax);
        else
            std::snprintf(msg, sizeof(msg), "窓を組めません（空き %lu B）。", (unsigned long)s_heapLog[0]);
        GuiNotification::NotifyRed(kNoticeTitle, msg);
    }
    DecorIcons::Service(6);
    if (Enabled())
        return true;
    if (!LoadTop() || !DecorIcons::Open()) {
        if (!s_toldFail)
            GuiNotification::NotifyRed(kNoticeTitle, u8"SD の gohan/common/ に arc と hhd_icons.bin が要ります。");
        s_toldFail = true;
        return true;
    }
    s_namesLoaded = ItemNames::LoadNormal();
    if (!s_hooked)
        s_hooked = CatalogBackdrop::Install() && GridCursor::InstallFrameHook() && GridCursor::AddExtraFrameStep(FrameStep);
    if (!s_hooked) {
        if (!s_toldFail)
            GuiNotification::NotifyRed(kNoticeTitle, u8"フレームを共有できません。");
        s_toldFail = true;
        return true;
    }
    s_toldFail = false;
    SetEnabled(true);
    return true;
}

bool Disable(int index) {
    if (index != s_index || index < 0)
        return false;
    SetEnabled(false);                              // ゲームのスレッドが退場させて片付ける
    return true;
}

bool IsListOpen(void) { return s_stage == Stage::Live && s_win_state != Win::Closed; }
bool IsEditorOpen(void) { const u32 indoor = IndoorBase(); return EditorLive(indoor, EditorCalc(indoor)); }

}  // namespace DecorCatalog
