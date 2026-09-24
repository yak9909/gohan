#include "BuildingEditor.hpp"

#include "BuildingHighlight.hpp"
#include "BuildingPreview.hpp"
#include "Cheats.hpp"
#include "GameLabel.hpp"
#include "GameList.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"
#include "PublicWorks.hpp"
#include "RomfsIndex.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

using namespace CTRPluginFramework;

namespace BuildingEditor {

namespace {

// ---- ゲーム側 ---------------------------------------------------------------------------------
const u32 kCameraGame = 0x0094A880;         // u32: CameraGame*（dtor 0x1A7C1C が 0 を書く）
const u32 kCameraBase = 4;                  // float x, y, z: 基準位置（注視点 = 基準 + +16..）
const u32 kCameraPatch = 0x001A5128;        // sub_1A5124 の 2 語目
const u32 kCameraPatchOrig = 0xE2805C01;    // ADD R5,R0,#0x100
const u32 kCameraPatchPop = 0xE8BD81F0;     // POP {R4-R8,PC}（先頭の PUSH と同じ組。戻り値は呼び元が使わない）
const u32 kRoomIdByte = 0x0095133A;         // u8: 0 = 村の屋外
const u32 kPlayerPtr = 0x00AA7994;          // Player*
const u32 kPlayerPosition = 0x14;
typedef float (*GroundHeightFn)(const float *pos, u32 zero);    // 0x006C69C0（S0 で返る）
const GroundHeightFn GroundHeight = reinterpret_cast<GroundHeightFn>(0x006C69C0);

// 占有マップの広さ（PublicWorks::SlotAtTile と同じ）
const s32 kTilesX = 112;
const s32 kTilesY = 96;

// 足元データ（Strc/data/<名>.bin）: 16 x 16 マス x 10 B、基点 (行 7, 列 7)、byte 8 = 属性（F001）
const u32 kFootprintBytes = 2560;
const s32 kFootprintSide = 16;
const s32 kFootprintOrigin = 7;
const u32 kMaxCells = 64;

const float kCameraFollow = 0.35f;          // 1 フレームで目標へ寄る割合

// ---- 足元データの取り寄せ（名前の無い建物 = 住民・プレイヤーの家）----------------------------------
// 家は名前表に名前が無く、足元のファイル名（hobj_npchouse_%02d 等）は住民ごとの型で決まる。村にある建物の足元は
// ゲームが読み込み済みなので、描画スレッドで Building_GetFootprint 0x1B1720(x, y, id) を呼んで写す（IDA-opus-5.5-F021）。
typedef const u8 *(*GetFootprintFn)(u32 x, u32 y, u32 id);
const GetFootprintFn GetFootprint = reinterpret_cast<GetFootprintFn>(0x001B1720);
volatile u32 s_fetchSeq;          // メニュー: 頼んだ番号
volatile u32 s_fetchDone;         // 描画: 済ませた番号
volatile u32 s_fetchId, s_fetchX, s_fetchY;
volatile bool s_fetchOk;
u8 s_fetchBuf[2560];

// ---- メニュー ↔ 描画スレッド -------------------------------------------------------------------
volatile bool s_want;           // メニュー: エディターを動かしたい
volatile bool s_patched;        // 描画: カメラを止めている
volatile bool s_lost;           // 描画: 場面が変わった／カメラが取れない
volatile s32 s_cx, s_cy;        // カーソルのマス
volatile u32 s_lostReason;
volatile bool s_snapCamera;     // 再開: カメラをプレイヤーから滑らせず、最初からカーソルへ置く
// 橋を出しているとき: カメラの高さもカーソルと同じ橋の高さにする（0 = カーソルのマスの地面）。
// 1 語に 有効 0x10000 | x | y<<8 で詰めて、スレッド間で 1 回で読み書きする。
volatile u32 s_bridgeAnchor;
u32 PackAnchor(u32 x, u32 y) { return 0x10000u | x | (y << 8); }

// ---- 描画スレッドだけ ---------------------------------------------------------------------------
u32 s_camera;
float s_offset[3];
float s_current[3];

// ---- メニュースレッドだけ ------------------------------------------------------------------------
bool s_running;
bool s_failed;                  // 失敗したらチェックを外すまで再開しない
Mode s_mode;
u8 s_kinds[256];
u32 s_kindCount;
u32 s_kind;
s32 s_selected = -1;
u32 s_prevKeys;
u32 s_hold[4];                  // スライドパッド 上下左右の押し続けティック

u8 s_footprint[kFootprintBytes];   // 足元データを読む置き場

u32 R32(u32 a) { return *reinterpret_cast<volatile u32 *>(a); }
bool IsHeap(u32 p) { return p >= 0x08000000u && p < 0x40000000u && (p & 3u) == 0u; }

// ---------------------------------------------------------------------------------------------
// 描画スレッド
// ---------------------------------------------------------------------------------------------

bool InVillage(void) {
    return *reinterpret_cast<volatile u8 *>(kRoomIdByte) == 0;
}

void WriteCode(u32 addr, u32 value) {
    *reinterpret_cast<volatile u32 *>(addr) = value;
    GuiMenu::FlushMemory(addr, 4);
}

void Unpatch(void) {
    if (R32(kCameraPatch) == kCameraPatchPop)
        WriteCode(kCameraPatch, kCameraPatchOrig);
    s_patched = false;
}

float CameraHeight(float *at) {
    const u32 anchor = s_bridgeAnchor;
    if (anchor == 0)
        return GroundHeight(at, 0);
    return PublicWorks::BridgeHeight(anchor & 0xFFu, (anchor >> 8) & 0xFFu);
}

void Lose(u32 reason) {
    Unpatch();
    s_lostReason = reason;
    s_lost = true;
}

}  // namespace

void FrameStep(void) {
    if (s_fetchDone != s_fetchSeq) {                // 足元の取り寄せ（エディターの状態に関係なく）
        const u8 *fp = GetFootprint(s_fetchX, s_fetchY, s_fetchId);
        s_fetchOk = fp != nullptr && BuildingHighlight::SafeReadable(reinterpret_cast<u32>(fp), sizeof(s_fetchBuf));
        if (s_fetchOk)
            std::memcpy(s_fetchBuf, fp, sizeof(s_fetchBuf));
        s_fetchDone = s_fetchSeq;
    }
    if (!s_patched) {
        if (!s_want || s_lost)
            return;
        if (!InVillage()) {
            Lose(1);
            return;
        }
        const u32 camera = R32(kCameraGame);
        const u32 player = R32(kPlayerPtr);
        if (!IsHeap(camera) || !IsHeap(player)) {
            Lose(2);
            return;
        }
        if (R32(kCameraPatch) != kCameraPatchOrig) {  // ほかの改造が当たっている。触らない
            Lose(3);
            return;
        }
        // 基準位置とプレイヤーのずれを控えて、カーソルへ同じずれで付ける。
        const float *base = reinterpret_cast<const float *>(camera + kCameraBase);
        const float *pos = reinterpret_cast<const float *>(player + kPlayerPosition);
        for (u32 i = 0; i < 3; ++i) {
            s_offset[i] = base[i] - pos[i];
            s_current[i] = base[i];
        }
        if (s_snapCamera) {
            float at[3] = { (float)(32 * s_cx + 16), 0.0f, (float)(32 * s_cy + 16) };
            at[1] = CameraHeight(at);
            for (u32 i = 0; i < 3; ++i)
                s_current[i] = at[i] + s_offset[i];
            s_snapCamera = false;
        }
        s_camera = camera;
        WriteCode(kCameraPatch, kCameraPatchPop);
        s_patched = true;
        return;
    }
    if (!s_want) {
        Unpatch();
        return;
    }
    if (!InVillage() || R32(kCameraGame) != s_camera) {
        Lose(1);
        return;
    }
    float target[3] = { (float)(32 * s_cx + 16), 0.0f, (float)(32 * s_cy + 16) };
    target[1] = CameraHeight(target);
    float *base = reinterpret_cast<float *>(s_camera + kCameraBase);
    for (u32 i = 0; i < 3; ++i) {
        s_current[i] += (target[i] + s_offset[i] - s_current[i]) * kCameraFollow;
        base[i] = s_current[i];
    }
}

namespace {

// ---------------------------------------------------------------------------------------------
// メニュースレッド
// ---------------------------------------------------------------------------------------------

const char *ModeName(Mode mode) {
    switch (mode) {
    case Mode::Place: return u8"配置";
    case Mode::Move: return u8"移動";
    case Mode::Remove: return u8"削除";
    default: return u8"?";
    }
}

// ---- 衝突判定の形 ----------------------------------------------------------------------------
// 利用者の定義（2026-09-24）: 足元データ（Strc/data/<名>.bin）で属性を書くマスのうち「物・花を置けない」マス。
//   それが 1 マスも無ければ、足元の範囲（属性を書くマスの外接の四角）を上下左右 1 マスずつ削った四角。
// 利用者の正解（ベンチ 2x1、街灯 1x1、噴水 3x3、交番 3x3＋下段中央の突起 1）と一致するのは
//   **花を植えられない = byte_957A35[code] == 0**（FieldAttr_CanPlantFlower 0x5CD55C、花を植える 0x0C のモード 1）だけ。
//   物を置けるかの表 byte_957D32 & 2（0x5CD370）は外周 0x05 を「置けない」とするのでベンチが 4x3 になる（IDA-opus-5.5-F020）。
//   code >= 0xFF は 0 番を引く。表はゲームのメモリから読む。
const u32 kPlantTable = 0x00957A35;

struct Shape {
    bool loaded;
    u8 count;
    s8 dx[kMaxCells];
    s8 dy[kMaxCells];
};
Shape s_shapes[256];

bool Blocks(u8 code) {
    const u32 i = code >= 0xFF ? 0u : code;
    return *reinterpret_cast<const volatile u8 *>(kPlantTable + i) == 0;
}

// 村にあるその id の建物の位置で、ゲームの足元データを描画スレッドに写してもらう（最大 0.3 秒待つ）
bool FetchFromGame(u16 id) {
    u32 x = 0, y = 0;
    bool found = false;
    for (u32 i = 0; i < PublicWorks::kSlots && !found; ++i) {
        PublicWorks::Slot slot;
        if (PublicWorks::ReadSlot(i, slot) && slot.id == id) {
            x = slot.x;
            y = slot.y;
            found = true;
        }
    }
    if (!found)
        return false;
    s_fetchId = id;
    s_fetchX = x;
    s_fetchY = y;
    const u32 seq = s_fetchSeq + 1;
    s_fetchSeq = seq;
    for (u32 i = 0; i < 18 && s_fetchDone != seq; ++i)
        svcSleepThread(16666667LL);
    if (s_fetchDone != seq || !s_fetchOk)
        return false;
    std::memcpy(s_footprint, s_fetchBuf, sizeof(s_footprint));
    return true;
}

void Push(Shape &s, s32 c, s32 r) {
    if (s.count >= kMaxCells)
        return;
    s.dx[s.count] = (s8)(c - kFootprintOrigin);
    s.dy[s.count] = (s8)(r - kFootprintOrigin);
    ++s.count;
}

const Shape &ShapeOf(u16 id) {
    Shape &s = s_shapes[id & 0xFF];
    if (s.loaded)
        return s;
    s.loaded = true;
    s.count = 0;
    char path[96];
    const char *name = PublicWorks::NameOf(id);
    u32 got = 0;
    if (name[0] != '\0') {
        std::snprintf(path, sizeof(path), "Strc/data/%s.bin", name);
        got = RomfsIndex::ReadFile(path, s_footprint, sizeof(s_footprint));
    } else {
        got = FetchFromGame(id) ? kFootprintBytes : 0;
    }
    if (got == kFootprintBytes) {
        s32 top = kFootprintSide, bottom = -1, left = kFootprintSide, right = -1;
        for (s32 r = 0; r < kFootprintSide; ++r) {
            for (s32 c = 0; c < kFootprintSide; ++c) {
                const u8 code = s_footprint[160 * r + 10 * c + 8];
                if (code == 0)
                    continue;
                if (r < top) top = r;
                if (r > bottom) bottom = r;
                if (c < left) left = c;
                if (c > right) right = c;
                if (Blocks(code))
                    Push(s, c, r);
            }
        }
        if (s.count == 0 && bottom >= 0) {           // 置けない・植えられないマスが無い: 範囲を一回り削る
            for (s32 r = top + 1; r <= bottom - 1; ++r)
                for (s32 c = left + 1; c <= right - 1; ++c)
                    Push(s, c, r);
        }
    }
    if (s.count == 0) {                             // 名前の無い家など・削って残らない形: 基点の 1 マス
        s.dx[0] = 0;
        s.dy[0] = 0;
        s.count = 1;
    }
    return s;
}

bool Covers(const PublicWorks::Slot &slot, s32 x, s32 y) {
    const Shape &s = ShapeOf(slot.id);
    for (u32 i = 0; i < s.count; ++i)
        if (slot.x + s.dx[i] == x && slot.y + s.dy[i] == y)
            return true;
    return false;
}

// カーソルの下の建物。**衝突判定のマスがカーソルに重なる建物**のうち、建物の基点がマンハッタン距離で一番近いもの。
// 重なる建物が無ければ、ゲームの占有マップ（足元の全マス）で引く。
s32 Hovered(void) {
    s32 best = -1;
    s32 bestDistance = 0x7FFFFFFF;
    for (u32 i = 0; i < PublicWorks::kSlots; ++i) {
        PublicWorks::Slot slot;
        if (!PublicWorks::ReadSlot(i, slot) || slot.id >= PublicWorks::kEmptyId || !Covers(slot, s_cx, s_cy))
            continue;
        // 近さはマンハッタン距離（利用者指示）
        const s32 dx = (s32)slot.x - s_cx;
        const s32 dy = (s32)slot.y - s_cy;
        const s32 distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = (s32)i;
        }
    }
    return best >= 0 ? best : PublicWorks::SlotAtTile((u32)s_cx, (u32)s_cy);
}

// ---- UnitCursor と設置プレビュー -----------------------------------------------------------------
const u8 kCursorTint = 0xB0;

void PutShape(u16 id, s32 ax, s32 ay, u32 color, u8 strength) {
    const Shape &s = ShapeOf(id);
    u8 xs[kMaxCells];
    u8 ys[kMaxCells];
    u32 n = 0;
    for (u32 i = 0; i < s.count; ++i) {
        const s32 x = ax + s.dx[i];
        const s32 y = ay + s.dy[i];
        if (x < 0 || y < 0 || x >= kTilesX || y >= kTilesY)
            continue;
        xs[n] = (u8)x;
        ys[n] = (u8)y;
        ++n;
    }
    GridCursor::SetTint(color, strength);
    // 高さは全部そろえる（PublicWorks::CursorHeight）。橋以外は基点の地面（建てたときの高さと同じ）。
    // ★橋はどこでも一定の高さ（利用者 2026-09-24）＝ゲームの橋の計算（川底 + flt_6DE560）を、村の川底で一番多い
    //   高さ（橋を架ける川の段）で行った値（IDA-opus-5.5-F027）。最寄りの川底だと河口で浜の高さになった。カメラも同じ高さ。
    const s32 cx = ax < 0 ? 0 : (ax >= kTilesX ? kTilesX - 1 : ax);
    const s32 cy = ay < 0 ? 0 : (ay >= kTilesY ? kTilesY - 1 : ay);
    s_bridgeAnchor = PublicWorks::IsBridgeId(id) ? PackAnchor((u32)cx, (u32)cy) : 0u;
    GridCursor::SetTiles(xs, ys, n, (s32)id, (u8)cx, (u8)cy);
}

void PutSingle(u32 color, u8 strength) {
    const u8 x = (u8)s_cx;
    const u8 y = (u8)s_cy;
    GridCursor::SetTint(color, strength);
    s_bridgeAnchor = 0;
    GridCursor::SetTiles(&x, &y, 1, -1, x, y);
}

// いまのモードで、UnitCursor と設置プレビューを置き直す。
void UpdateTiles(void) {
    PublicWorks::Slot slot;
    switch (s_mode) {
    case Mode::Place:
        if (s_kindCount > 0) {
            PutShape(s_kinds[s_kind], s_cx, s_cy, BuildingHighlight::kBlue, 0);   // 色はそのまま
            BuildingPreview::Show(s_kinds[s_kind], s_cx, s_cy);
        }
        return;
    case Mode::Move:
        BuildingPreview::Hide();
        if (s_selected >= 0 && PublicWorks::ReadSlot((u32)s_selected, slot))
            PutShape(slot.id, s_cx, s_cy, BuildingHighlight::kBlue, kCursorTint);
        else
            PutSingle(BuildingHighlight::kBlue, kCursorTint);
        return;
    case Mode::Remove:
        BuildingPreview::Hide();
        if (s_selected >= 0 && PublicWorks::ReadSlot((u32)s_selected, slot))
            PutShape(slot.id, slot.x, slot.y, BuildingHighlight::kRed, kCursorTint);
        else
            PutSingle(BuildingHighlight::kRed, kCursorTint);
        return;
    default:
        return;
    }
}

// 移動モードで何も選んでいないとき、カーソルを合わせた建物に薄い白（合成度合い 50）を重ねる（利用者指示）
const s16 kHoverTint = 50;
s32 s_hoverShown = -1;

void UpdateHover(void) {
    if (s_mode != Mode::Move || s_selected >= 0) {
        s_hoverShown = -1;
        return;
    }
    const s32 hovered = Hovered();
    if (hovered == s_hoverShown)
        return;
    s_hoverShown = hovered;
    if (hovered < 0) {
        PublicWorks::Unhighlight();
    } else {
        BuildingHighlight::SetStyle(BuildingHighlight::kWhite, kHoverTint);
        PublicWorks::Highlight((u32)hovered);
    }
}

void Select(s32 slot) {
    s_selected = slot;
    s_hoverShown = -1;
    if (slot < 0) {
        PublicWorks::Unhighlight();
    } else {
        BuildingHighlight::SetColor(s_mode == Mode::Remove ? BuildingHighlight::kRed : BuildingHighlight::kBlue);
        PublicWorks::Highlight((u32)slot);
    }
    UpdateTiles();
    UpdateHover();
}

void NotifyKind(const char *prefix) {
    static char message[96];
    if (s_kindCount == 0)
        return;
    const u8 id = s_kinds[s_kind];
    std::snprintf(message, sizeof(message), u8"%s 0x%02X %s", prefix, (unsigned)id, PublicWorks::NameOf(id));
    GuiNotification::Notify(Cheats::kBeOn, message);
}

// 上画面左上の箱（ゲームの所持ベルの箱、GameLabel）に今のモードを出す
void ShowModeLabel(void) {
    static const char *const kLabel[] = { u8"配置モード", u8"移動モード", u8"削除モード" };
    const u32 m = (u32)s_mode;
    GameLabel::SetText(m < 3 ? kLabel[m] : "");
    GameLabel::Show();
}

void NotifyMode(void) {
    ShowModeLabel();
    if (s_mode == Mode::Place) {
        NotifyKind(ModeName(s_mode));
        return;
    }
    GuiNotification::Notify(Cheats::kBeOn, ModeName(s_mode));
}

void Report(PublicWorks::Result result) {
    if (result == PublicWorks::Result::Ok)
        GuiNotification::Notify(Cheats::kBeOn, u8"完了");
    else
        GuiNotification::NotifyRed(Cheats::kBeOn, PublicWorks::ResultName(result));
}

// 村の屋外で、プレイヤーとカメラが取れるか（画面遷移のあとで再開してよいか）
bool ReadyToStart(void) {
    u32 x = 0, y = 0;
    return InVillage() && IsHeap(R32(kCameraGame)) && IsHeap(R32(kPlayerPtr)) && PublicWorks::PlayerTile(x, y);
}

// 下画面にゲームのリスト UI で配置の一覧を出す（GameList、IDA-opus-5.5-F036）。一覧は最初の 1 回だけ渡す。
bool s_listGiven;

void ShowKindList(void) {
    if (!s_listGiven && s_kindCount > 0) {
        static const char *names[sizeof(s_kinds)];
        for (u32 k = 0; k < s_kindCount; ++k)
            names[k] = PublicWorks::NameOf(s_kinds[k]);
        s_listGiven = GameList::SetItems(names, s_kindCount);
        // 名前はゲームの「STR_Fobj_name」があればそれ（公共事業の一覧と同じ引き方: 建物 ID → byte_887B94 → 番号、
        //   sub_56CDEC / sub_5C97B8）。0xFF（名前なし）はモデル名のまま。
        static s16 msg[sizeof(s_kinds)];
        const u8 *table = reinterpret_cast<const u8 *>(0x00887B94);    // 0xFC 個
        for (u32 k = 0; k < s_kindCount; ++k)
            msg[k] = (s_kinds[k] < 0xFC && table[s_kinds[k]] != 0xFF) ? (s16)table[s_kinds[k]] : (s16)-1;
        GameList::SetItemMessages("STR_Fobj_name", msg, s_kindCount);
    }
    if (s_listGiven)
        GameList::Show((s32)s_kind);
}

// リストで選ばれた種類を配置する種類にする（ほかのモードなら配置へ切り替える）
void TakeListChoice(void) {
    const s32 chosen = GameList::TakeDecided();
    if (chosen < 0 || (u32)chosen >= s_kindCount)
        return;
    const bool modeChanged = s_mode != Mode::Place;
    if (!modeChanged && (u32)chosen == s_kind)
        return;
    s_kind = (u32)chosen;
    if (modeChanged) {
        s_mode = Mode::Place;
        Select(-1);
        NotifyMode();
    } else {
        UpdateTiles();
        NotifyKind(ModeName(s_mode));
    }
}

// quiet: 再開のときは失敗を通知しない
bool Start(bool quiet) {
    if (!PublicWorks::StartFrameHook()) {
        GuiNotification::NotifyRed(Cheats::kBeOn, u8"フックが入れられません");
        s_failed = true;
        return false;
    }
    u32 x = 0, y = 0;
    if (!ReadyToStart() || !PublicWorks::PlayerTile(x, y)) {
        if (!quiet)
            GuiNotification::NotifyRed(Cheats::kBeOn, u8"村の屋外で使ってください");
        return false;
    }
    if (s_kindCount == 0) {
        for (u32 id = 0; id < PublicWorks::kEmptyId && s_kindCount < sizeof(s_kinds); ++id)
            if (PublicWorks::NameOf((u16)id)[0] != '\0')
                s_kinds[s_kindCount++] = (u8)id;
    }
    // 村にある建物の形を先に読んでおく（カーソル操作の途中でファイルを読まないように）
    for (u32 i = 0; i < PublicWorks::kSlots; ++i) {
        PublicWorks::Slot slot;
        if (PublicWorks::ReadSlot(i, slot) && slot.id < PublicWorks::kEmptyId)
            ShapeOf(slot.id);
    }
    // 部屋の読み直し・画面遷移のあとの再開では、読み込み前のカーソル・モード・種類をそのまま使う（利用者指示:
    // 特殊建物を置いたあとにカメラがプレイヤーへ戻らないように）。最初の開始だけプレイヤーの足元から。
    if (!quiet) {
        s_cx = (s32)x;
        s_cy = (s32)y;
        s_mode = Mode::Place;
    }
    s_snapCamera = quiet;
    s_selected = -1;
    s_prevKeys = 0xFFFFFFFFu;                       // 押しっぱなしのボタンを最初の押下にしない
    std::memset(s_hold, 0, sizeof(s_hold));
    PublicWorks::Unhighlight();
    UpdateTiles();
    if (!GridCursor::ShowTiles()) {
        GuiNotification::NotifyRed(Cheats::kBeOn, u8"グリッドカーソルを先に止めてください");
        s_failed = true;
        return false;
    }
    s_lost = false;
    s_want = true;
    s_running = true;
    ShowKindList();
    NotifyMode();
    return true;
}

// 押した瞬間と、押し続けたとき（約 200ms 後から約 64ms ごと）に真
bool Repeat(u32 &counter, bool held) {
    if (!held) {
        counter = 0;
        return false;
    }
    ++counter;
    return counter == 1 || (counter > 12 && ((counter - 12) % 4) == 0);
}

// スライドパッド: どれかの向きが連続移動に入っている間（離して kPadGrace ティック以内も含む）に
// 別の向きを入れたら、その向きは待たずに連続移動にする（押した瞬間に 1 マス、以後 4 ティックごと。利用者指示）。
const u32 kPadGrace = 3;
u32 s_padRepeatingAgo = 0xFFFFu;

bool PadRepeat(u32 &counter, bool held, bool fast) {
    if (held && counter == 0 && fast) {
        counter = 12;
        return true;
    }
    return Repeat(counter, held);
}

void MoveCursor(s32 dx, s32 dy) {
    s32 x = s_cx + dx;
    s32 y = s_cy + dy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= kTilesX) x = kTilesX - 1;
    if (y >= kTilesY) y = kTilesY - 1;
    if (x == s_cx && y == s_cy)
        return;
    s_cx = x;
    s_cy = y;
    if (s_mode == Mode::Remove) {                   // 削除は「カーソルの下の建物」がそのまま選択
        const s32 hovered = Hovered();
        if (hovered != s_selected) {
            Select(hovered);
            return;
        }
    }
    UpdateTiles();
    UpdateHover();
}

bool s_restart;                                     // 画面遷移のあとで再開を待っている
u32 s_restartTicks;

void AfterChange(PublicWorks::Result result) {
    // ★成功した操作のときだけ見る（タイムアウト等で前の操作の記録を読まない）
    if (result == PublicWorks::Result::Ok && PublicWorks::LastReloaded()) {
        static char message[96];
        std::snprintf(message, sizeof(message), u8"部屋を読み直しました（%s）",
                      PublicWorks::ReloadWhyName(PublicWorks::LastReloadWhy()));
        GuiNotification::Notify(Cheats::kBeOn, message);
        Stop();
        s_restart = true;
        s_restartTicks = 0;
    }
}

void Execute(void) {
    const s32 hovered = Hovered();
    switch (s_mode) {
    case Mode::Place: {
        if (s_kindCount == 0)
            return;
        const PublicWorks::Result result = PublicWorks::PlaceAt(s_kinds[s_kind], (u32)s_cx, (u32)s_cy);
        Report(result);
        AfterChange(result);
        return;
    }
    case Mode::Move: {
        // 選べるのは何も選んでいないときだけ（利用者指示）。選んでいる間の A は、ほかの建物の上でも必ず動かす
        if (s_selected < 0) {
            if (hovered >= 0)
                Select(hovered);
            return;
        }
        const PublicWorks::Result result = PublicWorks::MoveTo((u32)s_selected, (u32)s_cx, (u32)s_cy);
        // 動かしたら選択を外す（利用者指示）。Op::Move が光らせ直した要求をここで外す。
        Select(-1);
        Report(result);
        AfterChange(result);
        return;
    }
    case Mode::Remove: {
        if (hovered >= 0 && hovered != s_selected) {
            Select(hovered);
            return;
        }
        if (s_selected < 0)
            return;
        const PublicWorks::Result result = PublicWorks::Remove((u32)s_selected);
        if (result == PublicWorks::Result::Ok)
            Select(Hovered());
        Report(result);
        AfterChange(result);
        return;
    }
    default:
        return;
    }
}

// 配置: X でカーソルの下（無ければ一番近く）の建物の種類を、配置する建物にする（コピー）
void CopyKind(void) {
    s32 slotIndex = Hovered();
    if (slotIndex < 0)
        slotIndex = PublicWorks::NearestTo((u32)s_cx, (u32)s_cy);
    PublicWorks::Slot slot;
    if (slotIndex < 0 || !PublicWorks::ReadSlot((u32)slotIndex, slot)) {
        GuiNotification::NotifyRed(Cheats::kBeOn, u8"近くに建物がありません");
        return;
    }
    for (u32 k = 0; k < s_kindCount; ++k) {
        if (s_kinds[k] == slot.id) {
            s_kind = k;
            UpdateTiles();
            NotifyKind(u8"コピー");
            GameList::Select((s32)s_kind);
            return;
        }
    }
    GuiNotification::NotifyRed(Cheats::kBeOn, u8"この建物は配置の一覧にありません");
}

// 約 0.5 秒ごと: グリッドカーソルが止まっていたら理由を出して組み直し、プレビューが出せない種類なら知らせる
u32 s_watchTicks;
bool s_cursorRetry;
s32 s_previewNotifiedId = -1;

void Watch(void) {
    if (++s_watchTicks % 30 != 0)
        return;
    if (s_cursorRetry) {
        s_cursorRetry = false;
        if (GridCursor::ShowTiles())
            UpdateTiles();
        return;
    }
    const GridCursor::Status gs = GridCursor::Read();
    if (gs.stage == GridCursor::Stage::Failed) {
        // 通知は出さない（利用者指示）。黙って組み直す
        GridCursor::Hide();
        s_cursorRetry = true;
        return;
    }
    const BuildingPreview::Status ps = BuildingPreview::GetStatus();
    if (s_mode == Mode::Place && ps.shownId >= 0 && ps.shownId != s_previewNotifiedId) {
        if (!ps.available) {
            GuiNotification::NotifyRed(Cheats::kBeOn, u8"この建物にはプレビューがありません");
            s_previewNotifiedId = ps.shownId;
        } else if (ps.failed) {
            GuiNotification::NotifyRed(Cheats::kBeOn, (ps.failReason == 10 || ps.failReason == 2 || ps.failReason == 3)
                                                          ? u8"メモリの空きが足りないのでプレビューを出しません"
                                                          : u8"プレビューを作れませんでした");
            s_previewNotifiedId = ps.shownId;
        } else if (ps.ready) {
            s_previewNotifiedId = ps.shownId;
        }
    }
}

}  // namespace

bool Running(void) { return s_running; }

void Reset(void) {
    s_failed = false;
    s_restart = false;
}

void Stop(void) {
    if (!s_running)
        return;
    s_running = false;
    s_want = false;
    // カメラの書き換えは描画スレッドが戻す。0.3 秒まで待つ（30fps で 9 フレーム）。
    for (u32 i = 0; i < 18 && s_patched; ++i)
        svcSleepThread(16666667LL);
    GridCursor::Hide();
    BuildingPreview::Hide();
    GameList::Hide();
    GameLabel::Hide();
    PublicWorks::Unhighlight();
    s_selected = -1;
}

void Tick(u32 keys) {
    if (s_failed)
        return;
    if (!s_running) {
        if (!s_restart) {
            if (!Start(false))
                s_failed = true;
            return;
        }
        // 画面遷移のあと: 村の屋外に戻ってカメラとプレイヤーが揃ったら再開（約 0.5 秒ごとに見る）
        if (++s_restartTicks % 30 == 0 && ReadyToStart() && Start(true))
            s_restart = false;
        return;
    }
    if (s_lost) {
        Stop();
        if (s_lostReason == 1) {                    // 村の屋外を離れた: 戻ったら再開する
            s_restart = true;
            s_restartTicks = 0;
        } else {
            static const char *const kWhy[] = { "", "", u8"カメラが取れません",
                                                u8"カメラの関数がほかの改造で書き換わっています" };
            GuiNotification::NotifyRed(Cheats::kBeOn, kWhy[s_lostReason < 4 ? s_lostReason : 0]);
            s_failed = true;
        }
        return;
    }
    // プレイヤーを止める（メニューの表示中も。毎ティック頼み続けている間だけ効く）
    GuiMenu::BlockGameAll();
    Watch();

    const u32 pressed = keys & ~s_prevKeys;
    s_prevKeys = keys;

    // スライドパッド（画面の上 = マスの -y）
    bool repeating = false;
    for (u32 i = 0; i < 4; ++i)
        repeating = repeating || s_hold[i] > 12;
    if (repeating)
        s_padRepeatingAgo = 0;
    else if (s_padRepeatingAgo < 0xFFFFu)
        ++s_padRepeatingAgo;
    const bool fast = s_padRepeatingAgo <= kPadGrace;
    if (PadRepeat(s_hold[0], (keys & (u32)Key::CPadUp) != 0, fast)) MoveCursor(0, -1);
    if (PadRepeat(s_hold[1], (keys & (u32)Key::CPadDown) != 0, fast)) MoveCursor(0, +1);
    if (PadRepeat(s_hold[2], (keys & (u32)Key::CPadLeft) != 0, fast)) MoveCursor(-1, 0);
    if (PadRepeat(s_hold[3], (keys & (u32)Key::CPadRight) != 0, fast)) MoveCursor(+1, 0);

    if (pressed & ((u32)Key::L | (u32)Key::R)) {
        const u32 count = (u32)Mode::Count;
        const u32 now = (u32)s_mode;
        s_mode = (Mode)((pressed & (u32)Key::R) ? (now + 1) % count : (now + count - 1) % count);
        // 削除はカーソルの下がそのまま選択。移動と配置は選択なしで始める
        // （削除でカーソルを合わせた建物が、移動へ切り替えると選ばれていた。利用者報告）
        Select(s_mode == Mode::Remove ? Hovered() : -1);
        NotifyMode();
    }

    // 配置する種類は下画面のリストで選ぶ（十字キーはリスト自体の操作。利用者指示で十字左右の順送りはやめた）。
    //   十字はゲームの入力を止めたまま、リストの更新の間だけリストへ渡す（GameList::FeedDpad）。
    GameList::FeedDpad(keys & ((u32)Key::DPadUp | (u32)Key::DPadDown | (u32)Key::DPadLeft | (u32)Key::DPadRight));
    TakeListChoice();

    if (pressed & (u32)Key::A)
        Execute();
    // 移動: B で選択を外す（利用者指示）
    if ((pressed & (u32)Key::B) && s_mode == Mode::Move && s_selected >= 0) {
        Select(-1);
        GuiNotification::Notify(Cheats::kBeOn, u8"選択を外しました");
    }
    if ((pressed & (u32)Key::X) && s_mode == Mode::Place)
        CopyKind();
}

}  // namespace BuildingEditor

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            int     g_editorIndex = -1;
            int     g_bpAlpha = -1, g_bpWave = -1, g_bpSpeed = -1;
            bool    g_editorActive;                 // チェック項目の効果（ホットキーで入れ切りする）

            void    PreviewWaveApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                BuildingPreview::Wave w = BuildingPreview::GetWave();
                if (g_bpAlpha >= 0) w.alpha = (u8)GuiMenu::ItemApplied(g_bpAlpha);
                if (g_bpWave >= 0) w.wave = (u8)GuiMenu::ItemApplied(g_bpWave);
                if (g_bpSpeed >= 0) w.speed = (u8)GuiMenu::ItemApplied(g_bpSpeed);
                BuildingPreview::SetWave(w);
            }

            bool    EditorIsActive(int index)
            {
                (void)index;
                return g_editorActive;
            }

            void    EditorSetActive(int index, bool active)
            {
                (void)index;
                g_editorActive = active;
                if (!active)
                {
                    BuildingEditor::Stop();
                    BuildingEditor::Reset();
                }
            }

            const GuiMenu::ToggleEffectFuncs kEditorFuncs = { EditorIsActive, EditorSetActive };
        }

        bool    BuildingEditorTick(int index, u16 held)
        {
            (void)held;
            if (g_editorIndex < 0 || index != g_editorIndex)
                return false;
            if (!g_editorActive)                    // ホットキーで切られている（項目は ON のまま）
                return true;
            // ★held は HotkeyBit の並びでスライドパッドを含まないので、水準を直接読む。
            //   メニュー・入力 UI の表示中は 0 にする（held と同じ取り決め）。
            const u32 keys = GuiMenu::IsVisible() ? 0u : Controller::GetKeysDown(true);
            BuildingEditor::Tick(keys);
            return true;
        }

        bool    BuildingEditorDisable(int index)
        {
            if (g_editorIndex < 0 || index != g_editorIndex)
                return false;
            g_editorActive = false;
            BuildingEditor::Stop();
            BuildingEditor::Reset();                // 次に ON にしたときはやり直す
            return true;
        }

        void    WireBuildingEditor(void)
        {
            g_editorIndex = GuiMenu::FindItem(kBeOn);
            if (g_editorIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_editorIndex, &kEditorFuncs);
            g_bpAlpha = GuiMenu::FindItem(kBpAlpha);
            g_bpWave = GuiMenu::FindItem(kBpWave);
            g_bpSpeed = GuiMenu::FindItem(kBpSpeed);
            const int items[] = { g_bpAlpha, g_bpWave, g_bpSpeed };
            for (u32 k = 0; k < 3; ++k)
                if (items[k] >= 0)
                    GuiMenu::RegisterApply(items[k], PreviewWaveApplied);
            PreviewWaveApplied(-1, 0);
        }
    }
}
