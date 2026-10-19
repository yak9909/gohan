#pragma once
#include "menu/types.hpp"
#include <cmath>

// 1 軸のタッチスクロール。揺れの補正（1€ フィルタ・飛び値の保留・ヒステリシス）、
// タップとスクロールの見分け、離した後の慣性を持つ。座標は画素、時刻はミリ秒。
// scroll は 0..max で、指を上（座標が減る向き）へ動かすと増える。
namespace gohan::menu {
class TouchScroll {
  public:
    static constexpr float kTapSlop = 6.0f;       // 触れた所からこれ以上動いたらスクロール
    static constexpr float kSpikePx = 40.0f;      // 1 回でこれを越える動きは飛び値として 1 回保留する
    static constexpr float kMinCutoff = 1.0f;
    static constexpr float kBeta = 0.05f;
    static constexpr float kDerivativeCutoff = 5.0f;
    static constexpr float kHysteresisPx = 1.0f;
    static constexpr float kFlingWindow = 0.1f;   // 秒。離す直前のこの間の動きから速さを求める
    static constexpr float kFlingMin = 60.0f;     // 画素/秒
    static constexpr float kFlingMax = 2500.0f;
    static constexpr float kFlingTau = 0.35f;     // 秒。速さが 1/e になる時間
    static constexpr float kFlingStop = 15.0f;
    static constexpr int kHistory = 16;

    float scroll = 0.0f;
    float max = 0.0f;

    bool Scrolling() const { return scrolling_; }
    bool Flinging() const { return fling_ != 0.0f; }
    void Stop() { fling_ = 0.0f; }

    // 触れた瞬間。慣性で動いている最中だったら true（その触れ方は止めるだけで、タップにしない）。
    bool Press(float position, u32 now) {
        const bool was_flinging = fling_ != 0.0f;
        fling_ = 0.0f;
        last_tick_ = now;
        have_tick_ = true;
        ResetFilter(position, now);
        scrolling_ = false;
        start_position_ = filtered_;
        return was_flinging;
    }

    // 触れている間（毎フレーム）。
    void Hold(float position, u32 now) {
        const float dt = Elapsed(now);
        Push(position, dt, now);
        if (!scrolling_) {
            const float moved = filtered_ - start_position_;
            if (moved > kTapSlop || moved < -kTapSlop) {
                // 境目を越えた所から動かす（越えた分だけ飛ばない）。
                scrolling_ = true;
                drag_start_ = held_;
                drag_scroll_ = scroll;
            }
        }
        if (scrolling_) {
            scroll = drag_scroll_ - (held_ - drag_start_);
            Clamp();
        }
    }

    // 離した瞬間。スクロールしていたら、離す直前の速さで投げる。
    void Release(u32 now) {
        Elapsed(now);
        if (scrolling_) {
            const float v = ReleaseVelocity(now);
            if (v > kFlingMin || v < -kFlingMin) fling_ = -(v > kFlingMax ? kFlingMax : v < -kFlingMax ? -kFlingMax : v);
        }
        scrolling_ = false;
    }

    // 触れていない間（毎フレーム）。指数関数的に減速し、端か十分遅くなったら止める。
    void Idle(u32 now) {
        const float dt = Elapsed(now);
        if (fling_ == 0.0f) return;
        const float before = scroll;
        scroll += fling_ * dt;
        Clamp();
        fling_ *= std::exp(-dt / kFlingTau);
        if ((fling_ < kFlingStop && fling_ > -kFlingStop) || (dt > 0.0f && scroll == before)) fling_ = 0.0f;
    }

    void Clamp() {
        if (scroll > max) scroll = max;
        if (scroll < 0.0f) scroll = 0.0f;
    }

  private:
    bool scrolling_ = false;
    bool have_tick_ = false;
    float fling_ = 0.0f;
    u32 last_tick_ = 0;
    float last_raw_ = 0.0f;
    bool spike_held_ = false;
    float spike_dt_ = 0.0f;
    float filtered_ = 0.0f, derivative_ = 0.0f, held_ = 0.0f;
    float start_position_ = 0.0f;
    float drag_start_ = 0.0f, drag_scroll_ = 0.0f;
    float history_position_[kHistory]{};
    u32 history_time_[kHistory]{};
    int history_count_ = 0, history_head_ = 0;

    float Elapsed(u32 now) {
        const float dt = have_tick_ ? static_cast<float>(now - last_tick_) / 1000.0f : 0.0f;
        last_tick_ = now;
        have_tick_ = true;
        return dt;
    }

    static float Alpha(float cutoff, float dt) {
        const float tau = 1.0f / (2.0f * 3.14159265f * cutoff);
        return 1.0f / (1.0f + tau / dt);
    }

    void Remember(float position, u32 now) {
        history_position_[history_head_] = position;
        history_time_[history_head_] = now;
        history_head_ = (history_head_ + 1) % kHistory;
        if (history_count_ < kHistory) ++history_count_;
    }

    void ResetFilter(float position, u32 now) {
        last_raw_ = position;
        spike_held_ = false;
        filtered_ = held_ = position;
        derivative_ = 0.0f;
        history_count_ = history_head_ = 0;
        Remember(position, now);
    }

    void Push(float position, float dt, u32 now) {
        if (spike_held_) {
            spike_held_ = false;
            dt += spike_dt_;
        } else if (position - last_raw_ > kSpikePx || last_raw_ - position > kSpikePx) {
            spike_held_ = true;
            spike_dt_ = dt;
            return;
        }
        last_raw_ = position;
        if (dt <= 0.0f) return;
        const float speed_now = (position - filtered_) / dt;
        derivative_ += Alpha(kDerivativeCutoff, dt) * (speed_now - derivative_);
        const float speed = derivative_ < 0.0f ? -derivative_ : derivative_;
        filtered_ += Alpha(kMinCutoff + kBeta * speed, dt) * (position - filtered_);
        if (filtered_ - held_ > kHysteresisPx) held_ = filtered_ - kHysteresisPx;
        else if (held_ - filtered_ > kHysteresisPx) held_ = filtered_ + kHysteresisPx;
        Remember(filtered_, now);
    }

    float ReleaseVelocity(u32 now) const {
        const u32 window = static_cast<u32>(kFlingWindow * 1000.0f);
        const int last = (history_head_ + kHistory - 1) % kHistory;
        int first = last;
        if (history_count_ < 2 || now - history_time_[last] > window) return 0.0f;
        for (int n = 1; n < history_count_; ++n) {
            const int i = (last + kHistory - n) % kHistory;
            if (now - history_time_[i] > window) break;
            first = i;
        }
        const float dt = static_cast<float>(history_time_[last] - history_time_[first]) / 1000.0f;
        return dt >= 0.02f ? (history_position_[last] - history_position_[first]) / dt : 0.0f;
    }
};
} // namespace gohan::menu
