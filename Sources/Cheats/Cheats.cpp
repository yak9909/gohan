// ============================================================================
// Cheats — gohan.md の PatchList 型チート・天気・木／3x3 のフック
// ============================================================================
//
// 番地と値は gohan.md の各 PatchList 表（JPN 無印＋更新版）の写し。
// OFF 値は artifacts/update/exefs/code.bin の元の命令。
// 効果の ON/OFF は GuiMenu の規則（gohan-menu.md §4.3）で駆動され、通知もそちらが出す。

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Cheats.hpp"
#include "FieldHookCaves.h"
#include "BuildingEditor.hpp"
#include "GridCursor.hpp"
#include "ModelViewer.hpp"
#include "PublicWorks.hpp"
#include "GuiMenu.hpp"

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            typedef GuiMenu::TogglePatch Patch;

            #define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

            // ---- 壁抜け（gohan.md §3。実機確認済みの 3 語）----
            const Patch kWallPatches[] = {
                { 0x0064F03C, 4, 0xE3A07000, 0xE3A07001 },
                { 0x0064F19C, 4, 0xE1A00000, 0xED841A05 },
                { 0x0064F1B4, 4, 0xE1A00000, 0xED840A07 },
            };
            // ---- 花散らせない ----
            const Patch kFlowerPatches[] = {
                { 0x00596890, 4, 0xE3A0001D, 0xEBF5990F },
            };
            // ---- 穴に落下しない ----
            const Patch kTrapPatches[] = {
                { 0x00659160, 4, 0xEA000014, 0x1A000014 },
                { 0x006774DC, 4, 0xEA00002D, 0x1A00002D },
            };
            // ---- 寝癖付かない ----
            const Patch kBedHeadPatches[] = {
                { 0x0020C6D8, 4, 0xE1A00000, 0xE5C41004 },
            };
            // ---- 歩いた場所のアイテムを消し去る ----
            const Patch kTramplePatches[] = {
                { 0x00596920, 4, 0xE1A00000, 0x0A00002A },
                { 0x00596870, 4, 0xE1A00000, 0x0A000056 },
                { 0x0059689C, 4, 0xE1A00000, 0x0A00004B },
                { 0x005968D0, 4, 0xE1A00000, 0x1A000001 },
                { 0x005968D8, 4, 0xE1A00000, 0x1A00003C },
                { 0x005968E4, 4, 0xE1A00000, 0x0A000039 },
            };
            // ---- どこでも掘れる（9 語目 0x005982C4 は利用者指示で有効）----
            const Patch kDigPatches[] = {
                { 0x00595B00, 4, 0xE320F000, 0x0A00006F },
                { 0x00598294, 4, 0xEA000095, 0x0A000095 },
                { 0x00598478, 4, 0xEA00001C, 0x0A000042 },
                { 0x005984EC, 4, 0xE320F000, 0x0A000019 },
                { 0x00598550, 4, 0xE320F000, 0x13A0401A },
                { 0x00662E88, 4, 0xE320F000, 0x0A000150 },
                { 0x00663294, 4, 0xEA000028, 0x0A000028 },
                { 0x00680F10, 4, 0xE320F000, 0x0A00000A },
                { 0x005982C4, 4, 0xEA000017, 0x0A000017 },
            };
            // ---- 空を見上げない ----
            const Patch kLookUpPatches[] = {
                { 0x0064C594, 4, 0xEAFFFFDE, 0xE5D4191A },
            };
            // ---- 店24時間オープン ----
            const Patch kShopPatches[] = {
                { 0x003093BC, 4, 0xE3A00001, 0xE3A00000 },
                { 0x007102C8, 4, 0xE3A00001, 0xE3A00000 },
                { 0x00710380, 4, 0xE3A00001, 0xE3A00000 },
                { 0x00712664, 4, 0xE3A00001, 0xE3A00000 },
                { 0x0071BBE0, 4, 0xE3A00001, 0xE3A00000 },
                { 0x00710000, 4, 0xE3A00001, 0xE3A00000 },
                { 0x0071DE08, 4, 0xE3A00001, 0xE3A00000 },
                { 0x0071684C, 4, 0xE3A00001, 0xE3A00000 },
                { 0x00716BF8, 4, 0xE3A00001, 0xE3A00000 },
            };
            // ---- メッセージ即表示 ----
            const Patch kTextPatches[] = {
                { 0x005F8278, 4, 0xE3A00001, 0xE5D400CA },
            };
            // ---- フレームレート制限解除 ----
            const Patch kFpsPatches[] = {
                { 0x0054C6E8, 4, 0xE3E004FF, 0xE59400A0 },
            };

            struct PatchCheat
            {
                const char  *label;
                const Patch *patches;
                int          count;
                int          index;
            };

            PatchCheat g_patchCheats[] = {
                { kWalkThroughWalls, kWallPatches,    COUNT(kWallPatches),    -1 },
                { kNoBreakFlower,    kFlowerPatches,  COUNT(kFlowerPatches),  -1 },
                { kNoTrap,           kTrapPatches,    COUNT(kTrapPatches),    -1 },
                { kNoBedHead,        kBedHeadPatches, COUNT(kBedHeadPatches), -1 },
                { kTrampler,         kTramplePatches, COUNT(kTramplePatches), -1 },
                { kDigAnywhere,      kDigPatches,     COUNT(kDigPatches),     -1 },
                { kNoLookUp,         kLookUpPatches,  COUNT(kLookUpPatches),  -1 },
                { kShopsOpen,        kShopPatches,    COUNT(kShopPatches),    -1 },
                { kInstantText,      kTextPatches,    COUNT(kTextPatches),    -1 },
                { kUnlockFps,        kFpsPatches,     COUNT(kFpsPatches),     -1 },
            };

            PatchCheat *FindPatchCheat(int index)
            {
                for (int i = 0; i < COUNT(g_patchCheats); i++)
                    if (g_patchCheats[i].index == index)
                        return &g_patchCheats[i];
                return nullptr;
            }

            bool    PatchIsActive(int index)
            {
                const PatchCheat *c = FindPatchCheat(index);

                return c != nullptr && GuiMenu::PatchListIsActive(c->patches, c->count);
            }

            void    PatchSetActive(int index, bool active)
            {
                const PatchCheat *c = FindPatchCheat(index);

                if (c != nullptr)
                    GuiMenu::PatchListSetActive(c->patches, c->count, active);
            }

            const GuiMenu::ToggleEffectFuncs kPatchFuncs = { PatchIsActive, PatchSetActive };

            // ---- 木にぶつかって切り倒す / スコップ3x3マス掘り（gohan.md §17.6 / §17.7）----
            //   ON : ケーブを置く -> フックを書く
            //   OFF: フックを戻す。★ケーブは消さない（ゲームのスレッドが中を実行中かもしれない。
            //        フックを戻した後は到達しないので残しても害はない）
            int     g_treeIndex = -1;
            int     g_digIndex = -1;

            void    HookSet(const Patch *cave, int caveCount, const Patch *hooks, int hookCount, bool active)
            {
                if (active)
                    GuiMenu::PatchListSetActive(cave, caveCount, true);
                GuiMenu::PatchListSetActive(hooks, hookCount, active);
            }

            bool    HookIsActive(int index)
            {
                using namespace FieldHookCaves;

                if (index == g_treeIndex)
                    return GuiMenu::PatchListIsActive(kTreeHooks, COUNT(kTreeHooks))
                           && GuiMenu::PatchListIsActive(kTreeCave, COUNT(kTreeCave));
                if (index == g_digIndex)
                    return GuiMenu::PatchListIsActive(kDigHooks, COUNT(kDigHooks))
                           && GuiMenu::PatchListIsActive(kDigCave, COUNT(kDigCave));
                return false;
            }

            void    HookSetActive(int index, bool active)
            {
                using namespace FieldHookCaves;

                if (index == g_treeIndex)
                    HookSet(kTreeCave, COUNT(kTreeCave), kTreeHooks, COUNT(kTreeHooks), active);
                else if (index == g_digIndex)
                    HookSet(kDigCave, COUNT(kDigCave), kDigHooks, COUNT(kDigHooks), active);
            }

            const GuiMenu::ToggleEffectFuncs kHookFuncs = { HookIsActive, HookSetActive };

            // ---- 天気（gohan.md §17.8。連動型リスト）----
            const u32   kWeatherAddr = 0x0062E728;
            const u32   kWeatherOrig = 0xE1A00004;  // MOV R0,R4
            const u32   kWeatherMov  = 0xE3A00000;  // MOV R0,#n
            const u32   kWeatherMax  = 6;

            bool    WeatherRead(int index, s32 *value)
            {
                const u32 w = *(u32 *)kWeatherAddr;

                (void)index;
                if (w == kWeatherOrig)
                {
                    *value = 0;
                    return true;
                }
                if ((w & 0xFFFFFF00u) == kWeatherMov && (w & 0xFFu) <= kWeatherMax)
                {
                    *value = (s32)(w & 0xFFu) + 1;
                    return true;
                }
                return false;               // 想定外の命令（ほかの改造が当たっている）は触らない
            }

            void    WeatherWrite(int index, s32 value)
            {
                (void)index;
                if (value < 0 || value > (s32)kWeatherMax + 1)
                    return;
                *(u32 *)kWeatherAddr = value == 0 ? kWeatherOrig : kWeatherMov + (u32)(value - 1);
                GuiMenu::FlushMemory(kWeatherAddr, 4);
            }
        }

        namespace
        {
            // ToggleHandlers は大域に 1 組だけ。ここで受けて各チートへ回す。
            // 自分の項目でなければ偽を返す取り決めなので、並び順は結果に影響しない。
            void    DispatchTick(int index, u16 held)
            {
                if (PlayerMoveTick(index, held))
                    return;
                if (BuildingEditorTick(index, held))
                    return;
                GridCursorTick(index, held);
            }

            void    DispatchDisable(int index)
            {
                if (PlayerMoveDisable(index))
                    return;
                if (BuildingEditorDisable(index))
                    return;
                GridCursorDisable(index);
            }

            const GuiMenu::ToggleHandlers kDispatch = { nullptr, DispatchTick, DispatchDisable };
        }

        void    Wire(void)
        {
            for (int i = 0; i < COUNT(g_patchCheats); i++)
            {
                g_patchCheats[i].index = GuiMenu::FindItem(g_patchCheats[i].label);
                if (g_patchCheats[i].index >= 0)
                    GuiMenu::RegisterToggleEffect(g_patchCheats[i].index, &kPatchFuncs);
            }
            g_treeIndex = GuiMenu::FindItem(kFellTree);
            if (g_treeIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_treeIndex, &kHookFuncs);
            g_digIndex = GuiMenu::FindItem(kDig3x3);
            if (g_digIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_digIndex, &kHookFuncs);

            const int weather = GuiMenu::FindItem(kWeather);

            if (weather >= 0)
                GuiMenu::RegisterLinked(weather, WeatherRead, WeatherWrite);
            WirePlayerMove();
            WirePlayerResources();
            WireGridCursor();
            WireModelViewer();
            WirePublicWorks();
            WireBuildingEditor();
            // ★ResetState が ToggleHandlers を消すので、登録は全部の Wire のあと 1 回だけ。
            GuiMenu::SetToggleHandlers(&kDispatch);
        }
    }
}
