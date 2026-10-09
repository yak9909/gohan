#pragma once

#include <3ds/types.h>

// Game thread only. Fade the opening tabs and native editor while a list is up.
namespace CatalogBackdrop {
bool Install(void);
void Set(u32 editor, u32 topLayout, u8 alpha, u32 editorSlot);
void Reset(void);
}
