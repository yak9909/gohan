#include "features/world_dev/world_dev.hpp"
#ifdef GOHAN_DEV
#include "world/unit_cursor.hpp"
#include <CTRPluginFramework.hpp>
#include <array>
#include <cmath>
#include <string>
#include <vector>
namespace gohan::features::world_dev {
namespace {
int owner;
bool shown = false;
unsigned colour = 0;
std::vector<world::UnitCursor::Position> positions;
bool Declare() {
    // 色の形式は下位バイトから赤・緑・青。
    constexpr std::uint32_t colours[] = {16711680, 255, 16777215};
    return world::UnitCursor::Desired(&owner, true, positions, {colours[colour], 128, 255});
}
}
void Toggle(CTRPluginFramework::MenuEntry *) {
    using Cursor = world::UnitCursor;
    if (shown) { Cursor::Release(&owner); decltype(positions){}.swap(positions); shown = false; return; }
    if (!Cursor::Acquire(&owner)) { CTRPluginFramework::OSD::Notify("カーソルは使用中です"); return; }
    Cursor::Position p;
    if (!Cursor::PlayerPosition(p)) {
        Cursor::Release(&owner); CTRPluginFramework::OSD::Notify("プレイヤーの位置を取得できません"); return;
    }
    const float x = std::floor(p[0] / 32) * 32 + 16, z = std::floor(p[2] / 32) * 32 + 16;
    positions.resize(9);
    unsigned n = 0;
    for (int row = -1; row <= 1; ++row) for (int col = -1; col <= 1; ++col)
        positions[n++] = {x + col * 32, p[1], z + row * 32};
    shown = Declare();
    if (!shown) Cursor::Release(&owner);
}
void Colour(CTRPluginFramework::MenuEntry *) { colour = (colour + 1) % 3; if (shown) Declare(); }
void Status(CTRPluginFramework::MenuEntry *) {
    const auto s = world::UnitCursor::Status();
    CTRPluginFramework::OSD::Notify("3D 段=" + std::to_string(s.stage) + " 失敗=" + std::to_string(s.failure) +
        " 読込=" + std::to_string(s.load_polls) + " 位置=" + std::to_string(s.positions) +
        " 描画=" + std::to_string(s.drawn) + " 親=" + std::to_string(s.parent_bytes) +
        " 保持=" + std::to_string(s.retained_on_shutdown));
}
}
#endif
