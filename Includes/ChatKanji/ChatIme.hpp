#pragma once
#include <stdint.h>

// 漢字変換（チャット）とかな入力のコンポジション（gohan.md §12、IDA-opus-5.5-F044）。
//
// 未確定の文字列はゲーム自身の TextManager+36（ローマ字の未確定の字数）で持つ。
// そうすると入力欄の飾り・Enter での確定・送信前の確定・Backspace・カーソル移動での確定が
// ゲームの処理のまま効く。変えるのは次の 3 か所だけ（CTRPF の MITM フック。一度入れたら外さない）。
//   TextManager_InputChar 0x5216FC … かなキー（KanaKeySet）の字を未確定に足す
//   TextManager_Backspace 0x5226EC … 変換中なら読みへ戻す
//   BsSkb_Wait_Calc       0x57B778 … メニュースレッドからの依頼（範囲の取得・候補の適用）をゲームのスレッドで行う
// 入力欄を書き換えるのは全部ゲームのスレッド。メニュースレッドは依頼を置いて結果を読むだけ。
namespace CTRPluginFramework
{
    namespace ChatIme
    {
        void    Wire(void);                         // 項目番号を引く（Cheats::Wire から）
        bool    Tick(int index, uint16_t held);     // 自分の項目なら真（メニュースレッド・毎 16ms）
        bool    Disable(int index);                 // 自分の項目なら真
        bool    BarVisible(void);                   // 候補欄を描くか（普通のチャットが開いていて漢字変換が ON）
        void    DrawBar(void);                      // GuiMenuDraw の BuildBottom から（下画面にほかの UI が無いとき）
    }
}
