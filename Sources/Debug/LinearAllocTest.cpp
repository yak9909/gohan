// GPU 可視メモリ（linear / VRAM）がプラグインから確保できるかの検証。
//
// 背景（A:/Disassemble/acnl の解析より）:
//   3DS の GPU は物理アドレスで DMA するので、頂点・テクスチャ・コマンドリストは
//   linear メモリに置き、物理アドレスが分かっている必要がある。
//   実機で計測した限り、ゲームのテクスチャは 仮想 = 物理 + 0x10000000 だった
//   （reg 0x085 = 物理 >> 3。0x23076480 >> 3 == 0x0460EC90 で一致）。
//   これは new linear heap のマッピング（vaddr 0x30000000+ / paddr 0x20000000+）と整合する。
//
// ★検証したいこと:
//   CTRPF プラグインは libctru の起動処理（__system_allocateHeaps）を通らないので、
//   linearAlloc が使えない可能性がある。
//   そこで svcControlMemory(MEMOP_ALLOC_LINEAR) の直接呼び出しも並べて試す。
//   こちらは libctru のヒープ初期化に依存しない。

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "LinearAllocTest.hpp"
#include "csvc.h"   // Luma の独自 SVC（svcControlMemoryEx 0xA2 / svcControlMemoryUnsafe 0xA3）

// ★デバッガから読むための結果（IDA-opus-5.5-F035。SD と画面にも出す）
struct MemProbeRow
{
    u32     kind;       // 1 = svcControlMemory、2 = svcControlMemoryEx、3 = svcControlMemoryUnsafe
    u32     op;         // MEMOP_* | MEMOP_REGION_*
    u32     size;
    u32     res;        // 確保の結果コード
    u32     va;
    u32     pa;
    u32     rw;         // 読み書きの確認 1 = OK
    u32     freeRes;    // 解放の結果コード
};
extern "C" {
    volatile u32   g_memProbeMagic;        // 0x4D454D50（'MEMP'）で試験完了
    volatile u32   g_memProbeCount;
    MemProbeRow    g_memProbe[32];
    s64            g_memRegionUsed[4];     // svcGetSystemInfo(0, 0..3)（0 = 全体）
    u32            g_memRegionSize[3];     // 設定ページ 0x1FF80040 / 44 / 48（APPLICATION / SYSTEM / BASE の割り当て）
    MemInfo        g_memMap[64];           // svcQueryMemory で 0 から順にたどったプロセスのメモリ配置
    u32            g_memMapCount;
    // [K] svcControlMemoryUnsafe で空き番地へ貼る試験: {va, res, paLuma, paCtru, rw, freeRes, sysUsedBefore, sysUsedAlloc, sysUsedFreed}
    u32            g_memK[9];
}

namespace CTRPluginFramework
{
    static const u32 kExpectedDelta = 0x10000000; // 実機で計測した 仮想 - 物理

    static std::string  g_log;

    static void Log(const char *fmt, ...)
    {
        char    buf[256];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        g_log += buf;
        g_log += "\n";
    }

    // 途中でクラッシュしても結果が残るよう、区切りごとに SD へ書き出す。
    static void Flush(void)
    {
        File    f;
        if (File::Open(f, "/CTRPF_LinearTest.txt",
                       File::RWC | File::TRUNCATE | File::SYNC) != File::SUCCESS)
            return;
        f.Write(g_log.c_str(), g_log.size());
        f.Close();
    }

    // 書いて読み返せるかを見る。0x40 バイト先と末尾付近も触る。
    static bool RwCheck(void *p, u32 size)
    {
        if (p == nullptr || size < 0x80)
            return false;

        volatile u32 *v = (volatile u32 *)p;
        u32           last = (size / 4) - 1;

        v[0]    = 0xDEADBEEF;
        v[16]   = 0xA5A5A5A5;
        v[last] = 0x12345678;

        return v[0] == 0xDEADBEEF && v[16] == 0xA5A5A5A5 && v[last] == 0x12345678;
    }

    static void ReportBlock(const char *tag, void *p, u32 size)
    {
        if (p == nullptr)
        {
            Log("  %s size 0x%08X -> 確保できず", tag, size);
            return;
        }

        u32 va   = (u32)p;
        u32 pa   = osConvertVirtToPhys(p);
        bool rw  = RwCheck(p, size);

        if (pa == 0)
        {
            Log("  %s size 0x%08X -> VA %08X  PA 取得できず(0)  rw=%s  ★GPU に見せられない",
                tag, size, va, rw ? "OK" : "NG");
            return;
        }

        u32 delta = va - pa;
        Log("  %s size 0x%08X -> VA %08X  PA %08X  delta %08X %s  rw=%s  align%%0x80=%u",
            tag, size, va, pa, delta,
            delta == kExpectedDelta ? "(実測値と一致)" : "(実測値と不一致)",
            rw ? "OK" : "NG", va & 0x7F);
    }

    static void AddRow(u32 kind, u32 op, u32 size, Result res, u32 va, u32 freeRes)
    {
        if (g_memProbeCount >= 32)
            return;
        MemProbeRow &r = g_memProbe[g_memProbeCount];
        r.kind = kind;
        r.op = op;
        r.size = size;
        r.res = (u32)res;
        r.va = va;
        r.pa = va ? osConvertVirtToPhys((void *)va) : 0;
        r.rw = va ? (RwCheck((void *)va, size) ? 1u : 0u) : 0u;
        r.freeRes = freeRes;
        ++g_memProbeCount;
    }

    // svcControlMemory による直接確保。libctru のヒープ初期化に依存しない。
    static void TestSvcLinear(u32 size)
    {
        u32     addr = 0;
        Result  res  = svcControlMemory(&addr, 0, 0, size, MEMOP_ALLOC_LINEAR, MEMPERM_READWRITE);

        if (R_FAILED(res))
        {
            Log("  svcControlMemory size 0x%08X -> 失敗 res=%08X", size, (u32)res);
            AddRow(1, MEMOP_ALLOC_LINEAR, size, res, 0, 0);
            return;
        }

        ReportBlock("svcLinear ", (void *)addr, size);

        u32 dummy = 0;
        Result fr = svcControlMemory(&dummy, addr, 0, size, MEMOP_FREE, (MemPerm)0);
        AddRow(1, MEMOP_ALLOC_LINEAR, size, res, addr, (u32)fr);
    }

    // [H] 各領域の使用量と割り当て
    static void TestRegions(void)
    {
        const volatile u32 *cfg = (const volatile u32 *)0x1FF80040;
        for (u32 i = 0; i < 3; ++i)
            g_memRegionSize[i] = cfg[i];
        for (u32 i = 0; i < 4; ++i)
        {
            s64 v = -1;
            svcGetSystemInfo(&v, 0, (s32)i);
            g_memRegionUsed[i] = v;
        }
        static const char *names[3] = { "APPLICATION", "SYSTEM", "BASE" };
        for (u32 i = 0; i < 3; ++i)
        {
            const s64 used = g_memRegionUsed[i + 1];
            Log("  %-11s 割り当て 0x%08X  使用 0x%08X  空き 0x%08X (%u KiB)", names[i], g_memRegionSize[i], (u32)used,
                (u32)((s64)g_memRegionSize[i] - used), (u32)(((s64)g_memRegionSize[i] - used) / 1024));
        }
    }

    // [I] / [J] 領域を指定した LINEAR 確保（Luma の独自 SVC）。確保できたらすぐ解放する
    static void TestRegionLinear(bool unsafe, u32 region, u32 size)
    {
        const u32 op = (u32)MEMOP_ALLOC_LINEAR | region;
        u32     addr = 0;
        Result  res = unsafe ? svcControlMemoryUnsafe(&addr, 0, size, (MemOp)op, MEMPERM_READWRITE)
                             : svcControlMemoryEx(&addr, 0, 0, size, (MemOp)op, MEMPERM_READWRITE, false);
        if (R_FAILED(res))
        {
            Log("  %s op %05X size 0x%06X -> 失敗 res=%08X", unsafe ? "Unsafe" : "Ex    ", op, size, (u32)res);
            AddRow(unsafe ? 3 : 2, op, size, res, 0, 0);
            return;
        }
        ReportBlock(unsafe ? "Unsafe" : "Ex    ", (void *)addr, size);
        u32 dummy = 0;
        Result fr = unsafe ? svcControlMemoryUnsafe(&dummy, addr, size, MEMOP_FREE, (MemPerm)0)
                           : svcControlMemoryEx(&dummy, addr, 0, size, MEMOP_FREE, (MemPerm)0, false);
        Log("    解放 res=%08X", (u32)fr);
        AddRow(unsafe ? 3 : 2, op, size, res, addr, (u32)fr);
    }

    static u32 SystemUsed(void)
    {
        s64 v = -1;
        svcGetSystemInfo(&v, 0, 2);
        return (u32)v;
    }

    // プロセスのメモリ配置をたどり、[0x0E000000, 0x14000000) で 1MB 以上空いている区画の先頭を返す（無ければ 0）
    static u32 FindFreeVa(u32 need)
    {
        g_memMapCount = 0;
        u32 addr = 0, pick = 0;
        while (addr < 0x40000000u && g_memMapCount < 64)
        {
            MemInfo mi;
            PageInfo pi;
            if (R_FAILED(svcQueryMemory(&mi, &pi, addr)) || mi.size == 0)
                break;
            g_memMap[g_memMapCount++] = mi;
            if (pick == 0u && mi.state == MEMSTATE_FREE && mi.base_addr >= 0x0E000000u && mi.base_addr < 0x14000000u
                && mi.size >= need)
                pick = mi.base_addr;
            addr = mi.base_addr + mi.size;
        }
        for (u32 i = 0; i < g_memMapCount; ++i)
            Log("    %08X +%08X perm %u state %u", g_memMap[i].base_addr, g_memMap[i].size, g_memMap[i].perm, g_memMap[i].state);
        return pick;
    }

    static void TestUnsafeAtFreeVa(void)
    {
        const u32 size = 0x40000;
        for (u32 i = 0; i < 9; ++i)
            g_memK[i] = 0;
        const u32 va = FindFreeVa(0x100000);
        g_memK[0] = va;
        if (va == 0u)
        {
            Log("  空き番地が見つからない");
            return;
        }
        g_memK[6] = SystemUsed();
        u32 out = 0;
        const Result res = svcControlMemoryUnsafe(&out, va, size, (MemOp)(MEMOP_ALLOC_LINEAR | MEMOP_REGION_SYSTEM),
                                                  MEMPERM_READWRITE);
        g_memK[1] = (u32)res;
        if (R_FAILED(res))
        {
            Log("  VA %08X -> 失敗 res=%08X", va, (u32)res);
            return;
        }
        g_memK[7] = SystemUsed();
        g_memK[2] = svcConvertVAToPA((void *)va, false);
        g_memK[3] = osConvertVirtToPhys((void *)va);
        g_memK[4] = RwCheck((void *)va, size) ? 1u : 0u;
        u32 dummy = 0;
        g_memK[5] = (u32)svcControlMemoryUnsafe(&dummy, va, size, MEMOP_FREE, (MemPerm)0);
        g_memK[8] = SystemUsed();
        Log("  VA %08X out %08X PA(Luma) %08X PA(ctru) %08X rw=%u 解放 res=%08X", va, out, g_memK[2], g_memK[3], g_memK[4], g_memK[5]);
        Log("  SYSTEM 使用量: 前 %08X / 確保後 %08X / 解放後 %08X", g_memK[6], g_memK[7], g_memK[8]);
    }

    static void RunTest(void)
    {
        g_log.clear();
        g_memProbeMagic = 0;
        g_memProbeCount = 0;

        Log("=== GPU 可視メモリ 可用性テスト ===");
        Log("");
        Log("※各段階ごとに SD へ書き出している。途中で落ちてもここまでは残る。");
        Log("");
        Flush();

        // --- 最重要かつ最も安全: libctru のヒープ初期化に依存しない直接確保 ---
        Log("[A] svcControlMemory(MEMOP_ALLOC_LINEAR)  ※libctru 非依存。これが本命");
        {
            const u32 sizes[4] = { 0x1000, 0x10000, 0x80000, 0x200000 };
            for (int i = 0; i < 4; ++i)
            {
                TestSvcLinear(sizes[i]);
                Flush();
            }
        }
        Log("");
        Flush();

        Log("[H] 各領域の割り当てと使用量（svcGetSystemInfo 0 / 設定ページ 0x1FF80040）");
        TestRegions();
        Log("");
        Flush();

        Log("[I] svcControlMemoryEx(MEMOP_ALLOC_LINEAR | 領域)  ※Luma 0xA2");
        {
            const u32 regions[3] = { MEMOP_REGION_APP, MEMOP_REGION_SYSTEM, MEMOP_REGION_BASE };
            const u32 sizes[3] = { 0x10000, 0x40000, 0x100000 };
            for (u32 r = 0; r < 3; ++r)
                for (u32 s = 0; s < 3; ++s)
                {
                    TestRegionLinear(false, regions[r], sizes[s]);
                    Flush();
                }
        }
        Log("");
        // [J]（addr0 = 0 の Unsafe）は外した: Unsafe は addr0 へ貼るので 0 だと番地 0 に貼ってしまう（実機 2026-09-25、IDA-opus-5.5-F035）
        Log("");
        Flush();

        Log("[K] 空き番地へ svcControlMemoryUnsafe(ALLOC_LINEAR | REGION_SYSTEM) 0x40000");
        TestUnsafeAtFreeVa();
        Log("");
        Flush();

        // --- プラグインのヒープ（対照）---
        Log("[B] プラグインヒープ（対照。GPU には見せられないはず）");
        {
            u8 *p = new u8[0x10000];
            Log("  new u8[0x10000] -> VA %08X  PA %08X", (u32)p, osConvertVirtToPhys(p));
            delete[] p;
        }
        Log("");
        Flush();

        // --- キャッシュ flush が呼べるか ---
        Log("[C] GSPGPU_FlushDataCache");
        {
            static u8 buf[256];
            Result res = GSPGPU_FlushDataCache(buf, sizeof(buf));
            Log("  戻り値 = %08X %s", (u32)res, R_SUCCEEDED(res) ? "(成功)" : "(失敗)");
        }
        Log("");
        Flush();

        // --- ここから先は libctru のアロケータ。未初期化なら落ちる可能性がある ---
        Log("[D] libctru アロケータの状態  ※ここから先は落ちる可能性あり");
        {
            u32 linFreeSz  = linearSpaceFree();
            Log("  linearSpaceFree = 0x%08X (%u KiB)", linFreeSz, linFreeSz / 1024);
            Flush();
            u32 vramFreeSz = vramSpaceFree();
            Log("  vramSpaceFree   = 0x%08X (%u KiB)", vramFreeSz, vramFreeSz / 1024);
            if (linFreeSz == 0)
                Log("  ★linear ヒープが未初期化の可能性が高い（プラグインは libctru の起動処理を通らない）");
        }
        Log("");
        Flush();

        Log("[E] linearAlloc");
        {
            const u32 sizes[4] = { 0x1000, 0x10000, 0x80000, 0x200000 };
            for (int i = 0; i < 4; ++i)
            {
                void *p = linearAlloc(sizes[i]);
                ReportBlock("linearAlloc", p, sizes[i]);
                Flush();
                if (p)
                    linearFree(p);
            }
        }
        Log("");
        Flush();

        Log("[F] linearMemAlign(0x10000, 0x80)");
        {
            void *p = linearMemAlign(0x10000, 0x80);
            ReportBlock("linearAlign", p, 0x10000);
            Flush();
            if (p)
                linearFree(p);
        }
        Log("");
        Flush();

        Log("[G] vramAlloc");
        {
            void *p = vramAlloc(0x10000);
            if (p == nullptr)
                Log("  vramAlloc 0x10000 -> 確保できず");
            else
            {
                Log("  vramAlloc 0x10000 -> VA %08X  PA %08X", (u32)p, osConvertVirtToPhys(p));
                vramFree(p);
            }
        }
        Log("");
        Log("=== 全段階を完走した ===");
        Flush();
        g_memProbeMagic = 0x4D454D50;
    }

    // 複数の候補パスへ書き、それぞれの結果コードを返す。
    static std::string TryWriteAll(void)
    {
        static const char *paths[3] = {
            "/CTRPF_LinearTest.txt",
            "/luma/plugins/CTRPF_LinearTest.txt",
            "/luma/CTRPF_LinearTest.txt"
        };
        std::string out;

        for (int i = 0; i < 3; ++i)
        {
            File    f;
            int     r = File::Open(f, paths[i], File::RWC | File::TRUNCATE | File::SYNC);

            out += paths[i];
            out += " -> open=";
            out += std::to_string(r);

            if (r == File::SUCCESS)
            {
                int w = f.Write(g_log.c_str(), g_log.size());
                f.Flush();
                f.Close();
                out += " write=";
                out += std::to_string(w);
            }
            out += "\n";
        }
        return out;
    }

    // ログを画面に分割表示する。SD が読めなくても結果が取れるように。
    static void ShowLogPaged(void)
    {
        const u32   linesPerPage = 12;
        std::string page;
        u32         count = 0;
        u32         pageNo = 1;
        size_t      pos = 0;

        while (pos <= g_log.size())
        {
            size_t nl = g_log.find('\n', pos);
            if (nl == std::string::npos)
                nl = g_log.size();

            page += g_log.substr(pos, nl - pos);
            page += "\n";
            ++count;
            pos = nl + 1;

            if (count >= linesPerPage || pos > g_log.size())
            {
                if (!page.empty())
                {
                    MessageBox(std::string("結果 ") + std::to_string(pageNo), page)();
                    ++pageNo;
                }
                page.clear();
                count = 0;
            }
            if (nl == g_log.size())
                break;
        }
    }

    void    LinearAllocTest(MenuEntry *entry)
    {
        if (!entry->WasJustActivated())
            return;

        RunTest();

        // SD への書き出しを試し、その結果自体もログへ足す
        std::string fileRes = TryWriteAll();
        g_log += "\n[SD への書き出し結果]\n";
        g_log += fileRes;

        // 画面へ分割表示（SD が読めなくても結果が取れる）
        ShowLogPaged();
    }
}
