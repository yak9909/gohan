// Generated native review callbacks; conversion model: gpt-6.
#include "NativeReview.hpp"
#include <CTRPluginFramework.hpp>
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
using namespace CTRPluginFramework;

namespace ReferenceCheats { namespace NativeReview {

// 名称候補: アイテム置いても消えない / 花散らない
// 元関数: InfiniteItem / GROUP_02424
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/Players/InfiniteItem.cpp:L5 (CPP_e4bd82af1097acf7)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L29
// 元登録条件: new MenuFolder("Player", "", {
// 元登録条件:         EnableEntry(new MenuEntry("花散らない", DeceiveFlower)),
// 元登録条件:         new MenuEntry("アイテム置いても消えない", InfiniteItem),
// 元登録条件:         new MenuEntry("キーボード拡張", KeyboardExtender),
// 元登録条件:         EnableEntry(new MenuEntry("セーブメニュー無効化", DisableSaveMenu))
// 元登録条件:       })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L31
// 元登録条件: new MenuEntry("アイテム置いても消えない", InfiniteItem)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Includes/types.h:L24 (DECL_681a1d3e72afb923)
void InfiniteItem_03b96242a59b(MenuEntry *entry)
{
    typedef uint32_t u32;

    if( entry->WasJustActivated() ) {
      *(u32*)(0x19C42C) = 0x0;
      *(u32*)(0x19C4D0) = 0x0;
    }

    if( !entry->IsActivated() ) {
      *(u32*)(0x19C42C) = 0xEB057D33;
      *(u32*)(0x19C4D0) = 0xEB057D0A;
    }
  }

// 名称候補: menuspeed
// 元関数: menuspeed / GROUP_02427
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L1168 (CPP_e452e90ae3290bc1)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L295
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Fast Menus", menuspeed, "Makes the menus xtra speedy. Thanks to Levi for this sweet cheat!")
void menuspeed_04cb4377cc9f(MenuEntry *entry)
{
        if (entry->WasJustActivated())
            Process::Write32(0x56A2C0, 0x7F7FFFFF);

        if(!entry->IsActivated())
            Process::Write32(0x56A2C0, 0x3F800000);
    }

// 名称候補: onlineplayermod
// 元関数: onlineplayermod / GROUP_02435
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L1714 (CPP_58d1453ec33cb8bc)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L180
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(Blue01) << "Visibility Modifier", onlineplayermod, "Press the hotkeys to change the effect that others see on you. You can go fully invisible, or appear as if you're not moving at all. Useful if you want to explore somewhere without someone following you. Remember what you used. This does work online."),{ Hotkey(Key::ZL | Key::A, "Change Appearence") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L180
// 元登録条件: new MenuEntry(Color(Blue01) << "Visibility Modifier", onlineplayermod, "Press the hotkeys to change the effect that others see on you. You can go fully invisible, or appear as if you're not moving at all. Useful if you want to explore somewhere without someone following you. Remember what you used. This does work online.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void onlineplayermod_064d56acba8b(MenuEntry *entry)
{
    typedef uint32_t u32;

        u32 vival;
		static bool Message = false;

        if (entry->Hotkeys[0].IsDown() && !Message)
        {
			if (Process::Read32(0x67743C, vival) && vival == 0xE1A07002)
            {
				Process::Write32(0x655E44, 0xE3A01017);
                Process::Write32(0x67743C, 0xE3A07006);
                Process::Write32(0x68DC3C, 0xE1A00000);
                OSD::Notify("Visibility: Stationary", Color::Blue);
				Message = true;
            }
            else if (vival == 0xE3A07006)
            {
				Process::Write32(0x655E44, 0xE3A01016);
                Process::Write32(0x67743C, 0xE3A07000);
                Process::Write32(0x68DC3C, 0x1BFF021A);
                OSD::Notify("Visibility: Invisible", Color::Yellow);
				Message = true;
            }
			else if (vival == 0xE3A07000)
            {
				Process::Write32(0x655E44, 0xE3A01017);
                Process::Write32(0x67743C, 0xE1A07002);
                Process::Write32(0x68DC3C, 0x1BFF021A);
                OSD::Notify("Visibility: Default", Color::Green);
				Message = true;
            }
        }

		if (!entry->Hotkeys[0].IsDown())
			Message = false;
    }

// 名称候補: tpccode
// 元関数: tpccode / GROUP_02437
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L945 (CPP_8571fa3b4743c1f3)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L327
// 元登録条件: new MenuEntry(Color(Blue01) << "Change Dream Code", nullptr, tpccode, "Change your dream address!")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L22 (DECL_27a0b2e0dd26a56d)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L23 (DECL_7b95ec037187a347)
void tpccode_065c7735f082(MenuEntry *entry)
{
    typedef uint8_t u8;
    typedef uint16_t u16;

        static bool Message = false;
        u8 Part1;
        u8 Part2;
        u16 Part3;
        u16 Part4;

        {
            Keyboard Choices("Page 1 of 4");
            int UserChoice = Choices.Open(Part1);
            Keyboard Choices1("Page 2 of 4");
            int UserChoice1 = Choices1.Open(Part2);
            Keyboard Choices2("Page 3 of 4");
            int UserChoice2 = Choices2.Open(Part3);
            Keyboard Choices3("Page 4 of 4");
            int UserChoice3 = Choices3.Open(Part4);
            OSD::Notify("Updated Dream Code!");
            Message = true;
            {
                Process::Write16(0x31F2C719, Part1);
                Process::Write16(0x31F2C714, Part2);
                Process::Write16(0x31F2C712, Part3);
                Process::Write16(0x31F2C710, Part4);
            }
        }
    }

// 名称候補: extender
// 元関数: extender / GROUP_02442
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L539 (CPP_328c0ba562026851)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L269
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(ccLightPurple) << "Keyboard Unlocker", extender, "This unlocks arobase and allows line breaking."),{ Hotkey(Key::ZR, "Extendo-keys") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L269
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Keyboard Unlocker", extender, "This unlocks arobase and allows line breaking.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L22 (DECL_27a0b2e0dd26a56d)
void extender_06edbc16185f(MenuEntry *entry)
{
    typedef uint8_t u8;

		u8 val;

		if (entry->Hotkeys[0].IsDown())
		{
			Process::Write8(0xAD7253, 0x1);
		}
		if (entry->Hotkeys[0].IsDown())
		{
			Process::Write8(0xAD75C0, 0x01);
		}
	}

// 名称候補: asmpresses
// 元関数: asmpresses / GROUP_02443
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4109 (CPP_99eb7ddff5fc2423)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L172
// 元登録条件: new MenuEntry(Color(Blue01) << "Multi-Presses", asmpresses, "Used for actual game things like using tools and whatnot.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void asmpresses_07080a289aaa(MenuEntry *entry)
{
    typedef uint32_t u32;

		u32 pasm;

		if (Controller::IsKeysPressed(Key::ZL + Key::DPadRight))
		{
			if (Process::Read32(0x5C5BEC, pasm) && pasm == 0x0A000028)
			{
				Process::Write32(0x5C5BEC, 0xE1A00000);
				OSD::Notify("Multi-Presses: On!", Color::Green);
			}
			else if (pasm == 0xE1A00000)
			{
				Process::Write32(0x5C5BEC, 0x0A000028);
				OSD::Notify("Multi-Presses: Off!", Color::Red);
			}
		}
	}

// 名称候補: アイテム保存 / 置いてもなくならない
// 元関数: InfinityItemDrop / GROUP_02446
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L485 (CPP_918600873d4c8e46)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L104
// 元登録条件: ModFolder(Color::SkyBlue, "\uE017", "インベントリ",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry("アイテム保存", nullptr, InventoryBackup),
// 元登録条件: 			new MenuEntry("すべて削除", nullptr, AllDelete),
// 元登録条件: 			new MenuEntry("特殊アイテム表示", nullptr, ViewTokushuItem),
// 元登録条件: 			new MenuEntry("置いてもなくならない", nullptr, InfinityItemDrop),
// 元登録条件: 			new MenuEntry("アイテム選択肢変更", nullptr, ChangeItemOption),
// 元登録条件: 			new MenuEntry("アイテム取得 " FONT_X " + " FONT_DR, TextToItem),
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L109
// 元登録条件: new MenuEntry("置いてもなくならない", nullptr, InfinityItemDrop)
void InfinityItemDrop_0748cdf5d105(MenuEntry *entry)
{
		Keyboard key("", { "オン", "オフ" });
		int r = key.Open();

		if (r == 0)
		{
			Process::Write32(0x0019C4D0, 0x00000000);
			Process::Write32(0x0019C42C, 0x00000000);
		}
		else if (r == 1)
		{
			Process::Write32(0x0019C4D0, 0xEB057D0A);
			Process::Write32(0x0019C42C, 0xEB057D33);
		}
	}

// 名称候補: InfinityItem
// 元関数: InfinityItem / GROUP_02447
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/インベントリ.cpp:L29 (CPP_7d916239d03f48a6)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Inventory.cpp:L29 (CPP_9b3a2c590d8d57fb)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L214
// 元登録条件: new MenuFolder(Color(ccSkyBlue) << "*\uE016" << Color::SkyBlue << " インベントリ " << Color(ccSkyBlue) << "\uE016*", "",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 未使用アイテム表示", nullptr, DisableItemLocks),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 置いても無くならない", nullptr, InfinityItem),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 すべて削除", nullptr, DeleteInvItems),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 選択肢変更", nullptr, InvItemOption),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 所持金変更", nullptr, ChangeWalletBell),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 アイテム取得 \uE002 + \uE07C", TextToItem),
// 元登録条件:
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L217
// 元登録条件: new MenuEntry(Color::SkyBlue << "\uE016 置いても無くならない", nullptr, InfinityItem)
void InfinityItem_0748cdf5d105(MenuEntry *e)
{
		Keyboard key("", { "オン", "オフ" });
		int r = key.Open();

		if (r == 0)
		{
			Process::Write32(0x0019C4D0, 0x00000000);
			Process::Write32(0x0019C42C, 0x00000000);
		}
		else if (r == 1)
		{
			Process::Write32(0x0019C4D0, 0xEB057D0A);
			Process::Write32(0x0019C42C, 0xEB057D33);
		}
	}

// 名称候補: walletfix
// 元関数: walletfix / GROUP_02466
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4436 (CPP_996240e7726bfa4f)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L161
// 元登録条件: new MenuEntry(Color(Blue01) << "Improved Wallet", walletfix, "Store up to 999,999 bells. Rich bitch.")
void walletfix_0b42a4a52779(MenuEntry *entry)
{
        if (entry->WasJustActivated())
        {
            Process::Write16(0x19F6F8, 0x2117);
            Process::Write32(0x2C02C0, 0x000F423F);
        }
        else if (!entry->IsActivated())
        {
            Process::Write16(0x19F6F8, 0x20BE);
            Process::Write32(0x2C02C0, 0x0001869F);
        }
    }

// 名称候補: TranslateFRA
// 元関数: TranslateFRA / GROUP_02497
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L780 (CPP_c62dad79d33066e8)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L274
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "French Communicator", TranslateFRA, "Refer to the plugin GUIDE button for tips on using this.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void TranslateFRA_10eebb5aa644(MenuEntry *entry)
{
    typedef uint32_t u32;

        u32		FRkeyboard1;
        u32		FRkeyboard2;
        u32		FRtypedtext;

        Process::Read32(0x95F11C, FRkeyboard1);
        if (FRkeyboard1 != 0)
        {
            Process::Read32(0x95F110, FRkeyboard2);
            {
                Process::Read32(FRkeyboard2, FRtypedtext);
                if (FRtypedtext == 0x041D0021 || FRtypedtext == 0x00680021)
                    Process::WriteString(FRkeyboard2, "Salut", StringFormat::Utf16);

				if (FRtypedtext == 0x04120021 || FRtypedtext == 0x00620021)
                    Process::WriteString(FRkeyboard2, "Au revoir", StringFormat::Utf16);

				if (FRtypedtext == 0x039D0021 || FRtypedtext == 0x006E0021)
                    Process::WriteString(FRkeyboard2, "Non", StringFormat::Utf16);

				if (FRtypedtext == 0x03A50021 || FRtypedtext == 0x00790021)
                    Process::WriteString(FRkeyboard2, "Oui", StringFormat::Utf16);

				if (FRtypedtext == 0x04100021 || FRtypedtext == 0x04300021)
                    Process::WriteString(FRkeyboard2, "Je suis désolé", StringFormat::Utf16);

				if (FRtypedtext == 0x00530021 || FRtypedtext == 0x00730021)
                    Process::WriteString(FRkeyboard2, "S'il vous plaît arrêter", StringFormat::Utf16);

				if (FRtypedtext == 0x00560021 || FRtypedtext == 0x00760021)
                    Process::WriteString(FRkeyboard2, "Cool", StringFormat::Utf16);

				if (FRtypedtext == 0x04210021 || FRtypedtext == 0x04410021)
                    Process::WriteString(FRkeyboard2, "Viens ici s'il te plait", StringFormat::Utf16);

				if (FRtypedtext == 0x00740021 || FRtypedtext == 0x04220021)
                    Process::WriteString(FRkeyboard2, "Je vous remercie!", StringFormat::Utf16);
            }
        }
    }

// 名称候補: mujintou_Tool4
// 元関数: mujintou_Tool4 / GROUP_02504
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L346 (CPP_811a81a2406b3d35)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L348 (CPP_a79111fca627b0b2)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L274
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 スコップ所持変更", nullptr, mujintou_Tool4)
void mujintou_Tool4_123521cca480(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2E8, 1 - choice);
	}

// 名称候補: FastGameSpeed / ゲーム速度高速 / セーブメニュー出さない
// 元関数: FastGameSpeed / GROUP_02511
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/その他.cpp:L50 (CPP_a5f08ab9d3b2d13b)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L822 (CPP_ff76115573910b73)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L299
// 元登録条件: new MenuFolder(Color(ccSkyBlue) << "*\uE018" << Color::SkyBlue << " その他" << Color(ccSkyBlue) << " \uE018*", "",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 セーブメニュー非表示", nullptr, DisableSaveMenu),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 他の人に押されない", nullptr, OtherPlayersCantPushYou),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 ゲーム速度上昇", nullptr, FastGameSpeed),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 天気変更", nullptr, ChangeWeather),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::SkyBlue << "\uE018 カメラに全員映す", CameraAllPlayers),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(A + B, "リスト変更"),
// 元登録条件: 				Hotkey(B + DU, "モード切替"),
// 元登録条件: 			}),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::SkyBlue << "\uE018 アドレス監視", ViewAddress),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(Key::R + A, "アドレス変更"),
// 元登録条件: 				Hotkey(Key::R + B, "リストに追加"),
// 元登録条件: 				Hotkey(Key::R + X, "リスト変更"),
// 元登録条件: 			}),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 関数呼び出し \uE000 + \uE07A", CallFunction),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 HEXエディタ", HexEditor),
// 元登録条件:
// 元登録条件:
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L303
// 元登録条件: new MenuEntry(Color::SkyBlue << "\uE018 ゲーム速度上昇", nullptr, FastGameSpeed)
void FastGameSpeed_13beaf1e0359(MenuEntry *entry)
{
		Keyboard k("", {"オン", "オフ"});
		switch(k.Open())
		{
			case 0:
			{
				Process::Write32(0x54c6e8, 0xe3e004ff);
				break;
			}
			case 1:
			{
				Process::Write32(0x54c6e8, 0xe59400a0);
				break;
			}
		}
	}

// 名称候補: ChangeIslandServer
// 元関数: ChangeIslandServer / GROUP_02523
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/OnlineIsland.cpp:L8 (CPP_def2f565053dd618)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L303
// 元登録条件: new MenuEntry(Color::DodgerBlue << "サーバー変更", nullptr, ChangeIslandServer)
void ChangeIslandServer_160a045d81ae(MenuEntry *e)
{
		Keyboard key("", { "日本", "イギリス", "韓国", "フランス", "アメリカ" });

		int r = key.Open();
		if (r == 0) Process::Write32(0x34f8f8, 0xe3a00001);
		if (r == 1) Process::Write32(0x34f8f8, 0xe3a0006e);
		if (r == 2) Process::Write32(0x34f8f8, 0xe3a00088);
		if (r == 3) Process::Write32(0x34f8f8, 0xe3a0004d);
		if (r == 4) Process::Write32(0x34f8f8, 0xe3a00031);
	}

// 名称候補: roomSeeder
// 元関数: roomSeeder / GROUP_02528
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L3924 (CPP_9e7f73a0a8ca1933)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L196
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Edit Every Room", roomSeeder, "Change furniture, wallpaper, flooring, music, everything inside another players house!")
void roomSeeder_17bb948b48b7(MenuEntry *entry)
{
        Process::Write32(0x998C7A, 0x10101010);
        Process::Write8(0x998C7E, 0x10);
        if (entry->WasJustActivated())
        {
			//Process::Patch(0x5B4B60, 0xE3A00001); //allow wallpaper change
            Process::Patch(0x5B5268, 0xE1A00000);
            Process::Patch(0x5B5284, 0xEA000026);
			Process::Patch(0x5B7558, 0xE3A00001);
        }
        else if (!entry->IsActivated())
        {
			//Process::Patch(0x5B4B60, 0xE3A00000); //allow wallpaper change
            Process::Patch(0x5B5268, 0x0A00000D);
            Process::Patch(0x5B5284, 0x0A000026);
			Process::Patch(0x5B7558, 0xE3A00000);
        }
    }

// 名称候補: mujintou_GTool1
// 元関数: mujintou_GTool1 / GROUP_02549
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L383 (CPP_30cb602153cf5933)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L381 (CPP_da27fc066581c695)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L279
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 金の釣り竿所持変更", nullptr, mujintou_GTool1)
void mujintou_GTool1_1c0898bbd555(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2FC, 1 - choice);
	}

// 名称候補: fixMenu
// 元関数: fixMenu / GROUP_02567
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L314 (CPP_35465b19a554f7c3)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L255
// 元登録条件: new MenuEntry(Color(Blue01) << "Fix Menu \uE053 + \uE003", fixMenu)
void fixMenu_1efcf47156a6(MenuEntry *entry)
{
		if (Controller::IsKeysPressed(Key::R + Key::Y))
		{
			Process::Write32(0x9526BC, 0);
			Process::Write16(0x950D68, 2);
			Process::Write32(0x318A4F10, 0);
			Process::Write32(0x318A4F14, 0);
			Process::Write32(0x318A4F18, 0);
			Process::Write32(0x318A4F20, 0);
			OSD::Notify("Menu fixed");
		}
	}

// 名称候補: TranslateGER
// 元関数: TranslateGER / GROUP_02577
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L738 (CPP_e849efa6802ed194)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L273
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "German Communicator", TranslateGER, "Refer to the plugin GUIDE button for tips on using this.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void TranslateGER_20eb932271ef(MenuEntry *entry)
{
    typedef uint32_t u32;

        u32		GRkeyboard1;
        u32		GRkeyboard2;
        u32		GRtypedtext;

        Process::Read32(0x95F11C, GRkeyboard1);
        if (GRkeyboard1 != 0)
        {
            Process::Read32(0x95F110, GRkeyboard2);
            {
                Process::Read32(GRkeyboard2, GRtypedtext);
                if (GRtypedtext == 0x041D0021 || GRtypedtext == 0x00680021)
                    Process::WriteString(GRkeyboard2, "Hallo", StringFormat::Utf16);

				if (GRtypedtext == 0x04120021 || GRtypedtext == 0x00620021)
                    Process::WriteString(GRkeyboard2, "Auf Wiedersehen", StringFormat::Utf16);

				if (GRtypedtext == 0x039D0021 || GRtypedtext == 0x006E0021)
                    Process::WriteString(GRkeyboard2, "Nein", StringFormat::Utf16);

				if (GRtypedtext == 0x03A50021 || GRtypedtext == 0x00790021)
                    Process::WriteString(GRkeyboard2, "Ja", StringFormat::Utf16);

				if (GRtypedtext == 0x04100021 || GRtypedtext == 0x04300021)
                    Process::WriteString(GRkeyboard2, "Es tut mir Leid", StringFormat::Utf16);

				if (GRtypedtext == 0x00530021 || GRtypedtext == 0x00730021)
                    Process::WriteString(GRkeyboard2, "Bitte hör auf", StringFormat::Utf16);

				if (GRtypedtext == 0x00560021 || GRtypedtext == 0x00760021)
                    Process::WriteString(GRkeyboard2, "Sehr beeindruckend", StringFormat::Utf16);

				if (GRtypedtext == 0x04210021 || GRtypedtext == 0x04410021)
                    Process::WriteString(GRkeyboard2, "Kannst du bitte herkommen?", StringFormat::Utf16);

				if (GRtypedtext == 0x00740021 || GRtypedtext == 0x04220021)
                    Process::WriteString(GRkeyboard2, "Danke vielmals!", StringFormat::Utf16);
            }
        }
    }

// 名称候補: mujintou_Item2
// 元関数: mujintou_Item2 / GROUP_02578
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L167 (CPP_d411e306addf6684)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L165 (CPP_d8903cd4ec6b82b5)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L261
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 糸所持数変更", nullptr, mujintou_Item2)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L58 (DECL_435621ce2b05901a)
void mujintou_Item2_21056f77286c(MenuEntry *e)
{
    typedef uint8_t u8;

		u8 fxx;

		Keyboard keyboard("所持数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write8(0x330BE3CC, fxx);
		}
	}

// 名称候補: 性別変更 / 村名変更
// 元関数: ChangeSeibetu / GROUP_02617
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L217 (CPP_abe36668e0772627)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L84
// 元登録条件: ModFolder(Color::Orange, "\uE015", "セーブデータ",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry("村名変更", nullptr, ChangeTownName),
// 元登録条件: 			new MenuEntry("プレイヤー名変更", nullptr, ChangePlayerName),
// 元登録条件: 			new MenuEntry("国籍変更", nullptr, ChangeKokuseki),
// 元登録条件: 			new MenuEntry("性別変更", nullptr, ChangeSeibetu),
// 元登録条件: 			new MenuEntry("バッジ変更", nullptr, ChangeBadgeData)
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L89
// 元登録条件: new MenuEntry("性別変更", nullptr, ChangeSeibetu)
void ChangeSeibetu_2abe9b960f6c(MenuEntry *entry)
{
		Keyboard key("プレイヤー", { "村長", "サブ1", "サブ2", "サブ3" });
		int r0 = key.Open();

		OSD::SwapBuffers();

		if (r0 >= 0)
		{
			Keyboard denger("性別", { "男", "女" });
			int r1 = denger.Open();

			if (r1 >= 0)
				Process::Write8(0x31F4F05A + r0 * 0xA480, r1);
		}
	}

// 名称候補: ChangeDenger
// 元関数: ChangeDenger / GROUP_02618
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/プレイヤー.cpp:L214 (CPP_067fd994f3127df7)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L182
// 元登録条件: new MenuFolder(Color(ccSkyBlue) << "*\uE050" << Color::SkyBlue << " プレイヤー" << Color(ccSkyBlue) << " \uE051*", "",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 プレイヤー変更", nullptr, ChangePlayer),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 エリア移動", nullptr, ChangeArea),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 名前変更", nullptr, ChangeName),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 国籍変更", nullptr, ChangeRegion),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 性別変更", nullptr, ChangeDenger),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 バッジ変更", nullptr, ChangeBadge),
// 元登録条件:
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L188
// 元登録条件: new MenuEntry(Color::SkyBlue << "\uE050 性別変更", nullptr, ChangeDenger)
void ChangeDenger_2abe9b960f6c(MenuEntry *e)
{
		Keyboard key("プレイヤー", { "村長", "サブ1", "サブ2", "サブ3" });
		int r0 = key.Open();

		OSD::SwapBuffers();

		if (r0 >= 0)
		{
			Keyboard denger("性別", { "男", "女" });
			int r1 = denger.Open();

			if (r1 >= 0)
				Process::Write8(0x31F4F05A + r0 * 0xA480, r1);
		}
	}

// 名称候補: Pose2
// 元関数: Pose2 / GROUP_02619
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Movement.cpp:L261 (CPP_960d5dd6d0b51ce7)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L155
// 元登録条件: EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "Tポーズ", Pose2, Color::SkyBlue << "キーを押すとみんながTポーズをする。"),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(Key::L | DPadUp, "Tポーズ"),
// 元登録条件: 				Hotkey(Key::L | DPadDown, "通常")
// 元登録条件: 			})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L155
// 元登録条件: new MenuEntry(Color::DodgerBlue << "Tポーズ", Pose2, Color::SkyBlue << "キーを押すとみんながTポーズをする。")
void Pose2_2af358ff2210(MenuEntry *entry)
{
		static bool key_status = false;

		if (entry->Hotkeys[0].IsDown() && key_status == false)
		{
			key_status = true;
			Process::Write32(0x0073AA30, 0xE1A00000);
			OSD::Notify("T pose " << Color::Green << "ON!");
		}

		if (entry->Hotkeys[1].IsDown() && key_status == false)
		{
			key_status = true;
			Process::Write32(0x0073AA30, 0x0A000011);
			OSD::Notify("T pose " << Color::Red << "OFF!");
		}
		if (entry->Hotkeys[0].IsDown() == 0 && entry->Hotkeys[1].IsDown() == 0) key_status = false;
	}

// 名称候補: frag_change
// 元関数: frag_change / GROUP_02620
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Shop.cpp:L794 (CPP_54c688c5c197495e)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L309
// 元登録条件: new MenuEntry(Color::DodgerBlue << "駅の旗大きさ変更", nullptr, frag_change, Color::SkyBlue << "駅の旗の大きさを変更できる。")
void frag_change_2b072254ea69(MenuEntry *e)
{
		float fxx;

		Keyboard keyboard(Utils::Format("指定した旗のサイズに変化。/nデフォルト14.1421/nmin1.8750/nmax536870912"));
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
			if (fxx >= 1.8750 && fxx <= 536870912) Process::WriteFloat(0x001C8CAC, fxx);
	}

// 名称候補: trampleSeeder
// 元関数: trampleSeeder / GROUP_02626
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L3469 (CPP_09a9eeb22628e54b)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L211
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Trampler \uE053 + \uE001", trampleSeeder, "Run over an item to remove it.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void trampleSeeder_2bfb49a0719c(MenuEntry *entry)
{
    typedef uint32_t u32;

		if (Controller::IsKeysPressed(Key::R + Key::B))
		{
			if (*(u32 *)0x597F64 == 0xE1A00000)
			{
				Process::Write32(0x64E4D4, 0x0A000032);
				Process::Write32(0x597F38, 0x0A000056);
				Process::Write32(0x597F58, 0xE3A0001D);
				Process::Write32(0x597F64, 0x0A00004B);
				Process::Write32(0x597FA0, 0x1A00003C);
				Process::Write32(0x597FE8, 0x0A00002A);
				Process::Write32(0x597FAC, 0x0A000039);
				OSD::Notify("RESTORUS");
			}
			else
			{
				Process::Write32(0x64E4D4, 0xE1A00000);
				Process::Write32(0x597F38, 0xE1A00000);
				Process::Write32(0x597F58, 0xEBF5935C);
				Process::Write32(0x597F64, 0xE1A00000);
				Process::Write32(0x597FA0, 0xE1A00000);
				Process::Write32(0x597FE8, 0xE1A00000);
				Process::Write32(0x597FAC, 0xE1A00000);
				OSD::Notify("DELETUS");
			}
		}
	}

// 名称候補: 花散らない
// 元関数: DeceiveFlower / GROUP_02659
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/Players/DeceiveFlower.cpp:L5 (CPP_8b0ee9a8f26ff978)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L29
// 元登録条件: new MenuFolder("Player", "", {
// 元登録条件:         EnableEntry(new MenuEntry("花散らない", DeceiveFlower)),
// 元登録条件:         new MenuEntry("アイテム置いても消えない", InfiniteItem),
// 元登録条件:         new MenuEntry("キーボード拡張", KeyboardExtender),
// 元登録条件:         EnableEntry(new MenuEntry("セーブメニュー無効化", DisableSaveMenu))
// 元登録条件:       })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L30
// 元登録条件: EnableEntry(new MenuEntry("花散らない", DeceiveFlower))
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L30
// 元登録条件: new MenuEntry("花散らない", DeceiveFlower)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Includes/types.h:L24 (DECL_681a1d3e72afb923)
void DeceiveFlower_32a7dfda8086(MenuEntry *entry)
{
    typedef uint32_t u32;

    if( entry->WasJustActivated() ) {
      *(u32*)(0x596890) = 0xE3A0001D;
    }

    if( !entry->IsActivated() ) {
      *(u32*)(0x596890) = 0xEBF5990F;
    }
  }

// 名称候補: mujintou_STool1
// 元関数: mujintou_STool1 / GROUP_02663
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L355 (CPP_3cc1fa9a6a1f5678)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L353 (CPP_4b05f84bea089ae7)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L275
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 銀の釣り竿所持変更", nullptr, mujintou_STool1)
void mujintou_STool1_3358406275df(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2EC, 1 - choice);
	}

// 名称候補: ChangeWeather
// 元関数: ChangeWeather / GROUP_02678
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/その他.cpp:L71 (CPP_d44b9d9005a0580c)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L299
// 元登録条件: new MenuFolder(Color(ccSkyBlue) << "*\uE018" << Color::SkyBlue << " その他" << Color(ccSkyBlue) << " \uE018*", "",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 セーブメニュー非表示", nullptr, DisableSaveMenu),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 他の人に押されない", nullptr, OtherPlayersCantPushYou),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 ゲーム速度上昇", nullptr, FastGameSpeed),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 天気変更", nullptr, ChangeWeather),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::SkyBlue << "\uE018 カメラに全員映す", CameraAllPlayers),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(A + B, "リスト変更"),
// 元登録条件: 				Hotkey(B + DU, "モード切替"),
// 元登録条件: 			}),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::SkyBlue << "\uE018 アドレス監視", ViewAddress),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(Key::R + A, "アドレス変更"),
// 元登録条件: 				Hotkey(Key::R + B, "リストに追加"),
// 元登録条件: 				Hotkey(Key::R + X, "リスト変更"),
// 元登録条件: 			}),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 関数呼び出し \uE000 + \uE07A", CallFunction),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 HEXエディタ", HexEditor),
// 元登録条件:
// 元登録条件:
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L304
// 元登録条件: new MenuEntry(Color::SkyBlue << "\uE018 天気変更", nullptr, ChangeWeather)
void ChangeWeather_35e622f24f33(MenuEntry *e)
{
		Keyboard key("",
			{
				"晴れ(雲なし)",
				"晴れ(雲あり)",
				"くもり空",
				"雨(小)",
				"雨",
				"雪(小)",
				"雪",
			});

		int r = key.Open();

		if (r >= 0)
		{
			Process::Write32(0x0062E728, 0xE3A00000 + r);
			Process::Write32(0x00949530, 0x01000000 * r);
		}
	}

// 名称候補: KeyboardExtender
// 元関数: KeyboardExtender / GROUP_02687
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Misc.cpp:L195 (CPP_bf580e58ae931b13)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L310
// 元登録条件: new MenuEntry(Color::DodgerBlue << "キーボード改行", KeyboardExtender)
void KeyboardExtender_384d078c796c(MenuEntry *e)
{
		Process::Write32(0x00AD0250, 0x1000000);
	}

// 名称候補: mujintou_STool3
// 元関数: mujintou_STool3 / GROUP_02690
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L367 (CPP_45ee137cffa7f1c1)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L369 (CPP_f9b07e34786661ec)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L277
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 銀の網所持変更", nullptr, mujintou_STool3)
void mujintou_STool3_39c5848ea935(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2F4, 1 - choice);
	}

// 名称候補: ChangeFlowerTreeSize
// 元関数: ChangeFlowerTreeSize / GROUP_02693
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Misc.cpp:L126 (CPP_d9f212a7b276d2d1)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L305
// 元登録条件: new MenuEntry(Color::DodgerBlue << "花や木のサイズ変更", nullptr, ChangeFlowerTreeSize)
void ChangeFlowerTreeSize_39d9d780e45a(MenuEntry *e)
{
		Keyboard key("default = 1.0");
		float size;
		if (key.Open(size) == 0) Process::WriteFloat(0x5901a4, size);
	}

// 名称候補: SpeedHack
// 元関数: SpeedHack / GROUP_02697
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Movement.cpp:L212 (CPP_bde76b1f30d77497)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L149
// 元登録条件: EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "スピードハック", SpeedHack), Hotkey(B, ""))
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L149
// 元登録条件: new MenuEntry(Color::DodgerBlue << "スピードハック", SpeedHack)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Includes/types.h:L60 (DECL_8f7c81b04adb9ff7)
void SpeedHack_3abacfa64115(MenuEntry *e)
{
    typedef uint32_t u32;

		u32		velocity;
		float* Flo = (float*)0x33099E7C;
		const float max = 100;

		if (e->Hotkeys[0].IsDown())
		{
			if (*Flo > max)
			{
				*Flo = max;
			}
			else if (*Flo > 0)
			{
				*Flo += 2.0;
			}
		}
	}

// 名称候補: FarCamera
// 元関数: FarCamera / GROUP_02718
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Misc.cpp:L115 (CPP_6fcae0664cc9fce2)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L304
// 元登録条件: new MenuEntry(Color::DodgerBlue << "遠距離カメラ", nullptr, FarCamera)
void FarCamera_3ff82ab5a8ef(MenuEntry *e)
{
		Keyboard key("", { "オン", "オフ" });
		int r = key.Open();
		if (r == 0) Process::Write32(0x47d170, 0x3fd00000);
		if (r == 1) Process::Write32(0x47d170, 0x40000000);
	}

// 名称候補: mujintou_syokuryou3
// 元関数: mujintou_syokuryou3 / GROUP_02719
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L476 (CPP_2cad63155213ba21)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L478 (CPP_cdbeb146acc98d74)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L287
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 食料所持数変更", nullptr, mujintou_syokuryou3)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_syokuryou3_401f91a195b5(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("食料所持数を指定してください。");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330E0FD8, fxx);
		}
	}

// 名称候補: 歩いたとこのアイテム消える
// 元関数: WalkRemover / GROUP_02737
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L650 (CPP_fe463ec3748ff495)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L114
// 元登録条件: ModFolder(Color::Yellow, "\uE018", "動き、アクション",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry("歩いたとこのアイテム消える", nullptr, WalkRemover),
// 元登録条件: 			new MenuEntry("走っても花散らない", nullptr, NoBreakFlower),
// 元登録条件: 			new MenuEntry("座標移動 " FONT_A " + \uE006", CoordinatesModifier),
// 元登録条件: 			new MenuEntry("壁抜け " FONT_L " + " FONT_DU, WalkOverObjects),
// 元登録条件: 			new MenuEntry("タッチワープ", TouchWarping),
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L116
// 元登録条件: new MenuEntry("歩いたとこのアイテム消える", nullptr, WalkRemover)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Includes/types.h:L60 (DECL_a801eff24b14c5a7)
void WalkRemover_43547bc4333b(MenuEntry *entry)
{
    typedef uint32_t u32;

		Keyboard k("", {"オン", "オフ"});
		const u32 nop = 0xE1A00000;

		switch( k.Open() )
		{
		case 0:
			Process::Write32(0x596920, nop);
			Process::Write32(0x5968E4, nop);
			Process::Write32(0x5968D8, nop);
			Process::Write32(0x5968D0, nop);
			Process::Write32(0x59689C, nop);
			Process::Write32(0x596870, nop);
			break;
		case 1:
			Process::Write32(0x596920, 0x0A00002A);
			Process::Write32(0x5968E4, 0x0A000039);
			Process::Write32(0x5968D8, 0x1A00003C);
			Process::Write32(0x5968D0, 0x1A000001);
			Process::Write32(0x59689C, 0x0A00004B);
			Process::Write32(0x596870, 0x0A000056);
			break;
		}
	}

// 名称候補: numbers
// 元関数: numbers / GROUP_02739
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4227 (CPP_247c0937ebdffef1)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L241
// 元登録条件: new MenuEntry(Color(Blue01) << "Allow writing more than 3 numbers", numbers)
void numbers_43baf77e4edf(MenuEntry *entry)
{
		Process::Write8(0xAD7158, 2);
	}

// 名称候補: mujintou_Tool1
// 元関数: mujintou_Tool1 / GROUP_02741
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L325 (CPP_a0e71d1e8cbc72bb)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L327 (CPP_fced6bfcea57eb3a)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L271
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 釣り竿所持変更", nullptr, mujintou_Tool1)
void mujintou_Tool1_4408b68b463a(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2DC, 1 - choice);
	}

// 名称候補: FastClothChange
// 元関数: FastClothChange / GROUP_02743
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Movement.cpp:L286 (CPP_2b7730abab3e65c4)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L160
// 元登録条件: new MenuEntry(Color::DodgerBlue << "はや着替え", FastClothChange, "オンにすると着替えが早くなる。")
void FastClothChange_4443421280ea(MenuEntry *entry)
{
		Process::Write32(0x3309A714, 0x00000000);
	}

// 名称候補: mujintou_STool2
// 元関数: mujintou_STool2 / GROUP_02765
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L360 (CPP_885dc204473eaccf)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L362 (CPP_e6721141596bdc1b)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L276
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 銀のパチンコ所持変更", nullptr, mujintou_STool2)
void mujintou_STool2_491c0182dab4(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2F0, 1 - choice);
	}

// 名称候補: TranslateKOR
// 元関数: TranslateKOR / GROUP_02781
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L696 (CPP_fe2d51d77a94d423)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L272
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Korean Communicator", TranslateKOR, "Refer to the plugin GUIDE button for tips on using this.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void TranslateKOR_4d3876d2e513(MenuEntry *entry)
{
    typedef uint32_t u32;

        u32		KOkeyboard1;
        u32		KOkeyboard2;
        u32		KOtypedtext;

        Process::Read32(0x95F11C, KOkeyboard1);
        if (KOkeyboard1 != 0)
        {
            Process::Read32(0x95F110, KOkeyboard2);
            {
                Process::Read32(KOkeyboard2, KOtypedtext);
                if (KOtypedtext == 0x041D0021 || KOtypedtext == 0x00680021)
                    Process::WriteString(KOkeyboard2, "여보세요", StringFormat::Utf16);

				if (KOtypedtext == 0x04120021 || KOtypedtext == 0x00620021)
                    Process::WriteString(KOkeyboard2, "나중에 보자", StringFormat::Utf16);

				if (KOtypedtext == 0x039D0021 || KOtypedtext == 0x006E0021)
                    Process::WriteString(KOkeyboard2, "아니", StringFormat::Utf16);

				if (KOtypedtext == 0x03A50021 || KOtypedtext == 0x00790021)
                    Process::WriteString(KOkeyboard2, "예", StringFormat::Utf16);

				if (KOtypedtext == 0x04100021 || KOtypedtext == 0x04300021)
                    Process::WriteString(KOkeyboard2, "당신을 귀찮게해서 미안 해요", StringFormat::Utf16);

				if (KOtypedtext == 0x00530021 || KOtypedtext == 0x00730021)
                    Process::WriteString(KOkeyboard2, "제발 그만해", StringFormat::Utf16);

				if (KOtypedtext == 0x00560021 || KOtypedtext == 0x00760021)
                    Process::WriteString(KOkeyboard2, "매우 인상적", StringFormat::Utf16);

				if (KOtypedtext == 0x04210021 || KOtypedtext == 0x04410021)
                    Process::WriteString(KOkeyboard2, "당신은 나에게 올 수 있습니까?", StringFormat::Utf16);

				if (KOtypedtext == 0x00740021 || KOtypedtext == 0x04220021)
                    Process::WriteString(KOkeyboard2, "감사!", StringFormat::Utf16);
            }
        }
    }

// 名称候補: mujintou_Item5
// 元関数: mujintou_Item5 / GROUP_02784
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L215 (CPP_412f464740f9bd11)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L213 (CPP_ca3e0fc879405a37)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L264
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 ぎんこうせき所持数変更", nullptr, mujintou_Item5)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L58 (DECL_435621ce2b05901a)
void mujintou_Item5_4d94ada284b3(MenuEntry *e)
{
    typedef uint8_t u8;

		u8 fxx;

		Keyboard keyboard("所持数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write8(0x330BE3DC, fxx);
		}
	}

// 名称候補: ChangeWeather
// 元関数: ChangeWeather / GROUP_02793
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Misc.cpp:L171 (CPP_240a31822dc0cd6e)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L308
// 元登録条件: new MenuEntry(Color::DodgerBlue << "天気変更", nullptr, ChangeWeather)
void ChangeWeather_4fc9e60d759e(MenuEntry *e)
{
		Keyboard key("",
			{
				"晴れ(雲なし)",
				"晴れ(雲あり)",
				"くもり空",
				"雨(小)",
				"雨",
				"雪(小)",
				"雪",
			});

		int r = key.Open();

		if (r >= 0) {
			Process::Write32(0x0062e728, 0xe3a00000 + r);
			Process::Write32(0x00949530, 0x1000000 * r);
		}
	}

// 名称候補: セーブメニュー無効化 / 花散らない
// 元関数: DisableSaveMenu / GROUP_02803
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/Players/DisableSaveMenu.cpp:L5 (CPP_186e8918ed75ef9c)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L29
// 元登録条件: new MenuFolder("Player", "", {
// 元登録条件:         EnableEntry(new MenuEntry("花散らない", DeceiveFlower)),
// 元登録条件:         new MenuEntry("アイテム置いても消えない", InfiniteItem),
// 元登録条件:         new MenuEntry("キーボード拡張", KeyboardExtender),
// 元登録条件:         EnableEntry(new MenuEntry("セーブメニュー無効化", DisableSaveMenu))
// 元登録条件:       })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L33
// 元登録条件: EnableEntry(new MenuEntry("セーブメニュー無効化", DisableSaveMenu))
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L33
// 元登録条件: new MenuEntry("セーブメニュー無効化", DisableSaveMenu)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Includes/types.h:L24 (DECL_681a1d3e72afb923)
void DisableSaveMenu_51d3ab2cab95(MenuEntry *entry)
{
    typedef uint32_t u32;

    if( entry->WasJustActivated() ) {
      *(u32*)(0x1A08C8) = 0xE1A00000;
      *(u32*)(0x1A08CC) = 0xE3A00000;
      *(u32*)(0x1A08D0) = 0xEB0E011D;
    }

    if( !entry->IsActivated() ) {
      *(u32*)(0x1A08C8) = 0xE3A01040;
      *(u32*)(0x1A08CC) = 0xE5900000;
      *(u32*)(0x1A08D0) = 0xEB14CAC6;
    }
  }

// 名称候補: DisableSaveMenu / セーブメニュー出さない
// 元関数: DisableSaveMenu / GROUP_02812
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/その他.cpp:L10 (CPP_7763f406d2d8707d)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L788 (CPP_afe8f7c7c71e09e1)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L299
// 元登録条件: new MenuFolder(Color(ccSkyBlue) << "*\uE018" << Color::SkyBlue << " その他" << Color(ccSkyBlue) << " \uE018*", "",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 セーブメニュー非表示", nullptr, DisableSaveMenu),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 他の人に押されない", nullptr, OtherPlayersCantPushYou),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 ゲーム速度上昇", nullptr, FastGameSpeed),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 天気変更", nullptr, ChangeWeather),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::SkyBlue << "\uE018 カメラに全員映す", CameraAllPlayers),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(A + B, "リスト変更"),
// 元登録条件: 				Hotkey(B + DU, "モード切替"),
// 元登録条件: 			}),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::SkyBlue << "\uE018 アドレス監視", ViewAddress),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(Key::R + A, "アドレス変更"),
// 元登録条件: 				Hotkey(Key::R + B, "リストに追加"),
// 元登録条件: 				Hotkey(Key::R + X, "リスト変更"),
// 元登録条件: 			}),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 関数呼び出し \uE000 + \uE07A", CallFunction),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 HEXエディタ", HexEditor),
// 元登録条件:
// 元登録条件:
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L301
// 元登録条件: new MenuEntry(Color::SkyBlue << "\uE018 セーブメニュー非表示", nullptr, DisableSaveMenu)
void DisableSaveMenu_5338fe878265(MenuEntry *entry)
{
		Keyboard k("", {"オン", "オフ"});

		switch (k.Open())
		{
		case 0:
			Process::Patch(0x1A08C8, 0xE1A00000);
			Process::Patch(0x1A08CC, 0xE3A00000);
			Process::Patch(0x1A08D0, 0xEB0E011D);
			break;
		case 1:
			Process::Patch(0x1A08C8, 0xE3A01040);
			Process::Patch(0x1A08CC, 0xE5900000);
			Process::Patch(0x1A08D0, 0xEB14CAC6);
			break;
		}
	}

// 名称候補: mujintou_Tool3
// 元関数: mujintou_Tool3 / GROUP_02836
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L339 (CPP_924aff70716108cd)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L341 (CPP_df54545254299ed9)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L273
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 網所持変更", nullptr, mujintou_Tool3)
void mujintou_Tool3_57a43ec91815(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2E4, 1 - choice);
	}

// 名称候補: mujintou_hoof3
// 元関数: mujintou_hoof3 / GROUP_02870
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L293 (CPP_237cd45bd8a5cdd4)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L295 (CPP_dde34fdd46ad571c)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L269
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 丸太の残りの数変更", nullptr, mujintou_hoof3)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_hoof3_608d9519ca08(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("残りの数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330E0FF0, fxx);
		}
	}

// 名称候補: すべて削除 / アイテム保存
// 元関数: AllDelete / GROUP_02900
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L593 (CPP_7a0322c85acf556a)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L104
// 元登録条件: ModFolder(Color::SkyBlue, "\uE017", "インベントリ",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry("アイテム保存", nullptr, InventoryBackup),
// 元登録条件: 			new MenuEntry("すべて削除", nullptr, AllDelete),
// 元登録条件: 			new MenuEntry("特殊アイテム表示", nullptr, ViewTokushuItem),
// 元登録条件: 			new MenuEntry("置いてもなくならない", nullptr, InfinityItemDrop),
// 元登録条件: 			new MenuEntry("アイテム選択肢変更", nullptr, ChangeItemOption),
// 元登録条件: 			new MenuEntry("アイテム取得 " FONT_X " + " FONT_DR, TextToItem),
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L107
// 元登録条件: new MenuEntry("すべて削除", nullptr, AllDelete)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Includes/types.h:L60 (DECL_a801eff24b14c5a7)
void AllDelete_6664a683da4f(MenuEntry *entry)
{
    typedef uint32_t u32;

		bool b = (MessageBox("確認", "持ち物欄のアイテムを全て削除しますか", DialogType::DialogYesNo))();
		if( b == true )
		{
			u32 *p = (u32*)( *(u32*)0xAA914C + 0x6BD0 );
			for( int i = 0; i < 16; i++ )
				p[i] = 0x7FFE;
		}
	}

// 名称候補: Change_Recycle1
// 元関数: Change_Recycle1 / GROUP_02922
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Shop.cpp:L806 (CPP_cd6333f70063d840)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L247
// 元登録条件: new MenuEntry(Color::DodgerBlue << "高額買取品変更", nullptr, Change_Recycle1)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Includes/types.h:L59 (DECL_c0ed724792a3e991)
void Change_Recycle1_6be18efbedb2(MenuEntry *e)
{
    typedef uint16_t u16;

		u16 fxx;

		Keyboard keyboard("指定したアイテムに変化。");
		int choice = keyboard.Open(fxx);
		if (choice >= 0) Process::Write16(0x31FB02A0, fxx);
	}

// 名称候補: alltour
// 元関数: alltour / GROUP_02937
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2875 (CPP_b41fc667ae2b947d)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L246
// 元登録条件: new MenuEntry(Color(Blue01) << "All Tours Selectable", alltour, "Season pass to all the tours")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void alltour_6f589ddbacae(MenuEntry *entry)
{
    typedef uint32_t u32;

        u32		toury;
        static bool beendone = false;

        Process::Read32(0x95D734, toury);
        if (toury != 0 && !beendone)
        {
            for (int tr = 0; tr < 64; tr++)
                Process::Write8(toury + 0x10 + (0x1 * tr), 0x01);

            OSD::Notify("All Tours Open");
            beendone = true;
        }
        else if (toury == 0 && beendone)
        {
            beendone = false;
        }
    }

// 名称候補: mujintou_Item4
// 元関数: mujintou_Item4 / GROUP_02938
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L199 (CPP_df472391d5130509)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L197 (CPP_e6333c0d744db7d4)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L263
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 石ころ所持数変更", nullptr, mujintou_Item4)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L58 (DECL_435621ce2b05901a)
void mujintou_Item4_6f76ffa8a0bb(MenuEntry *e)
{
    typedef uint8_t u8;

		u8 fxx;

		Keyboard keyboard("所持数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write8(0x330BE3D8, fxx);
		}
	}

// 名称候補: mujintou_syokuryou1
// 元関数: mujintou_syokuryou1 / GROUP_02943
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L444 (CPP_5610c44a16d0c7ea)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L446 (CPP_5c68e89c1a382e58)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L285
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 食料落下数変更", nullptr, mujintou_syokuryou1)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_syokuryou1_70aa51da9f30(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("食料落下数を指定してください。");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330F9080, fxx);
		}
	}

// 名称候補: mujintou_Item7
// 元関数: mujintou_Item7 / GROUP_02950
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L247 (CPP_b95a2d66e3179e97)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L245 (CPP_fde57ecc05da2346)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L266
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 おくすり所持数変更", nullptr, mujintou_Item7)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L58 (DECL_435621ce2b05901a)
void mujintou_Item7_71dcc5038413(MenuEntry *e)
{
    typedef uint8_t u8;

		u8 fxx;

		Keyboard keyboard("所持数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write8(0x330BE3D4, fxx);
		}
	}

// 名称候補: mujintou_Item1
// 元関数: mujintou_Item1 / GROUP_02951
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L151 (CPP_6e0e6c33f290a985)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L149 (CPP_bd2cd5c590cc7bcd)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L260
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 棒所持数変更", nullptr, mujintou_Item1)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L58 (DECL_435621ce2b05901a)
void mujintou_Item1_71e333db0d56(MenuEntry *e)
{
    typedef uint8_t u8;

		u8 fxx;

		Keyboard keyboard("所持数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write8(0x330BE3C8, fxx);
		}
	}

// 名称候補: wtw
// 元関数: wtw / GROUP_02972
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2513 (CPP_36e8ee1d076bcbfa)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L174
// 元登録条件: new MenuEntry(Color(Blue01) << "Walk over objects \uE052 + \uE079", wtw)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L22 (DECL_27a0b2e0dd26a56d)
void wtw_773f094bd338(MenuEntry *entry)
{
    typedef uint8_t u8;

		u8 val;

		if (Controller::IsKeysPressed(Key::L + Key::DPadUp))
		{
			if (Process::Read8(0x6503FF, val) && val == 0x0A)
			{
				Process::Write8(0x6503FF, 0xEA);
				Process::Write8(0x650417, 0xEA);
				Process::Write8(0x65057B, 0xEA);
				Process::Write8(0x6505F3, 0xEA);
				Process::Write32(0x6506A4, 0xE1A00000);
				Process::Write32(0x6506BC, 0xE1A00000);
				Process::Write8(0x6506C3, 0xEA);
				Process::Write8(0x6506EF, 0xEA);
				OSD::Notify("Walk over objects: Enabled!", Color::Green);
			}
			else if (val == 0xEA)
			{
				Process::Write8(0x6503FF, 0x0A);
				Process::Write8(0x650417, 0x0A);
				Process::Write8(0x65057B, 0x0A);
				Process::Write8(0x6505F3, 0xDA);
				Process::Write32(0x6506A4, 0xED841A05);
				Process::Write32(0x6506BC, 0xED840A07);
				Process::Write8(0x6506C3, 0x0A);
				Process::Write8(0x6506EF, 0x0A);
				OSD::Notify("Walk over objects: Disabled!", Color::Red);
			}
		}
	}

// 名称候補: slmoanms
// 元関数: slmoanms / GROUP_02989
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2608 (CPP_be7f6d025d501016)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L226
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Slow-Motion Animations", slmoanms, "Press \uE054 + \uE07B to enable or disable this. It makes animations act weird and do cool things! :)  Press ZL while walking around to change clothes while you move!")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void slmoanms_7bd73a5c3dcf(MenuEntry *entry)
{
    typedef uint32_t u32;

		u32 saval;

		if (Controller::IsKeysPressed(Key::ZL + Key::DPadLeft))
		{
			if (Process::Read32(0x654578, saval) && saval == 0x0A000004)
			{
				Process::Write32(0x654578, 0xE3A00001);
				Process::Write32(0x652C10, 0x40ff0000);
				Process::Write32(0x887880, 0x40C00000);
				OSD::Notify("Slow Animations: On", Color::Blue);
			}
			else if (saval == 0xE3A00001)
			{
				Process::Write32(0x654578, 0x0A000004);
				Process::Write32(0x652C10, 0x3F800000);
				Process::Write32(0x887880, 0x3F800000);
				OSD::Notify("Slow Animations: Off", Color::Orange);
			}
		}
	}

// 名称候補: アイテムランダマイザ
// 元関数: DD_Spam / GROUP_02993
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Drop.cpp:L58 (CPP_18c3a8af0c91c9e8)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L98
// 元登録条件: new MenuFolder(Color::DodgerBlue << "ドロップ",
// 元登録条件: 			{
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "ドロップ無効化", DropStopper),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::L, "オン"),
// 元登録条件: 					Hotkey(Key::R, "オフ"),
// 元登録条件: 				}),
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "連続スライドドロップ", DD_Spam), Hotkey(Key::R + DL, "オン/オフ")),
// 元登録条件: 				/*
// 元登録条件: 				EntryWithHotkey(new MenuEntry("アイテムランダマイザ", ItemRandomizer),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::R, "ランダムに変更"),
// 元登録条件: 					Hotkey(Key::R + X, "設定変更"),
// 元登録条件: 				}),
// 元登録条件: 				*/
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "アイテム変更", ChangeItemID), Hotkey(Key::Y + DR, "")),
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "ドロップ変更", ChangeDropID), Hotkey(Key::Y + DL, "")),
// 元登録条件: 								new MenuEntry(Color::DodgerBlue << "タッチドロップ", TouchDrop),
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "自動ドロップ", AutoDrop), Hotkey(Key::B + DL, "")),
// 元登録条件: 				/*
// 元登録条件: 				EntryWithHotkey(new MenuEntry("マップエディタ \ue001 + \ue07b", MapEditor),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::A, "置く / 消す"),
// 元登録条件: 					Hotkey(Key::B + DL, "モード切替"),
// 元登録条件: 				}),
// 元登録条件: 				*/
// 元登録条件: 				/*
// 元登録条件: 				EntryWithHotkey(new MenuEntry("塗りつぶし", FillDrop),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::L + B, "Change size & location"),
// 元登録条件: 					Hotkey(Key::B + DD, "Drop!"),
// 元登録条件: 				}),
// 元登録条件: 				EntryWithHotkey(new MenuEntry("四角形", BoxDrop),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::L + B, "Change size & location"),
// 元登録条件: 					Hotkey(Key::B + DD, "Drop!"),
// 元登録条件: 				}),
// 元登録条件: 				*/
// 元登録条件: 			})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L105
// 元登録条件: EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "連続スライドドロップ", DD_Spam), Hotkey(Key::R + DL, "オン/オフ"))
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L105
// 元登録条件: new MenuEntry(Color::DodgerBlue << "連続スライドドロップ", DD_Spam)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Includes/types.h:L60 (DECL_8f7c81b04adb9ff7)
void DD_Spam_7c5fa365cc17(MenuEntry *e)
{
    typedef uint32_t u32;

		static bool mode;

		u32* infItemAddr1 = (u32*)0x19C4D0;
		u32* infItemAddr2 = (u32*)0x19C42C;
		const u32 nop = 0xE1A00000;

		if (*infItemAddr1 != 0)
		{
			*infItemAddr1 = 0;
			*infItemAddr2 = 0;
		}

		if (Controller::IsKeysPressed(e->Hotkeys[0].GetKeys()))
		{
			if (!mode)
			{
				mode = true;
				OSD::Notify("DragDropSpam: ON");
				Process::Write32(0x19c548, nop);
				Process::Write32(0x19dde4, nop);
				Process::Write32(0x26f000, nop);
			}
			else
			{
				mode = false;
				OSD::Notify("DragDropSpam: OFF");
				Process::Write32(0x19c548, 0xeb03fb85);
				Process::Write32(0x19dde4, 0xeb03f55e);
				Process::Write32(0x26f000, 0xeb00b0d7);
			}
		}
	}

// 名称候補: tourend
// 元関数: tourend / GROUP_03002
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4232 (CPP_d63d98aaac2cd5dd)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L245
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(Blue01) << "Force End Tour (Host only)", tourend, "Meh. ._."),{ Hotkey(Key::R, "End tour") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L245
// 元登録条件: new MenuEntry(Color(Blue01) << "Force End Tour (Host only)", tourend, "Meh. ._.")
void tourend_7ed4a5731f01(MenuEntry *entry)
{
        static bool button = false;

        if (entry->Hotkeys[0].IsDown() && !button)
        {
            Process::Write8(0x95171D, 1);
            button = true;
        }
        if (!entry->Hotkeys[0].IsDown())
            button = false;
    }

// 名称候補: mujintou_GTool2
// 元関数: mujintou_GTool2 / GROUP_03010
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L388 (CPP_0a983c4eead9f402)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L390 (CPP_0f1c8ef50850cea1)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L280
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 金のパチンコ所持変更", nullptr, mujintou_GTool2)
void mujintou_GTool2_7fe51ee4d608(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC300, 1 - choice);
	}

// 名称候補: bgmchange
// 元関数: bgmchange / GROUP_03031
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4574 (CPP_d1381a319d237a76)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L313
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Change Outside BGM", nullptr, bgmchange, "Change the music to be whatever you want it to be!")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L22 (DECL_27a0b2e0dd26a56d)
void bgmchange_85ea6b6ffd95(MenuEntry *entry)
{
    typedef uint8_t u8;

		static u8 bgmout = 0;
		Keyboard *kb = new Keyboard("Enter Music ID:");
		kb->IsHexadecimal(true);
		if (kb->Open(bgmout, bgmout) != -1)
		{
        OSD::Notify("Playing BGM 0x" << Utils::ToHex(bgmout));
        for (int i = 0x846784; i < 0x8468A0; i += 4)
            *(u8 *)i = bgmout;
		}
	}

// 名称候補: MovementChanger
// 元関数: MovementChanger / GROUP_03034
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L3053 (CPP_35cfa5f0d5db7c2e)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L179
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(Blue01) << "Movement Changer", MovementChanger, "Press the hotkeys to set the movement to swimming or walking. (Let's you walk in the ocean.)"),{ Hotkey(Key::ZL | Key::B, "Movement Changer") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L179
// 元登録条件: new MenuEntry(Color(Blue01) << "Movement Changer", MovementChanger, "Press the hotkeys to set the movement to swimming or walking. (Let's you walk in the ocean.)")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void MovementChanger_8725920e55cf(MenuEntry *entry)
{
    typedef uint32_t u32;

        u32 waval;
		static bool Message = false;

        if (entry->Hotkeys[0].IsDown() && !Message)
        {
			if (Process::Read32(0x64E82C, waval) && waval == 0xE3A00000)
            {
                Process::Write32(0x64E824, 0x03A00001);
                Process::Write32(0x64E82C, 0xE3A00001);
                Process::Write32(0x653154, 0xE1A00000);
                Process::Write32(0x653530, 0xE3A00000);
                Process::Write32(0x763ABC, 0xE3A00000);
                OSD::Notify("Movement Mode: Swimming", Color::Blue);
				Message = true;
            }
            else if (waval == 0xE3A00001)
            {
                Process::Write32(0x64E824, 0x03A00000);
                Process::Write32(0x64E82C, 0xE3A00000);
                Process::Write32(0x653154, 0xEBFC6348);
                Process::Write32(0x653530, 0xEB00AFB7);
                Process::Write32(0x763ABC, 0xE3A00001);
                OSD::Notify("Movement Mode: Walking", Color::Green);
				Message = true;
            }
        }
		if (!entry->Hotkeys[0].IsDown())
			Message = false;
    }

// 名称候補: 歩いたとこのアイテム消える / 走っても花散らない
// 元関数: NoBreakFlower / GROUP_03058
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L676 (CPP_f8197ab022c3d9d7)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L114
// 元登録条件: ModFolder(Color::Yellow, "\uE018", "動き、アクション",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry("歩いたとこのアイテム消える", nullptr, WalkRemover),
// 元登録条件: 			new MenuEntry("走っても花散らない", nullptr, NoBreakFlower),
// 元登録条件: 			new MenuEntry("座標移動 " FONT_A " + \uE006", CoordinatesModifier),
// 元登録条件: 			new MenuEntry("壁抜け " FONT_L " + " FONT_DU, WalkOverObjects),
// 元登録条件: 			new MenuEntry("タッチワープ", TouchWarping),
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L117
// 元登録条件: new MenuEntry("走っても花散らない", nullptr, NoBreakFlower)
void NoBreakFlower_8c04d8394071(MenuEntry *entry)
{
		Keyboard k("", {"オン", "オフ"});
		switch( k.Open() )
		{
		case 0:
			Process::Write32(0x596890, 0xE3A0001D);
			break;
		case 1:
			Process::Write32(0x596890, 0xEBF5990F);
			break;
		}
	}

// 名称候補: tourCorrupter
// 元関数: tourCorrupter / GROUP_03070
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4392 (CPP_773f7323013bcf6a)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L244
// 元登録条件: new MenuEntry(Color(Blue01) << "Tour corrupter", nullptr, tourCorrupter)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L22 (DECL_27a0b2e0dd26a56d)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void tourCorrupter_8f937388c493(MenuEntry *entry)
{
    typedef uint8_t u8;
    typedef uint32_t u32;

		//0x33037660: tour data
		u32 addr = 0x33037668;
		u8 number;
		for (u32 i = 0; i < 0x500; i++)
		{
			number = Utils::Random(0, 6);
			if (number == 4) number = 0x64;
			if (number == 3) number = 0xC;
			if (number == 2) number = 4;
			if (number == 5) number = 1;
			Process::Write8(addr + i, number);
		}
		Sleep(Milliseconds(100));
		MessageBox("Tours have been corrupted.")();
	}

// 名称候補: forcelag
// 元関数: forcelag / GROUP_03090
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2205 (CPP_c199dd0016b32092)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L281
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(Blue01) << "Network Lagger", forcelag, "Use this to temporarily slow down any online game you're in, then wait a few seconds and the game will start running normally again. Alternatively, you can press the second set of hotkeys to drop a quick lagspike. When it says 'Done!', you can drop another lagspike."),{ Hotkey(Key::ZL | Key::ZR, "Network Lagger Commencer!"), Hotkey(Key::L | Key::ZL, "Drop lag spike"), })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L281
// 元登録条件: new MenuEntry(Color(Blue01) << "Network Lagger", forcelag, "Use this to temporarily slow down any online game you're in, then wait a few seconds and the game will start running normally again. Alternatively, you can press the second set of hotkeys to drop a quick lagspike. When it says 'Done!', you can drop another lagspike.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L22 (DECL_27a0b2e0dd26a56d)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void forcelag_926b2da0bee5(MenuEntry *entry)
{
    typedef uint8_t u8;
    typedef uint32_t u32;

		u32 lagon;
		u32 lagoff;
		u8 lagms;
		static bool Message = false;

		if (entry->Hotkeys[0].IsDown())
		{
            Keyboard    keyboard("Enter lag timer: \nDon't lag it for more than 50 Seconds. \nThe shorter the lag time, the less chance you have of it erroring.");
            keyboard.IsHexadecimal(true);
			if (keyboard.Open(lagms, lagms) == -1)
            return;
			Process::Read32(0x31FF599C, lagon);
			Process::Write32(0x9EA000, lagon);
			OSD::Notify("Starting Network Lagger!", Color::Green);
			Message = true;
			Sleep(Seconds(1.30));
			Process::Write32(0x31FF599C, 0x00000000);
			OSD::Notify("Lagging Session!", Color::Blue);
			Message = true;
			Sleep(Seconds(lagms));
			Process::Read32(0x9EA000, lagoff);
			Process::Write32(0x31FF599C, lagoff);
			OSD::Notify("Lagging Session Over!", Color::Red);
			Message = true;
        }
		if (entry->Hotkeys[1].IsDown())
		{
			Process::Read32(0x31FF599C, lagon);
			Process::Write32(0x9EA000, lagon);
			OSD::Notify("Dropping Lagspike!", Color::Blue);
			Message = true;
			Sleep(Seconds(0.08));
			Process::Write32(0x31FF599C, 0x00000000);
			Sleep(Seconds(0.12));
			Process::Read32(0x9EA000, lagoff);
			Process::Write32(0x31FF599C, lagoff);
			OSD::Notify("Done!", Color::Blue);
			Message = true;
		}

		if (!entry->Hotkeys[0].IsDown())
			Message = false;
	}

// 名称候補: mujintou_Tool2
// 元関数: mujintou_Tool2 / GROUP_03092
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L332 (CPP_5708532157c5ec6e)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L334 (CPP_8300e7f30a0206b2)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L272
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 パチンコ所持変更", nullptr, mujintou_Tool2)
void mujintou_Tool2_9327b83f46aa(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2E0, 1 - choice);
	}

// 名称候補: アイテムランダマイザ
// 元関数: DropStopper / GROUP_03095
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Drop.cpp:L49 (CPP_8f4c9bc4a27b67ec)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L98
// 元登録条件: new MenuFolder(Color::DodgerBlue << "ドロップ",
// 元登録条件: 			{
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "ドロップ無効化", DropStopper),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::L, "オン"),
// 元登録条件: 					Hotkey(Key::R, "オフ"),
// 元登録条件: 				}),
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "連続スライドドロップ", DD_Spam), Hotkey(Key::R + DL, "オン/オフ")),
// 元登録条件: 				/*
// 元登録条件: 				EntryWithHotkey(new MenuEntry("アイテムランダマイザ", ItemRandomizer),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::R, "ランダムに変更"),
// 元登録条件: 					Hotkey(Key::R + X, "設定変更"),
// 元登録条件: 				}),
// 元登録条件: 				*/
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "アイテム変更", ChangeItemID), Hotkey(Key::Y + DR, "")),
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "ドロップ変更", ChangeDropID), Hotkey(Key::Y + DL, "")),
// 元登録条件: 								new MenuEntry(Color::DodgerBlue << "タッチドロップ", TouchDrop),
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "自動ドロップ", AutoDrop), Hotkey(Key::B + DL, "")),
// 元登録条件: 				/*
// 元登録条件: 				EntryWithHotkey(new MenuEntry("マップエディタ \ue001 + \ue07b", MapEditor),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::A, "置く / 消す"),
// 元登録条件: 					Hotkey(Key::B + DL, "モード切替"),
// 元登録条件: 				}),
// 元登録条件: 				*/
// 元登録条件: 				/*
// 元登録条件: 				EntryWithHotkey(new MenuEntry("塗りつぶし", FillDrop),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::L + B, "Change size & location"),
// 元登録条件: 					Hotkey(Key::B + DD, "Drop!"),
// 元登録条件: 				}),
// 元登録条件: 				EntryWithHotkey(new MenuEntry("四角形", BoxDrop),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::L + B, "Change size & location"),
// 元登録条件: 					Hotkey(Key::B + DD, "Drop!"),
// 元登録条件: 				}),
// 元登録条件: 				*/
// 元登録条件: 			})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L100
// 元登録条件: EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "ドロップ無効化", DropStopper),
// 元登録条件: 				{
// 元登録条件: 					Hotkey(Key::L, "オン"),
// 元登録条件: 					Hotkey(Key::R, "オフ"),
// 元登録条件: 				})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L100
// 元登録条件: new MenuEntry(Color::DodgerBlue << "ドロップ無効化", DropStopper)
void DropStopper_93d54bb14cd8(MenuEntry *e)
{
		if (Controller::IsKeysPressed(e->Hotkeys[0].GetKeys()))
			Process::Write32(0x5a0e50, 0xeaffff84);

		if (Controller::IsKeysPressed(e->Hotkeys[1].GetKeys()))
			Process::Write32(0x5a0e50, 0xaffff84);
	}

// 名称候補: アイテム保存 / 特殊アイテム表示
// 元関数: ViewTokushuItem / GROUP_03141
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L470 (CPP_8f8a46efd036c485)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L104
// 元登録条件: ModFolder(Color::SkyBlue, "\uE017", "インベントリ",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry("アイテム保存", nullptr, InventoryBackup),
// 元登録条件: 			new MenuEntry("すべて削除", nullptr, AllDelete),
// 元登録条件: 			new MenuEntry("特殊アイテム表示", nullptr, ViewTokushuItem),
// 元登録条件: 			new MenuEntry("置いてもなくならない", nullptr, InfinityItemDrop),
// 元登録条件: 			new MenuEntry("アイテム選択肢変更", nullptr, ChangeItemOption),
// 元登録条件: 			new MenuEntry("アイテム取得 " FONT_X " + " FONT_DR, TextToItem),
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/main.cpp:L108
// 元登録条件: new MenuEntry("特殊アイテム表示", nullptr, ViewTokushuItem)
void ViewTokushuItem_9c848528cd58(MenuEntry *entry)
{
		Keyboard key("", { "オン", "オフ" });
		int r = key.Open();

		if (r == 0) {
			Process::Write32(0x7238c0, 0xe1a00000);
			Process::Write32(0xad0250, 0x01000000);
		}
		else if (r == 1) {
			Process::Write32(0x7238c0, 0x0a000001);
			Process::Write32(0xad0250, 0x00000000);
		}
	}

// 名称候補: DisableItemLocks
// 元関数: DisableItemLocks / GROUP_03142
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Inventory.cpp:L10 (CPP_97bb839802a1819b)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/インベントリ.cpp:L8 (CPP_9e7737466dbd7ac8)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L214
// 元登録条件: new MenuFolder(Color(ccSkyBlue) << "*\uE016" << Color::SkyBlue << " インベントリ " << Color(ccSkyBlue) << "\uE016*", "",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 未使用アイテム表示", nullptr, DisableItemLocks),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 置いても無くならない", nullptr, InfinityItem),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 すべて削除", nullptr, DeleteInvItems),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 選択肢変更", nullptr, InvItemOption),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 所持金変更", nullptr, ChangeWalletBell),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE016 アイテム取得 \uE002 + \uE07C", TextToItem),
// 元登録条件:
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L216
// 元登録条件: new MenuEntry(Color::SkyBlue << "\uE016 未使用アイテム表示", nullptr, DisableItemLocks)
void DisableItemLocks_9c848528cd58(MenuEntry *e)
{
		Keyboard key("", { "オン", "オフ" });
		int r = key.Open();

		if (r == 0)
		{
			Process::Write32(0x7238c0, 0xe1a00000);
			Process::Write32(0xad0250, 0x01000000);
		}
		else if (r == 1)
		{
			Process::Write32(0x7238c0, 0x0a000001);
			Process::Write32(0xad0250, 0x00000000);
		}
	}

// 名称候補: mujintou_GTool3
// 元関数: mujintou_GTool3 / GROUP_03146
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L395 (CPP_8b22000e93846af4)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L397 (CPP_b9fa0689bcc981a7)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L281
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 金の網所持変更", nullptr, mujintou_GTool3)
void mujintou_GTool3_9e2632fe0798(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC304, 1 - choice);
	}

// 名称候補: akarusa_change
// 元関数: akarusa_change / GROUP_03149
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Misc.cpp:L15 (CPP_b9e6ec3f474e9242)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L301
// 元登録条件: new MenuEntry(Color::DodgerBlue << "村の明るさ変更", nullptr, akarusa_change)
void akarusa_change_9eb5f17c58eb(MenuEntry *e)
{
		float level;

		Keyboard keyboard("指定した明るさに変化。");
		if (keyboard.Open(level) >= 0) Process::WriteFloat(0x1E6CB4, level);
	}

// 名称候補: Pose1
// 元関数: Pose1 / GROUP_03157
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Movement.cpp:L235 (CPP_999cd0c6e227fd9b)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L150
// 元登録条件: EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "逆さま", Pose1, Color::SkyBlue << "キーを押すと自分が逆さになる。"),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(Key::L | DPadUp, "逆さま"),
// 元登録条件: 				Hotkey(Key::L | DPadDown, "通常")
// 元登録条件: 			})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L150
// 元登録条件: new MenuEntry(Color::DodgerBlue << "逆さま", Pose1, Color::SkyBlue << "キーを押すと自分が逆さになる。")
void Pose1_a0ad78590ce1(MenuEntry *entry)
{
		static bool key_status = false;

		if (entry->Hotkeys[0].IsDown() && key_status == false)
		{
			key_status = true;
			Process::Write32(0x3309A080, 0x00008000);
			Process::Write32(0x3309A1AC, 0x00008000);
			OSD::Notify("Hand stand " << Color::Green << "ON!");
		}

		if (entry->Hotkeys[1].IsDown() && key_status == false)
		{
			key_status = true;
			Process::Write32(0x3309A080, 0x00000000);
			Process::Write32(0x3309A1AC, 0x00000000);
			OSD::Notify("Hand stand " << Color::Red << "OFF!");
		}
		if (entry->Hotkeys[0].IsDown() == 0 && entry->Hotkeys[1].IsDown() == 0) key_status = false;
	}

// 名称候補: EncChat
// 元関数: EncChat / GROUP_03176
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L3632 (CPP_978946fe8e6456eb)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L265
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Encyclopedia Chatter", EncChat, "Use the Encyclopedia to chat. Prevents others from leaving when you do.")
void EncChat_a3fc7d89a415(MenuEntry *entry)
{
        if (entry->WasJustActivated())
			Process::Patch(0x32C5B384, 0x06);

		if (!entry->IsActivated())
			Process::Patch(0x32C5B384, 0x02);
    }

// 名称候補: mujintou_syokuryou2
// 元関数: mujintou_syokuryou2 / GROUP_03182
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L462 (CPP_079c06427c269d66)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L460 (CPP_5ff7e6c3768c2462)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L286
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 リンゴゲージ変更", nullptr, mujintou_syokuryou2)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_syokuryou2_a5cd64148cea(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("リンゴゲージを指定してください。");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330E0FDC, fxx);
		}
	}

// 名称候補: mujintou_Item6
// 元関数: mujintou_Item6 / GROUP_03193
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L231 (CPP_02340365b527f9ed)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L229 (CPP_7ab729beb431e7db)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L265
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 きんこうせき所持数変更", nullptr, mujintou_Item6)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L58 (DECL_435621ce2b05901a)
void mujintou_Item6_a802b21a4ba7(MenuEntry *e)
{
    typedef uint8_t u8;

		u8 fxx;

		Keyboard keyboard("所持数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write8(0x330BE3E0, fxx);
		}
	}

// 名称候補: infRock
// 元関数: infRock / GROUP_03203
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L3561 (CPP_aa7c90be3902960e)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L217
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Infinite money rock + set item", infRock, menuRock, "Money will drop from the money rock infinitely. Touch the white keyboard icon to modify the item that is dropped from the money rock. Works with drop type 0x11. May not work online.")
void infRock_ab4e300694ea(MenuEntry *entry)
{
		//if (*(vu16 *)0x9536C0 < 0x60)
		//{
		Process::Write32(0x9536C0, 0xFFFF);
		Process::Write32(0x9536C4, 0);
		//}
	}

// 名称候補: maxturbo
// 元関数: maxturbo / GROUP_03283
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2251 (CPP_1f6ebe9dc450aee5)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L227
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Max Turbo Presses", maxturbo, "Use this to spam ASM Animations. Just hold the A, B, X or Y button.")
void maxturbo_ba18cd7549e5(MenuEntry *entry)
{
        Sleep(Seconds(0.0085f));
        Process::Write8(0x32922CA4, 0); //abxy
        Process::Write8(0x32922CA6, 0); //dpad
    }

// 名称候補: mujintou_hoof4
// 元関数: mujintou_hoof4 / GROUP_03287
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L311 (CPP_93800d32f39536b2)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L309 (CPP_a7a84df95ba93dd4)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L270
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 船の旗の残りの数変更", nullptr, mujintou_hoof4)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_hoof4_baa508d3e033(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("残りの数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330E0FF8, fxx);
		}
	}

// 名称候補: OtherPlayersCantPushYou / セーブメニュー出さない / 他人の当たり判定無効
// 元関数: OtherPlayersCantPushYou / GROUP_03299
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L807 (CPP_0461e36752987ecb)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/その他.cpp:L32 (CPP_df2757b6d279d08c)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L299
// 元登録条件: new MenuFolder(Color(ccSkyBlue) << "*\uE018" << Color::SkyBlue << " その他" << Color(ccSkyBlue) << " \uE018*", "",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 セーブメニュー非表示", nullptr, DisableSaveMenu),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 他の人に押されない", nullptr, OtherPlayersCantPushYou),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 ゲーム速度上昇", nullptr, FastGameSpeed),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 天気変更", nullptr, ChangeWeather),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::SkyBlue << "\uE018 カメラに全員映す", CameraAllPlayers),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(A + B, "リスト変更"),
// 元登録条件: 				Hotkey(B + DU, "モード切替"),
// 元登録条件: 			}),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::SkyBlue << "\uE018 アドレス監視", ViewAddress),
// 元登録条件: 			{
// 元登録条件: 				Hotkey(Key::R + A, "アドレス変更"),
// 元登録条件: 				Hotkey(Key::R + B, "リストに追加"),
// 元登録条件: 				Hotkey(Key::R + X, "リスト変更"),
// 元登録条件: 			}),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 関数呼び出し \uE000 + \uE07A", CallFunction),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE018 HEXエディタ", HexEditor),
// 元登録条件:
// 元登録条件:
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L302
// 元登録条件: new MenuEntry(Color::SkyBlue << "\uE018 他の人に押されない", nullptr, OtherPlayersCantPushYou)
void OtherPlayersCantPushYou_bda25ace4179(MenuEntry *entry)
{
		Keyboard k("", {"オン", "オフ"});

		switch(k.Open())
		{
			case 0:
				Process::Write8(0x650D83, 0xEA);
				break;
			case 1:
				Process::Write8(0x650D83, 0x2A);
				break;
		}
	}

// 名称候補: Moguru_black
// 元関数: Moguru_black / GROUP_03310
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Movement.cpp:L365 (CPP_931ddad5360b6771)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L165
// 元登録条件: new MenuEntry(Color::DodgerBlue << "潜ると空黒くなる", Moguru_black)
void Moguru_black_bf445b5dbe8e(MenuEntry *e)
{
		Process::Write32(0x3309a70c, 0xbff);
	}

// 名称候補: itemontreemod
// 元関数: itemontreemod / GROUP_03314
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L3522 (CPP_b4f4ab6e1ea6b9e5)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L213
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Tree Item Modifier", itemontreemod, "Changes item sprites on fruit trees to the drop item.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void itemontreemod_bfc0dcb18f40(MenuEntry *entry)
{
    typedef uint32_t u32;

		u32 invindex;

		Process::Read32(0x31F2DBF0, invindex);
		Process::Write32(0x2FE6A0, 0xE59F0020);
		Process::Write32(0x2FE6AC, 0xE3500000);
		Process::Write32(0x2FE6C8, invindex);
	}

// 名称候補: patternedit
// 元関数: patternedit / GROUP_03339
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4450 (CPP_9499c028ed7fc9bc)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L162
// 元登録条件: new MenuEntry(Color(Blue01) << "Edit All Patterns", patternedit, "Edit any pattern as if they're your own.")
void patternedit_c487a7aaf0a4(MenuEntry *entry)
{
        if (entry->WasJustActivated())
            Process::Patch(0x2FEC44, 0xE3A00001);

        if (!entry->IsActivated())
            Process::Patch(0x2FEC44, 0xE3A00000);
    }

// 名称候補: PickupMode
// 元関数: PickupMode / GROUP_03356
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Movement.cpp:L105 (CPP_96de75c524d017a0)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L143
// 元登録条件: new MenuEntry(Color::DodgerBlue << "ピックアップ変更", nullptr, PickupMode)
void PickupMode_c803e70e553e(MenuEntry *e)
{
		Keyboard key("", { "通常動作", "なんでも拾う", "雑草として抜く"/*, "四つ葉のクローバー"*/ });
		int a = key.Open();
		if (a == 0) Process::Write32(0x5989fc, 0xea000044);
		if (a == 1) Process::Write32(0x5989fc, 0xea000030);
		if (a == 2) Process::Write32(0x5989fc, 0xea000019);
	}

// 名称候補: noshot
// 元関数: noshot / GROUP_03366
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2460 (CPP_b307bf6385714437)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L294
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Disable L + R Screenshots", noshot, "Disable L and R screenshots! Use the plugin screenshot tool instead, it's better! This is my own version of the cheat. :p its a one liner. uwu")
void noshot_c9e78a4ee529(MenuEntry *entry)
{
        if (entry->WasJustActivated())
            Process::Patch(0x5B41A8, 0xE3A00001);

        if (!entry->IsActivated())
            Process::Patch(0x5B41A8, 0xE3500000);
    }

// 名称候補: itemrandom
// 元関数: itemrandom / GROUP_03378
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2771 (CPP_bb6a528e24100c25)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L165
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(Blue01) << "Item Randomizer", itemrandom, "Randomize items, flags, and coordinates for items. When you're done, be sure to reset the drop area by pressing the defined hotkeys. "),{ Hotkey(Key::ZR, "Apply Items"), Hotkey(Key::ZR | Key::X, "Set items"), Hotkey(Key::L | Key::DPadDown, "Random Drop Range") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L165
// 元登録条件: new MenuEntry(Color(Blue01) << "Item Randomizer", itemrandom, "Randomize items, flags, and coordinates for items. When you're done, be sure to reset the drop area by pressing the defined hotkeys. ")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L22 (DECL_27a0b2e0dd26a56d)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L23 (DECL_7b95ec037187a347)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void itemrandom_cbcf1ea4a00b(MenuEntry *entry)
{
    typedef uint8_t u8;
    typedef uint16_t u16;
    typedef uint32_t u32;

		u32 item1send;
		u32 item2send;
        u16 item1;
        u16 item2;
		u16 flag1;
		u16 flag2;
		u8 droploc1;
		//u8 range1;
		//u8 range2;
		//u8 range3;
		//u8 range4;
		//static bool Message = false;

		if (entry->Hotkeys[1].IsDown())
        {
			Sleep(Milliseconds(100));
            Keyboard Choices("Start Item");
            int UserChoice = Choices.Open(item1send);
			Process::Write32(0x9E0004, item1send);
			Sleep(Milliseconds(100));
            Keyboard Choices1("End Item");
            int UserChoice1 = Choices1.Open(item2send);
			Process::Write32(0x9E0008, item2send);
        }
        if (entry->Hotkeys[0].IsDown())
        {
			Process::Read16(0x9E0004, item1);
			Process::Read16(0x9E0008, item2);
			if (item1 != 0x0000)
			{
				if (item2 != 0x0000)
				{
					u16 itemidtoplace = Utils::Random(item1, item2);
					Process::Write16(0x31f2dbf0, itemidtoplace);
					Process::Write16(0x9E0000, itemidtoplace);
					Process::Write16(0x9E0020, itemidtoplace);
					//Process::Write16(selectedItem, itemidtoplace);
				}
			}
			else if (item1 == 0x0000)
			{
				if (item2 == 0x0000)
				{
					u16 nonzero = Utils::Random(0x009F, 0x00C8);
					Process::Write16(0x31f2dbf0, nonzero);
					Process::Write16(0x9E0000, nonzero);
					//Process::Write16(selectedItem, nonzero);
				}
			}
			//flags
			Process::Read16(0x9E0006, flag1);
			Process::Read16(0x9E000A, flag2);
			if (flag1 != 0x0000)
			{
				if (flag2 != 0x0000)
				{
					u16 flagtoset = Utils::Random(flag1, flag2);
					Process::Write16(0x31F2DBF2, flagtoset);
					Process::Write16(0x9E0002, flagtoset);
					Process::Write16(0x9E0022, flagtoset);
				}
			}
			else if (flag1 == 0x0000)
			{
				if (flag2 == 0x0000)
				{
					Process::Write16(0x31F2DBF2, 0x0000);
					Process::Write16(0x9E0002, 0x0000);
					Process::Write16(0x9E0022, 0x0000);
				}
				else if (flag2 != 0x0000)
				{
					u16 flagtoset = Utils::Random(0x0000, flag2);
					Process::Write16(0x31F2DBF2, flagtoset);
					Process::Write16(0x9E0002, flagtoset);
					Process::Write16(0x9E0022, flagtoset);
				}
			}
		}
		if (entry->Hotkeys[2].IsDown())
		{
			u8 droploc1 = Utils::Random(0x01, 0xFF);
			//u8 range1 = Utils::Random(0xE0, 0xFF);
			//u8 range2 = Utils::Random(0xE4, 0xFF);
			//u8 range3 = Utils::Random(0xE8, 0xFF);
			//u8 range4 = Utils::Random(0xE8, 0xFF);
			Process::Write8(0x59915C, droploc1);
			Process::Write8(0x599348, droploc1);
			Process::Write32(0x85FE58, 0xFBFAF9F8); //range1 essentially, but no longer random as the items would drop too far from you. Maybe if you weren't such a little fucking cum whore, this wouldnt be a problem.
			Process::Write32(0x5991E8, 0xE1A01000);
			Process::Write32(0x599248, 0xEA000006);
        }
    }

// 名称候補: mujintou_hoof1
// 元関数: mujintou_hoof1 / GROUP_03389
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L261 (CPP_548b6da5440dc153)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L263 (CPP_e4ec3f8cad8062b1)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L267
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 丸太の入手数変更", nullptr, mujintou_hoof1)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_hoof1_cdf9b1812baa(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("入手数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330BE3E4, fxx);
		}
	}

// 名称候補: menuTouchDrop / プレイヤー名変更 / 村名変更
// 元関数: menuTouchDrop / GROUP_03414
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4513 (CPP_660a38d45dc0a28a)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L146 (CPP_7761e7d300ec06df)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/ROTATION/Sources/cheats.cpp:L140 (CPP_d8b852812643fc58)
void menuTouchDrop_d42c9dac8683(MenuEntry *entry)
{
		/*optKb->Populate(cmnOpt);
		optKb->GetMessage() = "Bypass replace check?";
		switch (optKb->Open())
		{
		case 0:
			Process::Write32(0x5A036C, 0xE3A00001);
			break;
		case 1:
			Process::Write32(0x5A036C, 0xEB0002E1);
			break;
		default:
			break;
		}
		optKb->GetMessage() = "Choose option:";*/
	}

// 名称候補: Copy / DeleteInvItems / Motion1 / Motion2 / Motion3 / Motion4 / Motion5 / Motion6 / Motion7 / ViewSlotItems
// 元関数: DeleteInvItems / GROUP_03415
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Motion.cpp:L9 (CPP_44b40fa2ee47fa3d)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Inventory.cpp:L95 (CPP_48b4d758baabfb8a)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Motion.cpp:L25 (CPP_5504db8f56438f22)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Motion.cpp:L5 (CPP_6cf4649254b94f9e)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Inventory.cpp:L120 (CPP_7cdc492c466f5640)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Motion.cpp:L17 (CPP_934a0450df708db4)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Motion.cpp:L29 (CPP_9d83b2a1615c7f31)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Motion.cpp:L21 (CPP_d0abd58091e92cff)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Motion.cpp:L13 (CPP_d5d3d5636ab8bf81)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Inventory.cpp:L50 (CPP_dec288bf68865f00)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L172
// 元登録条件: new MenuFolder(Color::Orange << "インベントリ",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::Orange << "未使用アイテムを表示", nullptr, DisableItemLocks),
// 元登録条件: 			new MenuEntry(Color::Orange << "置いても無くならない", nullptr, InfinityItem),
// 元登録条件: 			new MenuEntry(Color::Orange << "すべて削除", nullptr, DeleteInvItems),
// 元登録条件: 			new MenuEntry(Color::Orange << "選択肢変更", nullptr, InvItemOption),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::Orange << "コピー", Copy), Hotkey(Key::R, "")),
// 元登録条件: 			EntryWithHotkey(new MenuEntry(Color::Orange << "アイテム取得", TextToItem), Hotkey(Key::X + DR, "")),
// 元登録条件: 			new MenuEntry(Color::Orange << "アイテムID表示", ViewSlotItems),
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L176
// 元登録条件: new MenuEntry(Color::Orange << "すべて削除", nullptr, DeleteInvItems)
void DeleteInvItems_d42c9dac8683(MenuEntry *e)
{

	}

// 名称候補: moonjump
// 元関数: moonjump / GROUP_03416
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2546 (CPP_6d786d4fb8097064)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L175
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(Blue01) << "Online moonjump", moonjump, "Press the hotkeys to go up!"),{ Hotkey(Key::ZL | Key::DPadUp, "Go up"), Hotkey(Key::ZL | Key::DPadDown, "Go down") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L175
// 元登録条件: new MenuEntry(Color(Blue01) << "Online moonjump", moonjump, "Press the hotkeys to go up!")
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L224
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(ccLightPurple) << "Force Animations (Everyone)", doonall, "Force specified animation on every player at once, or force everyone to moonjump with you. This does set the game to online mode to prevent issues when playing online. If you want to time travel in your town, set the game type to offline. NOTE: This does not work with ASM animations."),{ Hotkey(Key::ZR | Key::A, "Force animation on everyone"), Hotkey(Key::ZR | Key::R, "Idle Everyone"), Hotkey(Key::ZL | Key::DPadUp, "Moonjump Everyone") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L224
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Force Animations (Everyone)", doonall, "Force specified animation on every player at once, or force everyone to moonjump with you. This does set the game to online mode to prevent issues when playing online. If you want to time travel in your town, set the game type to offline. NOTE: This does not work with ASM animations.")
void moonjump_d45cc0c908e1(MenuEntry *entry)
{
		if (entry->Hotkeys[0].IsDown())
		{
			Process::Write32(0x33077C82, 0x007FFFFF);
			Process::Write32(0x33077DAE, 0x007FFFFF);
		}
		if (entry->Hotkeys[1].IsDown())
		{
			Process::Write32(0x33077C82, 0x00019D5D);
			Process::Write32(0x33077DAE, 0x00019D5D);
		}
	}

// 名称候補: mujintou_Item3
// 元関数: mujintou_Item3 / GROUP_03438
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L183 (CPP_8a4eaa2db4f3582c)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L181 (CPP_b6c10bfc7e8e32af)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L262
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 布きれ所持数変更", nullptr, mujintou_Item3)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L58 (DECL_435621ce2b05901a)
void mujintou_Item3_d91848344529(MenuEntry *e)
{
    typedef uint8_t u8;

		u8 fxx;

		Keyboard keyboard("所持数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write8(0x330BE3D0, fxx);
		}
	}

// 名称候補: autoPickup
// 元関数: autoPickup / GROUP_03439
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2895 (CPP_d884b43f3450f9ed)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L192
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(ccLightPurple) << "Auto Pickup", autoPickup, "Press the hotkeys to automatically pickup. Press it again to turn it off."),{ Hotkey(Key::ZL | Key::Y, "Auto Pickup") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L192
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Auto Pickup", autoPickup, "Press the hotkeys to automatically pickup. Press it again to turn it off.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void autoPickup_d976f4c3d755(MenuEntry *entry)
{
    typedef uint32_t u32;

		u32 PickupLoop;
		static bool Message = false;

		if (entry->Hotkeys[0].IsDown() && !Message)
		{
			if (Process::Read32(0x67CCB8, PickupLoop) && PickupLoop == 0x0A000039)
			{
				Process::Write32(0x67CCB8, 0xE1A00000); //Loop Pickup Code, This line was made by Brume, not Nico.
				Process::Write32(0x59A0D0, 0xE1A00000);
				Process::Write32(0x59A1BC, 0xE1A00000);
				Process::Write32(0x59A268, 0x31F2DBF0);
				OSD::Notify("Auto-Pickup: On", Color:: Blue);
				Message = true;
			}
			else if (PickupLoop == 0xE1A00000)
			{
				Process::Write32(0x67CCB8, 0x0A000039); //Loop Pickup Code, This line was made by Brume, not Nico.
				Process::Write32(0x59A0D0, 0x1A000041);
				Process::Write32(0x59A1BC, 0x0A000006);
				Process::Write32(0x59A268, 0x0095CFFC);
				OSD::Notify("Auto-Pickup: Off", Color::Orange);
				Message = true;
			}
		}
		if (!entry->Hotkeys[0].IsDown())
			Message = false;
	}

// 名称候補: inf_expression
// 元関数: inf_expression / GROUP_03458
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2294 (CPP_eaef3ef8e509c484)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L225
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(ccLightPurple) << "Infinite Expressions", inf_expression, "Press B to keep an expression lasting forever in a loop. You can smile forever! If only this worked irl... Thanks to Levi for this!"),{ Hotkey(Key::B, "Infinite Expression") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L225
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Infinite Expressions", inf_expression, "Press B to keep an expression lasting forever in a loop. You can smile forever! If only this worked irl... Thanks to Levi for this!")
void inf_expression_dd8290494b8b(MenuEntry *entry)
{
        if (entry->Hotkeys[0].IsDown())
		{
			Process::Write32(0x65E9B0, 0xE3A010FF);
		}
		if (!entry->Hotkeys[0].IsDown())
		{
			Process::Write32(0x65E9B0, 0xE1D010B0);
		}
	}

// 名称候補: ChangeDenger
// 元関数: ChangeDenger / GROUP_03470
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/General.cpp:L213 (CPP_dcc5c4058aabe669)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L73
// 元登録条件: new MenuFolder(Color::DodgerBlue << "ゲーム全般",
// 元登録条件: 			{
// 元登録条件: 				new MenuEntry(Color::DodgerBlue << "プレイヤー変更", nullptr, ChangePlayer),
// 元登録条件: 				new MenuEntry(Color::DodgerBlue << "エリア移動", nullptr, ChangeArea),
// 元登録条件: 				new MenuEntry(Color::DodgerBlue << "名前変更", nullptr, ChangeName),
// 元登録条件: 				new MenuEntry(Color::DodgerBlue << "国籍変更", nullptr, ChangeRegion),
// 元登録条件: 				new MenuEntry(Color::DodgerBlue << "性別変更", nullptr, ChangeDenger),
// 元登録条件: 				new MenuEntry(Color::DodgerBlue << "バッジ変更", nullptr, ChangeBadge),
// 元登録条件: 				EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "ソパカ写真編集", TCP_ImageEditor, "写真機の中で、キーを押す"), Hotkey(Key::R + DU, "")),
// 元登録条件:
// 元登録条件: 			})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L79
// 元登録条件: new MenuEntry(Color::DodgerBlue << "性別変更", nullptr, ChangeDenger)
void ChangeDenger_e02e1f2efe4d(MenuEntry *e)
{
		Keyboard key("プレイヤー", { "村長", "サブ1", "サブ2", "サブ3" });
		int r0 = key.Open();

		OSD::SwapBuffers();

		if (r0 >= 0)
		{
			Keyboard denger("性別", { "男", "女" });
			int r1 = denger.Open();

			if (r1 >= 0)
				Process::Write32(0x31F4F05A + r0 * 0xA480, r1);
		}

	}

// 名称候補: allseeding
// 元関数: allseeding / GROUP_03496
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L490 (CPP_a400fb9b74d42c80)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L184
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(ccLightPurple) << "All Seeder", allseeding, "Every item you interact with will be what you set the ID to. Any item. This will remind you that it's on. Use ID 0x00 to disable. Credit to Levi for this."),{ Hotkey(Key::Select | Key::DPadUp, "Change ID") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L184
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "All Seeder", allseeding, "Every item you interact with will be what you set the ID to. Any item. This will remind you that it's on. Use ID 0x00 to disable. Credit to Levi for this.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L23 (DECL_7b95ec037187a347)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void allseeding_e6516ad3d714(MenuEntry *entry)
{
    typedef uint16_t u16;
    typedef uint32_t u32;

		static int spriteID;
        static u32 allID = 0;
        static bool beendone = false;
        u16 remind;

        if (entry->WasJustActivated());
        {
            Process::Patch(0x839A00, 0xE59F1004);
            Process::Patch(0x839A04, 0xE12FFF1E);
            Process::Write32(0x839A0C, 0x9E0020);
        }
        if (entry->Hotkeys[0].IsDown())
        {
            Keyboard    keyboard("Choose an Item ID \nUse 0 to disable.");
            keyboard.IsHexadecimal(true);
            if (keyboard.Open(allID, allID) != -1)
            {
                if (allID != 0)
                {
                    Process::Patch(0x59FCD4, 0xEB0A6749);
                    Process::Write32(0x9E0020, allID);
                    if (spriteID == 0)
                        Process::Patch(0x59FCE0, 0xEB0A6746);

                    else if (spriteID == 1)
                        Process::Patch(0x59FCE0, 0xE59D1028);
                }
                else if (allID == 0)
                {
                    Process::Patch(0x59FCD4, 0xE59D1024);
                    Process::Patch(0x59FCE0, 0xE59D1028);
                }
            }
        }
        if (allID != 0)
        {
            Process::Read16(0x9513D4, remind);
            if (remind != 0xFFFF && !beendone)
            {
                OSD::Notify(Utils::Format("Reminder: All Seeder - 0x%08X", allID));
                beendone = true;
            }
            else if (remind == 0xFFFF && beendone)
                beendone = false;
        }
    }

// 名称候補: instantchop
// 元関数: instantchop / GROUP_03497
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L3181 (CPP_042d250edf01958b)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L205
// 元登録条件: new MenuEntry(Color(Blue01) << "Instant Tree Chop", instantchop, "Trees chop down with one hit. What kind of sourcery is this? Got arms like the hulk.")
void instantchop_e65863e069af(MenuEntry *entry)
{
        if (entry->WasJustActivated())
            Process::Write32(0x59945C, 0xE1A00000);

        if (!entry->IsActivated())
            Process::Write32(0x59945C, 0xCA000005);
    }

// 名称候補: CrazyScreen
// 元関数: CrazyScreen / GROUP_03503
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Misc.cpp:L136 (CPP_ceb0bebd4fc15875)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L306
// 元登録条件: new MenuEntry(Color::DodgerBlue << "画面崩壊", nullptr, CrazyScreen)
void CrazyScreen_e72678034dbf(MenuEntry *e)
{
		Keyboard key("", { "オン", "オフ" });
		int r = key.Open();
		if (r == 0) Process::Write32(0x569530, 0x40130020);
		if (r == 1) Process::Write32(0x569530, 0x40000000);
	}

// 名称候補: hatz
// 元関数: hatz / GROUP_03519
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L3805 (CPP_2ecc5ba67d8450eb)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L164
// 元登録条件: new MenuEntry(Color(Blue01) << "Wear helmet and accesory", hatz, "Wear a mask with a accesory. Make sure the accesory is on first! Thanks to Levi for making this awesome code! <3")
void hatz_ea53dba3a488(MenuEntry *entry)
{
        if (entry->WasJustActivated())
            Process::Patch(0x68C630, 0xE1A00000);

        if (!entry->IsActivated())
            Process::Patch(0x68C630, 0x3AFFFFD6);
    }

// 名称候補: FastGameSpeed
// 元関数: FastGameSpeed / GROUP_03524
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Misc.cpp:L26 (CPP_74056a2eb2cedd1c)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L302
// 元登録条件: new MenuEntry(Color::DodgerBlue << "ゲーム速度上昇", nullptr, FastGameSpeed)
void FastGameSpeed_eb0fc92cc531(MenuEntry *e)
{
		Keyboard key("", { "オン", "オフ" });

		int r = key.Open();

		if (r == 0) Process::Write32(0x54c6e8, 0xe3e004ff);
		if (r == 1) Process::Write32(0x54c6e8, 0xe59400a0);
	}

// 名称候補: WalkOver
// 元関数: WalkOver / GROUP_03527
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/Movement.cpp:L177 (CPP_f60f3dfd2251bf6b)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L148
// 元登録条件: EntryWithHotkey(new MenuEntry(Color::DodgerBlue << "壁抜け", WalkOver), Hotkey(Key::L + DL, ""))
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/InitEntrys.cpp:L148
// 元登録条件: new MenuEntry(Color::DodgerBlue << "壁抜け", WalkOver)
void WalkOver_ec2284e68051(MenuEntry *e)
{
		static bool mode;

		if (Controller::IsKeysPressed(e->Hotkeys[0].GetKeys())) {
			if (mode) {
				mode = false;
				OSD::Notify("Walk Over Objects: OFF");
				Process::Write32(0x0064EEF4, 0x0A000094);
				Process::Write32(0x0064EF0C, 0x0A000052);
				Process::Write32(0x0064F070, 0x0A000001);
				Process::Write32(0x0064F0E8, 0xDA000014);
				Process::Write32(0x0064F19C, 0xED841A05);
				Process::Write32(0x0064F1B4, 0xED840A07);
				Process::Write32(0x0064F1B8, 0x0A000026);
				Process::Write32(0x0064F1E4, 0x0A000065);
			}
			else {
				mode = true;
				OSD::Notify("Walk Over Objects: ON");
				Process::Write32(0x0064EEF4, 0xEA000094);
				Process::Write32(0x0064EF0C, 0xEA000052);
				Process::Write32(0x0064F070, 0xEA000001);
				Process::Write32(0x0064F0E8, 0xEA000014);
				Process::Write32(0x0064F19C, 0xE1A00000);
				Process::Write32(0x0064F1B4, 0xE1A00000);
				Process::Write32(0x0064F1B8, 0xEA000026);
				Process::Write32(0x0064F1E4, 0xEA000065);
			}
		}
	}

// 名称候補: digany
// 元関数: digany / GROUP_03547
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L4098 (CPP_c6d6d210436d69a6)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L197
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "AC:NH Tree Digger", digany, "Well, the name sucks, doesn't it? But seriously, you can dig anything up like it's buried! Use animation 49! :P")
void digany_f0190acad4d3(MenuEntry *entry)
{
		{
			Process::Write32(0x5999F8, 0xE3A00001);
			Process::Write32(0x599A0C, 0xE3A04013);
			Process::Write32(0x6761B8, 0xE3A01050);
			Process::Write32(0x676214, 0xE3A01050);
			Process::Write32(0x67A2F4, 0x00003414);
		}
	}

// 名称候補: mujintou_GTool4
// 元関数: mujintou_GTool4 / GROUP_03552
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L402 (CPP_27e3e0214be518aa)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L404 (CPP_3629662069755e6e)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L282
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 金のスコップ所持変更", nullptr, mujintou_GTool4)
void mujintou_GTool4_f0eeb2bd2e29(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC308, 1 - choice);
	}

// 名称候補: ChangePlayer
// 元関数: ChangePlayer / GROUP_03560
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/プレイヤー.cpp:L11 (CPP_d00ffdb304c26bf9)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/General.cpp:L10 (CPP_d5b4d23ffeff8338)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L182
// 元登録条件: new MenuFolder(Color(ccSkyBlue) << "*\uE050" << Color::SkyBlue << " プレイヤー" << Color(ccSkyBlue) << " \uE051*", "",
// 元登録条件: 		{
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 プレイヤー変更", nullptr, ChangePlayer),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 エリア移動", nullptr, ChangeArea),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 名前変更", nullptr, ChangeName),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 国籍変更", nullptr, ChangeRegion),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 性別変更", nullptr, ChangeDenger),
// 元登録条件: 			new MenuEntry(Color::SkyBlue << "\uE050 バッジ変更", nullptr, ChangeBadge),
// 元登録条件:
// 元登録条件: 		})
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L184
// 元登録条件: new MenuEntry(Color::SkyBlue << "\uE050 プレイヤー変更", nullptr, ChangePlayer)
void ChangePlayer_f1b7e08a483b(MenuEntry *e)
{
		Keyboard keyboard("指定したプレイヤーに変化。", {"村長", "サブ1", "サブ2", "サブ3"});
		int r = keyboard.Open();

		if (r >= 0)
			Process::Write32(0xAA914C, 0x31f49aa0 + r * 0xa480);
	}

// 名称候補: slattzylove
// 元関数: slattzylove / GROUP_03564
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2867 (CPP_6350311ac1993567)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L257
// 元登録条件: new MenuEntry(Color(Blue01) << "Bypass Checksums", slattzylove, "Bypasses region checks, save checks, and secure value checks! Thank you Slattz for this! <3 I LOVE YOU!!!")
void slattzylove_f1ec25b71730(MenuEntry *entry)
{
		Process::Write32(0x1D43A4, 0xE3A00001);
		Process::Write32(0x1D43C0, 0xE3A00001);
		Process::Write32(0x1D43D0, 0xE3A00001);
		Process::Write32(0x759024, 0xE1A00005);
	}

// 名称候補: animmod
// 元関数: animmod / GROUP_03565
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L2061 (CPP_792ae01849ab60e1)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L223
// 元登録条件: EntryWithHotkey(new MenuEntry(Color(ccLightPurple) << "Animation Modifier (ASM)", animmod, "ASM Alternative to the above cheat! Use this if the above code annoys you. owo Also allows two animations to be used at once!"),{ Hotkey(Key::ZR | Key::A, "Set Animation:"), Hotkey(Key::ZR | Key::B, "Set Secondary Animation:"), Hotkey(Key::A, "Set Fish"), Hotkey(Key::DPadUp, "Drop fish at player 1"), Hotkey(Key::DPadLeft, "Drop fish at player 2"), Hotkey(Key::DPadRight, "Drop fish at player 3"), Hotkey(Key::DPadDown, "Drop fish at player 4") })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L223
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Animation Modifier (ASM)", animmod, "ASM Alternative to the above cheat! Use this if the above code annoys you. owo Also allows two animations to be used at once!")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L22 (DECL_27a0b2e0dd26a56d)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L23 (DECL_7b95ec037187a347)
void animmod_f225e5732502(MenuEntry *entry)
{
    typedef uint8_t u8;
    typedef uint16_t u16;

		u16 randy = Utils::Random(0x22E1, 0x234B);
		u16 fish1;
		u16 fish2;

        if (entry->Hotkeys[0].IsDown()) //to set anim id
        {
            static u8 animID = 0;
            Keyboard    keyboard("Type an Animation ID");
            keyboard.IsHexadecimal(true);
            if (keyboard.Open(animID, animID) != -1)
            {
                if (animID != 0)
                {
                    Process::Patch(0x67CB98, 0xE3A01000 + animID);
                    Process::Patch(0x67CB84, 0xE1A00000);
                    Process::Patch(0x67C9E4, 0xE1A00000);
                }
                else
                {
                    Process::Patch(0x67CB98, 0xE3A0101E);
                    Process::Patch(0x67CB84, 0x1A00003C);
                    Process::Patch(0x67C9E4, 0x0A000071);
                }
            }
        }
		if (entry->Hotkeys[1].IsDown()) //to set seconday anim id
        {
            static u8 animID2 = 0;
            Keyboard    keyboard("Type an Animation ID");
            keyboard.IsHexadecimal(true);
            if (keyboard.Open(animID2, animID2) != -1)
            {
                if (animID2 != 0)
                {
                    Process::Patch(0x683FA8, 0xE3A01000 + animID2);
                    Process::Patch(0x683F28, 0xE1A00000);
					Process::Patch(0x683F70, 0xE1A00000);
					Process::Patch(0x683F78, 0xE1A00000);
					Process::Patch(0x683F94, 0xE1A00000);
                }
                else
                {
                    Process::Patch(0x683FA8, 0xE3A01070);
                    Process::Patch(0x683F28, 0x1A000008);
					Process::Patch(0x683F70, 0x0A000026);
					Process::Patch(0x683F78, 0x1A000015);
					Process::Patch(0x683F94, 0x1A000008);
                }
            }
        }
		if (Controller::IsKeysPressed(Key::A))
		{
			if (0x33077C84 == 80000000)
			{
				Process::Write32(0x33077C84, 0x00002117);
			}
		}
        if (entry->Hotkeys[2].IsDown())
        {
            {
                    Process::Write16(0x33077C86, randy);
            }
        }
		if (entry->Hotkeys[3].IsDown())
		{
			Process::Read16(0x3200C20A, fish1);
			Process::Read16(0x3200C208, fish2);
			Process::Write16(0x33077C8C, fish1);
			Process::Write16(0x33077C8A, fish2);
		}
		if (entry->Hotkeys[4].IsDown())
		{
			Process::Read16(0xAAE9CE, fish1);
			Process::Read16(0xAAE9CC, fish2);
			Process::Write16(0x33077C8C, fish1);
			Process::Write16(0x33077C8A, fish2);
		}
		if (entry->Hotkeys[5].IsDown())
		{
			Process::Read16(0xAAE9F4, fish1);
			Process::Read16(0xAAE9F2, fish2);
			Process::Write16(0x33077C8C, fish1);
			Process::Write16(0x33077C8A, fish2);
		}
		if (entry->Hotkeys[6].IsDown())
		{
			Process::Read16(0xAAEA1A, fish1);
			Process::Read16(0xAAEA18, fish2);
			Process::Write16(0x33077C8C, fish1);
			Process::Write16(0x33077C8A, fish2);
		}
    }

// 名称候補: mujintou_hoof2
// 元関数: mujintou_hoof2 / GROUP_03570
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L279 (CPP_421e00149644542d)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L277 (CPP_f0fe588b6fef3f97)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L268
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 船の旗の入手数変更", nullptr, mujintou_hoof2)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_hoof2_f2b4dfc19e26(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("入手数を指定してください");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330BE3E8, fxx);
		}
	}

// 名称候補: mujintou_hizuke2
// 元関数: mujintou_hizuke2 / GROUP_03586
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L430 (CPP_10871ee8a54d0da6)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L428 (CPP_ef8a3bd3aa95bc55)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L284
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 経過日数変更", nullptr, mujintou_hizuke2)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_hizuke2_f61ee691e34d(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("経過日数を指定してください。");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330B1794, fxx);
		}
	}

// 名称候補: TranslateJAP
// 元関数: TranslateJAP / GROUP_03594
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L642 (CPP_3645be023d64dfcb)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L271
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Japanese Communicator", TranslateJAP, "Refer to the plugin GUIDE button for tips on using this. Thanks to a stupid bitch for help with some sentences.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void TranslateJAP_f76c3aad047b(MenuEntry *entry)
{
    typedef uint32_t u32;

        u32		JPkeyboard1;
        u32		JPkeyboard2;
        u32		JPtypedtext;

        Process::Read32(0x95F11C, JPkeyboard1);
        if (JPkeyboard1 != 0)
        {
            Process::Read32(0x95F110, JPkeyboard2);
            {
                Process::Read32(JPkeyboard2, JPtypedtext);
                if (JPtypedtext == 0x041D0021 || JPtypedtext == 0x00680021)
                    Process::WriteString(JPkeyboard2, "こんにちは", StringFormat::Utf16);

				if (JPtypedtext == 0x04120021 || JPtypedtext == 0x00620021)
                    Process::WriteString(JPkeyboard2, "さようなら", StringFormat::Utf16);

				if (JPtypedtext == 0x039D0021 || JPtypedtext == 0x006E0021)
                    Process::WriteString(JPkeyboard2, "いいえ", StringFormat::Utf16);

				if (JPtypedtext == 0x03A50021 || JPtypedtext == 0x00790021)
                    Process::WriteString(JPkeyboard2, "はい", StringFormat::Utf16);

				if (JPtypedtext == 0x04100021 || JPtypedtext == 0x04300021)
                    Process::WriteString(JPkeyboard2, "ごめんなさい", StringFormat::Utf16);

				if (JPtypedtext == 0x00530021 || JPtypedtext == 0x00730021)
                    Process::WriteString(JPkeyboard2, "やめてください", StringFormat::Utf16);

				if (JPtypedtext == 0x00560021 || JPtypedtext == 0x00760021)
                    Process::WriteString(JPkeyboard2, "すばらしいです", StringFormat::Utf16);

				if (JPtypedtext == 0x04210021 || JPtypedtext == 0x04410021)
                    Process::WriteString(JPkeyboard2, "ここに来てください", StringFormat::Utf16);

				if (JPtypedtext == 0x04200021 || JPtypedtext == 0x04400021)
                    Process::WriteString(JPkeyboard2, "えいごでおねがい", StringFormat::Utf16);

				if (JPtypedtext == 0x00490021 || JPtypedtext == 0x00690021)
                    Process::WriteString(JPkeyboard2, "えいごしかはなせない", StringFormat::Utf16);

				if (JPtypedtext == 0x00310021)
                    Process::WriteString(JPkeyboard2, "にほんごのこと、さっぱりわからない", StringFormat::Utf16);

				if (JPtypedtext == 0x00320021)
                    Process::WriteString(JPkeyboard2, "にほんごならつうじれないです", StringFormat::Utf16);

				if (JPtypedtext == 0x00740021 || JPtypedtext == 0x04220021)
                    Process::WriteString(JPkeyboard2, "ありがとう", StringFormat::Utf16);
            }
        }
    }

// 名称候補: customSymbols
// 元関数: customSymbols / GROUP_03599
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/cheats.cpp:L822 (CPP_ec174678001ca2da)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Sources/main.cpp:L268
// 元登録条件: new MenuEntry(Color(ccLightPurple) << "Keyboard Extender", customSymbols, "Lots of text.")
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/FOXXY/Includes/types.h:L24 (DECL_f775aed0b25af1c7)
void customSymbols_f8b1a6e93c39(MenuEntry *entry)
{
    typedef uint32_t u32;

        u32		keyboard1;

        Process::Read32(0x95F11C, keyboard1);
        if (keyboard1 != 0)
        {
            keyboard1 += 0xC;
            Process::Write8(keyboard1, 0x41);
            keyboard1 += 0x11F;
            Process::Write8(keyboard1, 0x44);
        }
    }

// 名称候補: mujintou_hizuke1
// 元関数: mujintou_hizuke1 / GROUP_03606
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L412 (CPP_18f18d2dc83a7186)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L414 (CPP_d62552b22b1574e3)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L283
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 残り日数変更", nullptr, mujintou_hizuke1)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Includes/types.h:L60 (DECL_8af691687c6f959b)
void mujintou_hizuke1_fa2999b8969d(MenuEntry *e)
{
    typedef uint32_t u32;

		u32 fxx;

		Keyboard keyboard("残り日数を指定してください。");
		keyboard.IsHexadecimal(false);
		int choice = keyboard.Open(fxx);
		if (choice >= 0)
		{ // 入力あり
			Process::Write32(0x330B1798, fxx);
		}
	}

// 名称候補: スピードハックメニュー  / メニュースピード高速化
// 元関数: BoostMenuSpeed / GROUP_03621
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/SpeedHacks/MenuSpeed.cpp:L5 (CPP_5b233b35dd844184)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L42
// 元登録条件: new MenuFolder("SpeedHacks", "", {
// 元登録条件:         new MenuEntry("スピードハックメニュー " FONT_B "+" FONT_DD, SpeedHackMenu),
// 元登録条件:         new MenuEntry("ゲームスピード上昇 " FONT_B "+" FONT_DU, BoostGameSpeed),
// 元登録条件:         new MenuEntry("メニュースピード高速化", BoostMenuSpeed)
// 元登録条件:       })
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Sources/Cheats/make_folder.cpp:L45
// 元登録条件: new MenuEntry("メニュースピード高速化", BoostMenuSpeed)
// 関数内へ移設した型別名: reference/old_project/REFERENCES/CTRPF PROJECTS/AnimalBytesv0.7.4/Includes/types.h:L24 (DECL_681a1d3e72afb923)
void BoostMenuSpeed_fd2c096e4800(MenuEntry *entry)
{
    typedef uint32_t u32;

      // Enable
      if( entry->WasJustActivated() ) {
        *(u32*)(0x5FAF68) = 0xE3A00001;
        *(u32*)(0x5FB298) = 0x43B00000;
		*(u32*)(0x568BF8) = 0x43B00000;

        OSD::Notify(Color::Yellow << "Menu Accelerating!!");
      }

      // Disable
      if( !entry->IsActivated() ) {
        *(u32*)(0x5FAF68) = 0xEBFFF4B2;
        *(u32*)(0x5FB298) = 0x3F800000;
		*(u32*)(0x568BF8) = 0x3F800000;

        OSD::Notify("Stop Menu Acceleration");
    }
  }

// 名称候補: mujintou_STool4
// 元関数: mujintou_STool4 / GROUP_03624
// 元解析: IDA-gpt-6.1-sol-F018; 関数内収録/検査担当: gpt-6
// 確度LOW・実機未確認。外部状態依存なし、原処理token一致、ARM C++11構文PASS。
// アドレス/地域/更新版と名称の意味は未確定のためReviewへ。
// 有効化・無効化・取消と復元は原本体の仕様。元ホットキーが必要な場合は登録条件も参照。
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/JOKER/Sources/cheats/IslandGame.cpp:L374 (CPP_55bba8c69c40453f)
// 出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Cheats/無人島ゲーム.cpp:L376 (CPP_b07fb537fae382fd)
// 登録出典: reference/old_project/REFERENCES/CTRPF PROJECTS/Adios/Sources/Main.cpp:L278
// 元登録条件: new MenuEntry(Color(ccNormalOrange) << "\uE015 銀のスコップ所持変更", nullptr, mujintou_STool4)
void mujintou_STool4_fd8b3cfaa3cd(MenuEntry *e)
{
		Keyboard keyboard("このアイテムを入手しますか？", { "入手", "未入手" });
		int choice = keyboard.Open();
		if (choice >= 0) Process::Write8(0x330BC2F8, 1 - choice);
	}

} }
