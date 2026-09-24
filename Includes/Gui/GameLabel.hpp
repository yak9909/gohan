#pragma once

#include <3ds.h>

// ゲームの「所持ベルの箱」（time_bel_win.arc の N_bell。テクスチャ my_cmn_base_00）を上画面に借りて、
// 短い文字を出す汎用部品。設計と根拠: docs/topics/game_list.md（IDA-opus-5.5-F036）。
//
// 時計（N_time）とベルのアイコンは隠し、数字専用の書体の欄 T_bell_00 を通常の書体（Garden_msg_size16）に替えて書く。
// 出入りはゲームと同じ time_bel_win_in/out を G_bell に結んで回す（途中で逆向きにできる）。
// スレッド: Show / Hide / SetText はメニューのスレッド。ゲームの関数は FrameStep（ゲームのスレッド）の中だけ。
namespace GameLabel
{
    const u32   kMaxChars = 16;

    void        SetText(const char *utf8);      // 出ている間でも差し替えられる
    void        Show(void);
    void        Hide(void);                     // 退場が終われば資源を返す
    bool        Present(void);
    const char *LastError(void);

    void        FrameStep(void);                // ゲームのスレッド
}
