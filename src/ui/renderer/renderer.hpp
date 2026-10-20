#pragma once
#include "core/feature.hpp"
#include "game/lyt/drawing.hpp"
#include "game/title_background.hpp"
#include "ui_parts.hpp"
#include <string_view>

namespace gohan::ui::renderer {
using game::lyt::Drawing;
using game::lyt::Font;
using game::lyt::Rect;
using game::lyt::Screen;
using game::lyt::Texture;
using Part = ui_data::Part;
enum class Result { Added, Empty, Full, Budget, Invalid, Truncated };

// 作成者が所有するフレーム。Begin で画面を選んでから描く。
// Commit 後も変更でき、共有された実体は書き換えない。
class Canvas {
  public:
    void Begin(Screen screen);
    Result FillRect(Rect bounds, std::uint32_t color);
    Result FrameRect(Rect bounds, std::uint32_t color);
    Result Image(int x, int y, Rect source, unsigned scale = 1,
                 std::uint32_t color = 0xFFFFFFFFu, Texture texture = Texture::Ui);
    Result Image(int x, int y, Part part, unsigned scale = 1, std::uint32_t color = 0xFFFFFFFFu);
    // 固定する端・角は元画像の画素数。中央だけを伸ばす。
    Result NineSlice(Rect bounds, Rect source, int cap, unsigned scale = 1,
                     std::uint32_t color = 0xFFFFFFFFu);
    Result ThreeSliceHorizontal(Rect bounds, Rect source, int cap, unsigned scale = 1,
                                std::uint32_t color = 0xFFFFFFFFu);
    Result ThreeSliceVertical(Rect bounds, Rect source, int cap, unsigned scale = 1,
                              std::uint32_t color = 0xFFFFFFFFu);
    Result PartBox(Rect bounds, Part part, unsigned scale = 1, std::uint32_t color = 0xFFFFFFFFu);
    // 全画像を繰り返す独立したテクスチャ。Ui（部品をまとめたアトラス）は不可。
    Result Repeat(Rect bounds, Texture texture, int phaseX = 0, int phaseY = 0,
                  unsigned scale = 1, std::uint32_t color = 0xFFFFFFFFu);
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
    Result AddImage(Rect bounds, Rect source, Texture texture, unsigned scale,
                    std::uint32_t color, bool repeat);
    Result Slice(Rect bounds, Rect source, int cap, unsigned scale, std::uint32_t color, int mode);
};

// UTF-8 の先頭から最大 RendererChars 字。非対応の字は幅にも含めない。
int MeasureText(std::string_view text, unsigned scale = 1, Font font = Font::Main);
const game::lyt::FontResource &Fonts();
// プラグインのスレッドで、フック登録より前に一度だけ呼ぶ。Fonts() 自体は I/O しない。
void PrepareTitleBackground();
void StartTitleAnimation();
void StopTitleAnimation();
// 描画スレッドはコマ番号を通知し、完成世代だけ受け取る。合成・転送は行わない。
void StepTitleAnimation(std::uint32_t frame, Drawing &drawing);
const game::titlebg::Sources *TitleBackgroundSources();
const game::lyt::TextureResource &TitleBackgroundImage(unsigned index);
const char *TitleBackgroundStatusText();
const char *BackgroundAllocationStatusText();

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
