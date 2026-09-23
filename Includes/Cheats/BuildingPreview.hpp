#pragma once

#include <3ds.h>

// 建物エディターの設置プレビュー。置こうとしている建物のモデルを、実際に置いたときと同じ位置・高さに
// 1 体だけ出し、不透明度を sin 波で揺らす。色は合成しない。
//
// 読み込みはゲーム自身の公共事業プレビュー `PwpPreview_LoadSlot 0x227CFC` と同じ組み合わせ:
//   モデル   Strc/{fobj,sobj}/<名>/<名>.bcres
//   テクスチャ Strc/fobj/<名>/Textures/season00.bcres / Strc/sobj/<名>/Textures/season00/season00.bcres
//   LUT      Strc/fobj/lut/Lut_fieldobj.bcres / Strc/sobj/lut/Lut_sobj.bcres
// を 3 つの G3dResHolder に読み、G3dResHolder_Setup(モデル, ヒープ, 0 → LUT → テクスチャ) で結ぶ。
// 置き場はモデルビューアと同じく親ヒープ `*(0x94CC48)` から作る自前のヒープ（ゲームの建物の枠は使わない）。
// インスタンスは mask 0x834（汎用の書き出し関数、材質の本体はインスタンスごとの写し: F010 §2）で作り、
// その写しのフラグメント設定を定数アルファのブレンドにする（IDA-gpt-6-astra-F002 と同じ欄）。
namespace BuildingPreview
{
    struct Wave
    {
        u8  alpha;      // 不透明度の中心 0〜255
        u8  wave;       // 揺れ幅
        u8  speed;      // 1 フレームに周期の speed/1000
    };

    // ---- メニュースレッド ----
    // id の建物を (x, y) に出す（違う id なら組み直す）。モデルが無い種類は何も出さない。
    void            Show(u16 id, s32 x, s32 y);
    void            Hide(void);
    void            SetWave(const Wave &wave);
    const Wave &    GetWave(void);
    // 通知用: いまの状態の名前
    const char *    StateName(void);
    struct Status
    {
        s32     shownId;        // 最後に頼まれた id
        bool    available;      // その種類にプレビューの資源がある（無ければ出さない）
        bool    failed;
        u32     failReason;     // 10 = 親ヒープの空きが足りない（ゲームの分を残すため作らない）
        bool    ready;
    };
    Status          GetStatus(void);

    // ---- 描画スレッド（PublicWorks::FrameStep から）----
    void            FrameStep(void);
}
