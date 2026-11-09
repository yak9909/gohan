#pragma once
#include "game/patch_set.hpp"
#include <span>

namespace gohan::game {
struct CodeCheck { std::uintptr_t address; std::uint32_t length, crc; };
enum class HookKind { Hook, Mitm };
struct HookSpec { CodeCheck check; std::uintptr_t callback; HookKind kind = HookKind::Hook; std::uint32_t flags = 3; };
// 実機は Pause の間に照合・導入・巻き戻しを行う。PC では同じ境界を代役で検査する。
struct HookBackend {
    bool (*supported)();
    bool (*pause)(); // 自分が停止した時だけ true
    void (*resume)(bool);
    bool (*read)(std::uintptr_t, std::uint8_t &);
    bool (*install)(void *owner, unsigned index, const HookSpec &);
    void (*remove)(void *owner, unsigned index);
};
const HookBackend &NativeHookBackend();
class PausedGame {
  public:
    PausedGame() : resume_(NativeHookBackend().pause()) {}
    ~PausedGame() { NativeHookBackend().resume(resume_); }
  private: bool resume_;
};
bool VerifyCode(const HookBackend &, std::span<const CodeCheck>);
bool PcRelativeInstruction(std::uint32_t word);
class HookBatch {
  public:
    HookBatch(const HookBackend &backend, std::span<const HookSpec> specs) : backend_(backend), specs_(specs) {}
    HookBatch(const HookBatch &) = delete;
    HookBatch &operator=(const HookBatch &) = delete;
    ~HookBatch() { Shutdown(); }
    Result Wire();
    void Shutdown();
  private:
    const HookBackend &backend_;
    std::span<const HookSpec> specs_;
    unsigned installed_ = 0;
};
// 関数の入口だけを照合する機能にも共通の停止・タイトル判定を使う。
bool VerifyNative(std::span<const CodeCheck>);
} // namespace gohan::game
