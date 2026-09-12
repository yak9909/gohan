#include <3ds.h>
#include "csvc.h"
#include "GuiNotification.hpp"
#include "LinearAllocTest.hpp"
#include "OwnGui.hpp"
#include "GuiMenu.hpp"
#include "GuiV2.hpp"
#include "ChatKanji.hpp"
#include <CTRPluginFramework.hpp>

#include <vector>

namespace CTRPluginFramework
{
    // This patch the NFC disabling the touchscreen when scanning an amiibo, which prevents ctrpf to be used
    static void    ToggleTouchscreenForceOn(void)
    {
        static u32 original = 0;
        static u32 *patchAddress = nullptr;

        if (patchAddress && original)
        {
            *patchAddress = original;
            return;
        }

        static const std::vector<u32> pattern =
        {
            0xE59F10C0, 0xE5840004, 0xE5841000, 0xE5DD0000,
            0xE5C40008, 0xE28DD03C, 0xE8BD80F0, 0xE5D51001,
            0xE1D400D4, 0xE3510003, 0x159F0034, 0x1A000003
        };

        Result  res;
        Handle  processHandle;
        s64     textTotalSize = 0;
        s64     startAddress = 0;
        u32 *   found;

        if (R_FAILED(svcOpenProcess(&processHandle, 16)))
            return;

        svcGetProcessInfo(&textTotalSize, processHandle, 0x10002);
        svcGetProcessInfo(&startAddress, processHandle, 0x10005);
        if(R_FAILED(svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, 0x14000000, processHandle, (u32)startAddress, textTotalSize)))
            goto exit;

        found = (u32 *)Utils::Search<u32>(0x14000000, (u32)textTotalSize, pattern);

        if (found != nullptr)
        {
            original = found[13];
            patchAddress = (u32 *)PA_FROM_VA((found + 13));
            found[13] = 0xE1A00000;
        }

        svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, 0x14000000, textTotalSize);
exit:
        svcCloseHandle(processHandle);
    }

    // This function is called before main and before the game starts
    // Useful to do code edits safely
    void    PatchProcess(FwkSettings &settings)
    {
        ToggleTouchscreenForceOn();
    }

    // This function is called when the process exits
    // Useful to save settings, undo patchs or clean up things
    void    OnProcessExit(void)
    {
        GuiMenu::Shutdown();
        ChatKanji::Shutdown();
        OwnGuiShutdown();
        GuiNotification::Shutdown();
        ToggleTouchscreenForceOn();
    }

    static void    NotificationDemo(MenuEntry *entry)
    {
        if (entry->WasJustActivated())
            GuiNotification::Show();
    }

    void    InitMenu(PluginMenu &menu)
    {
        // Create your entries here, or elsewhere
        // You can create your entries whenever/wherever you feel like it

        menu += new MenuEntry(
            u8"右下通知をテスト",
            NotificationDemo,
            u8"ONにすると、MS ゴシック8pxの通知を上画面右下へ表示します。連続で切り替えるとスタック動作も確認できます。"
        );

        menu += new MenuEntry(
            u8"自前 GUI を組み込む",
            OwnGuiToggle,
            u8"ゲーム自身の nw::lyt 経路へ自前の Layout ノードを差し込み、"
            u8"上下画面に常時・最前面で描けるようにします。"
            u8"OSD ではないのでチカチカせず、アルファブレンドも効き、ゲームは止まりません。\n"
            u8"ON にしたあと Select で自前メニューを開閉、Select + 十字上 で従来の CTRPF メニューです。\n"
            u8"基準仕様 v2: 矩形は nw::lyt::Picture、文字は自前フォント資源 + 標準の文字経路。"
            u8"GPU コマンドを 1 語も自分で書かず、入れるフックは RenderTop / RenderBottom の 2 本だけ。"
            u8"詳細は /gohan_owngui.txt と /gohan_gui.txt。"
        );

        menu += new MenuEntry(
            u8"GPU可視メモリ(linear/VRAM)を検証",
            LinearAllocTest,
            u8"linearAlloc / linearMemAlign / svcControlMemory(MEMOP_ALLOC_LINEAR) / vramAlloc がプラグインから使えるかを調べます。仮想アドレスと物理アドレスの差が実測値 0x10000000 と一致するかも確認します。全文は SD の /CTRPF_LinearTest.txt に出力します。"
        );

        // Example entry
        /*menu += new MenuEntry("Test", nullptr, [](MenuEntry *entry)
        {
            std::string body("What's the answer ?\n");

            body += std::to_string(42);

            MessageBox("UA", body)();
        });*/
    }

    int     main(void)
    {
        GuiNotification::Initialize();

        PluginMenu *menu = new PluginMenu("gohan", 1, 0, 0,
                                            "とびだせ どうぶつの森（JPN 無印）用。 自前 GUI（基準仕様 v2）と ActionReplay。");

        // Synnchronize the menu with frame event
        menu->SynchronizeWithFrame(true);

        // ★ホットキーの切り分け（TODO-134 段 G）
        //   Select 単独        -> 自前メニュー（ゲームは止まらない）
        //   Select + DPad Up   -> 従来の CTRPF メニュー
        //   OnOpening が false を返すと CTRPF のメニューは開かない。
        //   CTRPF 本体にパッチを当てる必要がない。
        menu->OnOpening = GuiMenu::OnOpening;

        // Init our menu entries & folders
        InitMenu(*menu);

        // Launch menu and mainloop
        menu->Run();

        delete menu;
        GuiMenu::Shutdown();
        ChatKanji::Shutdown();
        OwnGuiShutdown();
        GuiNotification::Shutdown();

        // Exit plugin
        return (0);
    }
}
