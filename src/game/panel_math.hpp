#pragma once
#include <algorithm>
namespace gohan::game {
inline float HalfDrag(float start, float now) { return start + (now - start) * 0.5f; }
inline bool OwnedDrag(const void *scroll, const void *owned, unsigned active, unsigned blocked, unsigned mode) {
    return owned && scroll == owned && active != 0 && blocked != 1 && mode == 2;
}
inline float LabelWidth(float measured) { return std::max(98.0f * 1.3f, measured + 60.0f); }
inline float LabelNext(float left, float width) { return left + (width + 6.0f) * 0.72f; }
}
