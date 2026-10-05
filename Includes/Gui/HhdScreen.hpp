#pragma once

#include <3ds.h>

// HHD のキャラクリ画面を ACNL の下画面に出す（解析リポジトリの IDA-opus-5.5-D005 / T012）。
// SD の DARC（/hhd_charcreate.arc。tools/hhd/build_hhd_arc.py）をゲームのヒープへ写して ArcResourceAccessor に渡し
// （ArcResAccReader_LoadArcStep 0x567244 が読み込み後にする手順と同じ。F073 で実機確認）、
// 地・顔・目・髪の 4 枚を組み、地・顔・目を下画面の最前面（優先度 0xFF）へ毎フレーム出す（T013 の静止画の段）。
// スレッド: Show / Hide はメニューのスレッド（SD を読むのもここ）。ゲームの関数は FrameStep（ゲームのスレッド）の中だけ。
namespace HhdScreen
{
    bool        Show(void);             // 偽: SD のファイルを読めない / フックを入れられない（LastError）
    void        Hide(void);             // 描くのをやめ、数フレーム後に資源を返す
    bool        Shown(void);
    const char *LastError(void);
    const char *StageName(void);        // いまの段（状態の通知用）
    void        Measure(char *out, u32 size);   // 測った値（コマンドの使用量 / 確保、ヒープの空き）

    void        FrameStep(void);        // ゲームのスレッド
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WireHhdScreen(void);    // root/テスト/HHD キャラクリ
    }
}
