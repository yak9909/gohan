// メニューが描くすべての文字に、字形の表（ui::renderer の書体）があるかを確かめる。
// 字形の無い文字は幅も無く、実機では何も出ない。見本の画面を一通り描いて、無い文字を集める。
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include "menu_rig.hpp"
#include "features/menu_screen/canvas_surface.hpp"
#include "game/lyt/renderer.hpp"
#include "ui/renderer/renderer.hpp"
#include <cstdio>
#include <algorithm>
#include <initializer_list>
#include <set>
#include <string>

// 描画の部品を丸ごとリンクするので、ゲーム側の関数を空で用意する（ここでは呼ばれない）。
namespace gohan::game::lyt {
InstallResult InstallRenderer(const FontResource &, BackgroundAllocation *) { return InstallResult::MissingBindings; }
bool Present(const Drawing &) { return false; }
bool DetachRenderer() { return true; }
void WaitMilliseconds(unsigned) {}
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
    canvas.Begin(gohan::ui::renderer::Screen::Top);
    gohan::features::menu_screen::CanvasSurface real(canvas, gohan::ui::renderer::Screen::Top);
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
    namespace ca = gohan::core::design::ca2;
    Scenes();
    // スイッチ10行・長い説明・7通知・選択とスクロール途中を同時に実際の文字枠へ通す。
    {
        ItemDef defs[24]{};
        for (unsigned n = 0; n < 24; ++n) {
            auto &d = defs[n];
            d.key = "stress"; d.label = "見本の長い名前のフラグ見本の長い名前のフラグ見本の長い名前のフラグ";
            d.desc = "見本の長い説明です。見本の長い説明です。見本の長い説明です。見本の長い説明です。見本の長い説明です。見本の長い説明です。見本の長い説明です。見本の長い説明です。見本の長い説明です。見本の長い説明です。";
            d.control = Control::Toggle; d.behavior = Behavior::Value; d.source = Source::Setting;
            d.parent = d.first_child = kNoItem; d.next = n < 23 ? n + 1 : kNoItem;
            d.min = 0; d.max = d.step = 1; d.hotkey = key::L | key::Up;
        }
        Rig r(defs, 24); r.Open();
        NoticeQueue notices; Notice notice{}; NoticeFailed(notice, "GOHANGOHANGOHANGOHANGOHANGOHAN", Result::NotReady);
        const u32 born = r.now;
        for (unsigned n = 0; n < 7; ++n) notices.Add(notice, r.now);
        std::size_t maxRects = 0, maxTexts = 0, maxBytes = 0;
        for (unsigned t = 0; t < 4000; t += 16) {
            if (t % 64 == 0) { r.engine.Select((t / 64) % 24); r.engine.SetValue((t / 64) % 24, (t / 64) % 2); }
            r.Frame();
            gohan::ui::renderer::Canvas canvas; canvas.Begin(gohan::ui::renderer::Screen::Top);
            gohan::features::menu_screen::CanvasSurface surface(canvas, gohan::ui::renderer::Screen::Top);
            r.shell.Draw(surface, r.now); notices.Draw(surface, r.now);
            assert(surface.dropped() == 0 && surface.truncated() == 0);
            const auto &d = canvas.Data().screens[0];
            unsigned actual = 0, expected = 0;
            for (unsigned n = 0; n < 7; ++n) {
                const s32 age = static_cast<s32>(r.now - (born + n * ca::Time::Notice[3]));
                expected += age >= ca::Time::Notice[0] && age < ca::Time::Notice[0] + ca::Time::Notice[1];
            }
            for (std::size_t n = 0; n < d.count; ++n) {
                const auto &c = d.commands[n];
                actual += c.kind == gohan::game::lyt::Kind::Text && c.length > 5 &&
                          std::u16string_view(c.text, 5) == u"GOHAN";
            }
            // 予算不足で通知そのものを省いた場合も、欠落として検出する。
            if (actual < expected) std::fprintf(stderr, "通知の欠落: %u/%u 時刻=%u 文字枠=%zu\n", actual, expected, t, d.texts);
            assert(actual >= expected);
            maxRects = std::max(maxRects, d.rectangles); maxTexts = std::max(maxTexts, d.texts); maxBytes = std::max(maxBytes, d.estimatedBytes);
        }
        std::printf("最大重なり: 板=%zu 文字枠=%zu 記録=%zu\n", maxRects, maxTexts, maxBytes);
    }
    // 狙いの場面が描かれたこと（描かれていない場面の文字は確かめられない）。
    for (const char *marker : {"GOHAN", "待機", "予約", "OK", "起動中", "開く", "全適用", "全戻し", "---", "決定", "取消",
                               "ボタンを押す", "変更を適用しますか？", "を実行しますか？", "キャンセル",
                               "を有効にしました", "を実行しました", "決定", "ホットキー"}) {
        if (seen.find(marker) == std::string::npos) std::fprintf(stderr, "描かれていない場面: %s\n", marker);
        assert(seen.find(marker) != std::string::npos);
    }
    for (const std::string &one : missing) {
        std::printf("字形が無い文字: %s\n", one.c_str());
    }
    assert(missing.empty());
    std::puts("menu glyphs: ok");
    return 0;
}
