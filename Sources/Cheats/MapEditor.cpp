#include "MapEditor.hpp"

#include "BuildingEditor.hpp"
#include "Cheats.hpp"
#include "CursorRepeat.hpp"
#include "FieldCamera.hpp"
#include "GameList.hpp"
#include "GridCursorGameApi.hpp"
#include "GuiDialog.hpp"
#include "GuiMenu.hpp"
#include "PublicWorks.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
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
enum ChipType : u8 { kItemC, kFgobjN, kBuild11, kBuild12, kBuild22, kChipTypes };
const char *const kChipLayouts[kChipTypes] = {
    "cip_01C_02x02.bclyt", "cip_01N_02x02.bclyt", "cip_01P_02x02.bclyt", "cip_01P_02x04.bclyt", "cip_01P_04x04.bclyt",
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
const GroundHeightFn GroundHeight   = reinterpret_cast<GroundHeightFn>(0x006C69C0);
const u32 kFieldPtr = 0x009AEA04;           // u32: Field_GetMapManager 0x6A53DC が返す（+0/+4/+8/+C = エーカーの最小 x, y / 最大 x, y）
const u16 kEmptyItem = 0x7FFE;              // Item_IsEmpty 0x2FCB24
const u16 kFgobjMax = 0xFD;                 // Item_IsFieldObj 0x2FCCBC
const s32 kTilesX = 112, kTilesY = 96;      // 村のマス（7 x 6 エーカー）

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
volatile u32 s_touchSeq;                    // タッチの押し始めごとに増える
volatile u16 s_touchX, s_touchY;            // 下画面の画素
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

struct WantChip { u8 type; bool rotated; s16 tx, ty; u8 w, h; };
WantChip s_wantChips[kMaxChips];
u32 s_wantCount;
Chip *s_drawOrder[kMaxChips];
u32 s_drawCount;

u32 s_touchDone;
s32 s_nameTileX = -1, s_nameTileY = -1;
Chip *s_nameChip;                           // 吹き出しの基準のコマ
s32 s_nameViewX, s_nameViewY;

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
    return true;
}

void DestroyAll(void) {
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
}

void CollectWants(s32 vx, s32 vy) {
    s_wantCount = 0;
    // 建物（P）。盤面に収まるものはコマ 1 個、はみ出すものは盤面の中のマスを 1x1 で
    const u32 n = s_buildChipCount;
    for (u32 k = 0; k < n; ++k) {
        const BuildChip &b = s_buildChips[k];
        const s32 x0 = b.x, y0 = b.y, x1 = b.x + b.w, y1 = b.y + b.h;
        if (x1 <= vx || y1 <= vy || x0 >= vx + kView || y0 >= vy + kView)
            continue;
        if (x0 >= vx && y0 >= vy && x1 <= vx + kView && y1 <= vy + kView) {
            if (b.w == 2 && b.h == 2)
                AddWant(kBuild22, x0, y0, 2, 2, false);
            else if (b.w == 1 && b.h == 2)
                AddWant(kBuild12, x0, y0, 1, 2, false);
            else if (b.w == 2 && b.h == 1)
                AddWant(kBuild12, x0, y0, 2, 1, true);     // 1x2 のコマを 90 度回す
            else
                AddWant(kBuild11, x0, y0, 1, 1, false);
            continue;
        }
        for (s32 y = y0; y < y1; ++y)
            for (s32 x = x0; x < x1; ++x)
                if (x >= vx && y >= vy && x < vx + kView && y < vy + kView)
                    AddWant(kBuild11, x, y, 1, 1, false);
    }
    // アイテム（C = 通常 / N = fgobj）。建物の上に載っていても出す（上に置ける家具に載る扱い）
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
            AddWant(id <= kFgobjMax ? kFgobjN : kItemC, vx + i, vy + j, 1, 1, false);
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
    // 描く順: 建物 → アイテム（アイテムを上に）
    for (u32 pass = 0; pass < 2; ++pass) {
        for (u32 k = 0; k < s_wantCount; ++k) {
            Chip *c = assigned[k];
            const WantChip &w = s_wantChips[k];
            const bool building = w.type >= kBuild11;
            if (c == nullptr || building != (pass == 0))
                continue;
            c->tx = w.tx;
            c->ty = w.ty;
            c->rotated = w.rotated;
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

// タッチ: アイテムのマスなら名前（建物は無視）
void StepTouch(s32 vx, s32 vy) {
    const u32 seq = s_touchSeq;
    if (seq == s_touchDone)
        return;
    s_touchDone = seq;
    if (s_boardAnim != nullptr)
        return;                             // 出入りの途中は受け付けない
    const float lx = (float)s_touchX - 160.0f;
    const float ly = 120.0f - (float)s_touchY;
    const float ox = F(s_roomPane, kPaneGlobalX);
    const float oy = F(s_roomPane, kPaneGlobalY);
    const float half = (float)kView * 0.5f * kTile;
    const float fx = (lx - (ox - half)) / kTile;
    const float fy = ((oy + half) - ly) / kTile;
    if (fx < 0.0f || fy < 0.0f || fx >= (float)kView || fy >= (float)kView) {
        HideName();
        return;
    }
    const s32 tx = vx + (s32)fx, ty = vy + (s32)fy;
    Chip *c = ChipAt(tx, ty, true);
    const u32 field = R32(kFieldPtr);
    const u32 *item = field != 0 ? ItemAt(field, tx, ty, 0) : nullptr;
    if (c == nullptr || item == nullptr || (u16)(*item & 0x7FFFu) == kEmptyItem) {
        HideName();
        return;
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
    out[2] = ViewCenter(s_viewY);
    out[1] = GroundHeight(out, 0);
}

// ---------------------------------------------------------------------------------------------
// メニュースレッド
// ---------------------------------------------------------------------------------------------

bool s_running;
bool s_failed;
CursorRepeat s_repeat;                      // 十字キーとスライドパッドの押し続け（公共事業エディターと同じ規則）
bool s_touchPrev;
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

// 建物の衝突判定（公共事業エディターと同じ形）をコマにする
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
        s32 x0 = 0x7FFF, y0 = 0x7FFF, x1 = -0x7FFF, y1 = -0x7FFF;
        for (u32 k = 0; k < cells; ++k) {
            const s32 x = (s32)slot.x + dx[k], y = (s32)slot.y + dy[k];
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
        const s32 w = x1 - x0 + 1, h = y1 - y0 + 1;
        const bool box = (u32)(w * h) == cells && w <= 2 && h <= 2;   // 1x1 / 2x1 / 1x2 / 2x2 の詰まった四角
        if (box && x0 >= 0 && y0 >= 0 && x1 < kTilesX && y1 < kTilesY) {
            if (n < kMaxBuildChips)
                s_buildChips[n++] = { (u8)x0, (u8)y0, (u8)w, (u8)h };
            continue;
        }
        for (u32 k = 0; k < cells && n < kMaxBuildChips; ++k) {      // それ以外（3x3・離れた形など）は 1x1 を並べる
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
    s_touchSeq = 0;
    s_touchDone = 0;
    s_touchPrev = true;                     // 押したまま始めたタッチは押し始めにしない
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

void StepTouchInput(void) {
    const bool down = Touch::IsDown();
    if (down && !s_touchPrev) {
        const UIntVector pos = Touch::GetPosition();
        s_touchX = (u16)pos.x;
        s_touchY = (u16)pos.y;
        s_touchSeq = s_touchSeq + 1;
    }
    s_touchPrev = down;
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
    CollectWants(vx, vy);
    AssignChips();
    for (u32 k = 0; k < s_drawCount; ++k)
        LayoutCalc(s_drawOrder[k]->layout);     // 吹き出しは基準のコマの大域位置を読むので先に計算する
    if (s_stage == Stage::Live) {
        StepName(vx, vy);
        StepTouch(vx, vy);
    }
    NameCalc();
    // 描画登録（リストは毎フレーム空になる）。盤面 → 建物 → アイテム → 名前
    if (mgr != nullptr) {
        AddLayout(mgr, s_board, 1);
        for (u32 k = 0; k < s_drawCount; ++k)
            AddLayout(mgr, s_drawOrder[k]->layout, 1);
        AddLayout(mgr, s_name + kNameLayout, 1);
    }
}

// ---------------------------------------------------------------------------------------------
// メニュースレッド（公開）
// ---------------------------------------------------------------------------------------------

bool Running(void) {
    return s_running;
}

void Reset(void) {
    s_failed = false;
}

void Stop(void) {
    if (!s_running)
        return;
    s_running = false;
    s_want = false;
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
    if (GuiMenu::IsVisible())
        return;
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
        }
    }
}
