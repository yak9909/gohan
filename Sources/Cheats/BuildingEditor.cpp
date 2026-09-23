#include "BuildingEditor.hpp"

#include "BuildingHighlight.hpp"
#include "BuildingPreview.hpp"
#include "Cheats.hpp"
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

// ---- メニュー ↔ 描画スレッド -------------------------------------------------------------------
volatile bool s_want;           // メニュー: エディターを動かしたい
volatile bool s_patched;        // 描画: カメラを止めている
volatile bool s_lost;           // 描画: 場面が変わった／カメラが取れない
volatile s32 s_cx, s_cy;        // カーソルのマス
volatile u32 s_lostReason;

// ---- 描画スレッドだけ ---------------------------------------------------------------------------
u32 s_camera;
float s_offset[3];
float s_current[3];

// ---- メニュースレッドだけ ------------------------------------------------------------------------
bool s_running;
bool s_failed;                  // 失敗したらチェックを外すまで再開しない
bool s_prevDiagonal;            // グリッドカーソルの縞の向きを借りる前の値
Mode s_mode;
u8 s_kinds[256];
u32 s_kindCount;
u32 s_kind;
s32 s_selected = -1;
u32 s_prevKeys;
u32 s_hold[4];                  // スライドパッド 上下左右の押し続けティック
u32 s_dpadHold[2];              // 十字 左右（建物の切り替え）

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

void Lose(u32 reason) {
    Unpatch();
    s_lostReason = reason;
    s_lost = true;
}

}  // namespace

void FrameStep(void) {
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
    target[1] = GroundHeight(target, 0);
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
// 足元データ（Strc/data/<名>.bin）の各マスの属性コード（byte 8）を、ゲームの属性の分類表で引き、
// **0 でないマス = プレイヤーが歩けないマス**とする（利用者の定義「衝突判定 = 歩けない場所」）。
// 分類表は `sub_5CD3C0`（`code >= 0xFF` なら 0 番、`byte_957B34[code]`）。フィールドの位置から属性を読んで
// この表で分類する `sub_6C4BDC` が使っている（IDA-opus-5.5-F017）。例: ベンチの外周 0x05 は 2（歩けない）→ 4x3、
// 柵・橋・交番の外周 0x31/0x32/0x33/0xA0 は 0（歩ける）→ 論理サイズと同じ。表はゲームのメモリから読む。
const u32 kCollisionClass = 0x00957B34;

struct Shape {
    bool loaded;
    u8 count;
    s8 dx[kMaxCells];
    s8 dy[kMaxCells];
};
Shape s_shapes[256];

u8 CollisionClass(u8 code) {
    return *reinterpret_cast<const volatile u8 *>(kCollisionClass + (code >= 0xFF ? 0u : code));
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
    }
    if (got == kFootprintBytes) {
        for (s32 r = 0; r < kFootprintSide; ++r) {
            for (s32 c = 0; c < kFootprintSide; ++c) {
                const u8 code = s_footprint[160 * r + 10 * c + 8];
                if (code == 0 || CollisionClass(code) == 0 || s.count >= kMaxCells)
                    continue;
                s.dx[s.count] = (s8)(c - kFootprintOrigin);
                s.dy[s.count] = (s8)(r - kFootprintOrigin);
                ++s.count;
            }
        }
    }
    if (s.count == 0) {                             // 名前の無い家など: 基点の 1 マス
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

// カーソルの下の建物。**衝突判定のマスがカーソルに重なる建物**のうち、建物の基点が一番近いもの。
// 重なる建物が無ければ、ゲームの占有マップ（足元の全マス）で引く。
s32 Hovered(void) {
    s32 best = -1;
    s32 bestDistance = 0x7FFFFFFF;
    for (u32 i = 0; i < PublicWorks::kSlots; ++i) {
        PublicWorks::Slot slot;
        if (!PublicWorks::ReadSlot(i, slot) || slot.id >= PublicWorks::kEmptyId || !Covers(slot, s_cx, s_cy))
            continue;
        const s32 dx = (s32)slot.x - s_cx;
        const s32 dy = (s32)slot.y - s_cy;
        if (dx * dx + dy * dy < bestDistance) {
            bestDistance = dx * dx + dy * dy;
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
    // 高さは全部そろえて、その建物をそこへ建てたときの高さ（設置プレビューと同じ）
    GridCursor::SetTiles(xs, ys, n, id, (u8)ax, (u8)ay);
}

void PutSingle(u32 color, u8 strength) {
    const u8 x = (u8)s_cx;
    const u8 y = (u8)s_cy;
    GridCursor::SetTint(color, strength);
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

void Select(s32 slot) {
    s_selected = slot;
    if (slot < 0) {
        PublicWorks::Unhighlight();
    } else {
        BuildingHighlight::SetColor(s_mode == Mode::Remove ? BuildingHighlight::kRed : BuildingHighlight::kBlue);
        PublicWorks::Highlight((u32)slot);
    }
    UpdateTiles();
}

void NotifyKind(const char *prefix) {
    static char message[96];
    if (s_kindCount == 0)
        return;
    const u8 id = s_kinds[s_kind];
    std::snprintf(message, sizeof(message), u8"%s 0x%02X %s", prefix, (unsigned)id, PublicWorks::NameOf(id));
    GuiNotification::Notify(Cheats::kBeOn, message);
}

void NotifyMode(void) {
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
    s_cx = (s32)x;
    s_cy = (s32)y;
    s_mode = Mode::Place;
    s_selected = -1;
    s_prevKeys = 0xFFFFFFFFu;                       // 押しっぱなしのボタンを最初の押下にしない
    std::memset(s_hold, 0, sizeof(s_hold));
    std::memset(s_dpadHold, 0, sizeof(s_dpadHold));
    PublicWorks::Unhighlight();
    UpdateTiles();
    s_prevDiagonal = GridCursor::DiagonalStripes();
    GridCursor::SetDiagonalStripes(true);           // 傾きは斜め（利用者指示）
    if (!GridCursor::ShowTiles()) {
        GridCursor::SetDiagonalStripes(s_prevDiagonal);
        GuiNotification::NotifyRed(Cheats::kBeOn, u8"グリッドカーソルを先に止めてください");
        s_failed = true;
        return false;
    }
    s_lost = false;
    s_want = true;
    s_running = true;
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
}

// Y + 十字: 建物のあるスロットを順に選ぶ
void Cycle(s32 step) {
    s32 at = s_selected;
    for (u32 n = 0; n < PublicWorks::kSlots; ++n) {
        at += step;
        if (at < 0) at = (s32)PublicWorks::kSlots - 1;
        if (at >= (s32)PublicWorks::kSlots) at = 0;
        PublicWorks::Slot slot;
        if (PublicWorks::ReadSlot((u32)at, slot) && slot.id < PublicWorks::kEmptyId) {
            s_cx = slot.x;
            s_cy = slot.y;
            if (s_mode == Mode::Place)
                UpdateTiles();                      // 配置では選択にしない（カーソルだけ飛ぶ）
            else
                Select(at);
            static char message[96];
            std::snprintf(message, sizeof(message), u8"%ld番 0x%02X %s (%u,%u)", (long)at, (unsigned)slot.id,
                          PublicWorks::NameOf(slot.id), (unsigned)slot.x, (unsigned)slot.y);
            GuiNotification::Notify(Cheats::kBeOn, message);
            return;
        }
    }
}

bool s_restart;                                     // 画面遷移のあとで再開を待っている
u32 s_restartTicks;

void AfterChange(void) {
    if (PublicWorks::LastReloaded()) {              // 部屋を読み直した。カメラも実体も作り直されている
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
        Report(PublicWorks::PlaceAt(s_kinds[s_kind], (u32)s_cx, (u32)s_cy));
        AfterChange();
        return;
    }
    case Mode::Move: {
        if (hovered >= 0 && hovered != s_selected) {
            Select(hovered);
            return;
        }
        if (s_selected < 0)
            return;
        const PublicWorks::Result result = PublicWorks::MoveTo((u32)s_selected, (u32)s_cx, (u32)s_cy);
        // 動かしたら選択を外す（利用者指示）。Op::Move が光らせ直した要求をここで外す。
        Select(-1);
        Report(result);
        AfterChange();
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
        AfterChange();
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
            return;
        }
    }
    GuiNotification::NotifyRed(Cheats::kBeOn, u8"この建物は配置の一覧にありません");
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
    GridCursor::SetDiagonalStripes(s_prevDiagonal);
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

    const u32 pressed = keys & ~s_prevKeys;
    s_prevKeys = keys;
    const bool y = (keys & (u32)Key::Y) != 0;

    // スライドパッド（画面の上 = マスの -y）
    if (Repeat(s_hold[0], (keys & (u32)Key::CPadUp) != 0)) MoveCursor(0, -1);
    if (Repeat(s_hold[1], (keys & (u32)Key::CPadDown) != 0)) MoveCursor(0, +1);
    if (Repeat(s_hold[2], (keys & (u32)Key::CPadLeft) != 0)) MoveCursor(-1, 0);
    if (Repeat(s_hold[3], (keys & (u32)Key::CPadRight) != 0)) MoveCursor(+1, 0);

    if (pressed & ((u32)Key::L | (u32)Key::R)) {
        const u32 count = (u32)Mode::Count;
        const u32 now = (u32)s_mode;
        s_mode = (Mode)((pressed & (u32)Key::R) ? (now + 1) % count : (now + count - 1) % count);
        // 削除はカーソルの下がそのまま選択。移動は選んでいるものを引き継ぐ。配置は選択なし
        Select(s_mode == Mode::Remove ? Hovered() : s_mode == Mode::Move ? s_selected : -1);
        NotifyMode();
    }

    if (y) {
        if (pressed & ((u32)Key::DPadRight | (u32)Key::DPadDown)) Cycle(+1);
        if (pressed & ((u32)Key::DPadLeft | (u32)Key::DPadUp)) Cycle(-1);
        s_dpadHold[0] = s_dpadHold[1] = 0;
    } else if (s_mode == Mode::Place && s_kindCount > 0) {
        bool changed = false;
        if (Repeat(s_dpadHold[0], (keys & (u32)Key::DPadLeft) != 0)) {
            s_kind = (s_kind + s_kindCount - 1) % s_kindCount;
            changed = true;
        }
        if (Repeat(s_dpadHold[1], (keys & (u32)Key::DPadRight) != 0)) {
            s_kind = (s_kind + 1) % s_kindCount;
            changed = true;
        }
        if (changed) {
            // 形は 1 回読めば残る。モデルの読み込みは描画スレッドが押す手が止まってから始める（待たない）
            UpdateTiles();
            NotifyKind(ModeName(s_mode));
        }
    }

    if (pressed & (u32)Key::A)
        Execute();
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
