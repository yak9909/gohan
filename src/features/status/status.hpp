#pragma once

namespace CTRPluginFramework { class MenuEntry; }

namespace gohan::features::status {
void Show(CTRPluginFramework::MenuEntry *);
#ifdef GOHAN_DEV
void Export(CTRPluginFramework::MenuEntry *);
#endif
} // namespace gohan::features::status
