#include "FieldCamera.hpp"

#include "GuiMenu.hpp"

// BuildingEditor.cpp のカメラ部分を切り出したもの（中身は同じ。IDA-opus-5.5-F015 と実機での調整）。

namespace FieldCamera {

namespace {

const u32 kCameraGame = 0x0094A880;         // u32: CameraGame*（dtor 0x1A7C1C が 0 を書く）
const u32 kCameraBase = 4;                  // float x, y, z: 基準位置（注視点 = 基準 + +16..）
const u32 kCameraPatch = 0x001A5128;        // sub_1A5124 の 2 語目
const u32 kCameraPatchOrig = 0xE2805C01;    // ADD R5,R0,#0x100
const u32 kCameraPatchPop = 0xE8BD81F0;     // POP {R4-R8,PC}（先頭の PUSH と同じ組。戻り値は呼び元が使わない）
const u32 kRoomIdByte = 0x0095133A;         // u8: 0 = 村の屋外
const u32 kPlayerPtr = 0x00AA7994;          // Player*
const u32 kPlayerPosition = 0x14;
const float kCameraFollow = 0.35f;          // 1 フレームで目標へ寄る割合

volatile TargetFn s_target;
volatile bool s_want;           // メニュー: 追いたい
volatile bool s_patched;        // 描画: カメラを止めている
volatile bool s_lost;           // 描画: 場面が変わった／カメラが取れない
volatile u32 s_lostReason;
volatile bool s_snap;           // 再開: カメラをプレイヤーから滑らせず、最初から目標へ置く

// ---- 描画スレッドだけ ----
u32 s_camera;
float s_offset[3];
float s_current[3];

u32 R32(u32 a) { return *reinterpret_cast<volatile u32 *>(a); }
bool IsHeap(u32 p) { return p >= 0x08000000u && p < 0x40000000u && (p & 3u) == 0u; }

bool InVillage(void) {
    return *reinterpret_cast<volatile u8 *>(kRoomIdByte) == 0;
}

void WriteCode(u32 addr, u32 value) {
    *reinterpret_cast<volatile u32 *>(addr) = value;
    CTRPluginFramework::GuiMenu::FlushMemory(addr, 4);
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

void Want(TargetFn target, bool snap) {
    s_target = target;
    s_snap = snap;
    s_lost = false;
    s_want = true;
}

void Release(void) {
    s_want = false;
}

bool Patched(void) {
    return s_patched;
}

bool Lost(u32 &reason) {
    reason = s_lostReason;
    return s_lost;
}

bool Available(void) {
    return InVillage() && IsHeap(R32(kCameraGame)) && IsHeap(R32(kPlayerPtr));
}

void FrameStep(void) {
    const TargetFn target = s_target;
    if (!s_patched) {
        if (!s_want || s_lost || target == nullptr)
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
        // 基準位置とプレイヤーのずれを控えて、目標へ同じずれで付ける。
        // ★横（x）と奥行き（z）のずれは使わない。歩いた直後のカメラはプレイヤーから遅れていて、そのずれまで引き継ぐと
        //   カーソルが画面の中央から偏った（利用者報告: カメラがプレイヤーより右にあるとカーソルが左に偏る）。
        //   落ち着いたカメラと同じく、目標を基準位置（注視点側）に置く。高さのずれだけ残す。
        const float *base = reinterpret_cast<const float *>(camera + kCameraBase);
        const float *pos = reinterpret_cast<const float *>(player + kPlayerPosition);
        for (u32 i = 0; i < 3; ++i) {
            s_offset[i] = i == 1 ? base[i] - pos[i] : 0.0f;
            s_current[i] = base[i];
        }
        if (s_snap) {
            float at[3];
            target(at);
            for (u32 i = 0; i < 3; ++i)
                s_current[i] = at[i] + s_offset[i];
            s_snap = false;
        }
        s_camera = camera;
        WriteCode(kCameraPatch, kCameraPatchPop);
        s_patched = true;
        return;
    }
    if (!s_want || target == nullptr) {
        Unpatch();
        return;
    }
    if (!InVillage() || R32(kCameraGame) != s_camera) {
        Lose(1);
        return;
    }
    float at[3];
    target(at);
    float *base = reinterpret_cast<float *>(s_camera + kCameraBase);
    for (u32 i = 0; i < 3; ++i) {
        s_current[i] += (at[i] + s_offset[i] - s_current[i]) * kCameraFollow;
        base[i] = s_current[i];
    }
}

}  // namespace FieldCamera
