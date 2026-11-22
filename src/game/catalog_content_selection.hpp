#pragma once
#include <algorithm>
namespace gohan::game {
// 描画先と音の呼び出しを分け、端で丸めた後の変化だけを通知する。
template<class Select, class Sound>
void SelectCatalogRow(int count, int current, int requested, Select select, Sound sound) {
    if (count <= 0) return;
    const int next = std::clamp(requested, 0, count - 1);
    if (next == current) return;
    select(next, current);
    sound();
}
}
