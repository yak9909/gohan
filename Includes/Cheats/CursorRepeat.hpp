#pragma once

#include <3ds/types.h>

// エディターのカーソル（範囲）を押し続けで動かす規則。公共事業エディターで利用者と合わせた値（2026-09-24）を、
// マップエディターでも同じにするため 1 か所にまとめた（利用者指示 2026-09-27）。1 ティック = メニューのスレッドの 1 回。
//   押した瞬間に 1 マス、押し続けると 12 ティック後から 4 ティックごと（約 200ms 後から約 64ms ごと）。
//   どれかの向きが連続移動に入っている間（離して kGrace ティック以内も含む）に別の向きを入れたら、その向きは待たずに
//   連続移動にする（押した瞬間に 1 マス、以後 4 ティックごと）。斜めを足しても連続移動がやり直しにならない。
class CursorRepeat
{
public:
    enum : u32 { kUp = 1u, kDown = 2u, kLeft = 4u, kRight = 8u };

    void Reset(void)
    {
        for (u32 i = 0; i < 4; ++i)
            m_hold[i] = 0;
        m_repeatingAgo = 0xFFFFu;
    }

    // held: kUp / kDown / kLeft / kRight の組。戻り値: このティックで 1 マス動かす向きの組
    u32 Step(u32 held)
    {
        bool repeating = false;
        for (u32 i = 0; i < 4; ++i)
            repeating = repeating || m_hold[i] > kRepeatStart;
        if (repeating)
            m_repeatingAgo = 0;
        else if (m_repeatingAgo < 0xFFFFu)
            ++m_repeatingAgo;
        const bool fast = m_repeatingAgo <= kGrace;
        u32 fire = 0;
        for (u32 i = 0; i < 4; ++i)
            if (PadRepeat(m_hold[i], (held & (1u << i)) != 0, fast))
                fire |= 1u << i;
        return fire;
    }

private:
    static const u32 kRepeatStart = 12;     // 押し始めから連続移動に入るまで（ティック）
    static const u32 kRepeatEvery = 4;      // 連続移動の間隔（ティック）
    static const u32 kGrace = 3;            // 連続移動が止まってからも「速い」扱いにするティック

    // 押した瞬間と、押し続けたとき（約 200ms 後から約 64ms ごと）に真
    static bool Repeat(u32 &counter, bool held)
    {
        if (!held) {
            counter = 0;
            return false;
        }
        ++counter;
        return counter == 1 || (counter > kRepeatStart && ((counter - kRepeatStart) % kRepeatEvery) == 0);
    }

    static bool PadRepeat(u32 &counter, bool held, bool fast)
    {
        if (held && counter == 0 && fast) {
            counter = kRepeatStart;
            return true;
        }
        return Repeat(counter, held);
    }

    u32 m_hold[4] = { 0, 0, 0, 0 };         // 上 下 左 右 の押し続けティック
    u32 m_repeatingAgo = 0xFFFFu;
};
