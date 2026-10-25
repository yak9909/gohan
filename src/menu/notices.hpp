#pragma once
#include "core/limits.hpp"
#include "menu/notice.hpp"
#include "menu/surface.hpp"
#include "ui_values.hpp"

namespace gohan::menu {
// 通知の列。右から入り、一定時間おいて右へ出る。積む位置は現在値から減速して動く。
// 題は1行、本文は画面に収まる1〜3行へ省略する。
class NoticeQueue {
  public:
    static constexpr int kCapacity = core::design::ca2::Notice::Capacity;

    void Add(const Notice &notice, u32 now);
    bool Alive(u32 now) const;
    // 本文を含む描画の予算が足りない通知は描かない。
    void Draw(Surface &surface, u32 now, bool menu_open = true);

  private:
    struct Slot {
        bool active = false;
        bool red = false;
        char title[core::limits::MenuText]{};
        char message[core::limits::MenuText]{};
        // 文面と書体は表示中不変。本文は行数上限が変わった時だけ組み直す。
        char lines[core::design::ca2::Notice::Inner::Body[3]][kLineBytes]{};
        char display_title[core::limits::MenuText]{};
        int lines_limit = 0;
        int line_count = 0;
        u32 created = 0;
        u32 enter_at = 0;
        motion::Tween y;
        bool placed = false;
        bool forced_exit = false;
        u32 exit_at = 0;
        float exit_x = 0.f;
        float exit_y = 0.f;
    };
    float X(const Slot &slot, int screen_width, u32 now) const;
    float Alpha(const Slot &slot, u32 now) const;
    bool SlotAlive(const Slot &slot, u32 now) const;
    void Insert(const Notice &notice, u32 now);
    void Flush(u32 now);
    void Layout(Slot &slot, Surface &surface, int lines_limit);

    Slot slots_[kCapacity]{};
    Slot leaving_{};
    static constexpr int kPendingCapacity = core::design::ca2::Notice::PendingCapacity;
    Notice pending_[kPendingCapacity]{};
    int pending_count_ = 0;
    u32 last_at_ = 0;
    bool ever_ = false;
};
} // namespace gohan::menu
