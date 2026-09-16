#pragma once
#include <stdint.h>
namespace CTRPluginFramework { namespace ChatKanji {
enum RequestResult { REQUEST_OK, REQUEST_BUSY, REQUEST_NO_CHAT, REQUEST_EMPTY,
    REQUEST_BAD_INPUT, REQUEST_UNSUPPORTED, REQUEST_NO_FONT, REQUEST_NO_THREAD };
RequestResult Request(); // called only by GuiMenu's thread; snapshots current unsent normal-chat input
bool Poll();             // called only by GuiMenu's thread; joins a finished worker and publishes immutable rows
void Dismiss();          // discard display ownership; never interrupts an engine call
void Shutdown();         // joins worker before plugin exit
bool Busy();
const char *const *Rows();
int RowCount();
const char *Title();
const char *Error();
bool FontReady();
uint32_t FontAddress();
int Glyph(uint32_t cp);
int GlyphAdvance(int glyph);
} }
