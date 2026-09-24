#pragma once

#include <3ds.h>

// ゲームのリスト UI（カタログの 8 行リスト）を下画面だけ借りて、文字列の一覧から 1 つ選ばせる汎用部品。
// 設計と根拠: docs/topics/game_list.md（IDA-opus-5.5-F036）。
//
// 借りるもの: `Layout/catalog/catalogue.arc` の枠 `ctlg_list` と中身 `ctlg_cntnt_00`、
//   中身を動かすゲーム共通の `InstSelect<8>`（件数と i 行目の文字だけ自前）。
// 借りないもの: 「やめる／けってい」（common/btn_pos.arc）と上画面 `ctlg_T`。メニューの状態にも触れない。
//
// スレッド: Set* / Show / Hide / Select / TakeDecided はメニュー（プラグイン）のスレッドから呼ぶ。
//   ゲームの関数は全部 FrameStep（ゲームの描画スレッド。GridCursor のフレームフック）の中で呼ぶ。
// 資源: 出している間だけゲームのヒープ（nw::lyt）・FCRAM（コマンドリスト）・VRAM（テクスチャ）を使い、
//   退場アニメが終わったら全部返す。ゲームのメニューが開いたら即座に片付け、閉じたら出し直す。
namespace GameList
{
    const u32   kMaxItems = 256;
    const u32   kMaxChars = 38;         // 1 行の文字数（UTF-16 の単位）。公共事業の一覧と同じ

    // 一覧を差し替える（UTF-8）。次に組み立てるときに使う。出している間に呼ぶと組み直す。
    bool        SetItems(const char *const *items, u32 count);
    // 行の名前をゲームのメッセージから引く（例: "STR_Fobj_name" と番号）。番号が負、または引けなかった行は SetItems の文字列。
    // SetItems の後に呼ぶ。label は呼び出し側で生かしておく（静的な文字列）。
    void        SetItemMessages(const char *label, const s16 *indices, u32 count);
    void        Show(s32 selected);     // 登場（退場中なら今の位置から逆向きに）
    void        Hide(void);             // 退場（登場中なら今の位置から逆向きに）。終われば資源を返す
    bool        Wanted(void);           // 利用者の意思（Show 中か）
    bool        Present(void);          // 下画面に出ている（アニメ中も含む）
    void        Select(s32 index);      // 選択の見た目を動かす（決定にはしない）
    s32         TakeDecided(void);      // 選ばれた行（タッチ・十字）。無ければ -1。取ったら消える
    // 十字キーの押し下げ（CTRPF の Key の DPad ビット）を毎ティック渡す。リストの更新の間だけゲームの
    // メニュー管理の入力（BsMenuMgr+68 の sead::ControllerWrapper）へ入れ、終わったら戻す。ゲームのほかの部分には届かない。
    // スライドパッドは入れない。
    void        FeedDpad(u32 heldKeys);
    const char *LastError(void);

    void        FrameStep(void);        // ゲームのスレッド
    void        Shutdown(void);         // プラグイン終了時（ゲームのスレッドで片付け終わるまで待つ）
}
