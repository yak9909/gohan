#pragma once

// DecorTrash — 家の模様替え（BsMenuInteriorEditor）に HHD のゴミ箱を足す（T022。解析リポジトリ docs/topics/t022_decorate_port_requirements.md）。
// 模様替えで家具を掴んで左下のゴミ箱に重ねて離すと、その家具が消える。模様替えの選択・掴む・動かすはゲームのまま。

namespace DecorTrash {

void Wire(void);
// GuiMenu のチェックの項目（Cheats.cpp の DispatchTick / DispatchDisable から）。自分の項目なら真
bool Tick(int index, unsigned short held);
bool Disable(int index);

}  // namespace DecorTrash
