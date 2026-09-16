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

    // svcControlMemory による直接確保。libctru のヒープ初期化に依存しない。
    static void TestSvcLinear(u32 size)
    {
        u32     addr = 0;
        Result  res  = svcControlMemory(&addr, 0, 0, size, MEMOP_ALLOC_LINEAR, MEMPERM_READWRITE);

        if (R_FAILED(res))
        {
            Log("  svcControlMemory size 0x%08X -> 失敗 res=%08X", size, (u32)res);
            return;
        }

        ReportBlock("svcLinear ", (void *)addr, size);

        u32 dummy = 0;
        svcControlMemory(&dummy, addr, 0, size, MEMOP_FREE, (MemPerm)0);
    }

    static void RunTest(void)
    {
        g_log.clear();

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
