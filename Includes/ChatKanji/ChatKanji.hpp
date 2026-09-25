#pragma once
#include <stdint.h>
#include <stddef.h>
namespace CTRPluginFramework { namespace ChatKanji {
enum RequestResult { REQUEST_OK, REQUEST_BUSY, REQUEST_NO_CHAT, REQUEST_EMPTY,
    REQUEST_BAD_INPUT, REQUEST_UNSUPPORTED, REQUEST_NO_FONT, REQUEST_NO_THREAD };
RequestResult Request(); // called only by GuiMenu's thread; snapshots current unsent normal-chat input
// Called only by GuiMenu's thread. Converts the given reading (at most 32 UTF-16 units) instead of the whole input.
RequestResult RequestText(const uint16_t *text, size_t length);
bool Poll();             // called only by GuiMenu's thread; joins a finished worker and publishes immutable rows
void Dismiss();          // discard display ownership; never interrupts an engine call
void Shutdown();         // joins worker before plugin exit
bool Busy();
const char *const *Rows();
int RowCount();
const char *Title();
const char *Error();
// Published candidates without the "N. " prefix. Valid until the next Request / RequestText.
int CandidateCount();
const uint16_t *Candidate(int index, int &length);
// Normal chat open with its input bound to the shared TextManager buffer (read-only check, any thread).
bool NormalChatOpen();
bool FontReady();
uint32_t FontAddress();
int Glyph(uint32_t cp);
int GlyphAdvance(int glyph);
int GlyphRawAdvance(int glyph);  // CWDH advance in font pixels (unscaled)
int FontCellWidth();             // FINF +0x15
int FontCellHeight();            // FINF +0x14
} }
