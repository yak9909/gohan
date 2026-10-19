#pragma once
#include "menu/motion.hpp"
#include <string_view>

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

inline constexpr std::size_t kLineBytes = 96;

// 幅に収まらなければ "..." を付けて切る。収まる時は text をそのまま返す。buffer は容量 capacity（終端を含む）。
// 1 つの文字列の最大字数（描画の 1 枠）も超えないようにする。
std::string_view Trim(const Surface &surface, std::string_view text, int max_width, char *buffer,
                      std::size_t capacity, bool numeric = false);
// 幅で折り返して lines（各 kLineBytes）に入れ、行数を返す。max_lines に入りきらなければ truncated を立て、
// 最後の行を "..." 付きで切る。
int Wrap(const Surface &surface, std::string_view text, int max_width, char (*lines)[kLineBytes], int max_lines,
         bool *truncated);
} // namespace gohan::menu
