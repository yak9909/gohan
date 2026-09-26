#pragma once

// HiddenIcons — 没アイテムの自前のアイコン（没アイテム表示の自前表示）。絵のデータ HiddenItemIcons.h が無ければ使えない。

#include <3ds/types.h>

namespace HiddenIcons {

bool Available(void);           // 絵のデータがビルドに入っている
bool SetEnabled(bool on);       // フックは最初に真にしたとき入れる。入れられなければ false
bool Enabled(void);

}  // namespace HiddenIcons
