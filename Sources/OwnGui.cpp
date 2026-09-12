// ============================================================================
// OwnGui — 自前 GUI の組み込み / 取り外し（基準仕様 v2 §5）
// ============================================================================
//
// v1 との違い
//   v1: ケーブ 6 本（Q/A1/A2/N/F/T）+ フック 3 本 + g_GfxAllocator から 0x4000
//   v2: ケーブ 3 本（A1/A2/N）      + フック 2 本 + nw::lyt ヒープから GuiV2::BorrowBytes()
//       描画はゲームの Layout_RecordPaneTree がやるので記録ケーブが要らない。
//
// ★ケーブは Includes/GuiCavesV2.h のバイト列をそのまま書く。C++ へ書き直さない。
//   書き直した瞬間に audit_node_cave.py / verify_literals.py / IDA の検証が無効になる。
//
// ★アロケータは **ゲームのスレッド（フック内）から呼ぶ**（DOCS/ctrpf_plugin.md 3 節）。
//   プラグインのスレッドから直接呼んではいけない。だから確保も解放もケーブ経由。

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "csvc.h"   // svcInvalidateEntireInstructionCache

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "OwnGui.hpp"
#include "GuiV2.hpp"
#include "GuiMenu.hpp"
#include "ShizueSkip.hpp"
#include "GuiCavesV2.h"

namespace CTRPluginFramework
{
    namespace
    {
        bool        g_enabled = false;
        std::string g_log;

        void    Log(const char *fmt, ...)
        {
            char    buf[256];
            va_list ap;

            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            g_log += buf;
            g_log += "\n";
        }

        void    LogFlush(void)
        {
            File    f;

            if (File::Open(f, "/gohan_owngui.txt",
                           File::RWC | File::TRUNCATE | File::SYNC) != File::SUCCESS)
                return;
            f.Write(g_log.c_str(), g_log.size());
            f.Close();
        }

        inline u32  R32(u32 a) { return *(volatile u32 *)a; }

        u32     MakeBranch(u32 at, u32 target)
        {
            return 0xEA000000 | (((target - (at + 8)) >> 2) & 0x00FFFFFF);
        }

        bool    WriteWords(u32 base, const u32 *words, u32 count)
        {
            u32 i = 0;

            while (i < count)
            {
                if (!Process::Patch(base + i * 4, words[i]))
                    return false;
                i++;
            }
            return true;
        }

        void    ZeroWords(u32 base, u32 count)
        {
            u32 i = 0;

            while (i < count)
            {
                Process::Patch(base + i * 4, (u32)0);
                i++;
            }
        }

        // ------------------------------------------------------------------
        // ヒープの残量を測る（基準仕様 v2 §4.7 / F-309）
        //   `getFreeSize()` 0x0074D744 の算法をそのまま写す。
        //   ★欄の対応は vtable の取得子を逆アセンブルして確定したもの。
        // ------------------------------------------------------------------
        bool    HeapFreeSize(u32 &total, u32 &biggest, u32 &freeCount)
        {
            const u32   alloc = R32(kLytAllocator);
            u32         heap, bias, sentinel, node;
            u32         n = 0;

            total = biggest = freeCount = 0;
            if (alloc == 0)
                return false;
            heap = R32(alloc + kAllocatorHeapOff);
            if (heap == 0 || R32(heap) != kExpHeapVtable)
                return false;

            bias = R32(heap + kExpFreeBias);
            freeCount = R32(heap + kExpFreeCount);
            sentinel = heap + kExpFreeHead - bias;
            node = R32(heap + kExpFreeNext) - bias;
            while (node != sentinel && n < 64)
            {
                const u32 sz = R32(node + kExpBlkSize);

                total += sz;
                if (sz > biggest)
                    biggest = sz;
                node = R32(node + kExpBlkNext) - bias;
                n++;
            }
            return true;
        }

        // ------------------------------------------------------------------
        // 確保 / 解放（ケーブを Render_FrameEnd に 1 回だけ差し込む）
        // ------------------------------------------------------------------
        bool    RunHeapCave(const u32 *words, u32 count, u32 sizeIndex, u32 sizeValue,
                            bool waitFilled, const char *what)
        {
            u32 cur = 0;
            int tries;

            Process::Read32(kAllocHook, cur);
            if (cur != kAllocHookOrig)
            {
                Log("[!] %s: 0x%08X が素ではない (0x%08X)。中止。",
                    what, (unsigned int)kAllocHook, (unsigned int)cur);
                return false;
            }
            if (!WriteWords(kAllocCaveBase, words, count))
            {
                Log("[!] %s: ケーブを書けない。", what);
                return false;
            }
            if (sizeIndex != 0xFFFFFFFF)
                Process::Patch(kAllocCaveBase + sizeIndex * 4, sizeValue);
            svcInvalidateEntireInstructionCache();
            Process::Patch(kAllocHook, MakeBranch(kAllocHook, kAllocCaveBase));
            svcInvalidateEntireInstructionCache();

            tries = 0;
            cur = waitFilled ? 0 : 1;
            while (tries < 60)
            {
                Sleep(Milliseconds(50));
                Process::Read32(kGuiHeapSlot, cur);
                tries++;
                if (waitFilled ? (cur != 0) : (cur == 0))
                    break;
            }
            Process::Patch(kAllocHook, kAllocHookOrig);
            svcInvalidateEntireInstructionCache();
            ZeroWords(kAllocCaveBase, count);
            svcInvalidateEntireInstructionCache();

            if (waitFilled ? (cur == 0) : (cur != 0))
            {
                Log("[!] %s: %d 回待っても済まなかった（ゲームが進んでいない？）", what, tries);
                return false;
            }
            return true;
        }

        bool    BorrowHeap(u32 size)
        {
            u32 cur = 0;
            u32 total = 0, biggest = 0, cnt = 0;

            Process::Read32(kGuiHeapSlot, cur);
            if (cur != 0)
            {
                Log("ヒープは既に借りてある: 0x%08X (0x%X)",
                    (unsigned int)cur, (unsigned int)R32(kGuiHeapSlot + 4));
                return true;
            }

            // ★§4.7 の安全弁。空きが 1 個しかない場面があるので、取り切らない。
            if (HeapFreeSize(total, biggest, cnt))
            {
                Log("nw::lyt ヒープ 空き %u B / 最大ブロック %u B / 空き %u 個",
                    (unsigned int)total, (unsigned int)biggest, (unsigned int)cnt);
                if (biggest < size)
                {
                    Log("[!] 最大ブロック %u B < 借りたい %u B。中止。",
                        (unsigned int)biggest, (unsigned int)size);
                    return false;
                }
                if (size > kGuiWorstFree)
                {
                    Log("[!] %u B は最悪時の配分 %u B を超える。中止。",
                        (unsigned int)size, (unsigned int)kGuiWorstFree);
                    return false;
                }
            }
            else
            {
                Log("[!] ヒープを読めない（sead::ExpHeap ではない？）。中止。");
                return false;
            }

            if (!RunHeapCave(kAllocCave, kAllocCaveCount, kAllocSizeIndex, size,
                             true, "確保"))
                return false;
            Process::Read32(kGuiHeapSlot, cur);
            Log("借用 0x%08X (0x%X)", (unsigned int)cur, (unsigned int)size);
            return cur != 0;
        }

        // ------------------------------------------------------------------
        // コマンドリストを削除する（F-317）
        // ------------------------------------------------------------------
        //   これを入れないと **1 回の組み込みごとに 132,180 B が nngx ヒープに残り、
        //   ON/OFF を 9 回で使い切る**（実機で計測）。
        //   nw::lyt の借用は返しているので戻るが、コマンドリストは別のヒープで、
        //   `.bss` をゼロにすると list id が失われて削除できなくなる。
        //
        //   ★★A1/A2 のフックを**外してから**呼ぶこと。
        //     外す前に削除すると、次のフレームで A1/A2 が作り直す。
        //   ★束縛中のリストを消すと g_GpuCmdPtr / End / Mgr が 0 にされるので、
        //     ケーブ側で GpuCmd_GetProperty(519) と比べて守っている。
        bool    DeleteCmdLists(void)
        {
            u32 cur = 0;
            int tries;

            Process::Read32(kGuiHooks[0].addr, cur);
            if (cur != kGuiHooks[0].orig)
            {
                Log("[!] 削除: A1 のフックがまだ生きている。先に外すこと。");
                return false;
            }
            if (R32(kGuiNodeTop + 0x100) == 0 && R32(kGuiNodeBot + 0x100) == 0)
            {
                Log("削除: リストは作られていない（何もしない）");
                return true;
            }
            Process::Read32(kAllocHook, cur);
            if (cur != kAllocHookOrig)
            {
                Log("[!] 削除: 0x%08X が素ではない (0x%08X)",
                    (unsigned int)kAllocHook, (unsigned int)cur);
                return false;
            }
            Log("削除前の list id: 上=%u 下=%u",
                (unsigned int)R32(kGuiNodeTop + 0x100),
                (unsigned int)R32(kGuiNodeBot + 0x100));

            *(volatile u32 *)kDelDoneFlag = 0;
            if (!WriteWords(kAllocCaveBase, kDelCave, kDelCaveCount))
            {
                Log("[!] 削除: ケーブを書けない。");
                return false;
            }
            svcInvalidateEntireInstructionCache();
            Process::Patch(kAllocHook, MakeBranch(kAllocHook, kAllocCaveBase));
            svcInvalidateEntireInstructionCache();

            tries = 0;
            while (tries < 60 && *(volatile u32 *)kDelDoneFlag == 0)
            {
                Sleep(Milliseconds(50));
                tries++;
            }
            Process::Patch(kAllocHook, kAllocHookOrig);
            svcInvalidateEntireInstructionCache();
            ZeroWords(kAllocCaveBase, kDelCaveCount);
            svcInvalidateEntireInstructionCache();

            const bool done = (*(volatile u32 *)kDelDoneFlag != 0);
            const u32  idT = R32(kGuiNodeTop + 0x100);
            const u32  idB = R32(kGuiNodeBot + 0x100);

            if (!done)
                Log("[!] 削除: %d 回待っても走らなかった（ゲームが進んでいない？）", tries);
            else if (idT != 0 || idB != 0)
                Log("[!] 削除: id が残っている 上=%u 下=%u（束縛中で見送られた？）",
                    (unsigned int)idT, (unsigned int)idB);
            else
                Log("コマンドリストを削除した（%d 回待った）", tries);
            return done && idT == 0 && idB == 0;
        }

        bool    ReturnHeap(void)
        {
            u32 cur = 0;

            Process::Read32(kGuiHeapSlot, cur);
            if (cur == 0)
                return true;
            return RunHeapCave(kFreeCave, kFreeCaveCount, 0xFFFFFFFF, 0, false, "解放");
        }

        // ------------------------------------------------------------------
        // ケーブ / .bss / フック
        // ------------------------------------------------------------------
        bool    WriteCaves(void)
        {
            u32 c = 0;

            while (c < kGuiCaveCount)
            {
                const GuiCave  &cv = kGuiCaves[c];
                u32             i = 0;

                while (i < cv.count)
                {
                    u32 v = cv.words[i];

                    // CAVE_N はテクスチャのキャッシュを吐き出す。
                    // v2 のアトラスは借りたヒープの中なので、その 2 語を差し替える。
                    if (std::strcmp(cv.name, "N") == 0)
                    {
                        if (i == kGuiCaveNTexVaIndex)
                            v = GuiV2::AtlasVa();
                        else if (i == kGuiCaveNTexLenIndex)
                            v = GuiV2::AtlasBytes();
                    }
                    // ★記録コマンドリストの大きさを画面ごとに焼き直す（F-313）。
                    //   ケーブの既定は 0x8000 だが、上画面は表示物が多い。
                    //   ゲーム自身も 0x10000〜0x18000 を渡している。
                    else if (i == kGuiListSizeIndex)
                    {
                        if (std::strcmp(cv.name, "A1") == 0)
                            v = kGuiListSizeTop;
                        else if (std::strcmp(cv.name, "A2") == 0)
                            v = kGuiListSizeBot;
                    }
                    if (!Process::Patch(cv.base + i * 4, v))
                    {
                        Log("[!] ケーブ %s を書けない (0x%08X)",
                            cv.name, (unsigned int)(cv.base + i * 4));
                        return false;
                    }
                    i++;
                }
                Log("ケーブ %-2s 0x%08X  %u 語", cv.name,
                    (unsigned int)cv.base, (unsigned int)cv.count);
                c++;
            }
            svcInvalidateEntireInstructionCache();
            return true;
        }

        void    InitBss(void)
        {
            u32 i = 0;

            std::memset((void *)kGuiLog, 0, kGuiLogWords * 4);
            std::memset((void *)kGuiNodeTop, 0, kGuiNodeLen);
            std::memset((void *)kGuiNodeBot, 0, kGuiNodeLen);
            std::memset((void *)kGuiVtbl, 0, 0x40);

            // 自前 vtable（基準仕様 v2 §1）
            //   ★既定を安全スタブにしてから、必要な 3 枠だけゲームの関数へ向ける。
            while (i < 16)
            {
                *(volatile u32 *)(kGuiVtbl + i * 4) = kGuiVtblStub;
                i++;
            }
            *(volatile u32 *)(kGuiVtbl + 0x08) = kGuiVtblReplay;
            *(volatile u32 *)(kGuiVtbl + 0x14) = kGuiVtblRecord;
            *(volatile u32 *)(kGuiVtbl + 0x18) = kGuiVtblSetPrj;
        }

        // ------------------------------------------------------------------
        // ★ゲーム側の入力を塞ぐケーブ（F-350）
        //   `sead::Controller::calc` の `BL sub_542E88` を差し替える。
        //   生読みの直後・edge 計算の直前で hold とタッチ旗を削るので、
        //   幻の押下・離しが出ない。
        //   ★これは A1/A2 とは別枠（フックが B ではなく BL）。
        // ------------------------------------------------------------------
        bool    InstallInputBlock(void)
        {
            u32 cur = 0;
            u32 i = 0;

            // 置き場が空か
            while (i < kGuiInputCaveCount)
            {
                Process::Read32(kGuiInputCaveBase + i * 4, cur);
                if (cur != 0)
                {
                    Log("[!] 入力遮断ケーブの置き場 0x%08X が空でない (0x%08X)",
                        (unsigned int)(kGuiInputCaveBase + i * 4), (unsigned int)cur);
                    return false;
                }
                i++;
            }
            // フックが素か
            Process::Read32(kGuiInputHookAddr, cur);
            if (cur != kGuiInputHookOrig)
            {
                Log("[!] 0x%08X が素ではない (0x%08X)。入力遮断は入れない。",
                    (unsigned int)kGuiInputHookAddr, (unsigned int)cur);
                return false;
            }
            // ★ケーブ B の置き場とフックも同じ検査を通す
            i = 0;
            while (i < kGuiInputCaveBCount)
            {
                Process::Read32(kGuiInputCaveBBase + i * 4, cur);
                if (cur != 0)
                {
                    Log("[!] 入力遮断ケーブ B の置き場 0x%08X が空でない (0x%08X)",
                        (unsigned int)(kGuiInputCaveBBase + i * 4), (unsigned int)cur);
                    return false;
                }
                i++;
            }
            Process::Read32(kGuiInputHookBAddr, cur);
            if (cur != kGuiInputHookBOrig)
            {
                Log("[!] 0x%08X が素ではない (0x%08X)。入力遮断は入れない。",
                    (unsigned int)kGuiInputHookBAddr, (unsigned int)cur);
                return false;
            }

            // 制御ブロックを初期化してからケーブを書く
            //   +0 ボタン遮断 / +1 タッチ遮断 / +2 SELECT 遮断（★常に 1）
            *(volatile u8 *)(kGuiInputCtl + 0) = 0;
            *(volatile u8 *)(kGuiInputCtl + 1) = 0;
            *(volatile u8 *)(kGuiInputCtl + 2) = 1;
            i = 0;
            while (i < kGuiInputCaveCount)
            {
                Process::Patch(kGuiInputCaveBase + i * 4, kGuiInputCave[i]);
                i++;
            }
            i = 0;
            while (i < kGuiInputCaveBCount)
            {
                Process::Patch(kGuiInputCaveBBase + i * 4, kGuiInputCaveB[i]);
                i++;
            }
            svcInvalidateEntireInstructionCache();
            // 書き戻し照合（★繋ぐ前に必ず見る）
            i = 0;
            while (i < kGuiInputCaveCount)
            {
                Process::Read32(kGuiInputCaveBase + i * 4, cur);
                if (cur != kGuiInputCave[i])
                {
                    Log("[!] 入力遮断ケーブの書き戻しが違う（語 %d）", (int)i);
                    return false;
                }
                i++;
            }
            i = 0;
            while (i < kGuiInputCaveBCount)
            {
                Process::Read32(kGuiInputCaveBBase + i * 4, cur);
                if (cur != kGuiInputCaveB[i])
                {
                    Log("[!] 入力遮断ケーブ B の書き戻しが違う（語 %d）", (int)i);
                    return false;
                }
                i++;
            }
            Process::Patch(kGuiInputHookAddr, kGuiInputHookBl);
            Process::Patch(kGuiInputHookBAddr, kGuiInputHookBBl);
            svcInvalidateEntireInstructionCache();
            Log("入力遮断 A 0x%08X %d 語 / フック 0x%08X <- 0x%08X",
                (unsigned int)kGuiInputCaveBase, (int)kGuiInputCaveCount,
                (unsigned int)kGuiInputHookAddr, (unsigned int)kGuiInputHookBl);
            Log("入力遮断 B 0x%08X %d 語 / フック 0x%08X <- 0x%08X （SELECT）",
                (unsigned int)kGuiInputCaveBBase, (int)kGuiInputCaveBCount,
                (unsigned int)kGuiInputHookBAddr, (unsigned int)kGuiInputHookBBl);
            return true;
        }

        void    RemoveInputBlock(void)
        {
            u32 i = 0;

            // ★フックを先に戻す。戻す前にケーブを消すと未定義命令へ落ちる。
            Process::Patch(kGuiInputHookAddr, kGuiInputHookOrig);
            Process::Patch(kGuiInputHookBAddr, kGuiInputHookBOrig);
            svcInvalidateEntireInstructionCache();
            *(volatile u8 *)(kGuiInputCtl + 0) = 0;
            *(volatile u8 *)(kGuiInputCtl + 1) = 0;
            *(volatile u8 *)(kGuiInputCtl + 2) = 0;
            while (i < kGuiInputCaveCount)
            {
                Process::Patch(kGuiInputCaveBase + i * 4, 0);
                i++;
            }
            i = 0;
            while (i < kGuiInputCaveBCount)
            {
                Process::Patch(kGuiInputCaveBBase + i * 4, 0);
                i++;
            }
            svcInvalidateEntireInstructionCache();
        }

        bool    InstallHooks(void)
        {
            u32 i = 0;
            u32 cur = 0;

            while (i < kGuiHookCount)
            {
                Process::Read32(kGuiHooks[i].addr, cur);
                if (cur != kGuiHooks[i].orig)
                {
                    Log("[!] 0x%08X が素ではない (0x%08X)。中止。",
                        (unsigned int)kGuiHooks[i].addr, (unsigned int)cur);
                    return false;
                }
                i++;
            }
            i = 0;
            while (i < kGuiHookCount)
            {
                Process::Patch(kGuiHooks[i].addr, kGuiHooks[i].branch);
                Log("フック %-12s 0x%08X <- 0x%08X", kGuiHooks[i].name,
                    (unsigned int)kGuiHooks[i].addr, (unsigned int)kGuiHooks[i].branch);
                i++;
            }
            svcInvalidateEntireInstructionCache();
            return true;
        }

        void    RemoveHooks(void)
        {
            u32 i = 0;

            while (i < kGuiHookCount)
            {
                Process::Patch(kGuiHooks[i].addr, kGuiHooks[i].orig);
                i++;
            }
            svcInvalidateEntireInstructionCache();
        }

        // ------------------------------------------------------------------
        bool    Enable(void)
        {
            if (g_enabled)
            {
                Log("[!] Enable(): 既に有効。二重実行を阻止。");
                return true;
            }
            g_log.clear();
            Log("=== 自前 GUI を組み込む（基準仕様 v2）===");

            // 1. ヒープを借りる
            // ★大きさは GuiV2 が持つ（以前ここに 0x30000 が直書きされていて、
            //   GuiV2 の kBorrowBytes と食い違っていた）。
            if (!BorrowHeap(GuiV2::BorrowBytes()))
            {
                LogFlush();
                return false;
            }

            // 2. .bss を初期化する
            //    ★★★順序が命（F-311 で実際に踏んだ）。
            //      初版はここが GuiV2::Install() の**あと**にあった。
            //      InitBss() はノード 0x140 バイトを memset するので、
            //      Install() が書いた `node+0x20`(root Pane) と `node+0x30`(DrawInfo) を
            //      消してしまい、**記録長が 0 のまま何も描かれなかった**
            //      （基準仕様 v2 §5.2 の detach と同じ状態）。
            //      .bss の初期化は必ず Install() の前に済ませること。
            InitBss();
            Log(".bss 初期化 完了（vtable +0x08/+0x14/+0x18 をゲームの関数へ）");

            // 3. 借りた中身を組む（Picture / フォント資源 / TextBox / root Pane）
            if (!GuiV2::Install())
            {
                Log("[!] GuiV2::Install 失敗: %s", GuiV2::LastError());
                GuiV2::DumpLog();
                ReturnHeap();
                LogFlush();
                return false;
            }
            Log("アトラス 0x%08X (%u B)", (unsigned int)GuiV2::AtlasVa(),
                (unsigned int)GuiV2::AtlasBytes());

            // 4. ケーブ（アトラス番地が決まってから）
            if (!WriteCaves())
            {
                GuiV2::Uninstall();
                ReturnHeap();
                LogFlush();
                return false;
            }

            // 5. ★ノードの必須欄が本当に入っているかを確かめる（同じ事故を二度と起こさない）
            {
                const u32 rt = R32(kGuiNodeTop + 0x20);
                const u32 rb = R32(kGuiNodeBot + 0x20);

                Log("node TOP +0x20=0x%08X +0x30=0x%08X / BOT +0x20=0x%08X +0x30=0x%08X",
                    (unsigned int)rt, (unsigned int)R32(kGuiNodeTop + 0x30),
                    (unsigned int)rb, (unsigned int)R32(kGuiNodeBot + 0x30));
                if (rt != kGuiRootTop || rb != kGuiRootBot)
                {
                    Log("[!] root Pane が入っていない。これでは記録長が 0 のままになる。中止。");
                    GuiV2::Uninstall();
                    ReturnHeap();
                    LogFlush();
                    return false;
                }
            }

            // 表が空のままだと何も描かれないので、まず一度組む。
            GuiMenu::Redraw();

            // 6. フック（★最後。これが入った瞬間からノードが登録される）
            if (!InstallHooks())
            {
                // ★途中で失敗してもケーブは残す。
                //   生きているフックがゼロコードを実行すると死ぬ（F-284）。
                GuiV2::Uninstall();
                LogFlush();
                return false;
            }
            g_enabled = true;

            // 7. ★入力遮断（F-350）。失敗しても GUI は続ける（SELECT が通るだけ）。
            if (!InstallInputBlock())
                Log("[!] 入力遮断を入れられなかった。ゲーム側のボタンは素のまま。");

            // 1 フレーム待って、ノードが本当に記録できたかを見る。
            Sleep(Milliseconds(120));
            {
                const u32 idTop = R32(kGuiNodeTop + 0x100);
                const u32 idBot = R32(kGuiNodeBot + 0x100);
                const u32 lenTop = R32(kGuiNodeTop + 0x108);
                const u32 lenBot = R32(kGuiNodeBot + 0x108);

                Log("上ノード list=%u 記録長=%u / %u B (%u%%)",
                    (unsigned int)idTop, (unsigned int)lenTop,
                    (unsigned int)kGuiListSizeTop,
                    (unsigned int)(lenTop * 100 / kGuiListSizeTop));
                Log("下ノード list=%u 記録長=%u / %u B (%u%%)",
                    (unsigned int)idBot, (unsigned int)lenBot,
                    (unsigned int)kGuiListSizeBot,
                    (unsigned int)(lenBot * 100 / kGuiListSizeBot));
                if (idTop == 0 || idBot == 0)
                    Log("[!] コマンドリストを作れていない。LIST_SIZE(0x%X) が大きすぎる疑い。",
                        (unsigned int)kGuiListSize);
                if (lenTop * 100 / kGuiListSize > 80 || lenBot * 100 / kGuiListSize > 80)
                    Log("[!] 記録長が LIST_SIZE の 8 割を超えている。"
                        "★溢れは無検査なので、これ以上増やしてはいけない。");
            }
            Log("組み込み完了。");
            GuiV2::DumpLog();
            LogFlush();
            return true;
        }

        void    Disable(void)
        {
            int waits;

            if (!g_enabled)
                return;

            // ★§5.3 の順序。1 つでも入れ替えると死ぬか漏れる。
            //   (1) 描画を止める（root Pane = 0、記録要求を立てる）
            //   (2) 記録長が 0 になるのを待つ  … これが「ゲームが進んでいる」証明にもなる
            //   (3) ★A1/A2 のフックを戻す（先に外さないと (4) で作り直される）
            //   (4) 100ms 待つ
            //   (5) ★コマンドリストを削除する（F-317。.bss を消す前でないと id が失われる）
            //   (6) ヒープを返す
            //   (7) .bss とケーブをゼロ埋め
            GuiV2::Uninstall();

            waits = 0;
            while (waits < 40)
            {
                if (R32(kGuiNodeTop + 0x108) == 0 && R32(kGuiNodeBot + 0x108) == 0)
                    break;
                Sleep(Milliseconds(50));
                waits++;
            }
            const bool  drained = (R32(kGuiNodeTop + 0x108) == 0
                                   && R32(kGuiNodeBot + 0x108) == 0);

            if (!drained)
                Log("[!] Disable: 記録長が 0 にならない (Pause 中?)。ケーブは残す。");
            else
                Log("記録長が上下とも 0 になった（%d 回待った）", waits);

            // ★入力遮断を先に外す（フックを戻してからケーブを消す順序は関数内で守る）
            RemoveInputBlock();

            // (3)(4) 先にフックを外し、走行中のスレッドがケーブから抜けるのを待つ
            ShizueSkip::Uninstall();
            RemoveHooks();
            Sleep(Milliseconds(100));

            // (5) コマンドリストを削除（★.bss をゼロにする前）
            bool listsGone = false;

            if (drained)
                listsGone = DeleteCmdLists();
            if (!listsGone)
                Log("[!] Disable: コマンドリストを削除できなかった。"
                    "nngx ヒープに 132KB 残る（再起動で戻る）。");

            // (6) ヒープを返す
            bool freed = false;

            if (drained)
                freed = ReturnHeap();
            if (!freed)
                Log("[!] Disable: ヒープを返せなかった。スロットは 0 にしない。");

            std::memset((void *)kGuiLog, 0, kGuiLogWords * 4);
            std::memset((void *)kGuiNodeTop, 0, kGuiNodeLen);
            std::memset((void *)kGuiNodeBot, 0, kGuiNodeLen);
            std::memset((void *)kGuiVtbl, 0, 0x40);
            std::memset((void *)kGuiRootTop, 0, kGuiRootLen);
            std::memset((void *)kGuiRootBot, 0, kGuiRootLen);

            if (drained)
            {
                u32 c = 0;

                while (c < kGuiCaveCount)
                {
                    ZeroWords(kGuiCaves[c].base, kGuiCaves[c].count);
                    c++;
                }
                svcInvalidateEntireInstructionCache();
            }
            g_enabled = false;
            LogFlush();
        }
    }

    void    OwnGuiToggle(MenuEntry *entry)
    {
        if (!entry->WasJustActivated())
            return;
        if (entry->IsActivated())
        {
            if (Enable())
            {
                GuiMenu::Initialize();
                if (!ShizueSkip::Install())
                    Log("[!] しずえスキップのフックを掛けられなかった（素でない）。");
            }
            else
                entry->Disable();
        }
        else
        {
            GuiMenu::Shutdown();
            Disable();
        }
    }

    void    OwnGuiShutdown(void)
    {
        Disable();
    }

    bool    OwnGuiIsEnabled(void)
    {
        return g_enabled;
    }
}
