#include "game/lyt/renderer.hpp"
#include "game/generated/api.hpp"
#include <CTRPluginFramework.hpp>

namespace gohan::game::lyt {
namespace {
InstallResult Check(std::uintptr_t address, std::uint32_t length, std::uint32_t expected) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < length; ++i) {
        std::uint8_t byte;
        if (!CTRPluginFramework::Process::Read8(address + i, byte))
            return InstallResult::Unreadable;
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
    }
    return (crc ^ 0xFFFFFFFFu) == expected ? InstallResult::Ready : InstallResult::Mismatch;
}
} // namespace

InstallResult InstallRenderer(const FontResource &) {
    if (CTRPluginFramework::Process::GetTitleID() != 0x0004000000086200ULL)
        return InstallResult::WrongTitle;
    auto result = Check(generated::LayoutTopEntry_addr, generated::LayoutTopEntry_len,
                        generated::LayoutTopEntry_crc32);
    if (result != InstallResult::Ready)
        return result;
    result = Check(generated::LayoutBottomEntry_addr, generated::LayoutBottomEntry_len,
                   generated::LayoutBottomEntry_crc32);
    if (result != InstallResult::Ready)
        return result;
    // Layout ノード、Material、TextBox、ResFont と管理者・アロケータの
    // 公開型が不足している。生のオフセットや未公開の番地で代用しない。
    return InstallResult::MissingBindings;
}

bool Present(const Drawing &) { return false; }
void DetachRenderer() {
    // 導入に成功する経路はまだ無く、ゲーム側に所有する資源も無い。
}
} // namespace gohan::game::lyt
