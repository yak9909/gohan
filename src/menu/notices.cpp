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
    const s32 age = static_cast<s32>(now - slot.created);
    if (age < 0) return off;
    if (age < static_cast<s32>(timing::kNoticeEnterMs))
        return Mix(off, target, EaseOut(static_cast<float>(age) / static_cast<float>(timing::kNoticeEnterMs)));
    if (age < static_cast<s32>(timing::kNoticeEnterMs + timing::kNoticeHoldMs)) return target;
    return Mix(target, off,
               EaseOut(static_cast<float>(age - static_cast<s32>(timing::kNoticeEnterMs + timing::kNoticeHoldMs)) /
                       static_cast<float>(timing::kNoticeExitMs)));
}

bool NoticeQueue::SlotAlive(const Slot &slot, u32 now) const {
    if (!slot.active) return false;
    const s32 age = static_cast<s32>(now - slot.created);
    return age < static_cast<s32>(timing::kNoticeEnterMs + timing::kNoticeHoldMs + timing::kNoticeExitMs);
}

bool NoticeQueue::Alive(u32 now) const {
    for (const Slot &slot : slots_)
        if (SlotAlive(slot, now)) return true;
    return false;
}

void NoticeQueue::Add(const Notice &notice, u32 now) {
    u32 at = now;
    for (Slot &slot : slots_)
        if (!SlotAlive(slot, now)) slot.active = false;
    // 続けて来た通知は少しずつずらして入れる。
    if (ever_ && static_cast<s32>(last_at_ + timing::kNoticeSpawnMs - at) > 0) at = last_at_ + timing::kNoticeSpawnMs;
    ever_ = true;
    last_at_ = at;
    // 生きている順（古い順）に前へ詰める。満杯なら一番古いものを落とす。
    int n = 0;
    for (int i = 0; i < kCapacity; ++i)
        if (slots_[i].active) slots_[n++] = slots_[i];
    for (int i = n; i < kCapacity; ++i) slots_[i].active = false;
    if (n >= kCapacity) {
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
        slot.placed = false;
        break;
    }
}


void NoticeQueue::Draw(Surface &s, u32 now, bool menu_open) {
    int bottom = ca::Notice::Bottom[menu_open ? 0 : 1];
    int count = 0;
    for (const Slot &slot : slots_)
        count += SlotAlive(slot, now) && static_cast<s32>(now - slot.created) >= 0;
    if (!count) return;
    const int room = (bottom - ca::Notice::Top - (count - 1) * ca::Notice::Gap) / count;
    const int maxLines = std::clamp(1 + (room - ca::Notice::Height[0]) / ca::Notice::Height[1], 1, ca::Notice::Inner::Body[3]);
    for (int i = kCapacity - 1; i >= 0; --i) {
        Slot &slot = slots_[i];
        if (!SlotAlive(slot, now) || static_cast<s32>(now - slot.created) < 0) continue;
        char lines[ca::Notice::Inner::Body[3]][kLineBytes], title[kLineBytes];
        const int n = Wrap(s, slot.message, ca::Notice::Inner::Body[4], lines, maxLines, nullptr);
        const int h = ca::Notice::Height[0] + ca::Notice::Height[1] * (n > 0 ? n - 1 : 0);
        const int x = Round(X(slot, s.Width(), now));
        const int target = bottom - h;
        if (!slot.placed) { slot.y.Jump(float(target), now); slot.placed = true; }
        else if (slot.y.to != float(target)) slot.y.Go(float(target), now, timing::kNoticeEnterMs);
        const int y = std::clamp(Round(slot.y.At(now)), ca::Notice::Top, ca::Notice::Bottom[menu_open ? 0 : 1] - h);
        bottom -= h + ca::Notice::Gap;
        if (s.TextsLeft() < n + 1 || s.RectsLeft() < 13) continue;
        Paper(s, {x, y, ca::Notice::Width, h}, 1, "notice.window");
        const u32 color = slot.red ? ca::Color::Red : ca::Color::Green;
        s.Fill({x + ca::Notice::Inner::Line[0], y + ca::Notice::Inner::Line[1], ca::Notice::Inner::Line[2], h + ca::Notice::Inner::Line[3]}, color);
        s.Sprite(x + (slot.red ? ca::Notice::Inner::Bang[0] : ca::Notice::Inner::Icon[0]),
                 y + (slot.red ? ca::Notice::Inner::Bang[1] : ca::Notice::Inner::Icon[1]), slot.red ? "bang" : "check", color);
        s.Text(x + ca::Notice::Inner::Title[0], y + ca::Notice::Inner::Title[1], Trim(s, slot.title, ca::Notice::Width - ca::Notice::Inner::Title[0] - ca::Margin, title, sizeof(title)), ca::Color::Text);
        for (int j = 0; j < n; ++j) s.Text(x + ca::Notice::Inner::Body[0], y + ca::Notice::Inner::Body[1] + j * ca::Notice::Inner::Body[2], lines[j], slot.red ? ca::Color::Red : ca::Color::Text);
    }
}
} // namespace gohan::menu
