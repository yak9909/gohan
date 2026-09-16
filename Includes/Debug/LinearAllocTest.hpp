#pragma once

#include <CTRPluginFramework.hpp>

namespace CTRPluginFramework
{
    // GPU 可視メモリ（linear / VRAM）がプラグインから確保できるかを検証する。
    // 結果は SD の /CTRPF_LinearTest.txt に書き、要約を MessageBox で出す。
    void    LinearAllocTest(MenuEntry *entry);
}
