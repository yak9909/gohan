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

// 足元の形（建物の (x, y) からのずれ）
s8 s_cellDx[kMaxCells];
s8 s_cellDy[kMaxCells];
u32 s_cellCount;
s32 s_cellsFor = -1;            // どの id の形か
u8 s_footprint[kFootprintBytes];

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

// その建物の衝突判定のマスを読む。足元データのうちゲームが属性を書くマス（Building_WriteOccupancy
// 0x526A0C と同じ規則: 橋は 0 と 0xA1 を飛ばし、それ以外は 0 だけ飛ばす）の、**外接の四角を上下左右
// 1 マスずつ縮めた内側**（利用者指示「一回り小さい方」。ベンチ 4x3 -> 2x1、街灯 3x3 -> 1x1、
// 噴水 5x5 -> 3x3、南北の橋 4x6 -> 橋板 2x4）。縮めて何も残らない形はそのまま。読めなければ 1 マス。
void LoadCells(u16 id) {
    if (s_cellsFor == (s32)id)
        return;
    s_cellsFor = id;
    s_cellCount = 0;
    char path[96];
    const char *name = PublicWorks::NameOf(id);
    u32 got = 0;
    if (name[0] != '\0') {
        std::snprintf(path, sizeof(path), "Strc/data/%s.bin", name);
        got = RomfsIndex::ReadFile(path, s_footprint, sizeof(s_footprint));
    }
    if (got == kFootprintBytes) {
        const bool bridge = PublicWorks::IsBridgeId(id);
        bool written[kFootprintSide][kFootprintSide];
        s32 top = kFootprintSide, bottom = -1, left = kFootprintSide, right = -1;
        for (s32 r = 0; r < kFootprintSide; ++r) {
            for (s32 c = 0; c < kFootprintSide; ++c) {
                const u8 code = s_footprint[160 * r + 10 * c + 8];
                written[r][c] = bridge ? (code != 0 && code != 0xA1) : code != 0;
                if (!written[r][c])
                    continue;
                if (r < top) top = r;
                if (r > bottom) bottom = r;
                if (c < left) left = c;
                if (c > right) right = c;
            }
        }
        const bool shrink = bottom - top >= 2 && right - left >= 2;
        for (s32 r = 0; r < kFootprintSide; ++r) {
            for (s32 c = 0; c < kFootprintSide; ++c) {
                if (!written[r][c] || s_cellCount >= kMaxCells)
                    continue;
                if (shrink && (r <= top || r >= bottom || c <= left || c >= right))
                    continue;
                s_cellDx[s_cellCount] = (s8)(c - kFootprintOrigin);
                s_cellDy[s_cellCount] = (s8)(r - kFootprintOrigin);
                ++s_cellCount;
            }
        }
    }
    if (s_cellCount == 0) {
        s_cellDx[0] = 0;
        s_cellDy[0] = 0;
        s_cellCount = 1;
    }
}

// いまのモードで、カーソルの下に出すマス。
void UpdateTiles(void) {
    u8 xs[kMaxCells];
    u8 ys[kMaxCells];
    u32 n = 0;
    bool footprint = false;
    if (s_mode == Mode::Place && s_kindCount > 0) {
        LoadCells(s_kinds[s_kind]);
        footprint = true;
    } else if (s_mode == Mode::Move) {
        PublicWorks::Slot slot;
        if (s_selected >= 0 && PublicWorks::ReadSlot((u32)s_selected, slot)) {
            LoadCells(slot.id);
            footprint = true;
        } else {
            xs[0] = (u8)s_cx;
            ys[0] = (u8)s_cy;
            n = 1;
        }
    }
    if (footprint) {
        for (u32 i = 0; i < s_cellCount && n < kMaxCells; ++i) {
            const s32 x = s_cx + s_cellDx[i];
            const s32 y = s_cy + s_cellDy[i];
            if (x < 0 || y < 0 || x >= kTilesX || y >= kTilesY)
                continue;
            xs[n] = (u8)x;
            ys[n] = (u8)y;
            ++n;
        }
    }
    // 削除モードは出さない（利用者指示）
    GridCursor::SetTiles(xs, ys, n);
    // 設置プレビュー（配置モードだけ。色は合成しない）
    if (s_mode == Mode::Place && s_kindCount > 0)
        BuildingPreview::Show(s_kinds[s_kind], s_cx, s_cy);
    else
        BuildingPreview::Hide();
}

void Select(s32 slot) {
    s_selected = slot;
    Cheats::SetSelectedPublicWork(slot);         // メニューの「選んだ公共事業」と同じもの
    if (slot < 0) {
        PublicWorks::Unhighlight();
    } else {
        BuildingHighlight::SetColor(s_mode == Mode::Remove ? BuildingHighlight::kRed : BuildingHighlight::kBlue);
        PublicWorks::Highlight((u32)slot);
    }
    UpdateTiles();
}

void NotifyKind(void) {
    static char message[96];
    if (s_kindCount == 0)
        return;
    const u8 id = s_kinds[s_kind];
    std::snprintf(message, sizeof(message), u8"%s 0x%02X %s", ModeName(s_mode), (unsigned)id, PublicWorks::NameOf(id));
    GuiNotification::Notify(Cheats::kBeOn, message);
}

void NotifyMode(void) {
    if (s_mode == Mode::Place) {
        NotifyKind();
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

bool Start(void) {
    if (!PublicWorks::StartFrameHook()) {
        GuiNotification::NotifyRed(Cheats::kBeOn, u8"フックが入れられません");
        return false;
    }
    u32 x = 0, y = 0;
    if (!PublicWorks::PlayerTile(x, y)) {
        GuiNotification::NotifyRed(Cheats::kBeOn, u8"村の屋外で使ってください");
        return false;
    }
    if (s_kindCount == 0) {
        for (u32 id = 0; id < PublicWorks::kEmptyId && s_kindCount < sizeof(s_kinds); ++id)
            if (PublicWorks::NameOf((u16)id)[0] != '\0')
                s_kinds[s_kindCount++] = (u8)id;
    }
    s_cx = (s32)x;
    s_cy = (s32)y;
    s_mode = Mode::Place;
    s_selected = -1;
    s_prevKeys = 0;
    std::memset(s_hold, 0, sizeof(s_hold));
    std::memset(s_dpadHold, 0, sizeof(s_dpadHold));
    s_cellsFor = -1;
    PublicWorks::Unhighlight();
    UpdateTiles();
    s_prevDiagonal = GridCursor::DiagonalStripes();
    GridCursor::SetDiagonalStripes(true);         // 傾きは斜め（利用者指示）
    if (!GridCursor::ShowTiles()) {
        GridCursor::SetDiagonalStripes(s_prevDiagonal);
        GuiNotification::NotifyRed(Cheats::kBeOn, u8"グリッドカーソルを先に止めてください");
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
    if (s_mode == Mode::Remove) {               // 削除は「カーソルの下の建物」がそのまま選択
        const s32 hovered = PublicWorks::SlotAtTile((u32)x, (u32)y);
        if (hovered != s_selected)
            Select(hovered);
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
            Select(at);
            static char message[96];
            std::snprintf(message, sizeof(message), u8"%ld番 0x%02X %s (%u,%u)", (long)at, (unsigned)slot.id,
                          PublicWorks::NameOf(slot.id), (unsigned)slot.x, (unsigned)slot.y);
            GuiNotification::Notify(Cheats::kBeOn, message);
            return;
        }
    }
}

void AfterChange(void) {
    if (PublicWorks::LastReloaded()) {          // 部屋を読み直した。カメラも実体も作り直されている
        GuiNotification::Notify(Cheats::kBeOn, u8"部屋を読み直したので終了しました");
        Stop();
        s_failed = true;
    }
}

void Execute(void) {
    const s32 hovered = PublicWorks::SlotAtTile((u32)s_cx, (u32)s_cy);
    switch (s_mode) {
    case Mode::Place: {
        if (s_kindCount == 0)
            return;
        const u8 id = s_kinds[s_kind];
        Report(PublicWorks::PlaceAt(id, (u32)s_cx, (u32)s_cy));
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
        // 動かしたら選択を外す（利用者指示）。Op::Move は光っていた建物を新しい実体で光らせ直すので、
        // そのあとで外す要求を置く（次のフレームで反映）。
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
            s_selected = -1;
        Report(result);
        AfterChange();
        return;
    }
    default:
        return;
    }
}

}  // namespace

bool Running(void) { return s_running; }

void Reset(void) { s_failed = false; }

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
        if (!Start())
            s_failed = true;
        return;
    }
    if (s_lost) {
        static const char *const kWhy[] = { "", u8"村の屋外を離れたので終了しました", u8"カメラが取れません",
                                            u8"カメラの関数がほかの改造で書き換わっています" };
        const u32 why = s_lostReason < 4 ? s_lostReason : 0;
        GuiNotification::NotifyRed(Cheats::kBeOn, kWhy[why]);
        Stop();
        s_failed = true;
        return;
    }
    // プレイヤーを止める（メニューの表示中も。毎ティック頼み続けている間だけ効く）
    GuiMenu::BlockGameAll();

    const u32 pressed = keys & ~s_prevKeys;
    s_prevKeys = keys;
    const bool y = (keys & (u32)Key::Y) != 0;

    // スライドパッド（画面の上 = マスの -y と仮定。実機で確かめる）
    if (Repeat(s_hold[0], (keys & (u32)Key::CPadUp) != 0)) MoveCursor(0, -1);
    if (Repeat(s_hold[1], (keys & (u32)Key::CPadDown) != 0)) MoveCursor(0, +1);
    if (Repeat(s_hold[2], (keys & (u32)Key::CPadLeft) != 0)) MoveCursor(-1, 0);
    if (Repeat(s_hold[3], (keys & (u32)Key::CPadRight) != 0)) MoveCursor(+1, 0);

    if (pressed & ((u32)Key::L | (u32)Key::R)) {
        const u32 count = (u32)Mode::Count;
        const u32 now = (u32)s_mode;
        s_mode = (Mode)((pressed & (u32)Key::R) ? (now + 1) % count : (now + count - 1) % count);
        // 削除はカーソルの下がそのまま選択。ほかは選んでいるものを引き継ぎ、色だけ替える
        Select(s_mode == Mode::Remove ? PublicWorks::SlotAtTile((u32)s_cx, (u32)s_cy) : s_selected);
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
            UpdateTiles();
            NotifyKind();
        }
    }

    if (pressed & (u32)Key::A)
        Execute();

    // 配置: X でカーソルに一番近い建物を「選んだ建物」にする（利用者指示）
    if ((pressed & (u32)Key::X) && s_mode == Mode::Place) {
        const s32 nearest = PublicWorks::NearestTo((u32)s_cx, (u32)s_cy);
        if (nearest < 0) {
            GuiNotification::NotifyRed(Cheats::kBeOn, u8"近くに建物がありません");
        } else {
            Select(nearest);
            PublicWorks::Slot slot;
            static char message[96];
            if (PublicWorks::ReadSlot((u32)nearest, slot)) {
                std::snprintf(message, sizeof(message), u8"選んだ: %ld番 0x%02X %s (%u,%u)", (long)nearest,
                              (unsigned)slot.id, PublicWorks::NameOf(slot.id), (unsigned)slot.x, (unsigned)slot.y);
                GuiNotification::Notify(Cheats::kBeOn, message);
            }
        }
    }
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
        }

        bool    BuildingEditorTick(int index, u16 held)
        {
            (void)held;
            if (g_editorIndex < 0 || index != g_editorIndex)
                return false;
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
            BuildingEditor::Stop();
            BuildingEditor::Reset();                // 次に ON にしたときはやり直す
            return true;
        }

        void    WireBuildingEditor(void)
        {
            g_editorIndex = GuiMenu::FindItem(kBeOn);
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
