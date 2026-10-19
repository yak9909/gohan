#include "game/lyt/verification.hpp"
#include <CTRPluginFramework.hpp>

namespace gohan::game::lyt::detail {
InstallResult VerifyRenderer() {
    if (CTRPluginFramework::Process::GetTitleID() != 0x0004000000086200ULL)
        return InstallResult::WrongTitle;
    for (const auto &check : RendererChecks) {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::uint32_t i = 0; i < check.bytes; ++i) {
            std::uint8_t byte;
            if (!CTRPluginFramework::Process::Read8(check.address + i, byte))
                return InstallResult::Unreadable;
            crc ^= byte;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
        }
        if ((crc ^ 0xFFFFFFFFu) != check.crc32)
            return InstallResult::Mismatch;
    }
    return InstallResult::Ready;
}
} // namespace gohan::game::lyt::detail
