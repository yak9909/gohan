#pragma once
#include "game/patch_set.hpp"

namespace gohan::game::shizue_skip {
struct Exit { void *manager = nullptr; int slot = -1; };
// ゲームが返す保存領域・固定領域を、配置場所を決めつけずに検証する。
struct ExitAccess {
    void *(*save)();
    void *(*garden)();
    int (*slot)(void *, void *);
    void *(*manager)();
    bool (*readable)(std::uintptr_t, std::size_t);
};
bool ResolveExit(const ExitAccess &, Exit &);
struct Access {
    unsigned (*room)();
    bool (*resolve)(Exit &);
    bool (*prepared)();
    void (*prepare)();
    void (*use_exit)(const Exit &);
};
class Sequence {
  public:
    explicit Sequence(Access access) : access_(access) {}
    bool Step(bool enabled);
    void Reset() { stable_ = 0; fired_ = false; }
  private:
    Access access_;
    unsigned stable_ = 0;
    bool fired_ = false;
};
bool Wire();
void Shutdown();
Result Set(bool on);
bool IsActive();
// 描画側は完了通知だけを受け取る。ゲームの関数は呼ばない。
bool TakeNotice();
} // namespace gohan::game::shizue_skip
