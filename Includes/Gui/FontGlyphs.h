// ACNL の書体（romfs Font/Garden_msg_size16.bcfnt ほか、ゲームのメッセージ・TextBox が使う書体）の私用領域にある記号。
// 書体の CMAP に U+E000〜U+E07E の 127 字があり、下の字は字形を描いて確かめた（利用者 2026-10-06 の一覧と一致）。
// ★gohan の自前メニューの書体（美咲ゴシック。GuiFontUi.h）には無い。ゲームの書体で描く文字（nw::lyt::TextBox など）にだけ使う。
// 解析リポジトリ側の同じ表: tools/hhd/make_acnl_layouts.py の GLYPH（HHD キャラクリのレイアウトの文字）。
//   GLYPH_U8_* は u8"..." の文字列に並べて書ける形（例: GLYPH_U8_B u8" キャンセル"）。
#pragma once
#include <3ds.h>

namespace FontGlyphs {
constexpr u16 kA = 0xE000;
constexpr u16 kB = 0xE001;
constexpr u16 kX = 0xE002;
constexpr u16 kY = 0xE003;
constexpr u16 kL = 0xE004;
constexpr u16 kR = 0xE005;
constexpr u16 kDpad = 0xE041;               // 十字キー（U+E006 も十字キーの形だが、一覧はこちら）
constexpr u16 kSlidePad = 0xE049;           // スライドパッド
constexpr u16 kCStick = 0xE04A;             // C スティック
constexpr u16 kZL = 0xE054;
constexpr u16 kZR = 0xE055;
constexpr u16 kTouch = 0xE058;              // タッチパネル
constexpr u16 kDpadUp = 0xE079;
constexpr u16 kDpadDown = 0xE07A;
constexpr u16 kDpadLeft = 0xE07B;
constexpr u16 kDpadRight = 0xE07C;
constexpr u16 kDpadUpDown = 0xE07D;
constexpr u16 kDpadLeftRight = 0xE07E;
}  // namespace FontGlyphs

#define GLYPH_U8_A u8"\uE000"
#define GLYPH_U8_B u8"\uE001"
#define GLYPH_U8_X u8"\uE002"
#define GLYPH_U8_Y u8"\uE003"
#define GLYPH_U8_L u8"\uE004"
#define GLYPH_U8_R u8"\uE005"
#define GLYPH_U8_DPAD u8"\uE041"
#define GLYPH_U8_SLIDE_PAD u8"\uE049"
#define GLYPH_U8_C_STICK u8"\uE04A"
#define GLYPH_U8_ZL u8"\uE054"
#define GLYPH_U8_ZR u8"\uE055"
#define GLYPH_U8_TOUCH u8"\uE058"
#define GLYPH_U8_DPAD_UP u8"\uE079"
#define GLYPH_U8_DPAD_DOWN u8"\uE07A"
#define GLYPH_U8_DPAD_LEFT u8"\uE07B"
#define GLYPH_U8_DPAD_RIGHT u8"\uE07C"
#define GLYPH_U8_DPAD_UP_DOWN u8"\uE07D"
#define GLYPH_U8_DPAD_LEFT_RIGHT u8"\uE07E"
