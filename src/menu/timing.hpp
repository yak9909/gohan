#pragma once
#include <cstddef>
#include <cstdint>

// 時間・寸法・色。値は旧版のメニューの定数をそのまま使う。
// エンジンが使うのは時間だけで、寸法と色は描画が読む。色は 0xAARRGGBB。
namespace gohan::menu::timing {
inline constexpr std::size_t kMaxDepth = 8; // ルートからのフォルダの深さ

inline constexpr std::uint32_t kEnterMs = 320;
inline constexpr std::uint32_t kSelectionMoveMs = 100;
inline constexpr std::uint32_t kScrollMs = 100;
inline constexpr std::uint32_t kValueBounceMs = 100;
inline constexpr std::uint32_t kActivationBounceMs = 80;
inline constexpr std::uint32_t kPulseMs = 1800;
inline constexpr std::uint32_t kHoldMs = 600;      // X／L の長押しの満了
inline constexpr std::uint32_t kHoldCancelNum = 2; // 満了の 2/5 未満で離すと単体の操作
inline constexpr std::uint32_t kHoldCancelDen = 5;
inline constexpr std::uint32_t kRepeatDelayMs = 200;
inline constexpr std::uint32_t kRepeatIntervalMs = 60;
inline constexpr std::uint32_t kFeedbackMs = 800;  // 「実行した」表示
inline constexpr std::uint32_t kListAnimMs = 180;
inline constexpr std::uint32_t kListScrollMs = 100;
inline constexpr std::uint32_t kBottomAnimMs = 180;
inline constexpr std::uint32_t kDialogMs = 160;
inline constexpr std::uint32_t kNoticeEnterMs = 360;
inline constexpr std::uint32_t kNoticeHoldMs = 2600;
inline constexpr std::uint32_t kNoticeExitMs = 360;
inline constexpr std::uint32_t kNoticeSpawnMs = 40;
} // namespace gohan::menu::timing

namespace gohan::menu::style {
constexpr std::uint32_t Argb(unsigned a, unsigned r, unsigned g, unsigned b) { return (a << 24) | (r << 16) | (g << 8) | b; }

inline constexpr int kMenuWidth = 160;
inline constexpr int kItemHeight = 18;
inline constexpr int kVisibleRows = 10;
inline constexpr int kInlineRows = 6;
inline constexpr int kScreenRows = 8;
inline constexpr int kDescriptionLines = 6;
inline constexpr int kNoticeWidth = 144;
inline constexpr int kNoticeHeight = 27;
inline constexpr int kNoticeGap = 4;
inline constexpr int kNoticeMargin = 8;
inline constexpr int kNoticeLineHeight = 10;

inline constexpr std::uint32_t kPanel = Argb(0xC7, 0x0C, 0x10, 0x0D); // rgba(12,16,13,.78)
inline constexpr std::uint32_t kBorder = Argb(0xE6, 0x63, 0xE4, 0xA4); // rgba(99,228,164,.9)
inline constexpr std::uint32_t kHeader = Argb(0xBD, 0x1C, 0x2B, 0x22); // rgba(28,43,34,.74)
inline constexpr std::uint32_t kFoot = Argb(0xC2, 0x15, 0x1B, 0x17); // rgba(21,27,23,.76)
inline constexpr std::uint32_t kSelectionFill = Argb(0x00, 0x29, 0x45, 0x37); // rgba(41,69,55,α) ★α は脈動
inline constexpr std::uint32_t kSelectionLine = Argb(0x00, 0x63, 0xE4, 0xA4); // rgba(99,228,164,α) ★α は脈動
inline constexpr std::uint32_t kAccent = Argb(0xFF, 0x63, 0xE4, 0xA4); // #63e4a4
inline constexpr std::uint32_t kWhite = Argb(0xFF, 0xFF, 0xFF, 0xFF); // #ffffff
inline constexpr std::uint32_t kText = Argb(0xFF, 0xAE, 0xB9, 0xB1); // #aeb9b1
inline constexpr std::uint32_t kBody = Argb(0xFF, 0xC4, 0xCE, 0xC7); // #c4cec7
inline constexpr std::uint32_t kCrumb = Argb(0xFF, 0x79, 0xD9, 0xA7); // #79d9a7
inline constexpr std::uint32_t kPending = Argb(0xFF, 0xFF, 0xD1, 0x66); // #ffd166
inline constexpr std::uint32_t kHotkey = Argb(0xFF, 0x78, 0xA9, 0xFF); // #78a9ff
inline constexpr std::uint32_t kDisabled = Argb(0xFF, 0x52, 0x5B, 0x54); // #525b54
inline constexpr std::uint32_t kLocked = Argb(0xFF, 0x5C, 0xC8, 0xFF); // #5cc8ff
inline constexpr std::uint32_t kCheckMark = Argb(0xFF, 0xFF, 0x6B, 0x6B); // #ff6b6b
inline constexpr std::uint32_t kFootText = Argb(0xFF, 0x82, 0x90, 0x87); // #829087
inline constexpr std::uint32_t kFootOff = Argb(0xFF, 0x58, 0x63, 0x5B); // #58635b
inline constexpr std::uint32_t kPanelEdge = Argb(0xFF, 0x4F, 0x6C, 0x5B); // #4f6c5b
inline constexpr std::uint32_t kBarTrack = Argb(0xA8, 0x26, 0x34, 0x2C); // rgba(38,52,44,.66)
inline constexpr std::uint32_t kBarThumb = Argb(0xE0, 0x63, 0xE4, 0xA4); // rgba(99,228,164,.88)
inline constexpr std::uint32_t kListSelection = Argb(0xA3, 0x29, 0x45, 0x37); // rgba(41,69,55,.64)
inline constexpr std::uint32_t kBoxBackground = Argb(0xD1, 0x0A, 0x0D, 0x0B); // rgba(10,13,11,.82)
inline constexpr std::uint32_t kDialogBackground = Argb(0xE6, 0x07, 0x09, 0x08); // rgba(7,9,8,.9)
inline constexpr std::uint32_t kDialogSelection = Argb(0xB8, 0x5B, 0x49, 0x1F); // rgba(91,73,31,.72)
inline constexpr std::uint32_t kHoldBackground = Argb(0xF0, 0x07, 0x09, 0x08); // rgba(7,9,8,.94)
inline constexpr std::uint32_t kHoldTrack = Argb(0xD9, 0x55, 0x48, 0x22); // rgba(85,72,34,.85)
inline constexpr std::uint32_t kKeyBackground = Argb(0xC2, 0x1C, 0x23, 0x1E); // rgba(28,35,30,.76)
inline constexpr std::uint32_t kKeyLine = Argb(0xFF, 0x3A, 0x45, 0x3D); // #3a453d
inline constexpr std::uint32_t kKeyText = Argb(0xFF, 0xB7, 0xC1, 0xB9); // #b7c1b9
inline constexpr std::uint32_t kHint = Argb(0xFF, 0x8F, 0x9A, 0x92); // #8f9a92
inline constexpr std::uint32_t kBound = Argb(0xFF, 0x7D, 0x89, 0x81); // #7d8981
inline constexpr std::uint32_t kSliderTrack = Argb(0xB8, 0x27, 0x30, 0x2A); // rgba(39,48,42,.72)
inline constexpr std::uint32_t kSliderFill = Argb(0xE0, 0x63, 0xE4, 0xA4); // rgba(99,228,164,.88)
inline constexpr std::uint32_t kSliderKnob = Argb(0xE6, 0xFF, 0xFF, 0xFF); // rgba(255,255,255,.9)
inline constexpr std::uint32_t kNoticeBackground = Argb(0xC7, 0x0A, 0x0D, 0x0B); // rgba(10,13,11,.78)
inline constexpr std::uint32_t kNoticeMessage = Argb(0xFF, 0xC4, 0xCE, 0xC7); // #c4cec7
inline constexpr std::uint32_t kDanger = Argb(0xFF, 0xE5, 0x48, 0x4D); // #e5484d
inline constexpr std::uint32_t kDangerEdge = Argb(0xFF, 0x6C, 0x4F, 0x5B); // #6c4f5b
inline constexpr std::uint32_t kDim = Argb(0xB8, 0x10, 0x14, 0x11); // rgba(16,20,17,.72)
} // namespace gohan::menu::style
