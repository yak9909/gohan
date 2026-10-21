#pragma once
#include <cstddef>
#include <cstdint>
#include "ui_values.hpp"

// メニューの時間と寸法。見た目の値は spec/ui.toml から生成する。
namespace gohan::menu::timing {
inline constexpr std::size_t kMaxDepth = 8; // ルートからのフォルダの深さ

inline constexpr std::uint32_t kEnterMs = core::design::ca2::Time::Open;
inline constexpr std::uint32_t kSelectionMoveMs = core::design::ca2::Time::Select;
inline constexpr std::uint32_t kScrollMs = core::design::ca2::Time::Scroll;
inline constexpr std::uint32_t kValueBounceMs = core::design::ca2::Time::Bounce::Value[0];
inline constexpr std::uint32_t kActivationBounceMs = core::design::ca2::Time::Bounce::Confirm[0];
inline constexpr std::uint32_t kPulseMs = 1800;
inline constexpr std::uint32_t kHoldMs = core::design::ca2::Time::Hold[0];      // X／L の長押しの満了
inline constexpr std::uint32_t kHoldCancelNum = 2; // 満了の 2/5 未満で離すと単体の操作
inline constexpr std::uint32_t kHoldCancelDen = 5;
inline constexpr std::uint32_t kRepeatDelayMs = 200;
inline constexpr std::uint32_t kRepeatIntervalMs = 60;
inline constexpr std::uint32_t kFeedbackMs = core::design::ca2::Time::Ok;  // 「実行した」表示
inline constexpr std::uint32_t kListAnimMs = 180;
inline constexpr std::uint32_t kListScrollMs = 100;
inline constexpr std::uint32_t kBottomAnimMs = 180;
inline constexpr std::uint32_t kDialogMs = core::design::ca2::Time::Dialog[0];
inline constexpr std::uint32_t kNoticeEnterMs = core::design::ca2::Time::Notice[0];
inline constexpr std::uint32_t kNoticeHoldMs = core::design::ca2::Time::Notice[1];
inline constexpr std::uint32_t kNoticeExitMs = core::design::ca2::Time::Notice[2];
inline constexpr std::uint32_t kNoticeSpawnMs = core::design::ca2::Time::Notice[3];
inline constexpr std::uint32_t kFolderMs = core::design::ca2::Time::Folder[0];
inline constexpr int kFolderDistance = core::design::ca2::Time::Folder[1];
} // namespace gohan::menu::timing

namespace gohan::menu::style {
inline constexpr int kMenuWidth = core::design::ca2::List::Rect[2];
inline constexpr int kItemHeight = core::design::ca2::Row::Pitch;
inline constexpr int kVisibleRows = core::design::ca2::Row::Count;
inline constexpr int kNoticeHeight = core::design::ca2::Notice::Height[0];
inline constexpr int kNoticeGap = core::design::ca2::Notice::Gap;
inline constexpr int kNoticeMargin = -core::design::ca2::Notice::Right;
}
