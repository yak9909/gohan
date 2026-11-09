#pragma once
#include "game/patch_set.hpp"

namespace gohan::game::shizue_skip {
struct Exit { void *manager = nullptr; int slot = -1; };
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
bool Step();
} // namespace gohan::game::shizue_skip
