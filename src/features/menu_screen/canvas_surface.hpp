#pragma once
#include "core/limits.hpp"
#include "menu/surface.hpp"
#include "ui/renderer/renderer.hpp"
#include <cstring>

namespace gohan::features::menu_screen {
// メニューの描画先を、ui::renderer の Canvas の 1 画面につなぐ。Begin は呼び出し側が済ませておく。
class CanvasSurface final : public menu::Surface {
  public:
    CanvasSurface(ui::renderer::Canvas &canvas, ui::renderer::Screen screen) : canvas_(canvas), screen_(screen) {}

    // 部品が受け付けなかった指示の数（枠・予算・文字枠の不足）と、字数で切られた文字の数。
    int dropped() const { return dropped_; }
    int truncated() const { return truncated_; }

    int Width() const override { return screen_ == ui::renderer::Screen::Bottom ? 320 : 400; }
    void Fill(menu::Rect bounds, menu::u32 color) override {
        Note(canvas_.FillRect({bounds.x, bounds.y, bounds.w, bounds.h}, color));
    }
    void Outline(menu::Rect bounds, menu::u32 color) override {
        Note(canvas_.FrameRect({bounds.x, bounds.y, bounds.w, bounds.h}, color));
    }
    void Text(int x, int y, std::string_view text, menu::u32 color, bool numeric, unsigned scale) override {
        std::string storage;
        Note(canvas_.Text(x, y, menu::DisplayText(text, storage), color, scale, numeric ? ui::renderer::Font::Numeric : ui::renderer::Font::Main));
    }
    int Measure(std::string_view text, bool numeric, unsigned scale) const override {
        std::string storage;
        return ui::renderer::MeasureText(menu::DisplayText(text, storage), scale, numeric ? ui::renderer::Font::Numeric : ui::renderer::Font::Main);
    }
    bool BeginClip(menu::Rect bounds) override { return canvas_.BeginClip({bounds.x, bounds.y, bounds.w, bounds.h}); }
    void EndClip() override { canvas_.EndClip(); }
    int TextsLeft() const override {
        return static_cast<int>(core::limits::RendererTexts) - static_cast<int>(Current().texts);
    }
    int RectsLeft() const override {
        return static_cast<int>(core::limits::RendererRects) - static_cast<int>(Current().rectangles);
    }
    void Sprite(int x, int y, std::string_view name, menu::u32 color) override {
        for (unsigned i = 0; i < static_cast<unsigned>(ui::renderer::Part::Count); ++i)
            if (name == ui::renderer::ui_data::PartNames[i]) {
                Note(canvas_.Image(x, y, static_cast<ui::renderer::Part>(i), 1, color)); return;
            }
        ++dropped_;
    }
    void Box(menu::Rect r, std::string_view name, menu::u32 color) override {
        for (unsigned i = 0; i < static_cast<unsigned>(ui::renderer::Part::Count); ++i)
            if (name == ui::renderer::ui_data::PartNames[i]) {
                Note(canvas_.PartBox({r.x, r.y, r.w, r.h}, static_cast<ui::renderer::Part>(i), 1, color)); return;
            }
        ++dropped_;
    }
    void Pattern(menu::Rect r, std::string_view name, menu::u32 color) override {
        using ui::renderer::Texture;
        const auto texture = name == "window" ? Texture::TitleWindow : name == "selection" ? Texture::TitleSelection : Texture::DashHorizontal;
        if (static_cast<unsigned>(texture) >= ui::renderer::Fonts().textureCount) return;
        Note(canvas_.Repeat({r.x, r.y, r.w, r.h}, texture, 0, 0, 1, color));
    }

  private:
    void Note(ui::renderer::Result result) {
        using ui::renderer::Result;
        if (result == Result::Full || result == Result::Budget || result == Result::Invalid) ++dropped_;
        if (result == Result::Truncated) ++truncated_;
    }
    const game::lyt::ScreenDrawing &Current() const {
        return canvas_.Data().screens[screen_ == ui::renderer::Screen::Bottom ? 1 : 0];
    }
    ui::renderer::Canvas &canvas_;
    ui::renderer::Screen screen_;
    int dropped_ = 0;
    int truncated_ = 0;
};
} // namespace gohan::features::menu_screen
