// 実際の毎フレーム処理を入力・時計・描画先の代役で動かし、確定した描画指示を保存する。
#include "core/frame.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "game/lyt/renderer.hpp"
#include "ui/renderer/renderer.hpp"
#include "features/menu_screen/canvas_surface.hpp"
#include "menu/shell.hpp"
#include <CTRPluginFramework.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>

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
bool Resources(const std::filesystem::path &directory) {
    renderer::PrepareTitleBackground();
    std::ofstream status(directory / "background-status.txt");
    status << renderer::TitleBackgroundStatusText() << '\n';
    const auto &f = renderer::Fonts();
    std::ofstream atlas(directory / "atlas.la4", std::ios::binary);
    atlas.write(reinterpret_cast<const char *>(f.atlas), static_cast<std::streamsize>(f.atlasBytes));
    std::ofstream out(directory / "fonts.json");
    out << "{\"sheet\":[" << f.sheetWidth << ',' << f.sheetHeight << "],\"cell\":["
        << f.cellWidth << ',' << f.cellHeight << "],\"columns\":" << f.columns << ",\"widths\":";
    Array(out, f.widths, f.glyphs * 3);
    out << ",\"main\":";
    Array(out, f.mainMap, f.mainCount * 2);
    out << ",\"numeric\":";
    Array(out, f.numericMap, f.numericCount * 2);
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
} // namespace

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    const std::filesystem::path directory(argv[1]);
    if (argc == 3 && std::string(argv[2]) == "--parts")
        return Resources(directory) && PartsCatalog(directory) ? 0 : 7;
    if (argc == 3 && std::string(argv[2]) == "--scroll")
        return Resources(directory) && ScrollCatalog(directory) ? 0 : 8;
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
