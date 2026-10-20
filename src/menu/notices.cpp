#include "menu/notices.hpp"
#include "menu/timing.hpp"
#include "menu/paint.hpp"
#include <cstring>

namespace gohan::menu {
namespace {
using namespace motion;
constexpr float kHeight = static_cast<float>(style::kNoticeHeight);

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

float NoticeQueue::Y(const Slot &slot, u32 now) const {
    return Mix(slot.from_y, slot.target_y, EaseOut(Progress(slot.moved_at, now, timing::kNoticeEnterMs)));
}

bool NoticeQueue::SlotAlive(const Slot &slot, u32 now) const {
    if (!slot.active) return false;
    const s32 age = static_cast<s32>(now - slot.created);
    if (age >= 0 && Y(slot, now) + kHeight <= 0.0f) return false; // 画面の上へ出切った
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
        if (!slot.active) continue;
        slot.from_y = Y(slot, at);
        slot.target_y -= kHeight + static_cast<float>(style::kNoticeGap);
        slot.moved_at = at;
    }
    for (Slot &slot : slots_) {
        if (slot.active) continue;
        slot.active = true;
        slot.red = notice.red;
        Copy(slot.title, sizeof(slot.title), notice.title);
        Copy(slot.message, sizeof(slot.message), notice.message);
        slot.created = at;
        slot.from_y = slot.target_y = static_cast<float>(kScreenHeight - style::kNoticeMargin) - kHeight;
        slot.moved_at = at;
        break;
    }
}


void NoticeQueue::Draw(Surface &s, u32 now, bool menu_open) const {
    int bottom = ca::Notice::Bottom[menu_open ? 0 : 1];
    float drift = 0;
    for (int i = kCapacity - 1; i >= 0; --i) {
        const Slot &slot = slots_[i];
        if (!SlotAlive(slot, now) || static_cast<s32>(now - slot.created) < 0) continue;
        char lines[ca::Notice::Inner::Body[3]][kLineBytes], title[kLineBytes];
        const int n = Wrap(s, slot.message, ca::Notice::Inner::Body[4], lines, ca::Notice::Inner::Body[3], nullptr);
        const int h = ca::Notice::Height[0] + ca::Notice::Height[1] * (n > 0 ? n - 1 : 0);
        const int x = Round(X(slot, s.Width(), now));
        const int y = bottom - h + Round(drift);
        bottom -= h + ca::Notice::Gap;
        const float enter = EaseOut(Progress(slot.created, now, timing::kNoticeEnterMs));
        drift += (h + ca::Notice::Gap) * (1 - enter);
        if (s.TextsLeft() < n + 1 || s.RectsLeft() < 13) continue;
        Paper(s, {x, y, ca::Notice::Width, h});
        const u32 color = slot.red ? ca::Color::Red : ca::Color::Green;
        s.Fill({x + ca::Notice::Inner::Line[0], y + ca::Notice::Inner::Line[1], ca::Notice::Inner::Line[2], h + ca::Notice::Inner::Line[3]}, color);
        s.Sprite(x + (slot.red ? ca::Notice::Inner::Bang[0] : ca::Notice::Inner::Icon[0]),
                 y + (slot.red ? ca::Notice::Inner::Bang[1] : ca::Notice::Inner::Icon[1]), slot.red ? "bang" : "check", color);
        s.Text(x + ca::Notice::Inner::Title[0], y + ca::Notice::Inner::Title[1], Trim(s, slot.title, ca::Notice::Width - ca::Notice::Inner::Title[0] - ca::Margin, title, sizeof(title)), color);
        for (int j = 0; j < n; ++j) s.Text(x + ca::Notice::Inner::Body[0], y + ca::Notice::Inner::Body[1] + j * ca::Notice::Inner::Body[2], lines[j], ca::Color::Text);
    }
}
} // namespace gohan::menu
