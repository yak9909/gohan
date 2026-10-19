#pragma once
#include "core/feature.hpp"
#include "game/lyt/drawing.hpp"
#include <string_view>

namespace gohan::ui::renderer {
using game::lyt::Drawing;
using game::lyt::Font;
using game::lyt::Rect;
using game::lyt::Screen;
enum class Result { Added, Empty, Full, Budget, Invalid, Truncated };

// 作成者が所有するフレーム。Begin で画面を選んでから描く。
// Commit 後も変更でき、共有された実体は書き換えない。
class Canvas {
  public:
    void Begin(Screen screen);
    Result FillRect(Rect bounds, std::uint32_t color);
    Result FrameRect(Rect bounds, std::uint32_t color);
    Result Text(int x, int y, std::string_view text, std::uint32_t color,
                unsigned scale = 1, Font font = Font::Main);
    // 入れ子は受け付けない。矩形・文字ともに同じ切り抜き範囲を保持する。
    bool BeginClip(Rect bounds);
    void EndClip();
    const Drawing &Data() const { return drawing_; }

  private:
    Drawing drawing_{};
    Screen screen_ = Screen::Top;
    Rect clip_{};
    bool clipped_ = false;
    game::lyt::ScreenDrawing &Current();
};

// UTF-8 の先頭から最大 RendererChars 字。非対応の字は幅にも含めない。
int MeasureText(std::string_view text, unsigned scale = 1, Font font = Font::Main);
const game::lyt::FontResource &Fonts();

enum class Status { Stopped, Waiting, Ready, WrongTitle, Unreadable, Mismatch,
                    MissingBindings, PresentFailed, ResourceFailed, Stopping };
// 描画を使う側。どれか一つでも要求している間は導入し、全員が取り下げると取り外す。
enum class Client : unsigned { Default, Menu, Count };
// メニューのスレッドから。競合した場合は false を返すので次回再試行する。
// 引数一つの版は Client::Default の要求。
bool SetEnabled(bool enabled);
bool SetEnabled(Client client, bool enabled);
// 確定は画面ごとに置き換わる。両方を一度に確定する版と、一方だけを確定する版がある
// （後者は、もう一方の画面を使う別の呼び出し元の確定に触れない）。
bool Commit(const Canvas &canvas);
bool Commit(const Canvas &canvas, Screen screen);
Status ReadStatus();
const char *StatusText();
const core::Feature &RendererFeature();
} // namespace gohan::ui::renderer
