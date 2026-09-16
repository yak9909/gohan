// ============================================================================
// しずえスキップ（DOCS/skip_isabelle.md）。
// ============================================================================
//
// Framework_ProcOneFrame の入口へ CTRPF の通常フックを置き、ゲームの
// メインスレッド上でだけ実行する。対象は部屋 0x63、メニュー適用値が ON、
// かつ 30 フレーム連続して必要ポインタが解決できた場合に限る。
//
// F-361 の実機成功版は ModulePlSelect.cro の case 1 だけを再現していた。
// この常設版は、その直前に case 0 が無引数で呼ぶ sub_30C994 を実行する。
// sub_30C994 は Save_GetTownBase() から対象を自力取得し、0x00956231 の
// boot-once ガードを介して一括処理を行う（本体の独立した呼出元も同じ契約）。

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstring>

#include "ShizueSkip.hpp"
#include "GuiMenu.hpp"

namespace CTRPluginFramework
{
    namespace ShizueSkip
    {
        extern "C" void ShizueSkipCallback(void);
        namespace
        {
            const u32   kFrameAddr = 0x0054C424; // Framework_ProcOneFrame
            const u32   kFrameOrig = 0xE92D4070; // PUSH {R4-R6,LR}
            const u32   kRoomIdAddr = 0x0095133A; // g_CurrentRoomId
            const u32   kPrepareDoneAddr = 0x00956231;
            const u8    kTargetRoom = 0x63;
            const int   kStableNeed = 30;

            typedef void *(*GuardFn)(void);
            typedef int (*GardenFn)(void);
            typedef int (*PlayerIndexFn)(int, void *);
            typedef int (*RoomdatFn)(void);
            typedef void (*PrepareFn)(void);
            typedef void (*UseExitFn)(int, int, int, int, int, int);

            Hook        g_hook;
            bool        s_fired = false;
            int         s_index = -2;
            int         s_stable = 0;

            int     ShizueIndex(void)
            {
                const int n = GuiMenu::ItemCount();

                if (n <= 0)
                    return -1;
                if (s_index >= 0 && s_index < n
                    && std::strcmp(GuiMenu::ItemLabel(s_index),
                                   u8"しずえスキップ") == 0)
                    return s_index;
                int i = 0;

                while (i < n)
                {
                    if (std::strcmp(GuiMenu::ItemLabel(i),
                                    u8"しずえスキップ") == 0)
                    {
                        s_index = i;
                        return i;
                    }
                    i++;
                }
                s_index = -1;
                return -1;
            }

            bool    IsArmed(void)
            {
                const int i = ShizueIndex();

                return i >= 0 && GuiMenu::ItemApplied(i) != 0;
            }

            bool    ResolveExit(int &mgr, int &idx)
            {
                void *const p = ((GuardFn)0x002FB900)();

                if (p == nullptr)
                    return false;
                const int base = ((GardenFn)0x002FB354)() + 0xA0;

                idx = ((PlayerIndexFn)0x00309F18)(base, p);
                if (idx < 0 || idx > 3)
                    return false;
                mgr = ((RoomdatFn)0x00308110)();
                return mgr != 0;
            }

            void    Fire(void)
            {
                int mgr = 0;
                int idx = -1;

                // 一括処理を早すぎる時点で呼ばないため、case 1 と同じ材料が
                // 全て揃っていることを先に確認する。
                if (!ResolveExit(mgr, idx))
                    return;

                // case 0 と同じ無引数呼出し。既に自然経路で済んでいれば
                // boot-once ガードにより即座に戻る。
                if (*(volatile u8 *)kPrepareDoneAddr == 0)
                    ((PrepareFn)0x0030C994)();
                if (*(volatile u8 *)kPrepareDoneAddr == 0)
                    return;

                // 一括処理の後でポインタとプレイヤー番号を取り直す。
                if (!ResolveExit(mgr, idx))
                    return;
                ((UseExitFn)0x005B464C)(mgr, idx, 1, 0, 1, 1);
                s_fired = true;
                GuiMenu::Notify(u8"しずえスキップ", u8"村へ移動しました");
            }

            void    OnFrame(void)
            {
                if (*(volatile u8 *)kRoomIdAddr != kTargetRoom)
                {
                    s_fired = false;
                    s_stable = 0;
                    return;
                }
                if (!IsArmed() || s_fired)
                {
                    s_stable = 0;
                    return;
                }
                if (s_stable < kStableNeed)
                {
                    s_stable++;
                    return;
                }
                Fire();
            }
        }

        // ASMからのみ呼ばれる。ゲームのR0をHookContextとして受け取らない。
        extern "C" __attribute__((noinline, used)) void ShizueSkipTick(void)
        {
            OnFrame();
        }

        bool    Install(void)
        {
            if (g_hook.IsEnabled())
                return true;
            u32 cur = 0;

            Process::Read32(kFrameAddr, cur);
            if (cur != kFrameOrig)
                return false;
            s_fired = false;
            s_stable = 0;
            s_index = -2;
            // 通常HookはC++ callback前後の引数レジスタを保持しない。
            // ASMラッパーで保持してからOnFrameを呼ぶ（F-393/F-394）。
            g_hook.Initialize(kFrameAddr, (u32)ShizueSkipCallback);
            return g_hook.Enable() == HookResult::Success;
        }

        void    Uninstall(void)
        {
            if (g_hook.IsEnabled())
                g_hook.Disable();
            s_fired = false;
            s_stable = 0;
            s_index = -2;
        }

        bool    IsHooked(void)
        {
            return g_hook.IsEnabled();
        }
    }
}
