// 実際の毎フレーム処理を入力・時計・描画先の代役で動かし、確定した描画指示を保存する。
#include "core/frame.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "game/lyt/renderer.hpp"
#include "game/lyt/scene.hpp"
#include "ui/renderer/renderer.hpp"
#include "features/menu_screen/canvas_surface.hpp"
#include "menu/shell.hpp"
#include <CTRPluginFramework.hpp>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace gohan::game::lyt {
namespace {
Drawing presented;
}
InstallResult InstallRenderer(const FontResource &, BackgroundAllocation *) { return InstallResult::Ready; }
bool Present(const Drawing &drawing) {
    presented = drawing;
    return true;
}
bool DetachRenderer() { return true; }
RendererMemory ReadRendererMemory() { return {}; }
void WaitMilliseconds(unsigned) { gohan::core::frame::Dispatch(); }
const Drawing &Last() { return presented; }
} // namespace gohan::game::lyt

namespace {
namespace lyt = gohan::game::lyt;
namespace renderer = gohan::ui::renderer;

template <typename T> void Array(std::ostream &out, const T *values, std::size_t count) {
    out << '[';
    for (std::size_t i = 0; i < count; ++i) {
        if (i) out << ',';
        out << static_cast<unsigned>(values[i]);
    }
    out << ']';
}
void Rect(std::ostream &out, const lyt::Rect &r) {
    out << '[' << r.x << ',' << r.y << ',' << r.width << ',' << r.height << ']';
}
// 実際の Scene が作る矩形の材質・UV を保存する。プレビュー側で白を仮定しない。
void RectangleMaterial(std::ostream &out, const lyt::FontResource &font) {
    namespace gen = gohan::game::generated;
    lyt::detail::ResourceApi api{};
    api.physical = [](const void *) -> std::uint32_t { return 0; };
    api.batchInit = [](gen::NwFontTextBatchPrefix *batch, unsigned) { return batch; };
    auto scene = std::make_unique<lyt::detail::Scene>();
    scene->Build(api, font, font.atlas);
    auto drawing = std::make_unique<lyt::ScreenDrawing>();
    drawing->count = 1;
    drawing->commands[0].bounds = {0, 0, 1, 1};
    scene->Apply(lyt::Screen::Top, *drawing);
    const auto *link = scene->Root(lyt::Screen::Top)->children.next;
    const auto *picture = reinterpret_cast<const gen::NwLytPicture *>(
        reinterpret_cast<const char *>(link) - offsetof(gen::NwLytPane, sibling_link));
    out << ",\"rectangle\":{\"uv\":[";
    for (unsigned i = 0; i < 4; ++i) {
        if (i) out << ',';
        out << picture->uv_output[0][i];
    }
    out << "],\"black\":[";
    for (unsigned c = 0; c < 2; ++c) {
        if (c) out << "],\"white\":[";
        for (unsigned i = 0; i < 4; ++i) {
            if (i) out << ',';
            out << ((picture->material->colors[c] >> (8 * i)) & 255);
        }
    }
    out << "]}";
}
bool Resources(const std::filesystem::path &directory) {
    renderer::PrepareTitleBackground();
    std::ofstream status(directory / "background-status.txt");
    status << renderer::TitleBackgroundStatusText() << '\n';
    const auto &f = renderer::Fonts();
    std::ofstream atlas(directory / "atlas.a4", std::ios::binary);
    atlas.write(reinterpret_cast<const char *>(f.atlas), static_cast<std::streamsize>(f.atlasBytes));
    std::ofstream out(directory / "fonts.json");
    out << "{\"format\":\"a4\",\"sheet\":[" << f.sheetWidth << ',' << f.sheetHeight << "],\"cell\":["
        << f.cellWidth << ',' << f.cellHeight << "],\"columns\":" << f.columns << ",\"widths\":";
    Array(out, f.widths, f.glyphs * 3);
    out << ",\"main\":";
    Array(out, f.mainMap, f.mainCount * 2);
    out << ",\"numeric\":";
    Array(out, f.numericMap, f.numericCount * 2);
    RectangleMaterial(out, f);
    out << "}\n";
    std::ofstream textures(directory / "textures.json");
    textures << '[';
    for (std::size_t i = 0; i < f.textureCount; ++i) {
        if (i) textures << ',';
        const auto &t = f.textures[i];
        const auto filename = "texture-" + std::to_string(i) + ".rgba8";
        textures << "{\"width\":" << t.width << ",\"height\":" << t.height
                 << ",\"file\":\"" << filename << "\"}";
        std::ofstream data(directory / filename, std::ios::binary);
        if (t.format == lyt::TextureFormat::L4) {
            for (std::size_t pixel = 0; pixel < std::size_t(t.width) * t.height; ++pixel) {
                const unsigned level = (t.data[pixel / 2] >> ((pixel & 1) * 4)) & 15;
                const char alpha = static_cast<char>(t.opacity);
                data.write(&alpha, 1);
                for (unsigned shift : {0u, 8u, 16u}) {
                    const unsigned black = (t.black >> shift) & 255, white = (t.white >> shift) & 255;
                    const char channel = static_cast<char>((black * (15 - level) + white * level + 7) / 15);
                    data.write(&channel, 1);
                }
            }
        } else {
            data.write(reinterpret_cast<const char *>(t.data), static_cast<std::streamsize>(t.bytes));
        }
        if (!data.good()) return false;
    }
    textures << "]\n";
    return atlas.good() && out.good() && textures.good();
}
bool Capture(const std::filesystem::path &directory, int index, int now) {
    const auto resources = directory / ("resources-" + std::to_string(index));
    std::filesystem::create_directories(resources);
    if (!Resources(resources)) return false;
    std::ofstream out(directory / ("frame-" + std::to_string(index) + ".json"));
    out << "{\"at\":" << now << ",\"screens\":[";
    for (unsigned s = 0; s < 2; ++s) {
        if (s) out << ',';
        out << '[';
        const auto &screen = lyt::Last().screens[s];
        for (std::size_t i = 0; i < screen.count; ++i) {
            if (i) out << ',';
            const auto &c = screen.commands[i];
            out << "{\"kind\":" << static_cast<unsigned>(c.kind) << ",\"bounds\":";
            Rect(out, c.bounds);
            out << ",\"clip\":";
            Rect(out, c.clip);
            out << ",\"color\":" << c.color << ",\"font\":" << static_cast<unsigned>(c.font)
                << ",\"scale\":" << c.scale << ",\"slot\":" << c.slot << ",\"text\":";
            Array(out, c.text, c.length);
            out << ",\"source\":";
            Rect(out, c.source);
            out << ",\"texture\":" << static_cast<unsigned>(c.texture);
            out << '}';
        }
        out << ']';
    }
    out << "]}\n";
    return out.good();
}

bool PartsCatalog(const std::filesystem::path &directory) {
    using renderer::Part;
    using renderer::Result;
    constexpr unsigned PerPage = 12;
    const auto total = static_cast<unsigned>(Part::Count);
    unsigned page = 0;
    for (unsigned start = 0; start < total; start += PerPage, ++page) {
        renderer::Canvas canvas;
        canvas.Begin(lyt::Screen::Top);
        canvas.Text(4, 4, "CA2 UI / 1x 2x stretch", 0xFFEAF5E4u);
        for (unsigned i = start; i < total && i < start + PerPage; ++i) {
            const int x = 4 + static_cast<int>((i - start) % 3) * 132;
            const int y = 20 + static_cast<int>((i - start) / 3) * 54;
            const auto part = static_cast<Part>(i);
            if (canvas.Image(x, y, part) != Result::Added ||
                canvas.Image(x + 28, y, part, 2) != Result::Added)
                return false;
            const auto &p = renderer::ui_data::Parts[i];
            if (p.mode) {
                const int w = p.mode == 4 ? p.width : 48;
                const int h = p.mode == 3 ? p.height : 30;
                if (canvas.PartBox({x + 76, y, w, h}, part) != Result::Added) return false;
            }
            if (canvas.Text(x, y + 38, renderer::ui_data::PartNames[i], 0xFFEAF5E4u) != Result::Added)
                return false;
        }
        lyt::Present(canvas.Data());
        if (!Capture(directory, static_cast<int>(page), 0)) return false;
    }
    renderer::Canvas canvas;
    canvas.Begin(lyt::Screen::Top);
    canvas.Text(4, 4, "repeat / phase / tint / clip", 0xFFEAF5E4u);
    for (int phase = 0; phase < 8; ++phase) {
        canvas.Repeat({8, 24 + phase * 12, 180, 4}, lyt::Texture::DashHorizontal, phase, 0, 1, 0xFF4F9A52u);
        canvas.Repeat({204 + phase * 12, 24, 4, 96}, lyt::Texture::DashVertical, 0, -phase, 1, 0xFF2B8FD8u);
    }
    canvas.Repeat({8, 128, 180, 8}, lyt::Texture::DashHorizontal, -3, 0, 2, 0x809A6B00u);
    canvas.BeginClip({20, 150, 80, 16});
    canvas.Image(12, 144, Part::Finger, 8, 0xFF88FFAAu);
    canvas.EndClip();
    lyt::Present(canvas.Data());
    if (!Capture(directory, static_cast<int>(page++), 0)) return false;
    canvas.Begin(lyt::Screen::Top);
    canvas.Text(4, 4, "CA2 title background / frame 0", 0xFFEAF5E4u);
    canvas.FillRect({4, 20, 228, 210}, 0xFFF8F4E6u);
    canvas.Repeat({7, 23, 222, 204}, lyt::Texture::TitleWindow);
    canvas.Repeat({238, 24, 156, 13}, lyt::Texture::TitleSelection);
    canvas.Repeat({238, 48, 64, 64}, lyt::Texture::TitleAlpha, 0, 0, 4);
    canvas.Repeat({308, 48, 64, 64}, lyt::Texture::TitleLuminance, 0, 0, 4);
    lyt::Present(canvas.Data());
    return Capture(directory, static_cast<int>(page), 0);
}
// 項目数の多い画面だけは PC 専用の定義を渡す。Shell・省略・描画は製品と同じ物。
bool ScrollCatalog(const std::filesystem::path &directory) {
    namespace menu = gohan::menu;
    menu::ItemDef defs[25]{};
    std::string labels[25];
    labels[0] = "スクロールの見本";
    for (unsigned n = 0; n < 25; ++n) {
        auto &d = defs[n];
        if (n) {
            char text[96];
            std::snprintf(text, sizeof(text), n == 12 || n == 14 ? "とても長い名前の見本の項目 %02u" : "見本の項目 %02u", n);
            labels[n] = text;
        }
        d.key = "preview"; d.label = labels[n].c_str();
        d.desc = "項目が多い時のスクロールの棒と、長い名前の省き方を見るための仮の項目です。";
        d.control = n ? menu::Control::Toggle : menu::Control::Folder;
        d.behavior = n ? menu::Behavior::Value : menu::Behavior::None;
        d.source = n ? menu::Source::Setting : menu::Source::None;
        d.parent = n ? 0 : menu::kNoItem;
        d.first_child = n ? menu::kNoItem : 1;
        d.next = n && n < 24 ? n + 1 : menu::kNoItem;
        d.def = n % 3 == 0; d.min = 0; d.max = 1; d.step = 1;
    }
    menu::Engine engine(defs, 25); menu::Shell shell(engine, {});
    engine.Open(0); engine.Press(menu::key::A, 0); engine.Select(14);
    shell.Step(0, {}, false); shell.Step(400, {}, false);
    renderer::Canvas canvas; canvas.Begin(lyt::Screen::Top);
    gohan::features::menu_screen::CanvasSurface surface(canvas, lyt::Screen::Top);
    shell.Draw(surface, 400);
    if (surface.dropped() || surface.truncated()) return false;
    lyt::Present(canvas.Data());
    return Capture(directory, 0, 400);
}

// 7件の後に5件を同時追加する。実製品の通知・Canvas・切り抜きで連続するあふれを見る。
bool NoticeOverflow(const std::filesystem::path &directory) {
    gohan::menu::NoticeQueue queue;
    auto add = [&](unsigned number, unsigned now) {
        gohan::menu::Notice notice{};
        std::snprintf(notice.title, sizeof(notice.title), "通知 %02u", number);
        std::snprintf(notice.message, sizeof(notice.message), "連続追加の見本 %02u", number);
        notice.red = number % 2 == 0;
        queue.Add(notice, now);
    };
    const unsigned captures[] = {1399, 1400, 1401, 1416, 1440, 1480, 1520, 1560, 1600, 1760, 2120};
    unsigned capture = 0;
    renderer::Canvas canvas;
    for (unsigned now = 0; now <= captures[std::size(captures) - 1]; ++now) {
        if (now <= 1200 && now % 200 == 0) add(now / 200 + 1, now);
        if (now == 1400)
            for (unsigned n = 8; n <= 12; ++n) add(n, now);
        canvas.Begin(lyt::Screen::Top);
        gohan::features::menu_screen::CanvasSurface surface(canvas, lyt::Screen::Top);
        queue.Draw(surface, now);
        if (surface.dropped() || surface.truncated()) return false;
        lyt::Present(canvas.Data());
        if (now == captures[capture]) {
            if (!Capture(directory, static_cast<int>(capture), static_cast<int>(now))) return false;
            if (++capture == std::size(captures)) break;
        }
    }
    return true;
}
// CA2 の行の見本（固定の印・お気に入り・複数選択の枠）。製品の Engine・Shell・Panel・Entry で描き、
// 項目と状態だけを PC 用に作る。ゲームの値は固定の値を返す。通知は画面を作る前に起きるので出ない。
namespace rows {
namespace menu = gohan::menu;
using menu::Behavior;
using menu::Control;
using menu::Engine;
using menu::Format;
using menu::ItemDef;
using menu::Shell;
using menu::Source;
namespace key = menu::key;
using menu::u16;
using menu::u32;

enum : u16 {
    kPlayer, kWall, kStop, kSpeed, kRun, kMoney, kSavings, kWeather, kSpin, kPocket, kMap, kFurniture, kClothes,
    kVillage, kHouse, kDisplay, kButtons, kCount
};
const char *const kWeatherOptions[] = {"晴れ", "雨", "雪", "曇り"};
const char *const kButtonOptions[] = {"A", "B", "X", "Y", "START"};

bool ReadMoney(menu::s32 *value) { *value = 48210; return true; }
bool ReadWeather(menu::s32 *value) { *value = 1; return true; }
menu::Result WriteNothing(menu::s32) { return menu::Result::Ok; }

ItemDef MakeRowItem(const char *label, const char *desc, Control control, Behavior behavior, Source source,
            u16 parent, u16 first, u16 next) {
    ItemDef d{};
    d.key = label; d.label = label; d.desc = desc;
    d.control = control; d.behavior = behavior; d.source = source;
    d.parent = parent; d.first_child = first; d.next = next;
    return d;
}

void DefineRowItems(ItemDef (&d)[kCount]) {
    const u16 none = menu::kNoItem;
    d[kPlayer] = MakeRowItem("プレイヤー", "プレイヤーの項目です。", Control::Folder, Behavior::None, Source::None, none, kWall, kVillage);
    d[kWall] = MakeRowItem("壁を通り抜ける", "壁や木を通り抜けて歩けます。", Control::Toggle, Behavior::State, Source::None, kPlayer, none, kStop);
    d[kStop] = MakeRowItem("時間を止める", "時計を止めます。ON の間は時間が進みません。", Control::Toggle, Behavior::State, Source::None, kPlayer, none, kSpeed);
    d[kSpeed] = MakeRowItem("歩く速さ", "歩く速さです。", Control::Number, Behavior::Value, Source::Setting, kPlayer, none, kRun);
    d[kRun] = MakeRowItem("走り続ける", "ホットキーを押している間だけ走ります。", Control::Toggle, Behavior::State, Source::None, kPlayer, none, kMoney);
    d[kMoney] = MakeRowItem("所持金", "持っているお金です。開いた時にゲームから読みます。", Control::Number, Behavior::Value, Source::Game, kPlayer, none, kSavings);
    d[kSavings] = MakeRowItem("貯金", "貯めているお金です。", Control::Number, Behavior::Value, Source::Setting, kPlayer, none, kWeather);
    d[kWeather] = MakeRowItem("天気", "今の天気です。開いた時にゲームから読みます。", Control::Select, Behavior::Value, Source::Game, kPlayer, none, kSpin);
    d[kSpin] = MakeRowItem("その場で回る", "その場で 1 回転します。", Control::Action, Behavior::Trigger, Source::None, kPlayer, none, kPocket);
    d[kPocket] = MakeRowItem("持ち物を並べる", "持ち物を並べ直します。", Control::Action, Behavior::Trigger, Source::None, kPlayer, none, kMap);
    d[kMap] = MakeRowItem("地図の画面", "地図を開きます。もう一度で閉じます。", Control::Action, Behavior::Mode, Source::None, kPlayer, none, kFurniture);
    d[kFurniture] = MakeRowItem("家具の並べ方", "家具を並べ直します。", Control::Action, Behavior::Trigger, Source::None, kPlayer, none, kClothes);
    d[kClothes] = MakeRowItem("服の選び方", "服を選び直します。", Control::Folder, Behavior::None, Source::None, kPlayer, none, none);
    d[kVillage] = MakeRowItem("村", "村の項目です。", Control::Folder, Behavior::None, Source::None, none, none, kHouse);
    d[kHouse] = MakeRowItem("見本", "見本の項目です。", Control::Folder, Behavior::None, Source::None, none, none, kDisplay);
    d[kDisplay] = MakeRowItem("表示", "表示の項目です。", Control::Folder, Behavior::None, Source::None, none, kButtons, none);
    d[kButtons] = MakeRowItem("押し切るボタン", "押し切るボタンを選びます。", Control::Multiselect, Behavior::Value, Source::Setting, kDisplay, none, none);
    d[kWall].persist = d[kStop].persist = d[kRun].persist = true;
    d[kSpeed].persist = d[kSavings].persist = true;
    d[kSpeed].format = Format::Decimal1; d[kSpeed].def = 10; d[kSpeed].min = 0; d[kSpeed].max = 990; d[kSpeed].step = 1;
    d[kMoney].lockable = d[kWeather].lockable = true;
    d[kMoney].min = 0; d[kMoney].max = 99999; d[kMoney].step = 1;
    d[kSavings].def = 1250000; d[kSavings].min = 0; d[kSavings].max = 9999999; d[kSavings].step = 1000;
    d[kWeather].option_count = 4; d[kWeather].options = kWeatherOptions; d[kWeather].min = 0; d[kWeather].max = 3;
    d[kButtons].option_count = 5; d[kButtons].options = kButtonOptions;
    d[kButtons].min = 0; d[kButtons].max = 31; d[kButtons].def = 17; d[kButtons].persist = true;
}

// 所持金と天気だけは、ゲームの値を読めるようにする（固定と鍵の表示に要る）。
bool BindRowGameValues(Engine &engine) {
    menu::Bound money{};
    money.item = kMoney; money.behavior = Behavior::Value; money.source = Source::Game;
    money.game.read = ReadMoney; money.game.write = WriteNothing;
    menu::Bound weather{};
    weather.item = kWeather; weather.behavior = Behavior::Value; weather.source = Source::Game;
    weather.game.read = ReadWeather; weather.game.write = WriteNothing;
    return engine.Bind(money) && engine.Bind(weather);
}

bool Render(const std::filesystem::path &directory, int index, const std::string &name) {
    ItemDef defs[kCount];
    DefineRowItems(defs);
    Engine engine(defs, kCount);
    if (!BindRowGameValues(engine)) return false;
    // 行の見本の状態。鍵は読めた値を固定し、ホットキーは壁と走り続けるに付ける。
    engine.SetValue(kWall, 1);
    engine.SetHotkey(kWall, key::L | key::Up);
    engine.Commit(kWall, 0);
    engine.SetHotkey(kRun, key::R | key::A);
    engine.Commit(kRun, 0);
    u16 target = kStop;
    std::vector<u16> locks;
    if (name == "lock-unselected") locks = {kWall, kMoney, kWeather};
    else if (name == "lock-selected-value") { locks = {kWall, kMoney}; target = kMoney; }
    else if (name == "lock-selected-toggle") { locks = {kWall, kMoney}; target = kWall; }
    else if (name == "lock-toggle-off") locks = {kWall, kStop};
    else if (name == "fav-top") target = kPlayer;
    else if (name == "fav-top-selected") target = menu::kFavoritesFolder;
    else if (name == "entry-check") target = kButtons;
    else return false;
    // ゲームの値は開いた時に読むので、鍵とお気に入りは開いた後に付ける（読めない値は固定できない）。
    engine.Open(0);
    for (u16 item : locks)
        if (!engine.SetFixed(item, true, 0)) {
            std::cerr << "固定できない項目: " << item << '\n';
            return false;
        }
    if (name.rfind("fav-", 0) == 0)
        for (u16 item : {kWall, kMoney, kWeather, kMap}) engine.SetFavorite(item, true);
    if (target == menu::kFavoritesFolder || target == kPlayer) engine.Select(target);
    else {
        // 項目の入った一覧へ入ってから選ぶ（表示の中の押し切るボタンは、A で入力の枠を開く）。
        const u16 folder = target == kButtons ? kDisplay : kPlayer;
        engine.Select(folder); engine.Press(key::A, 0);
        engine.Select(target);
        if (target == kButtons) engine.Press(key::A, 0);
    }
    Shell shell(engine, {});
    u32 now = 0;
    shell.Step(now, {}, false);
    now = 400; shell.Step(now, {}, false);
    if (target == kButtons) {
        // 2 つ下へ動かし、X の行の箱を選んだ状態にする。
        for (int i = 0; i < 2; ++i) {
            menu::Input down{}; down.held = key::Down;
            shell.Step(now += 40, down, false);
            shell.Step(now += 40, {}, false);
        }
    }
    now = 1500; shell.Step(now, {}, false);
    renderer::Canvas canvas;
    canvas.Begin(lyt::Screen::Top);
    gohan::features::menu_screen::CanvasSurface surface(canvas, lyt::Screen::Top);
    shell.Draw(surface, now);
    if (surface.dropped() || surface.truncated()) return false;
    lyt::Present(canvas.Data());
    return Capture(directory, index, static_cast<int>(now));
}
} // namespace rows

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    const std::filesystem::path directory(argv[1]);
    if (argc >= 3 && std::string(argv[2]) == "--rows") {
        // 残りの引数は場面の名前（台本の順）。場面ごとに frame-<番号>.json と resources-<番号> を書く。
        if (!Resources(directory)) return 10;
        for (int i = 0; i + 3 < argc; ++i)
            if (!rows::Render(directory, i, argv[i + 3])) return 11;
        return 0;
    }
    if (argc != 2 && argc != 3) return 2;
    if (argc == 3 && std::string(argv[2]) == "--parts")
        return Resources(directory) && PartsCatalog(directory) ? 0 : 7;
    if (argc == 3 && std::string(argv[2]) == "--scroll")
        return Resources(directory) && ScrollCatalog(directory) ? 0 : 8;
    if (argc == 3 && std::string(argv[2]) == "--notice-overflow")
        return Resources(directory) && NoticeOverflow(directory) ? 0 : 9;
    if (!Resources(directory) || !renderer::RendererFeature().Wire() ||
        !gohan::features::menu_screen::Feature().Wire()) return 3;
    using CTRPluginFramework::Fake;
    int time, touching, x, y, toggle, capture;
    unsigned keys;
    while (std::cin >> time >> keys >> touching >> x >> y >> toggle >> capture) {
        Fake::nowMs = time;
        Fake::keys = keys;
        Fake::touching = touching != 0;
        Fake::touchX = static_cast<unsigned>(x);
        Fake::touchY = static_cast<unsigned>(y);
        if (toggle && !gohan::features::menu_screen::RequestToggle()) return 4;
        gohan::core::frame::Dispatch();
        if (capture >= 0 && !Capture(directory, capture, time)) return 5;
    }
    if (!std::cin.eof() || renderer::ReadStatus() != renderer::Status::Ready) return 6;
    gohan::features::menu_screen::Feature().Shutdown();
    renderer::RendererFeature().Shutdown();
    return 0;
}
