#pragma once
#include "core/limits.hpp"
#include "menu/notice.hpp"
#include "menu/surface.hpp"

namespace gohan::menu {
// 通知の列。右から入り、一定時間おいて右へ出る。新しいものが入ると古いものは上へ押し上げられる。
// 題と本文は 1 行ずつ（長い時は "..." で切る）。高さは一定。
class NoticeQueue {
  public:
    static constexpr int kCapacity = 4;

    void Add(const Notice &notice, u32 now);
    bool Alive(u32 now) const;
    // 描画の予算が足りない通知は描かない（文字の枠が 2 つ要る）。
    void Draw(Surface &surface, u32 now) const;

  private:
    struct Slot {
        bool active = false;
        bool red = false;
        char title[core::limits::MenuText]{};
        char message[core::limits::MenuText]{};
        u32 created = 0;
        float from_y = 0.0f, target_y = 0.0f;
        u32 moved_at = 0;
    };
    float X(const Slot &slot, int screen_width, u32 now) const;
    float Y(const Slot &slot, u32 now) const;
    bool SlotAlive(const Slot &slot, u32 now) const;

    Slot slots_[kCapacity]{};
    u32 last_at_ = 0;
    bool ever_ = false;
};
} // namespace gohan::menu
