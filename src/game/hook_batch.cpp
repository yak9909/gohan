#include "game/hook_batch.hpp"
#include "game/code_ownership.hpp"

namespace gohan::game {
namespace {
class Pause {
  public:
    explicit Pause(const HookBackend &backend) : backend_(backend), resume_(backend.pause()) {}
    ~Pause() { backend_.resume(resume_); }
  private:
    const HookBackend &backend_;
    bool resume_;
};
}
bool VerifyCode(const HookBackend &backend, std::span<const CodeCheck> checks) {
    for (const auto &check : checks) {
        if (!check.length || check.address + check.length < check.address) return false;
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::uint32_t i = 0; i < check.length; ++i) {
            std::uint8_t byte;
            if (!backend.read(check.address + i, byte)) return false;
            crc ^= byte;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
        }
        if ((crc ^ 0xFFFFFFFFu) != check.crc) return false;
    }
    return true;
}
bool PcRelativeInstruction(std::uint32_t w) {
    // 分岐、PC を基準/値に使う load/store・算術、PC を含む複数転送を拒否する。
    // 未対応の命令群も許可せず、入口の PUSH 等だけを安全に移す。
    const unsigned group = (w >> 25) & 7;
    const unsigned rn = (w >> 16) & 15, rd = (w >> 12) & 15;
    if (group == 5 || group == 7) return true;
    if (group == 4) return rn == 15 || (w & (1u << 15));
    if (group == 6) return rn == 15;
    if (group == 2 || group == 3) return rn == 15 || rd == 15 || (group == 3 && (w & 15) == 15);
    return rn == 15 || rd == 15 || (!(w & (1u << 25)) && ((w & 15) == 15 || ((w & 16) && ((w >> 8) & 15) == 15)));
}
Result HookBatch::Wire() {
    if (installed_) return Result::Ok;
    if (!backend_.supported() || specs_.empty()) return Result::Unsupported;
    Pause pause(backend_);
    for (const auto &spec : specs_) {
        if (!spec.callback || spec.check.length < 4 || spec.check.length > 64 ||
            (spec.check.address & 3) || !VerifyCode(backend_, {&spec.check, 1})) return Result::Unsupported;
        std::uint32_t first = 0;
        for (unsigned i = 0; i < 4; ++i) {
            std::uint8_t byte;
            if (!backend_.read(spec.check.address + i, byte)) return Result::Unsupported;
            first |= std::uint32_t(byte) << (8 * i);
        }
        if (PcRelativeInstruction(first)) return Result::Unsupported;
    }
    for (const auto &spec : specs_) {
        if (!code_ownership::Take(this, spec.check.address)) {
            code_ownership::Release(this);
            return Result::Unsupported;
        }
    }
    for (unsigned i = 0; i < specs_.size(); ++i) {
        if (!backend_.install(this, i, specs_[i])) {
            while (installed_) backend_.remove(this, --installed_);
            code_ownership::Release(this);
            return Result::Failed;
        }
        ++installed_;
    }
    return Result::Ok;
}
void HookBatch::Shutdown() {
    if (!installed_) return;
    Pause pause(backend_);
    while (installed_) backend_.remove(this, --installed_);
    code_ownership::Release(this);
}
} // namespace gohan::game
