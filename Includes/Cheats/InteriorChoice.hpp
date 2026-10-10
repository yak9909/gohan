#pragma once
#include <3ds/types.h>

// Same native ItemSelectWindow as MapEditor's range actions. Game thread only.
namespace InteriorChoice {
bool Open(float x, float y);
void FrameStep(bool contextValid);
bool Busy(void);
int TakeResult(void);                           // 0 duplicate, 1 cancel, -1 none.
}
