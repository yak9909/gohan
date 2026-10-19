// メニューが描くすべての文字に、字形の表（ui::renderer の書体）があるかを確かめる。
// 字形の無い文字は幅も無く、実機では何も出ない。見本の画面を一通り描いて、無い文字を集める。
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include "menu_rig.hpp"
#include "features/menu_screen/canvas_surface.hpp"
#include "game/lyt/renderer.hpp"
#include "ui/renderer/renderer.hpp"
#include <cstdio>
#include <initializer_list>
#include <set>
#include <string>

// 描画の部品を丸ごとリンクするので、ゲーム側の三つの関数を空で用意する（ここでは呼ばれない）。
namespace gohan::game::lyt {
InstallResult InstallRenderer(const FontResource &) { return InstallResult::MissingBindings; }
bool Present(const Drawing &) { return false; }
void DetachRenderer() {}
} // namespace gohan::game::lyt

using namespace menu_test;

namespace {
u16 Idx(MenuId id) { return static_cast<u16>(id); }

std::set<std::string> missing;
std::string seen; // 描いた文字列をすべて連ねた物（場面が狙いどおりに出たかの確認用）

// 描く文字を 1 字ずつ書体に問い合わせる。
class AuditSurface final : public RecordingSurface {
  public:
    void Text(int x, int y, std::string_view text, u32 color, bool numeric = false, unsigned scale = 1) override {
        const auto font = numeric ? gohan::ui::renderer::Font::Numeric : gohan::ui::renderer::Font::Main;
        for (std::size_t i = 0; i < text.size();) {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
            const std::string_view one = text.substr(i, length);
            if (gohan::ui::renderer::MeasureText(one, 1, font) == 0) missing.insert(std::string(one));
            i += length;
        }
        seen.append(text).push_back('\n');
        RecordingSurface::Text(x, y, text, color, numeric, scale);
    }
};

int scenes = 0;

void Audit(Rig &rig) {
    AuditSurface surface;
    rig.shell.Draw(surface, rig.now);
    // 同じ場面を本物の描画の部品へ渡し、文字の枠（長さごとの枠の数）や予算で捨てられないことも確かめる。
    gohan::ui::renderer::Canvas canvas;
    canvas.Begin(gohan::ui::renderer::Screen::Bottom);
    gohan::features::menu_screen::CanvasSurface real(canvas, gohan::ui::renderer::Screen::Bottom);
    rig.shell.Draw(real, rig.now);
    if (real.dropped() != 0 || real.truncated() != 0) std::fprintf(stderr, "場面 %d: 捨てた %d、切った %d\n", scenes, real.dropped(), real.truncated());
    assert(real.dropped() == 0 && real.truncated() == 0);
    ++scenes;
}

void Scenes() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    rig.Open();
    Audit(rig);
    rig.Tap(key::A);
    rig.Advance(300);
    rig.Draw();
    Audit(rig);
    // 状態の 3 つ（OFF・編集中の ON・待機）、予約、実行後の OK、画面の起動中。
    rig.engine.SetValue(Idx(MenuId::SampleFlag), 1);
    Audit(rig);
    rig.engine.Commit(Idx(MenuId::SampleFlag), rig.now);
    Audit(rig);
    rig.engine.SetValue(Idx(MenuId::SampleHold), 1);
    rig.engine.Commit(Idx(MenuId::SampleHold), rig.now);
    rig.engine.SetValue(Idx(MenuId::SampleStamp), 1);
    Audit(rig);
    rig.engine.Commit(Idx(MenuId::SampleStamp), rig.now);
    Audit(rig);
    rig.engine.Select(Idx(MenuId::SampleEditorA));
    rig.Tap(key::A);
    Audit(rig);
    // 長押し・確認・通知。
    rig.Hold(key::X);
    rig.Advance(300);
    Audit(rig);
    rig.Advance(400);
    Audit(rig);
    rig.Release();
    rig.Tap(key::B);
    rig.Frame(16, false);
    // 値の入力（数値・選択）とボタンの割り当て、値の読めない表示（---）。
    rig.engine.Select(Idx(MenuId::SampleValues));
    rig.Tap(key::A);
    rig.engine.Select(Idx(MenuId::SampleMode));
    rig.Tap(key::A);
    rig.Advance(300);
    Audit(rig);
    rig.Tap(key::B);
    rig.engine.Select(Idx(MenuId::SampleAmount));
    rig.Tap(key::A);
    rig.Advance(300);
    Audit(rig);
    rig.Tap(key::B);
    sample::Get().wallet_readable = false;
    rig.engine.DiscardAll();
    rig.engine.RequestClose(rig.now);
    rig.Advance(400);
    rig.Tap(key::Select);
    rig.Advance(400);
    rig.Tap(key::A);
    rig.engine.Select(Idx(MenuId::SampleValues));
    rig.Tap(key::A);
    rig.Advance(300);
    Audit(rig);
    rig.engine.Select(Idx(MenuId::SampleAmount));
    rig.Hold(key::Y);
    rig.Advance(300);
    Audit(rig);
    rig.Release();
    rig.Advance(50);
    Audit(rig);
    rig.Hold(key::R); // 割り当てを決めて枠を閉じる
    rig.Release();
    rig.Hold(key::L);
    rig.Advance(40);
    Audit(rig);
    rig.Release();
    // 閉じる確認、ホットキーの確認、通知。
    rig.Tap(key::B);
    rig.Tap(key::B);
    rig.engine.SetValue(Idx(MenuId::SampleMode), 1);
    rig.Tap(key::Select);
    rig.Advance(300);
    Audit(rig);
    rig.Tap(key::B);
    rig.engine.Discard(Idx(MenuId::SampleMode));
    rig.Tap(key::Select);
    rig.Advance(400);
    rig.Hold(key::L | key::Down);
    rig.Release();
    rig.Advance(300);
    Audit(rig);
    rig.engine.Confirm(false, rig.now);
    rig.Advance(100);
    Audit(rig);
    rig.Hold(key::L | key::Up); // 待機のフラグをホットキーで入れる
    rig.Release();
    rig.Advance(300);
    Audit(rig);
}
} // namespace

int main() {
    Scenes();
    // 狙いの場面が描かれたこと（描かれていない場面の文字は確かめられない）。
    for (const char *marker : {"-* GOHAN *-", "待機", "予約", "OK", "起動中", "開く", "全適用", "全戻し", "---", "決定", "取消",
                               "全てのボタンを離す", "ボタンを押す", "変更を適用しますか？", "を実行しますか？", "キャンセル",
                               "を有効にしました", "を実行しました", "A決定", "HOTKEY:"}) {
        if (seen.find(marker) == std::string::npos) std::fprintf(stderr, "描かれていない場面: %s\n", marker);
        assert(seen.find(marker) != std::string::npos);
    }
    // 字形の表に無い文字のうち、報告済みの物（「予約」「待機」の予・約・機）だけは許す。
    // ここに無い文字が増えたら、メニューの文言か字形の表を直す。
    const std::set<std::string> known = {"予", "約", "機"};
    for (const std::string &one : missing) {
        std::printf("字形が無い文字: %s%s\n", one.c_str(), known.count(one) ? "（報告済み）" : "");
        assert(known.count(one));
    }
    std::puts("menu glyphs: ok");
    return 0;
}
