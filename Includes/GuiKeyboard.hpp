#pragma once
#include <cstdint>

namespace CTRPluginFramework { namespace GuiKeyboard {
    enum Kind { NONE, TEXT, NUMBER };
    enum Format { DECIMAL, HEXADECIMAL, FLOAT_TENTHS };
    // Fixed capacity; text positions count Unicode characters, never UTF-8 bytes.
    struct State {
        Kind kind;
        Format format;
        bool compact, abc, katakana, caps, shift, hex, closing, apply;
        int row, column, cursor, length, item;
        int32_t minimum, maximum;
        uint32_t chars[9];
        char buffer[24];
        uint32_t started;
        float from, duration;
    };
    struct Result { Kind kind; int item; int32_t value; bool apply; char text[33]; };
    void Reset();
    void OpenText(bool compact, uint32_t now);
    void OpenNumber(int item, int32_t value, int32_t minimum, int32_t maximum,
                    Format format, bool apply, uint32_t now);
    bool Active();
    // ★下画面ロックの暗幕（F-350）。自前では描かず、この 2 つを GuiMenu へ渡す。
    uint32_t DimColor();
    int      DimFadeMs();
    void Cancel(uint32_t now);
    void Update(uint32_t now);
    // Touch is a sampled level; the module computes its own rising edge.
    void Handle(int dx, int dy, bool accept, bool cancel, bool touch,
                int tx, int ty, uint32_t now);
    void Draw(uint32_t now);
    bool TakeResult(Result &result);
    const char *TextValue();
    const State &Current();
    // Shared by controller/touch and the host differential verifier.
    bool Activate(const char *key, uint32_t now);
    bool Enabled(const char *key);
    void SetCursor(int cursor);
}}
