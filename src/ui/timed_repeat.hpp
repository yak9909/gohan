#pragma once
#include <cstdint>

namespace gohan::ui {
struct RepeatPolicy {
    std::uint32_t delay_ms = 200, interval_ms = 60;
    std::uint32_t accelerate_after_ms = 0, accelerated_ms = 60, added_direction_grace_ms = 0;
};
// 方向はビット集合。開始・方向変更を即時に返し、方向を足した直後は猶予を置ける。
// 遅れた分をまとめて返さず、次回は今回の時刻から数える。時刻は差で周回を扱う。
class TimedRepeat {
  public:
    explicit TimedRepeat(RepeatPolicy policy = {}) : policy_(policy) {}
    void Reset() { direction_ = 0; }
    bool Step(std::uint32_t direction, std::uint32_t now) {
        if (!direction) { Reset(); return false; }
        if (direction != direction_) {
            const bool added = direction_ && (direction & direction_) == direction_;
            direction_ = direction; started_ = now;
            next_ = now + (added ? policy_.added_direction_grace_ms : policy_.delay_ms);
            if (added && policy_.added_direction_grace_ms) return false;
            next_ = now + policy_.delay_ms;
            return true;
        }
        if (static_cast<std::int32_t>(now - next_) < 0) return false;
        const auto interval = policy_.accelerate_after_ms && now - started_ >= policy_.accelerate_after_ms
            ? policy_.accelerated_ms : policy_.interval_ms;
        next_ = now + interval;
        return true;
    }
  private:
    RepeatPolicy policy_;
    std::uint32_t direction_ = 0, started_ = 0, next_ = 0;
};
} // namespace gohan::ui
