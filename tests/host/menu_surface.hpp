#pragma once
// メニューの描画先の代役。描画指示を記録し、ui::renderer と同じ予算（矩形・文字の数、1 つの文字列の字数）で
// 受け付ける。入りきらない指示は捨てて dropped に数える（実際の部品が Full を返すのと同じ）。
#include "core/limits.hpp"
#include "menu/surface.hpp"
#include <string>
#include <vector>

namespace menu_test {
using namespace gohan::menu;

struct Command {
    enum Kind { Fill, Outline, Text, Sprite, Slice, Pattern } kind;
    Rect bounds;
    u32 color;
    std::string text;
    bool numeric = false;
    unsigned scale = 1;
    Rect clip{0, 0, 0, 0};
    bool clipped = false;
};

class RecordingSurface : public Surface {
  public:
    // text_limit・rect_limit は、受け付ける描画の予算（既定は ui::renderer の予算）。
    explicit RecordingSurface(int width = 400, int text_limit = static_cast<int>(gohan::core::limits::RendererTexts),
                              int rect_limit = static_cast<int>(gohan::core::limits::RendererRects))
        : width_(width), text_limit_(text_limit), rect_limit_(rect_limit) {}

    std::vector<Command> commands;
    int rects = 0, texts = 0, dropped = 0;

    int Width() const override { return width_; }
    void Fill(Rect bounds, u32 color) override { Add(Command::Fill, bounds, color, {}, false, 1, 1); }
    void Outline(Rect bounds, u32 color) override {
        Add(Command::Outline, bounds, color, {}, false, 1, bounds.w >= 2 && bounds.h >= 2 ? 4 : 1);
    }
    void Text(int x, int y, std::string_view text, u32 color, bool numeric = false, unsigned scale = 1) override {
        const int width = Measure(text, numeric, scale);
        if (texts >= text_limit_ || (color >> 24) == 0 || width == 0) {
            if (width != 0) ++dropped;
            return;
        }
        ++texts;
        commands.push_back({Command::Text, {x, y, width, 8 * static_cast<int>(scale)}, color, std::string(text), numeric,
                            scale, clip_, clipped_});
    }
    // 半角（ASCII）は 4、数字用は 5、それ以外は 8。字形の無い文字は想定しない。
    int Measure(std::string_view text, bool numeric = false, unsigned scale = 1) const override {
        int width = 0, chars = 0;
        for (std::size_t i = 0; i < text.size() && chars < static_cast<int>(gohan::core::limits::RendererChars); ++chars) {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
            width += lead < 0x80 ? (numeric ? 5 : 4) : 8;
            i += length;
        }
        return width * static_cast<int>(scale);
    }
    bool BeginClip(Rect bounds) override {
        if (clipped_) return false;
        clipped_ = true;
        clip_ = bounds;
        return true;
    }
    void EndClip() override { clipped_ = false; }
    int TextsLeft() const override { return text_limit_ - texts; }
    int RectsLeft() const override { return rect_limit_ - rects; }

    void Sprite(int x, int y, std::string_view name, u32 color) override { Add(Command::Sprite, {x, y, 1, 1}, color, std::string(name), false, 1, 1); }
    void Box(Rect r, std::string_view name, u32 color) override { Add(Command::Slice, r, color, std::string(name), false, 1, name == "window" || name == "band9" || name == "box" ? 9 : 3); }
    void Pattern(Rect r, std::string_view name, u32 color) override { Add(Command::Pattern, r, color, std::string(name), false, 1, 1); }
    // ---- 試験の補助 ----
    const Command *FindText(const std::string &needle) const {
        for (const Command &c : commands)
            if (c.kind == Command::Text && c.text.find(needle) != std::string::npos) return &c;
        return nullptr;
    }
    bool HasText(const std::string &needle) const { return FindText(needle) != nullptr; }
    int Count(Command::Kind kind) const {
        int n = 0;
        for (const Command &c : commands) n += c.kind == kind;
        return n;
    }

  private:
    void Add(Command::Kind kind, Rect bounds, u32 color, std::string text, bool numeric, unsigned scale, int cost) {
        if ((color >> 24) == 0 || bounds.w <= 0 || bounds.h <= 0) return;
        if (rects + cost > rect_limit_) {
            ++dropped;
            return;
        }
        rects += cost;
        commands.push_back({kind, bounds, color, std::move(text), numeric, scale, clip_, clipped_});
    }
    int width_, text_limit_, rect_limit_;
    Rect clip_{};
    bool clipped_ = false;
};
} // namespace menu_test
