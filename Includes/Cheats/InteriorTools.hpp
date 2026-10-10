#pragma once
#include <3ds/types.h>

namespace InteriorTools {
void Wire(void);
void Tick(void);                              // Plugin/menu thread: files + input.
bool InputBusy(void);                        // Game thread: choice/load focus.
bool IsFrozenEditorCalc(u32 calc);
}
