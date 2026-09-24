#pragma once

#include <3ds.h>

// ゲームの「所持ベルの箱」（time_bel_win.arc の N_bell。テクスチャ my_cmn_base_00）を上画面の左上に借りて、
// 短い文字を出す汎用部品。箱は kSlots 個まで、上から順に並ぶ。設計と根拠: docs/topics/game_list.md（IDA-opus-5.5-F036）。
//
// 時計（N_time）とベルのアイコンは隠し、数字専用の書体の欄 T_bell_00 を通常の書体（Garden_msg_size16）に替え、
// 文字の器をゲームの関数（TextBox vt[28]）で kMaxChars 文字へ広げて書く。箱の幅は文字の長さに合わせて伸ばし、左端をそろえる。
// 出入りはゲームと同じ time_bel_win_in/out を G_bell_00 に結んで回す（途中で逆向きにできる）。
// スレッド: SetText / Show / Hide はメニューのスレッド。ゲームの関数は FrameStep（ゲームのスレッド）の中だけ。
namespace GameLabel
{
    const u32   kSlots = 3;
    const u32   kMaxChars = 24;

    void        SetText(u32 slot, const char *utf8);    // 出ている間でも差し替えられる
    void        Show(u32 slot);
    void        Hide(u32 slot);                         // 退場が終われば資源を返す（全部の箱が消えたら arc も）
    bool        Present(void);
    const char *LastError(void);

    void        FrameStep(void);                        // ゲームのスレッド
}
