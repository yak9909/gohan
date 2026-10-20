// 実際の毎フレーム処理を入力・時計・描画先の代役で動かし、確定した描画指示を保存する。
#include "core/frame.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "game/lyt/renderer.hpp"
#include "ui/renderer/renderer.hpp"
#include <CTRPluginFramework.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace gohan::game::lyt {
namespace {
Drawing presented;
}
InstallResult InstallRenderer(const FontResource &) { return InstallResult::Ready; }
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
    return atlas.good() && out.good();
}
bool Capture(const std::filesystem::path &directory, int index, int now) {
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
            out << '}';
        }
        out << ']';
    }
    out << "]}\n";
    return out.good();
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const std::filesystem::path directory(argv[1]);
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
