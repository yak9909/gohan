#pragma once

// HiddenIcons — 没アイテムの自前のアイコン（没アイテム表示の自前表示）。絵は sdmc:/luma/plugins/gohan/common/hidden_item_icons.arc（GohanFiles.hpp）。

#include <3ds/types.h>

namespace HiddenIcons {

u32 LoadIcons(void);            // arc を SD から読む（起動時に 1 回。フックを入れる前に）。使える絵の枚数（無ければ 0）
bool Available(void);           // 絵が 1 枚以上読めた
bool SetEnabled(bool on);       // フックは最初に真にしたとき入れる。入れられなければ false
bool Enabled(void);

}  // namespace HiddenIcons
