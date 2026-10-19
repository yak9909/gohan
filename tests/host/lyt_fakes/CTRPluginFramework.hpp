#pragma once
#include "game/lyt/verification.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iterator>

namespace CTRPluginFramework {
struct FakeLayout {
    static inline std::uint64_t title = 0x0004000000086200ULL;
    static inline bool readable = true;
    static inline int reads = 0;
    static inline int sleeps = 0;
    static inline unsigned slept = 0;
    static inline std::size_t unreadableCheck = std::size(gohan::game::lyt::detail::RendererChecks);
    static inline std::array<std::array<std::uint8_t, 64>,
                             std::size(gohan::game::lyt::detail::RendererChecks)> bytes{};

    static std::uint32_t Crc(const std::uint8_t *data, std::size_t length) {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < length; ++i) {
            crc ^= data[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    static void Reset() {
        title = 0x0004000000086200ULL;
        readable = true;
        reads = 0;
        unreadableCheck = bytes.size();
        bytes = {};
        // ゲームの命令を使わず、ゼロ列の末尾 32 bit を線形代数で選んで照合する。
        // 検査に使うヘッダーは実際の生成物で、代役の番地・型で置き換えない。
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            auto &data = bytes[index];
            const auto &check = gohan::game::lyt::detail::RendererChecks[index];
            assert(check.bytes >= 4 && check.bytes <= data.size());
            const auto base = Crc(data.data(), check.bytes);
            std::uint32_t basis[32]{}, masks[32]{};
            for (unsigned column = 0; column < 32; ++column) {
                const auto byte = check.bytes - 4 + column / 8;
                data[byte] ^= 1u << (column % 8);
                auto value = Crc(data.data(), check.bytes) ^ base;
                data[byte] ^= 1u << (column % 8);
                auto mask = std::uint32_t{1} << column;
                for (int bit = 31; bit >= 0; --bit) {
                    if (!(value & (std::uint32_t{1} << bit)))
                        continue;
                    if (!basis[bit]) {
                        basis[bit] = value;
                        masks[bit] = mask;
                        break;
                    }
                    value ^= basis[bit];
                    mask ^= masks[bit];
                }
            }
            auto difference = check.crc32 ^ base;
            std::uint32_t solution = 0;
            for (int bit = 31; bit >= 0; --bit) {
                if (difference & (std::uint32_t{1} << bit)) {
                    assert(basis[bit]);
                    difference ^= basis[bit];
                    solution ^= masks[bit];
                }
            }
            assert(!difference);
            for (unsigned bit = 0; bit < 32; ++bit)
                if (solution & (std::uint32_t{1} << bit))
                    data[check.bytes - 4 + bit / 8] ^= 1u << (bit % 8);
            assert(Crc(data.data(), check.bytes) == check.crc32);
        }
    }
};
// 待機の代役（呼ばれた回数と合計だけを数える）。
struct Milliseconds {
    unsigned value;
    explicit Milliseconds(unsigned ms) : value(ms) {}
};
inline void Sleep(Milliseconds time) {
    ++FakeLayout::sleeps;
    FakeLayout::slept += time.value;
}
// 読み取りだけを公開する。書き換えとフックの設置は利用できない。
struct Process {
    static std::uint64_t GetTitleID() { return FakeLayout::title; }
    static bool Read8(std::uintptr_t address, std::uint8_t &value) {
        ++FakeLayout::reads;
        if (!FakeLayout::readable)
            return false;
        for (std::size_t i = 0; i < FakeLayout::bytes.size(); ++i) {
            const auto &check = gohan::game::lyt::detail::RendererChecks[i];
            if (address >= check.address && address - check.address < check.bytes) {
                if (i == FakeLayout::unreadableCheck)
                    return false;
                value = FakeLayout::bytes[i][address - check.address];
                return true;
            }
        }
        assert(false);
        return false;
    }
};
} // namespace CTRPluginFramework
