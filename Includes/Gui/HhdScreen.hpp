#pragma once

#include <3ds.h>

// HHD のキャラクリ画面を ACNL の下画面に出す（解析リポジトリの IDA-opus-5.5-D005 / T012）。
// SD の DARC（/hhd_charcreate.arc。tools/hhd/build_hhd_arc.py）をゲームのヒープへ写して ArcResourceAccessor に渡し
// （ArcResAccReader_LoadArcStep 0x567244 が読み込み後にする手順と同じ。F073 で実機確認）、
// 地・顔・目・髪の 4 枚を組み、地・顔・目（髪のモードでは髪）を下画面の最前面（優先度 0xFF）へ毎フレーム出す。
// 開いている間はゲームへの入力を止め、タッチで目の形・目の色・肌・髪・髪の色を選び、左のボタンで顔 ↔ 髪、右のボタンか B で閉じる（T013 段 2）。
// スレッド: Show / Hide はメニューのスレッド（SD を読むのもここ）。ゲームの関数は FrameStep（ゲームのスレッド）の中だけ。
namespace HhdScreen
{
    bool        Show(void);             // 偽: SD のファイルを読めない / フックを入れられない（LastError）
    void        Hide(void);             // 描くのをやめ、数フレーム後に資源を返す
    bool        Shown(void);
    const char *LastError(void);
    const char *ApplyResult(void);      // 「けってい」の反映の結果（空 = まだ）
    u32         ProfileSyncResult(void); // 目の形を通信相手へ送った結果（0 未 / 1 送った / 2 送れなかった。T020）
    const char *StageName(void);        // いまの段（状態の通知用）
    void        Measure(char *out, u32 size);   // 測った値（コマンドの使用量 / 確保、ヒープの空き）

    void        Tick(bool menuVisible); // メニューのスレッド（毎ティック）: ゲームの入力を止め、タッチと B を読む
    void        FrameStep(void);        // ゲームのスレッド
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WireHhdScreen(void);    // root/テスト/HHD キャラクリ
        bool    HhdScreenTick(int index, u16 held);
        bool    HhdScreenDisable(int index);
    }
}
