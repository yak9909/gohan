#pragma once

// DecorLayout — SD の DARC をゲームの nw::lyt で出すための下回り（T022 段 2 のカタログ用）。
// 手順は実機で動いている HhdScreen.cpp / DecorTrash.cpp と同じ番地・同じ順（根拠は両ファイルの冒頭と work/FINDINGS.md IDA-opus-5.5-F073 / F074）。
// ゲームの関数を呼ぶのはゲームのスレッドだけ（GridCursor の相乗り FrameStep の中）。

#include <3ds/types.h>

namespace DecorLayout {

struct Arc {                                    // ArcResAccReader（GameLabel / HhdScreen と同じ大きさ）とヒープに写した DARC
    alignas(8) u8 holder[584];
    void *heap, *data;
    bool made;
};
struct Layout {
    alignas(8) u8 obj[332];
    bool made, built;
};
struct Anim {                                   // 1 つのアニメを最大 2 つの群に結ぶ（壁紙/床紙の窓の in / out は G_InOut と G_Page_00）
    alignas(8) u8 obj[40];
    Layout *lay;
    void *group[2];
    bool made, bound, hold;                     // hold: 結んだまま止める（Step で進めない。そのコマの見た目を保つ）
};

// ---- 資源 ----
bool LoadArc(Arc &a, const u8 *file, u32 size);  // ヒープへ写し、アクセサを作り、書体（種類 0 = Garden_msg_size16）とテクスチャを登録する
void FreeArc(Arc &a);
void *HeapAlloc(u32 size, u32 align);           // 読み込みのヒープ [[0x96FC40]+4] から借りる（返すのは HeapFree）
void HeapFree(void *p);
u32 HeapFreeBytes(void);

// ---- レイアウト ----
bool Build(Layout &l, Arc &a, const char *name, u32 cmdBytes, u8 priority);
void Free(Layout &l);                           // 結んだアニメは先に Unbind / Free すること
void Draw(Layout &l, u32 screen);               // Calc + AddLayout（screen 1 = 下画面）
void *Pane(Layout &l, const char *name);        // 0x567BAC（無ければ nullptr）
void *Group(Layout &l, const char *name);       // 0x4B4328（入れ子も探す）

// ---- アニメ ----
bool LoadAnim(Anim &an, Arc &a, const char *name);
bool Bind(Anim &an, Layout &l, const char *group, float frame = 0.0f, const char *group2 = nullptr);   // 群に結んで frame から
void Unbind(Anim &an);
void FreeAnim(Anim &an);
bool Step(Anim &an);                            // 1 コマ進める（終わっていたら偽。終わっても外さない）
bool Done(const Anim &an);
void Hold(Anim &an);                            // そのコマで止める（外すとペインは最後に当てた値のまま残るので、戻すときは 0 コマ目で止める）
void Reverse(Anim &an);                         // 今のコマから 0 へ逆に再生する（UiAnim の +0x14 bit1。Done は 0 に着いたら真。F108）
void SetFrame(Anim &an, float frame);           // 最後のコマへ飛ぶときはアニメのコマ数（生成した表の frame_size）を渡す

// ---- ペイン ----
void SetVisible(void *pane, bool on);           // +0xB7 bit0
bool Visible(const void *pane);
void SetAlpha(void *pane, u8 a);                // +180
void SetPos(void *pane, float x, float y);      // +0x28 / +0x2C（行列を作り直させる）
float PosX(const void *pane);
float PosY(const void *pane);
void SetSize(void *pane, float w, float h);     // +0x48 / +0x4C
float Width(const void *pane);
float Height(const void *pane);
void SetText(void *textBox, const u16 *text, u32 len);                     // nwlyt_TextBox_SetString 0x4BACBC
float MeasureText(void *textBox, const u16 *text, u32 len);                // ゲームの持ち物欄の測り方 sub_5E9430 の写し
bool SetTexture(void *picture, u32 va, u16 w, u16 h, u32 format);         // HiddenIcons と同じ TexMap の書き方（format 11 = ETC1A4）
bool SetTextureByName(void *picture, Arc &a, const char *name);           // arc の絵へ（HhdScreen::SetTexture と同じ）
// BsMenuRoomLightSwitchの十字キー案内N_angl_00を一時的に隠す。ゲームスレッドから毎フレーム呼び、falseで元の可視bitへ戻す。
void HideRoomAngleGuide(bool hide);

}  // namespace DecorLayout
