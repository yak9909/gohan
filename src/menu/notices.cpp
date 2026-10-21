#include "menu/notices.hpp"
#include "menu/timing.hpp"
#include "menu/paint.hpp"
#include <cstring>
#include <algorithm>

namespace gohan::menu {
namespace {
using namespace motion;

void Copy(char *to, std::size_t capacity, const char *from) {
    const std::size_t n = std::strlen(from);
    const std::size_t size = n < capacity - 1 ? n : capacity - 1;
    std::memcpy(to, from, size);
    to[size] = '\0';
}
} // namespace

float NoticeQueue::X(const Slot &slot, int screen_width, u32 now) const {
    const float target = static_cast<float>(screen_width + ca::Notice::Right - ca::Notice::Width);
    const float off = static_cast<float>(screen_width);
    if (slot.forced_exit)
        return Mix(off + slot.exit_x, off, EaseOut(Progress(slot.exit_at, now, timing::kNoticeExitMs)));
    const s32 age = static_cast<s32>(now - slot.enter_at);
    if (age < 0) return off;
    if (age < static_cast<s32>(timing::kNoticeEnterMs))
        return Mix(off, target, EaseOut(static_cast<float>(age) / static_cast<float>(timing::kNoticeEnterMs)));
    if (age < static_cast<s32>(timing::kNoticeEnterMs + timing::kNoticeHoldMs)) return target;
    return Mix(target, off,
               EaseOut(static_cast<float>(age - static_cast<s32>(timing::kNoticeEnterMs + timing::kNoticeHoldMs)) /
                       static_cast<float>(timing::kNoticeExitMs)));
}

float NoticeQueue::Alpha(const Slot &slot, u32 now) const {
    if (slot.forced_exit) return 1.f - EaseOut(Progress(slot.exit_at, now, timing::kNoticeExitMs));
    const s32 age = static_cast<s32>(now - slot.enter_at);
    const s32 exit = static_cast<s32>(timing::kNoticeEnterMs + timing::kNoticeHoldMs);
    return age < exit ? 1.f : 1.f - EaseOut(Progress(slot.enter_at + exit, now, timing::kNoticeExitMs));
}

bool NoticeQueue::SlotAlive(const Slot &slot, u32 now) const {
    if (!slot.active) return false;
    if (slot.forced_exit) return motion::Progress(slot.exit_at, now, timing::kNoticeExitMs) < 1.f;
    const s32 age = static_cast<s32>(now - slot.enter_at);
    return age < static_cast<s32>(timing::kNoticeEnterMs + timing::kNoticeHoldMs + timing::kNoticeExitMs);
}

bool NoticeQueue::Alive(u32 now) const {
    if (pending_count_ || SlotAlive(leaving_, now)) return true;
    for (const Slot &slot : slots_)
        if (SlotAlive(slot, now)) return true;
    return false;
}

void NoticeQueue::Add(const Notice &notice, u32 now) {
    if (pending_count_ || SlotAlive(leaving_, now)) {
        int active = 0;
        for (const Slot &slot : slots_) active += SlotAlive(slot, now);
        if (pending_count_ || active == kCapacity) {
            if (pending_count_ < kPendingCapacity) pending_[pending_count_++] = notice;
            return;
        }
    }
    Insert(notice, now);
}

void NoticeQueue::Insert(const Notice &notice, u32 now) {
    u32 at = now;
    for (Slot &slot : slots_)
        if (!SlotAlive(slot, now)) slot.active = false;
    // 続けて来た通知は少しずつずらして入れる。
    if (ever_ && static_cast<s32>(last_at_ + timing::kNoticeSpawnMs - at) > 0) at = last_at_ + timing::kNoticeSpawnMs;
    ever_ = true;
    last_at_ = at;
    // 生きている順（古い順）に前へ詰める。満杯なら一番古いものを退場へ移す。
    int n = 0;
    for (int i = 0; i < kCapacity; ++i)
        if (slots_[i].active) slots_[n++] = slots_[i];
    for (int i = n; i < kCapacity; ++i) slots_[i].active = false;
    if (n >= kCapacity) {
        leaving_ = slots_[0];
        leaving_.forced_exit = true;
        leaving_.exit_at = now;
        leaving_.exit_x = X(slots_[0], 400, now) - 400.f;
        leaving_.exit_y = slots_[0].placed ? slots_[0].y.At(now) : float(ca::Notice::Top);
        for (int i = 1; i < kCapacity; ++i) slots_[i - 1] = slots_[i];
        slots_[kCapacity - 1].active = false;
    }
    for (Slot &slot : slots_) {
        if (slot.active) continue;
        slot.active = true;
        slot.red = notice.red;
        Copy(slot.title, sizeof(slot.title), notice.title);
        Copy(slot.message, sizeof(slot.message), notice.message);
        slot.created = at;
        slot.wait_for_stack = n >= kCapacity;
        slot.enter_at = at + (slot.wait_for_stack ? timing::kNoticeStackMs : 0);
        slot.placed = false;
        slot.forced_exit = false;
        break;
    }
}

void NoticeQueue::Flush(u32 now) {
    if (SlotAlive(leaving_, now)) return;
    leaving_.active = false;
    while (pending_count_) {
        const Notice next = pending_[0];
        for (int i = 1; i < pending_count_; ++i) pending_[i - 1] = pending_[i];
        --pending_count_;
        Insert(next, now);
        if (SlotAlive(leaving_, now)) break;
    }
}

void NoticeQueue::Draw(Surface &s, u32 now, bool menu_open) {
    Flush(now);
    int bottom = ca::Notice::Bottom[menu_open ? 0 : 1];
    int count = 0;
    for (const Slot &slot : slots_)
        count += SlotAlive(slot, now) && static_cast<s32>(now - slot.created) >= 0;
    if (!count && !SlotAlive(leaving_, now)) return;
    const int room = count ? (bottom - ca::Notice::Top - (count - 1) * ca::Notice::Gap) / count : ca::Notice::Height[0];
    const int maxLines = std::clamp(1 + (room - ca::Notice::Height[0]) / ca::Notice::Height[1], 1, ca::Notice::Inner::Body[3]);
    auto draw = [&](Slot &slot, int x, int y, int lines_limit) {
        const float alpha = Alpha(slot, now);
        if (alpha <= 0.f) return 0;
        char lines[ca::Notice::Inner::Body[3]][kLineBytes], title[kLineBytes];
        const int n = Wrap(s, slot.message, ca::Notice::Inner::Body[4], lines, lines_limit, nullptr);
        const int h = ca::Notice::Height[0] + ca::Notice::Height[1] * (n > 0 ? n - 1 : 0);
        if (s.TextsLeft() < n + 1 || s.RectsLeft() < 13) return h;
        FadedSurface faded(s, alpha);
        Paper(faded, {x, y, ca::Notice::Width, h}, 1, "notice.window");
        const u32 color = slot.red ? ca::Color::Red : ca::Color::Green;
        faded.Fill({x + ca::Notice::Inner::Line[0], y + ca::Notice::Inner::Line[1], ca::Notice::Inner::Line[2], h + ca::Notice::Inner::Line[3]}, color);
        faded.Sprite(x + (slot.red ? ca::Notice::Inner::Bang[0] : ca::Notice::Inner::Icon[0]),
                     y + (slot.red ? ca::Notice::Inner::Bang[1] : ca::Notice::Inner::Icon[1]), slot.red ? "bang" : "check", color);
        faded.Text(x + ca::Notice::Inner::Title[0], y + ca::Notice::Inner::Title[1], Trim(s, slot.title, ca::Notice::Width - ca::Notice::Inner::Title[0] - ca::Margin, title, sizeof(title)), ca::Color::Text);
        for (int j = 0; j < n; ++j) faded.Text(x + ca::Notice::Inner::Body[0], y + ca::Notice::Inner::Body[1] + j * ca::Notice::Inner::Body[2], lines[j], slot.red ? ca::Color::Red : ca::Color::Text);
        return h;
    };
    if (SlotAlive(leaving_, now))
        draw(leaving_, Round(X(leaving_, s.Width(), now)), Round(leaving_.exit_y), 1);
    for (int i = kCapacity - 1; i >= 0; --i) {
        Slot &slot = slots_[i];
        if (!SlotAlive(slot, now) || static_cast<s32>(now - slot.created) < 0) continue;
        char lines[ca::Notice::Inner::Body[3]][kLineBytes];
        const int n = Wrap(s, slot.message, ca::Notice::Inner::Body[4], lines, maxLines, nullptr);
        const int h = ca::Notice::Height[0] + ca::Notice::Height[1] * (n > 0 ? n - 1 : 0);
        const int target = bottom - h;
        if (!slot.placed) {
            slot.y.Jump(float(target), now); slot.placed = true;
            // 初回の描画で積み替えを始める。描画間隔が空いても、入りだけ先行させない。
            if (slot.wait_for_stack) { slot.enter_at = now + timing::kNoticeStackMs; slot.wait_for_stack = false; }
        }
        else if (slot.y.to != float(target)) slot.y.Go(float(target), now, timing::kNoticeStackMs);
        const int y = std::clamp(Round(slot.y.At(now)), ca::Notice::Top, ca::Notice::Bottom[menu_open ? 0 : 1] - h);
        bottom -= h + ca::Notice::Gap;
        if (static_cast<s32>(now - slot.enter_at) >= 0) draw(slot, Round(X(slot, s.Width(), now)), y, maxLines);
    }
}
} // namespace gohan::menu
