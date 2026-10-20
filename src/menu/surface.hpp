#pragma once
#include "menu/motion.hpp"
#include <string_view>
#include <string>

// メニューが描く先。描画の部品（ui）には依存せず、組み立て側が実装を渡す。
// 色は 0xAARRGGBB。座標は画面の左上が原点の画素。
namespace gohan::menu {
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class Surface {
  public:
    virtual int Width() const = 0;
    virtual void Fill(Rect bounds, u32 color) = 0;
    virtual void Outline(Rect bounds, u32 color) = 0;
    // numeric は数字用の書体。scale は 1 以上の整数倍。
    virtual void Text(int x, int y, std::string_view text, u32 color, bool numeric = false, unsigned scale = 1) = 0;
    virtual int Measure(std::string_view text, bool numeric = false, unsigned scale = 1) const = 0;
    virtual bool BeginClip(Rect bounds) = 0;
    virtual void EndClip() = 0;
    // この画面で、あと何個描けるか（描画の予算）。優先度の低い物から省くのに使う。
    virtual int TextsLeft() const = 0;
    virtual int RectsLeft() const = 0;
    // 名前は描画先との契約。menu は UI の型やアトラスに依存しない。
    virtual void Sprite(int, int, std::string_view, u32 = 0xFFFFFFFFu) {}
    virtual void Box(Rect bounds, std::string_view, u32 color = 0xFFFFFFFFu) { Fill(bounds, color); }
    virtual void Pattern(Rect, std::string_view, u32 = 0xFFFFFFFFu) {}

  protected:
    ~Surface() = default;
};

inline constexpr int kScreenHeight = 240;

inline constexpr u32 kRgb = (1u << 24) - 1u; // 色の RGB の部分

// 元の色の不透明度に係数を掛ける。
inline u32 Fade(u32 color, float amount) {
    const u32 alpha = static_cast<u32>(static_cast<float>(color >> 24) * motion::Clamp01(amount) + 0.5f);
    return (color & kRgb) | (alpha << 24);
}
// 不透明度を指定した値にする。
inline u32 WithAlpha(u32 color, float alpha) {
    return (color & kRgb) | (static_cast<u32>(motion::Clamp01(alpha) * 255.0f + 0.5f) << 24);
}

inline u32 MixColor(u32 from, u32 to, float amount) {
    u32 result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        result |= static_cast<u32>(motion::Mix(float((from >> shift) & 255), float((to >> shift) & 255),
                                               motion::Clamp01(amount)) + .5f) << shift;
    return result;
}

// 印も含め、操作の帯の全要素へ同じ不透明度を掛ける。
class FadedSurface final : public Surface {
  public:
    FadedSurface(Surface &surface, float alpha) : surface_(surface), alpha_(alpha) {}
    int Width() const override { return surface_.Width(); }
    void Fill(Rect r, u32 c) override { surface_.Fill(r, Fade(c, alpha_)); }
    void Outline(Rect r, u32 c) override { surface_.Outline(r, Fade(c, alpha_)); }
    void Text(int x, int y, std::string_view t, u32 c, bool n = false, unsigned scale = 1) override {
        surface_.Text(x, y, t, Fade(c, alpha_), n, scale);
    }
    int Measure(std::string_view t, bool n = false, unsigned scale = 1) const override { return surface_.Measure(t, n, scale); }
    bool BeginClip(Rect r) override { return surface_.BeginClip(r); }
    void EndClip() override { surface_.EndClip(); }
    int TextsLeft() const override { return surface_.TextsLeft(); }
    int RectsLeft() const override { return surface_.RectsLeft(); }
    void Sprite(int x, int y, std::string_view n, u32 c = 0xFFFFFFFFu) override { surface_.Sprite(x, y, n, Fade(c, alpha_)); }
    void Box(Rect r, std::string_view n, u32 c = 0xFFFFFFFFu) override { surface_.Box(r, n, Fade(c, alpha_)); }
    void Pattern(Rect r, std::string_view n, u32 c = 0xFFFFFFFFu) override { surface_.Pattern(r, n, Fade(c, alpha_)); }
  private:
    Surface &surface_;
    float alpha_;
};

inline constexpr std::size_t kLineBytes = 96;
// 設計表の表記を描画時に統一する。幅の測定にも同じ結果を使う。
std::string DisplayText(std::string_view text);

// 幅に収まらなければ末尾の開き括弧と空白を落とし、設計表の省略記号を付ける。
// 収まる時は text をそのまま返す。buffer は容量 capacity（終端を含む）。
// 1 つの文字列の最大字数（描画の 1 枠）も超えないようにする。
std::string_view Trim(const Surface &surface, std::string_view text, int max_width, char *buffer,
                      std::size_t capacity, bool numeric = false);
// 幅で折り返して lines（各 kLineBytes）に入れ、行数を返す。max_lines に入りきらなければ truncated を立て、
// 最後の行を省略記号付きで切る。
int Wrap(const Surface &surface, std::string_view text, int max_width, char (*lines)[kLineBytes], int max_lines,
         bool *truncated);
} // namespace gohan::menu
