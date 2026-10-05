#ifndef GUI_TOUCHSCROLL_HPP
#define GUI_TOUCHSCROLL_HPP

// 1 軸のタッチスクロール（揺れの補正・タップとスクロールの見分け・慣性）。
// 漢字変換の候補欄（Sources/ChatKanji/ChatIme.cpp の HandleTouch / FilterPush / ReleaseVelocity）と同じ算法・同じ定数の写し。
// 利用者の指示（2026-10-05）: HHD 画面の髪型のスクロールは「漢字変換の変換欄のスクロールが理想」。
//   ChatIme.cpp は実機確認済みなので書き換えず、ここに写した（直すときは両方を見ること）。
// 座標は画素。scroll は 0..max で、指を左へ動かすと増える（候補欄と同じ向き）。時刻は svcGetSystemTick。

#include <3ds.h>
#include <cmath>

class TouchScroll
{
public:
    static constexpr float kTapSlop = 6.0f;         // 触れた所から（補正後の位置で）これ以上動いたらスクロール
    static constexpr float kSpikePx = 40.0f;        // 1 回で 40px を越える動きは飛び値として 1 回保留する
    static constexpr float kEuroMinCutoff = 1.0f;   // 1€ フィルタ（Casiez 2012）
    static constexpr float kEuroBeta = 0.05f;
    static constexpr float kEuroDCutoff = 5.0f;
    static constexpr float kHysteresisPx = 1.0f;
    static constexpr float kFlingWindow = 0.1f;     // s。離す直前のこの間の動きから速さを求める
    static constexpr float kFlingMin = 60.0f;       // px/s
    static constexpr float kFlingMax = 2500.0f;     // px/s
    static constexpr float kFlingTau = 0.35f;       // s。速さが 1/e になる時間
    static constexpr float kFlingStop = 15.0f;      // px/s
    static constexpr int   kHist = 16;

    float   scroll = 0.0f;
    float   max = 0.0f;

    bool    Scrolling(void) const { return m_scrolling; }
    bool    Flinging(void) const { return m_flingV != 0.0f; }
    void    Stop(void) { m_flingV = 0.0f; }

    // 触れた瞬間。戻り値: 慣性で動いている最中だった（その触れ方は止めるだけで、タップにしない）
    bool    Press(float x, u64 now)
    {
        const bool wasFlinging = m_flingV != 0.0f;

        m_flingV = 0.0f;
        m_lastTick = now;
        FilterReset(x, now);
        m_scrolling = false;
        m_touchStartX = m_fx;
        return wasFlinging;
    }

    // 触れている間（毎回）
    void    Hold(float x, u64 now)
    {
        const float dt = Dt(now);

        FilterPush(x, dt, now);
        if (!m_scrolling)
        {
            const float moved = m_fx - m_touchStartX;

            if (moved > kTapSlop || moved < -kTapSlop)
            {
                // スロップを越えた所から動かす（越えた分だけ飛ばない）
                m_scrolling = true;
                m_dragStartX = m_hx;
                m_dragStartScroll = scroll;
            }
        }
        if (m_scrolling)
        {
            scroll = m_dragStartScroll - (m_hx - m_dragStartX);
            Clamp();
        }
    }

    // 離した瞬間。スクロールしていたら離す直前の速さで投げる
    void    Release(u64 now)
    {
        Dt(now);
        if (m_scrolling)
        {
            const float v = ReleaseVelocity(now);

            if (v > kFlingMin || v < -kFlingMin)
                m_flingV = -(v > kFlingMax ? kFlingMax : v < -kFlingMax ? -kFlingMax : v);
        }
        m_scrolling = false;
    }

    // 触れていない間（毎回）: 慣性。指数関数的に減速し、端か十分遅くなったら止める
    void    Idle(u64 now)
    {
        const float dt = Dt(now);

        if (m_flingV == 0.0f)
            return;

        const float before = scroll;

        scroll += m_flingV * dt;
        Clamp();
        m_flingV *= expf(-dt / kFlingTau);
        if ((m_flingV < kFlingStop && m_flingV > -kFlingStop) || (dt > 0.0f && scroll == before))
            m_flingV = 0.0f;
    }

    void    Clamp(void)
    {
        if (scroll > max)
            scroll = max;
        if (scroll < 0.0f)
            scroll = 0.0f;
    }

private:
    bool    m_scrolling = false;
    float   m_flingV = 0.0f;
    u64     m_lastTick = 0;
    float   m_lastRaw = 0.0f;
    bool    m_spikeHeld = false;
    float   m_spikeDt = 0.0f;
    float   m_fx = 0.0f, m_fdx = 0.0f, m_hx = 0.0f;
    float   m_touchStartX = 0.0f;
    float   m_dragStartX = 0.0f, m_dragStartScroll = 0.0f;
    float   m_histX[kHist];
    u64     m_histT[kHist];
    int     m_histCount = 0, m_histHead = 0;

    float   Dt(u64 now)
    {
        const float dt = m_lastTick != 0 ? (float)(now - m_lastTick) / (float)SYSCLOCK_ARM11 : 0.0f;

        m_lastTick = now;
        return dt;
    }

    static float Alpha(float cutoff, float dt)
    {
        const float tau = 1.0f / (2.0f * 3.14159265f * cutoff);

        return 1.0f / (1.0f + tau / dt);
    }

    void    HistPush(float x, u64 now)
    {
        m_histX[m_histHead] = x;
        m_histT[m_histHead] = now;
        m_histHead = (m_histHead + 1) % kHist;
        if (m_histCount < kHist)
            m_histCount++;
    }

    void    FilterReset(float x, u64 now)
    {
        m_lastRaw = x;
        m_spikeHeld = false;
        m_fx = x;
        m_hx = x;
        m_fdx = 0.0f;
        m_histCount = 0;
        m_histHead = 0;
        HistPush(x, now);
    }

    void    FilterPush(float x, float dt, u64 now)
    {
        if (m_spikeHeld)
        {
            m_spikeHeld = false;
            dt += m_spikeDt;
        }
        else if (x - m_lastRaw > kSpikePx || m_lastRaw - x > kSpikePx)
        {
            m_spikeHeld = true;
            m_spikeDt = dt;
            return;
        }
        m_lastRaw = x;
        if (dt <= 0.0f)
            return;

        const float dx = (x - m_fx) / dt;

        m_fdx += Alpha(kEuroDCutoff, dt) * (dx - m_fdx);

        const float speed = m_fdx < 0.0f ? -m_fdx : m_fdx;

        m_fx += Alpha(kEuroMinCutoff + kEuroBeta * speed, dt) * (x - m_fx);
        if (m_fx - m_hx > kHysteresisPx)
            m_hx = m_fx - kHysteresisPx;
        else if (m_hx - m_fx > kHysteresisPx)
            m_hx = m_fx + kHysteresisPx;
        HistPush(m_fx, now);
    }

    float   ReleaseVelocity(u64 now)
    {
        const u64   window = (u64)(kFlingWindow * (float)SYSCLOCK_ARM11);
        const int   last = (m_histHead + kHist - 1) % kHist;
        int         first = last;

        if (m_histCount < 2 || now - m_histT[last] > window)
            return 0.0f;
        for (int n = 1; n < m_histCount; n++)
        {
            const int i = (last + kHist - n) % kHist;

            if (now - m_histT[i] > window)
                break;
            first = i;
        }

        const float dt = (float)(m_histT[last] - m_histT[first]) / (float)SYSCLOCK_ARM11;

        return dt >= 0.02f ? (m_histX[last] - m_histX[first]) / dt : 0.0f;
    }
};

#endif
