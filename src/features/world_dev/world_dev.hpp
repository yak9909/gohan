#pragma once
#ifdef GOHAN_DEV
namespace CTRPluginFramework { class MenuEntry; }
namespace gohan::features::world_dev {
void Toggle(CTRPluginFramework::MenuEntry *);
void Colour(CTRPluginFramework::MenuEntry *);
void Status(CTRPluginFramework::MenuEntry *);
}
#endif
