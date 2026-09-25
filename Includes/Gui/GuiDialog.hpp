#pragma once

#include <3ds.h>

// 上画面に出す「OK だけのダイアログ」（汎用。利用者指示 2026-09-25: 警告・エラーは通知欄では長文が省略されるので
// ダイアログで出す）。見た目はメニューの確認ダイアログ（DrawDialog）と同じ部品で描く。
//
// スレッド: どのスレッドから呼んでもよい（中身を写してメニューのスレッドへ渡す）。
// 出ている間: ゲームのボタン入力を止め、メニューの項目のティック（GuiMenu::IsVisible）もキーを 0 で受ける。
//   A か B で閉じる（OK ボタンは 1 つだけなので A = OK）。閉じる前に次を頼まれたら、閉じてから順に出す（kQueue 件まで。あふれたら古い物を捨てる）。
namespace GuiDialog
{
    const u32   kQueue = 4;

    // title は 1 行（長ければ "..." で切る）。message は幅で折り返す（最大 kMaxLines 行）。error: 縁と題を赤くする
    const u32   kMaxLines = 7;
    void        ShowMessage(const char *title, const char *message, bool error = true);
    bool        IsOpen(void);           // 出ている（出入りのアニメ中も含む）か、待っている物がある
}
