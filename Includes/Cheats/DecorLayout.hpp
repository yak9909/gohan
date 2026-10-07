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
struct Anim {
    alignas(8) u8 obj[40];
    Layout *lay;
    void *group;
    bool made, bound;
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
bool Bind(Anim &an, Layout &l, const char *group, float frame = 0.0f);   // 群に結んで frame から
void Unbind(Anim &an);
void FreeAnim(Anim &an);
bool Step(Anim &an);                            // 1 コマ進める（終わっていたら偽。終わっても外さない）
bool Done(const Anim &an);
void SetFrame(Anim &an, float frame);           // 最後のコマへ飛ぶときはアニメのコマ数（生成した表の frame_size）を渡す

// ---- ペイン ----
void SetVisible(void *pane, bool on);           // +0xB7 bit0
bool Visible(const void *pane);
void SetAlpha(void *pane, u8 a);                // +180
void SetPos(void *pane, float x, float y);      // +0x28 / +0x2C（行列を作り直させる）
void SetSize(void *pane, float w, float h);     // +0x48 / +0x4C
float Width(const void *pane);
float Height(const void *pane);
void SetText(void *textBox, const u16 *text, u32 len);                     // nwlyt_TextBox_SetString 0x4BACBC
float MeasureText(void *textBox, const u16 *text, u32 len);                // ゲームの持ち物欄の測り方 sub_5E9430 の写し
bool SetTexture(void *picture, u32 va, u16 w, u16 h, u32 format);         // HiddenIcons と同じ TexMap の書き方（format 11 = ETC1A4）
bool SetTextureByName(void *picture, Arc &a, const char *name);           // arc の絵へ（HhdScreen::SetTexture と同じ）

}  // namespace DecorLayout
