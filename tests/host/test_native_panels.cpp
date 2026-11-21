// Panel の排出順と、一覧・箱の公開 API を代役の描画先で検査する。
#include "ui/bottom_screen_panel.hpp"
#include "ui/native_panel_input.hpp"
#include "ui/lists/catalog_list.hpp"
#include "ui/game_label.hpp"
#include "game/panel_math.hpp"
#include "game/catalog_layout.hpp"
#include "game/lyt/bclyt_edit.hpp"
#include <cstring>
#include <cmath>
#include <cassert>
#include <cstdio>
using namespace gohan;
namespace {
unsigned held = 0, returned = 0;
bool Acquire(std::uint32_t n, bool) { held |= n; returned &= ~n; return true; }
bool Release(std::uint32_t n) { held &= ~n; return true; }
bool Returned(std::uint32_t n) { returned |= n; return true; }
ui::LeaseStatus Hidden() { ui::LeaseStatus s; s.stage = ui::LeaseStage::Hidden; s.hidden = true; return s; }
struct Content : game::PanelContent {
    int loading = 2, updates = 0, draws = 0, detached = 0, reset = 0;
    bool show = false, failed = false;
    game::PanelBuild Build() override {
        if (loading-- > 0) return game::PanelBuild::Pending;
        return failed ? game::PanelBuild::Failed : game::PanelBuild::Ready;
    }
    void Show(bool v) override { show = v; }
    void Update() override { ++updates; }
    bool Finished() const override { return updates >= 3; }
    void Draw() override { ++draws; }
    void Detach() override { ++detached; }
    void Reset() override { assert(!returned); ++reset; }
};
void Panel() {
    held = returned = 0;
    Content c; ui::BottomScreenPanel p(c, 1, false, {Acquire, Release, Returned});
    ui::LeaseStatus visible;
    p.Request(true); p.Step(visible);
    assert(held == 1 && c.loading == 2 && c.draws == 0); // 地図の退場待ちには確保しない
    p.Step(Hidden()); p.Request(false); p.Step(Hidden());
    assert(c.reset == 0 && held == 1); // Stop が組み立てを途中で壊さない
    p.Step(Hidden()); p.Step(Hidden()); p.Step(Hidden());
    assert(c.detached == 1 && c.reset == 0);
    const int draws = c.draws;
    p.Step(Hidden()); p.Step(Hidden()); assert(c.reset == 0 && c.draws == draws);
    p.Step(Hidden()); assert(c.reset == 1 && returned == 0);
    p.Step(Hidden()); assert(p.Drained() && returned == 1 && held == 0);
    p.Stop(); p.Request(true); p.Step(Hidden()); assert(p.Drained());
    held = returned = 0;
    Content f; f.loading = 0; f.failed = true;
    ui::BottomScreenPanel failed(f, 2, false, {Acquire, Release, Returned});
    failed.Request(true); failed.Step(Hidden());
    assert(f.detached == 1 && f.reset == 0);
    for (int i = 0; i < 4; ++i) failed.Step(Hidden());
    assert(failed.Drained() && f.reset == 1 && returned == 2);
    held = returned = 0;
    Content t; t.loading = 0;
    ui::BottomScreenPanel top(t, 4, true, {Acquire, Release, Returned});
    auto transition = visible; transition.stage = ui::LeaseStage::Restoring;
    top.Request(true); top.Step(transition); assert(t.loading == 0);
    top.Step(visible); assert(t.draws == 1); // 地図の取り直しと組み立てを重ねない
}
std::vector<game::CatalogRow> catalog_rows;
std::vector<game::LabelLine> label_lines;
int selected = 0, picked = -1, extra_calls = 0, label_draws = 0;
bool catalog_shown = false, label_shown = false;
void Lists() {
    held = returned = 0;
    ui::lists::CatalogList list;
    std::vector<ui::lists::Row> rows;
    for (unsigned i = 0; i < 40; ++i) rows.push_back({"name", i + 1, "prefix", "extra"});
    assert(list.Show(std::move(rows), "message", true));
    list.Step(Hidden(), 0, 0, false, 0, 0, false);
    assert(catalog_rows.size() == 40 && catalog_rows[3].message == 4 &&
           catalog_rows[3].prefix == "prefix" && catalog_rows[3].extra == "extra");
    list.Step(Hidden(), ui::lists::DpadRepeat::Right, 100, false, 0, 0, false);
    assert(selected == 6);
    list.Step(Hidden(), ui::lists::DpadRepeat::Right, 499, false, 0, 0, false); assert(selected == 6);
    list.Step(Hidden(), ui::lists::DpadRepeat::Right, 500, false, 0, 0, false); assert(selected == 12);
    list.Step(Hidden(), 0, 501, false, 0, 0, true); assert(list.TakeDecision() == 12);
    list.Step(Hidden(), 0, 502, false, 0, 0, true); assert(list.TakeDecision() == -1);
    picked = 35; list.Step(Hidden(), 0, 503, false, 0, 0, false); assert(list.TakeDecision() == 35);
    // メニュー中は方向・A・タッチの決定を処理しない。
    picked = 8;
    list.Step(Hidden(), ui::lists::DpadRepeat::Right, 504, true, 20, 30, true, false);
    assert(selected == 12 && list.TakeDecision() == -1);
    list.Step(Hidden(), 0, 505, false, 0, 0, false);
    assert(list.TakeDecision() == -1);
    // ゲームが地図を自動復帰させても、再入場には同じ行の一括データを再設定する。
    for (unsigned i = 0; i < 8; ++i) list.Step({}, 0, 510 + i, false, 0, 0, false);
    for (unsigned i = 0; i < 3; ++i) list.Step(Hidden(), 0, 520 + i, false, 0, 0, false);
    assert(catalog_rows.size() == 40 && catalog_rows[3].message == 4);
    list.Close();
    list.Step(Hidden(), 0, 599, false, 0, 0, true);
    assert(list.TakeDecision() == -1); // 閉じる途中のAを決定として出さない
    for (unsigned i = 0; i < 10; ++i) list.Step(Hidden(), 0, 600 + i, false, 0, 0, false);
    assert(list.Drained());
    assert(list.Show({{"new", 0, "", ""}}));
    for (unsigned i = 0; i < 3; ++i) list.Step(Hidden(), 0, 700 + i, false, 0, 0, false);
    assert(catalog_rows.size() == 1 && catalog_rows[0].text == "new");
    list.Stop();
    for (unsigned i = 0; i < 10; ++i) list.Step(Hidden(), 0, 800 + i, false, 0, 0, false);
    assert(list.Drained() && !list.Show({{"after stop", 0, "", ""}}));
}
void Layout() {
    std::vector<std::uint8_t> raw(20, 0);
    std::memcpy(raw.data(), "CLYT", 4); raw[4] = 0xFF; raw[5] = 0xFE; raw[6] = 20;
    auto put = [&](unsigned pos, unsigned value) { std::memcpy(raw.data() + pos, &value, 4); };
    for (unsigned i = 0; i < 9; ++i) {
        const unsigned pos = raw.size(); raw.resize(pos + 116, 0);
        std::memcpy(raw.data() + pos, i ? "txt1" : "pan1", 4); put(pos + 4, 116);
        char name[16]; if (!i) std::strcpy(name, "N_frame_00");
        else std::snprintf(name, sizeof(name), "T_itm_%02u", i - 1);
        std::memcpy(raw.data() + pos + 12, name, std::strlen(name));
        const float x = 10, w = 100, font = 20;
        std::memcpy(raw.data() + pos + 36, &x, 4);
        std::memcpy(raw.data() + pos + 68, &w, 4);
        std::memcpy(raw.data() + pos + 100, &font, 4); std::memcpy(raw.data() + pos + 104, &font, 4);
        raw[pos + 85] = 0x5A; // 配置の隣の欄を編集で壊さない
    }
    put(12, raw.size()); put(16, 9);
    const auto result = game::MakeCatalogExtraLayout(raw.data(), raw.size());
    game::lyt::BclytEdit edited; assert(edited.Load(result.data(), result.size()) && edited.SectionCount() == 17);
    float rect[4], font;
    assert(edited.GetRect(edited.FindByName("N_frame_00"), rect) && rect[0] == -3 && rect[2] == 126);
    assert(edited.GetRect(edited.FindByName("T_id_07"), rect) && rect[0] == -47 && rect[2] == 47);
    assert(edited.GetFloat(edited.FindByName("T_id_03"), 92, font) && std::abs(font - 13) < 0.001);
    assert(edited.GetFloat(edited.FindByName("T_itm_03"), 92, font) && font == 20);
    unsigned duplicates = 0;
    for (unsigned pos = 20; pos < result.size();) {
        unsigned bytes; std::memcpy(&bytes, result.data() + pos + 4, 4);
        if (!std::memcmp(result.data() + pos + 12, "T_id_", 5)) {
            assert(result[pos + 84] == 4 && result[pos + 85] == 0x5A); ++duplicates;
        }
        pos += bytes;
    }
    assert(duplicates == 8);
    auto bad = raw; bad[0] = 0; assert(game::MakeCatalogExtraLayout(bad.data(), bad.size()).empty());
    // 8行揃っていなければ、半端な追加テキストを出さない。
    bad = raw; bad[20 + 8 * 116 + 12] = 'X';
    assert(game::MakeCatalogExtraLayout(bad.data(), bad.size()).empty());
}
void Labels() {
    held = returned = 0;
    ui::GameLabel labels;
    assert(labels.Declare(0, {{"first", 0}, {"second", 1}}));
    assert(labels.Declare(1, {{"another owner", 0}}));
    labels.Step({});
    assert(label_lines.size() == 3 && label_lines[1].row == 1 && label_lines[2].text == "another owner");
    assert(!labels.Declare(8, {{"bad owner", 0}}));
    assert(!labels.Declare(2, std::vector<ui::LabelLine>(6)));
    assert(labels.Declare(0, {}));
    for (int i = 0; i < 10; ++i) labels.Step({});
    assert(label_lines.size() == 1 && label_lines[0].text == "another owner");
    labels.Stop(); for (int i = 0; i < 10; ++i) labels.Step({});
    assert(labels.Drained() && !labels.Declare(0, {{"stopped", 0}}));
}
}
namespace gohan::ui::bottom_screen_lease {
bool Acquire(std::uint32_t n, bool chat) { return ::Acquire(n, chat); }
bool Release(std::uint32_t n) { return ::Release(n); }
bool ResourcesReturned(std::uint32_t n) { return ::Returned(n); }
}
namespace gohan::game {
bool CatalogContent::Set(std::vector<CatalogRow> r, std::string, bool e) { catalog_rows = std::move(r); extra_calls += e; return true; }
PanelBuild CatalogContent::Build() { return PanelBuild::Ready; }
void CatalogContent::Show(bool b) { catalog_shown = b; }
void CatalogContent::Update() {}
bool CatalogContent::Finished() const { return !catalog_shown; }
void CatalogContent::Draw() {}
void CatalogContent::Detach() {}
void CatalogContent::Reset() { catalog_rows.clear(); }
void CatalogContent::Select(int n) { selected = n; }
int CatalogContent::Selected() const { return selected; }
int CatalogContent::VisibleRows() const { return 6; }
int CatalogContent::TakeDecision() { const int n = picked; picked = -1; return n; }
bool LabelContent::Set(std::vector<LabelLine> lines) { label_lines = std::move(lines); return true; }
PanelBuild LabelContent::Build() { return PanelBuild::Ready; }
void LabelContent::Show(bool b) { label_shown = b; }
void LabelContent::Update() {}
bool LabelContent::Finished() const { return !label_shown; }
void LabelContent::Draw() { ++label_draws; }
void LabelContent::Detach() {}
void LabelContent::Reset() { label_lines.clear(); }
}
int main() {
    // メニューの間は一覧へ渡さず、閉じた後も各ボタン・タッチを離すまで使わない。
    ui::native_panels::InputGate gate;
    ui::input::Snapshot pad{ui::input::button::A | ui::input::button::Down, true, 20, 30};
    gate.Block(true);
    assert(gate.Filter(pad).buttons == 0 && !gate.Filter(pad).touching);
    gate.Block(false);
    assert(gate.Filter(pad).buttons == 0 && !gate.Filter(pad).touching);
    pad.buttons = ui::input::button::Down;
    assert(gate.Filter(pad).buttons == 0);
    pad.buttons |= ui::input::button::A;
    assert(gate.Filter(pad).buttons == ui::input::button::A);
    pad = {};
    gate.Filter(pad);
    pad = {ui::input::button::Down, true, 20, 30};
    assert(gate.Filter(pad).buttons == ui::input::button::Down && gate.Filter(pad).touching);
    assert(gate.Filter(pad, true).buttons == 0);
    assert(!gate.Filter(pad).touching);

    Panel(); Lists(); Labels(); Layout();
    int owned, foreign;
    assert(game::OwnedDrag(&owned, &owned, 1, 0, 2));
    assert(!game::OwnedDrag(&foreign, &owned, 1, 0, 2));
    assert(!game::OwnedDrag(&owned, &owned, 0, 0, 2));
    assert(!game::OwnedDrag(&owned, &owned, 1, 1, 2));
    assert(!game::OwnedDrag(&owned, &owned, 1, 0, 3));
    assert(!game::OwnedDrag(nullptr, nullptr, 1, 0, 2));
    assert(game::HalfDrag(-15, 25) == 5 && game::HalfDrag(10, 4) == 7);
    assert(game::LabelWidth(10) == 98.0f * 1.3f && game::LabelWidth(200) == 260);
    assert(game::LabelNext(-192, 100) > -192);
    std::puts("test_native_panels: OK");
}
