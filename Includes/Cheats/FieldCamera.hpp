#pragma once

#include <3ds.h>

// 村の屋外のカメラを、プレイヤーではなく持ち主の決めた目標へ寄せる（公共事業エディター・マップエディター共用）。
//
// カメラ: `CameraGame`（`*(0x0094A880)`）の基準位置 +4/+8/+C を毎フレーム書く。ゲームは `sub_1A5124` で目標値（+276..）を
//   基準位置へ写すので、使っている間だけその関数の 2 語目を `POP {R4-R8,PC}` にして止める（Vapecord の UNLOCKCAMERA と同じ語。
//   JPN 0x001A5128）。高さのずれ（基準位置 − プレイヤー）だけ引き継ぎ、横と奥行きは目標そのもの（BuildingEditor の実機調整のまま）。
// 持ち主は同時に 1 つ（エディター同士は同時に動かさない）。
namespace FieldCamera
{
    // 描画スレッドで毎フレーム呼ばれ、目標（ワールド x, 高さ, z）を書く
    typedef void (*TargetFn)(float out[3]);

    // ---- メニュースレッド ----
    // 目標を追い始める。snap = 滑らせず最初から目標へ置く（画面遷移のあとの再開）
    void            Want(TargetFn target, bool snap);
    void            Release(void);                  // 次のフレームで元へ戻す
    bool            Patched(void);                  // まだカメラを止めている
    // 追えなくなった（1 = 村の屋外を離れた / 2 = カメラかプレイヤーが取れない / 3 = カメラの関数がほかの改造で書き換わっている）
    bool            Lost(u32 &reason);
    // 村の屋外で、カメラとプレイヤーが取れる（始めてよい）
    bool            Available(void);

    // ---- 描画スレッド（PublicWorks::FrameStep から）----
    void            FrameStep(void);
}
