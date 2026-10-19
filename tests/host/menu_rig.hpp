#pragma once
// メニューの画面（Shell）を、時刻・ボタン・タッチを与えて動かす試験の道具。
#include "menu/shell.hpp"
#include "menu_surface.hpp"
#include <cassert>

namespace menu_test {
struct Rig {
    Engine engine;
    Shell shell;
    u32 now = 1000;
    Input in;
    explicit Rig(const ItemDef *defs, std::size_t count, ShellConfig config = {})
        : engine(defs, count), shell(engine, config) {
        engine.SetReservedHotkeys(key::Select);
    }

    void Frame(u32 dt = 16, bool toggle = false) {
        now += dt;
        shell.Step(now, in, toggle);
    }
    void Advance(u32 ms) {
        for (u32 t = 0; t < ms; t += 16) Frame();
    }
    void Hold(u16 keys) {
        in.held = keys;
        Frame();
    }
    void Release() {
        in.held = 0;
        Frame();
    }
    void Tap(u16 keys) {
        Hold(keys);
        Release();
    }
    void TouchAt(int x, int y) {
        in.touch = {true, x, y};
        Frame();
    }
    void Untouch() {
        in.touch = {};
        Frame();
    }
    void TapAt(int x, int y) {
        TouchAt(x, y);
        Untouch();
    }
    // 開く動き（320ms）が終わるまで進める。
    void Open() {
        Tap(key::Select);
        Advance(400);
    }
    // 描画の予算（文字・矩形の数）。既定は ui::renderer の予算。小さくすると帯の行数が減る。
    int text_limit = static_cast<int>(gohan::core::limits::RendererTexts);
    int rect_limit = static_cast<int>(gohan::core::limits::RendererRects);
    RecordingSurface Draw(int width = 320) {
        RecordingSurface surface(width, text_limit, rect_limit);
        shell.Draw(surface, now);
        return surface;
    }
};
} // namespace menu_test
