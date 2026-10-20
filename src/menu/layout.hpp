#pragma once
#include "menu/paint.hpp"
#include "menu/timing.hpp"
namespace gohan::menu::layout {
inline constexpr int kListTop = ca::Row::Top;
inline constexpr int kFirstRowY = ca::Row::Top;
inline constexpr int kFootTop = ca::Bar::Rect[1];
inline constexpr int kSideX = ca::Desc::Rect[0];
inline constexpr int kSideTop = ca::Desc::Rect[1];
inline int SideWidth(int) { return ca::Desc::Rect[2]; }
inline int RowsFor(int) { return ca::Row::Count; }
}
