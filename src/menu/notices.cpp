#include "menu/notices.hpp"
#include "menu/timing.hpp"
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
    const float target = static_cast<float>(screen_width - style::kNoticeMargin - style::kNoticeWidth);
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

void NoticeQueue::Draw(Surface &surface, u32 now) const {
    const int width = surface.Width();
    char buffer[kLineBytes];
    for (const Slot &slot : slots_) {
        if (!SlotAlive(slot, now) || static_cast<s32>(now - slot.created) < 0) continue;
        if (surface.TextsLeft() < 2 || surface.RectsLeft() < 2) return;
        const int x = Round(X(slot, width, now));
        const int y = Round(Y(slot, now));
        const Rect box{x, y, style::kNoticeWidth, style::kNoticeHeight};
        surface.Fill(box, style::kNoticeBackground);
        if (surface.RectsLeft() >= 5) surface.Outline(box, slot.red ? style::kDangerEdge : style::kPanelEdge);
        surface.Fill({x + 1, y + 1, 2, box.h - 2}, slot.red ? style::kDanger : style::kAccent);
        const int text_width = style::kNoticeWidth - 14;
        surface.Text(x + 7, y + 4, Trim(surface, slot.title, text_width, buffer, sizeof(buffer)), style::kWhite);
        surface.Text(x + 7, y + 4 + style::kNoticeLineHeight,
                     Trim(surface, slot.message, text_width, buffer, sizeof(buffer)), style::kNoticeMessage);
    }
}
} // namespace gohan::menu
