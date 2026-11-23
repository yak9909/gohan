#include "game/material_tint.hpp"
#include <algorithm>

namespace gohan::game::material {
namespace {
std::uint32_t *Stage(Tev &b, unsigned n) { return b.data() + 11 + 7 * n; }
bool Pass(const std::uint32_t *s, std::span<const std::uint32_t, 55> palette) {
    if (!(s[4] & 65535) && (s[1] & 15) == 15 && !(s[3] & 15)) return true;
    if ((s[1] & 65535) != 0x0EFD || (s[3] & 4095) != 0x300 || (s[4] & 65535) != 4) return false;
    const auto sel = s[0] & 255;
    const auto i = sel >= 1 && sel <= 5 ? 49 + sel : sel >= 6 && sel <= 10 ? 38 + sel : 49;
    return palette[i] >> 24 == 255;
}
}
Plan Prepare(Tev &b, std::span<const std::uint32_t, 55> palette) {
    std::array<std::array<std::uint32_t, 7>, 6> stages{};
    for (unsigned i = 0; i < 6; ++i) std::copy_n(Stage(b, i), 7, stages[i].begin());
    const auto bits = ((b[57] >> 8) & 15) | ((b[57] >> 12) & 15);
    std::array<std::uint32_t, 7> alpha = stages[5];
    Plan plan = Plan::Free;
    if (!Pass(stages[5].data(), palette)) {
        int free = -1;
        for (int i = 4; i >= 0 && free < 0; --i) {
            bool clean = true;
            for (int j = i; j < 4; ++j) clean = clean && !(bits & (1u << j));
            if (clean && Pass(stages[i].data(), palette)) free = i;
        }
        if (free < 0) {
            auto *s = Stage(b, 5);
            std::uint32_t base = 15;
            for (unsigned n = 0; n < 3; ++n) if (((s[1] >> (4 * n)) & 15) == 13) base = 13;
            s[0] = 5; s[1] = (s[1] & 0xFFFF0000u) | 0xEE0 | base;
            s[3] = (s[3] & ~4095u) | 0x30;
            s[4] = (s[4] & 0xFFFF0000u) | 8; s[6] = 0;
            return Plan::Replace;
        }
        for (unsigned i = static_cast<unsigned>(free); i < 5; ++i) {
            auto *d = Stage(b, i); const auto header = d[2];
            std::copy(stages[i + 1].begin(), stages[i + 1].end(), d); d[2] = header;
        }
        alpha = {0, 0x0FFF0000u, 0, 0, 0, 0, 0};
        plan = Plan::Compact;
    }
    auto *s = Stage(b, 5);
    s[0] = 5; s[1] = (alpha[1] & 0xFFFF0000u) | 0xEFE;
    s[3] = (alpha[3] & ~4095u) | 0x200;
    s[4] = (alpha[4] & 0xFFFF0000u) | 4; s[6] = 0;
    return plan;
}
bool CopyTev(const View &source, Tev &copy) {
    if (!source.tev || !source.lut_valid) return false;
    copy = *source.tev;
    if (copy[10]) {
        const auto from = reinterpret_cast<std::uintptr_t>(source.tev) + 40;
        const auto to = reinterpret_cast<std::uintptr_t>(&copy) + 40;
        copy[10] = static_cast<std::uint32_t>(from + copy[10] - to);
    }
    return true;
}
void ScaleAlpha(Tev &b, bool tint) {
    auto *s = Stage(b, 5);
    s[0] = 5; s[1] = tint ? 0x00EF0EEFu : 0x00EF000Fu;
    s[3] = tint ? 0x10 : 0;
    s[4] = tint ? 0x00010008u : 0x00010000u; s[6] = 0;
}
std::uint32_t Constant(Colour c, bool premultiplied, bool scale_alpha) {
    std::uint32_t rgb = c.rgb & 0xFFFFFF;
    if (premultiplied) {
        rgb = 0;
        for (unsigned n = 0; n < 3; ++n) rgb |= (((c.rgb >> (8 * n)) & 255) * c.strength / 255) << (8 * n);
    }
    return rgb | (static_cast<std::uint32_t>(scale_alpha ? c.alpha : c.strength) << 24);
}
bool Patch::Push(View v, Colour c) {
    if (Active() || !v.tev || !v.palette || !v.tev_key || !v.lut_valid) return false;
    auto block = *v.tev;
    const auto plan = Prepare(block, std::span<const std::uint32_t, 55>(v.palette, 55));
    const bool blended = v.blend && (((*v.blend >> 16) & 15) != 1 || ((*v.blend >> 20) & 15) != 0);
    // 全段使用の半透明材質は元の最終段のアルファを保って掛けられないので変更しない。
    if (blended && plan == Plan::Replace) return false;
    if (blended) ScaleAlpha(block, true);
    auto log = std::make_unique<WriteLog<12>>();
    // TEV は64Bずつ控えられる単位に分ける。LUT の相対ポインタは動かさない。
    for (unsigned n = 0; n < 6; ++n) {
        std::array<std::uint32_t, 7> s{};
        std::copy_n(Stage(block, n), 7, s.begin());
        log->Write(reinterpret_cast<std::array<std::uint32_t, 7> *>(Stage(*v.tev, n)), s);
    }
    log->Write<std::uint32_t>(v.tev_key, 0u);
    log->Write<std::uint32_t>(v.palette + 54, Constant(c, blended || plan == Plan::Replace, blended));
    if (!blended && v.blend && v.fb_read && v.blend_colour && v.frag_key) {
        log->Write<std::uint32_t>(v.fb_read, 0u);
        log->Write<std::uint32_t>(v.blend, (12u << 16) | (13u << 20) | (1u << 24) | (13u << 28));
        log->Write<std::uint32_t>(v.blend_colour, static_cast<std::uint32_t>(c.alpha) << 24);
        log->Write<std::uint32_t>(v.frag_key, 0u);
    }
    log_ = std::move(log);
    return true;
}
void Patch::Restore() { if (log_) { log_->Restore(); log_.reset(); } }
} // namespace gohan::game::material
