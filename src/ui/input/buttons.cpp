#include "ui/input/buttons.hpp"
#include <CTRPluginFramework.hpp>
#include <iterator>

namespace gohan::ui::input {
namespace {
using CTRPluginFramework::Key;
struct Mapping {
    std::uint32_t source;
    std::uint16_t bit;
};
constexpr Mapping kMap[] = {
    {Key::ZL, button::ZL},         {Key::L, button::L},         {Key::R, button::R},
    {Key::ZR, button::ZR},         {Key::DPadUp, button::Up},   {Key::DPadDown, button::Down},
    {Key::DPadLeft, button::Left}, {Key::DPadRight, button::Right}, {Key::A, button::A},
    {Key::B, button::B},           {Key::X, button::X},         {Key::Y, button::Y},
    {Key::Select, button::Select}, {Key::Start, button::Start},
};
// 読み込み時に始める時計。関数内の static にすると初期化の保護が要るため、ここに置く。
const CTRPluginFramework::Clock epoch;
} // namespace

Snapshot Read() {
    Snapshot snapshot;
    // 押した瞬間のぶんも含めた水準。立ち上がりは受け取る側が前フレームとの差で作る。
    const std::uint32_t keys = CTRPluginFramework::Controller::GetKeysDown(true);
    for (const Mapping &mapping : kMap)
        if (keys & mapping.source) snapshot.buttons = static_cast<std::uint16_t>(snapshot.buttons | mapping.bit);
    snapshot.touching = CTRPluginFramework::Touch::IsDown();
    if (snapshot.touching) {
        const auto position = CTRPluginFramework::Touch::GetPosition();
        snapshot.x = static_cast<int>(position.x);
        snapshot.y = static_cast<int>(position.y);
    }
    return snapshot;
}

std::uint32_t NowMs() {
    return static_cast<std::uint32_t>(epoch.GetElapsedTime().AsMilliseconds());
}
} // namespace gohan::ui::input
