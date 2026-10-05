#pragma once

#include <3ds.h>

// HHD のキャラクリ画面を ACNL の下画面に出す（解析リポジトリの IDA-opus-5.5-D005 / T012）。
// いまは最小の実機確認だけ: SD の DARC（/hhd_bg_only.arc。tools/hhd/build_hhd_arc.py --only hhd_bg）を
// ゲームのヒープへ写して ArcResourceAccessor に渡し（ArcResAccReader_LoadArcStep 0x567244 が読み込み後にする手順と同じ）、
// hhd_bg.bclyt を組んで下画面の LayoutMgr へ毎フレーム出す。
// スレッド: Show / Hide はメニューのスレッド（SD を読むのもここ）。ゲームの関数は FrameStep（ゲームのスレッド）の中だけ。
namespace HhdScreen
{
    bool        Show(void);             // 偽: SD のファイルを読めない / フックを入れられない（LastError）
    void        Hide(void);             // 描くのをやめ、数フレーム後に資源を返す
    bool        Shown(void);
    const char *LastError(void);
    const char *StageName(void);        // いまの段（状態の通知用）

    void        FrameStep(void);        // ゲームのスレッド
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WireHhdScreen(void);    // root/テスト/HHD キャラクリ
    }
}
