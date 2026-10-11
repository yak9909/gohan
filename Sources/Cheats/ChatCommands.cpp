#include "ChatCommands.hpp"
#include "ChatCommandParser.hpp"
#include "ChatKanji.hpp"
#include "ChatKanjiText.hpp"
#include "ChatIme.hpp"
#include "Cheats.hpp"
#include "NativePortGuard.hpp"
#include "GridCursor.hpp"
#include "GameList.hpp"
#include "GuiMenu.hpp"
#include "GuiDialog.hpp"
#include "ItemNames.hpp"
#include "PocketItem.hpp"
#include "HhdScreen.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace CTRPluginFramework;

namespace ChatCommands {
namespace {
using namespace ChatCommandParser;
const u32 kTm = 0x958114, kNumbers = 0xAD0158;
const NativePortGuard::Word kHooks[] = {
    {0x522D40,0xE59F2018}, {0x57D720,0xE92D41F0}, {0x522638,0xE92D41F0},
    {0x2FB3A4,0xE59F0000}, {0x56C5BC,0xE92D4010}, {0x56A9C4,0xE92D4010}, {0x6D1330,0xE92D4010},
    {0x300914,0xE59F0004}, {0x2F6E40,0xE92D4070}, {0x2F75E4,0xE35100A5}
};
Hook s_sendHook, s_returnHook;
bool s_installed;
u32 s_enabled, s_pause, s_incoming;
int s_index = -1;
bool s_wanted;
u32 s_hotkeyKeys, s_hotkeyPause, s_badCommand;
u32 s_hotkeyBlocked;
bool s_hotkeyHeld;
const u32 kHotkeyKeys[] = {
    (u32)Key::ZL, (u32)Key::L, (u32)Key::R, (u32)Key::ZR,
    (u32)Key::DPadUp, (u32)Key::DPadDown, (u32)Key::DPadLeft, (u32)Key::DPadRight,
    (u32)Key::A, (u32)Key::B, (u32)Key::X, (u32)Key::Y, (u32)Key::Select, (u32)Key::Start
};
char s_text[100];
u32 s_busyError;
enum Op : u32 { NoOp, Replace, CloseChat, ChangeTime, OpenTimeMenu };
u32 s_op;
Command s_time;
u16 s_replace[33];
u32 s_opResult;
bool s_list, s_retiring, s_commandList;
u32 s_lastKeys;
Kind s_afterClose = Kind::None;
u32 s_waitFrames;
char s_rows[GameList::kMaxItems][160];
const char *s_rowPointers[GameList::kMaxItems];
u16 s_ids[GameList::kMaxItems];
u32 s_count;
std::vector<u16> s_matches;
u32 s_page, s_previousRow, s_nextRow;
const u32 kNoRow = 0xFFFFFFFFu;
const u32 kPageItems = GameList::kMaxItems - 2u;
const char *const kCommands[] = {
    "list", "time set", "time set day", "time set night", "time set noon", "time set midnight",
    "time set [HH:]mm", "time set [yyyy-]MM-dd", "time add [HH:]mm", "time add [yyyy-]MM-dd",
    "time sub [HH:]mm", "time sub [yyyy-]MM-dd", "<ITEMID>", "<ITEMNAME>",
    "weather clear", "weather rain", "weather cloudy", "hair", "style", "eye", "eyes"
};
const char *const kInsert[] = {
    "list", "time set", "time set day", "time set night", "time set noon", "time set midnight",
    "time set ", "time set ", "time add ", "time add ", "time sub ", "time sub ", "", "",
    "weather clear", "weather rain", "weather cloudy", "hair", "style", "eye", "eyes"
};
void Error(const char *message) { GuiDialog::ShowMessage(Cheats::kChatCommands,message); }
bool Text(char *out) {
    if (!ChatKanji::NormalChatOpen()) return false;
    const u32 tm=NativePortGuard::Read(kTm);
    if (!NativePortGuard::Heap(tm)) return false;
    const u32 length=NativePortGuard::Read(tm+8), limit=NativePortGuard::Read(tm+0xC), buf=NativePortGuard::Read(tm+0x10);
    if (!length || length>32 || limit>32 || length>limit || !NativePortGuard::Heap(buf)) return false;
    return ChatKanjiText::Utf8(reinterpret_cast<const u16 *>(buf),length,out,100);
}
void SetText(const u16 *text) {
    const u32 tm=NativePortGuard::Read(kTm);
    if (!NativePortGuard::Heap(tm)) return;
    reinterpret_cast<void (*)(u32,const u16 *,int)>(0x522638)(tm,text,-1);
    // SetTextの既定カーソルは先頭。コマンドの続きを入力できるよう末尾へ移す。
    if (text) *reinterpret_cast<volatile u32 *>(tm+0x14)=NativePortGuard::Read(tm+8);
}
// SDKのMITMはPC相対LDRを移設しない。先頭LDRだけ同じ値で補い、原関数+4へ戻す。
__attribute__((naked)) u32 OriginalStatus(u32,int) {
    asm volatile("ldr r2, =0x009580E0\n\tldr pc, =0x00522D44");
}
bool CaptureCommand() {
    char text[100];
    if (!Text(text) || Parse(text).kind==Kind::None) return false;
    u32 expected=0;
    if (__atomic_compare_exchange_n(&s_incoming,&expected,2u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) {
        std::memcpy(s_text,text,std::strlen(text)+1);
        __atomic_store_n(&s_incoming,1u,__ATOMIC_RELEASE);
        SetText(nullptr);
    } else __atomic_store_n(&s_busyError,1u,__ATOMIC_RELEASE);
    return true;
}
__attribute__((noinline)) u32 SendStatus(u32 ready,int result) {
    if (__atomic_load_n(&s_enabled,__ATOMIC_ACQUIRE) && ready==1 && result==2 && CaptureCommand()) {
        return ready; // result-readyを立てず、nativeキーボードのwaitへ戻す
    }
    return OriginalStatus(ready,result);
}
__attribute__((noinline)) int ReturnWait(u32 self) {
    HookContext &ctx=HookContext::GetCurrent();
    char text[100];
    const bool command=__atomic_load_n(&s_enabled,__ATOMIC_ACQUIRE) && Text(text) && Parse(text).kind!=Kind::None;
    if (!command) return ctx.OriginalFunction<int>(self);
    const u32 flags=NativePortGuard::Read(kNumbers);
    *reinterpret_cast<volatile u32 *>(kNumbers)=flags & ~0x30u;
    const int result=ctx.OriginalFunction<int>(self);
    *reinterpret_cast<volatile u32 *>(kNumbers)=flags;
    return result;
}
bool Offline() {
    if (reinterpret_cast<int (*)()>(0x300914)()!=0) return false;
    const u32 net=NativePortGuard::Read(0x94D644);
    if (!NativePortGuard::Heap(net)) return false;
    const u32 own=*reinterpret_cast<const u8 *>(net+78440), peers=*reinterpret_cast<const u8 *>(net+78444)&0x0F;
    return peers==0 || (own<4 && (peers & ~(1u<<own))==0);
}
bool NormalClockRoom() {
    const int room=*reinterpret_cast<const u16 *>(0x95133A);
    return !reinterpret_cast<int (*)(u32,int)>(0x2F75E4)(0x180000u,room);
}
void FrameStep() {
    const Op op=static_cast<Op>(__atomic_load_n(&s_op,__ATOMIC_ACQUIRE));
    if (op==NoOp) return;
    u32 result=1;
    if (!__atomic_load_n(&s_enabled,__ATOMIC_ACQUIRE)) result=2;
    else if (op==Replace) {
        if (ChatKanji::NormalChatOpen()) SetText(s_replace); else result=2;
    } else if (op==CloseChat) {
        if (ChatKanji::NormalChatOpen()) OriginalStatus(1,1);
    } else if (op==OpenTimeMenu) {
        if (!Offline() || !NormalClockRoom() || !reinterpret_cast<int (*)(int,int)>(0x6D1330)(0x2E,0)) result=2;
    } else if (op==ChangeTime) {
        Date target;
        const Date *current=reinterpret_cast<const Date *(*)()>(0x2FB3A4)();
        if (!Offline() || !NormalClockRoom() || !ApplyTime(s_time,*current,target)) result=3;
        else {
            reinterpret_cast<void (*)(const Date *)>(0x56C5BC)(&target);
            reinterpret_cast<void (*)()>(0x56A9C4)();
        }
    }
    __atomic_store_n(&s_opResult,result,__ATOMIC_RELEASE);
    __atomic_store_n(&s_op,static_cast<u32>(NoOp),__ATOMIC_RELEASE);
}
bool Post(Op op) {
    if (__atomic_load_n(&s_op,__ATOMIC_ACQUIRE)!=NoOp) {Error(u8"前のコマンドを処理中です。");return false;}
    __atomic_store_n(&s_opResult,0u,__ATOMIC_RELEASE);
    __atomic_store_n(&s_op,static_cast<u32>(op),__ATOMIC_RELEASE);
    return true;
}
void AddItem(u16 id) {
    char name[128];
    if (!ItemNames::NameUtf8(id,name,sizeof(name))) {Error(u8"そのアイテムIDは存在しません。");return;}
    int slot=-1;bool updated=false;
    const auto result=PocketItem::Request(id,slot,updated);
    if (result!=PocketItem::Result::Ok) {Error(PocketItem::ResultName(result));return;}
    char message[180];
    std::snprintf(message,sizeof(message),u8"%s\nスロット index %d",name,slot);
    GuiMenu::Notify(Cheats::kPocketItem,message);
}
void FillPage() {
    s_count=0;s_previousRow=kNoRow;s_nextRow=kNoRow;
    if(s_page) {s_previousRow=s_count;s_rowPointers[s_count++]=u8"前の候補";}
    const u32 first=s_page*kPageItems;
    for(u32 i=first;i<s_matches.size() && i<first+kPageItems;++i) {
        const u16 id=s_matches[i];char name[128];
        if(!ItemNames::NameUtf8(id,name,sizeof(name)))continue;
        s_ids[s_count]=id;
        std::snprintf(s_rows[s_count],sizeof(s_rows[s_count]),"%04X %s",static_cast<unsigned>(id),name);
        s_rowPointers[s_count]=s_rows[s_count];++s_count;
    }
    if(first+kPageItems<s_matches.size()) {s_nextRow=s_count;s_rowPointers[s_count++]=u8"次の候補";}
}
void OpenList(bool commandList,const char *query=nullptr) {
    if (GameList::Wanted() || GameList::Present()) {Error(u8"別のリストを使用中です。");return;}
    s_count=0;
    if (commandList) {
        s_count=sizeof(kCommands)/sizeof(kCommands[0]);
        for(u32 i=0;i<s_count;++i)s_rowPointers[i]=kCommands[i];
    } else {
        if (!ItemNames::LoadNormal()) {Error(u8"ゲームのアイテム名を読めません。");return;}
        char normalized[100];
        if (!NormalizeKana(query,normalized,sizeof(normalized)) || !*normalized) {Error(u8"かなでアイテム名を入力してください。");return;}
        const u32 normal=ItemNames::NormalCount(), hidden=ItemNames::HiddenCount();
        s_matches.clear();
        for(u32 i=0;i<normal+hidden;++i) {
            const auto *entry=i>=normal?ItemNames::HiddenAt(i-normal):nullptr;
            const u32 id=i<normal?0x2000u+i:entry->id;
            if(id>=0x7FFE)continue;
            char kana[128], folded[128];
            if(!ItemNames::NameUtf8(static_cast<u16>(id),kana,sizeof(kana),true)
                || !NormalizeKana(kana,folded,sizeof(folded)) || !std::strstr(folded,normalized))continue;
            s_matches.push_back(static_cast<u16>(id));
        }
        if (s_matches.empty()) {Error(u8"一致するアイテムがありません。");return;}
        s_page=0;FillPage();
    }
    ChatIme::DismissForCommand();
    if(!GameList::SetItems(s_rowPointers,s_count)){Error(GameList::LastError());return;}
    GameList::SetOverChat(true);
    s_commandList=commandList;s_list=true;s_retiring=false;
    __atomic_store_n(&s_pause,1u,__ATOMIC_RELEASE);
    s_lastKeys=Controller::GetKeysDown(true);
    GameList::Show(0);
}
void CloseList() {
    GameList::Hide();s_list=false;s_retiring=true;
    GuiMenu::CaptureGameTouchUntilRelease();
}
bool IsActive(int) {return __atomic_load_n(&s_enabled,__ATOMIC_ACQUIRE)!=0;}
void SetActive(int,bool on) {
    if(on && !s_installed) {
        if(!NativePortGuard::Matches(kHooks) || !ChatIme::InstallCommandSupport()
            || !GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep)) {
            Error(u8"この版のチャット処理を確認できません。");return;
        }
        s_sendHook.InitializeForMitm(0x522D40,reinterpret_cast<u32>(SendStatus));
        s_returnHook.InitializeForMitm(0x57D720,reinterpret_cast<u32>(ReturnWait));
        if(s_sendHook.Enable()!=HookResult::Success || s_returnHook.Enable()!=HookResult::Success) {
            s_sendHook.Disable();s_returnHook.Disable();Error(u8"チャットのフックを登録できません。");return;
        }
        s_installed=true;
    }
    __atomic_store_n(&s_enabled,on?1u:0u,__ATOMIC_RELEASE);
    if(!on) {
        __atomic_store_n(&s_hotkeyPause,0u,__ATOMIC_RELEASE);
        if(s_list)CloseList();
        s_afterClose=Kind::None;
    }
}
}
bool KeyboardPaused() {
    return __atomic_load_n(&s_pause,__ATOMIC_ACQUIRE)!=0 || __atomic_load_n(&s_hotkeyPause,__ATOMIC_ACQUIRE)!=0;
}
bool HandleKeyboardHotkey() {
    if (!IsActive(0) || GuiMenu::IsVisible() || !ChatKanji::NormalChatOpen()) {
        s_hotkeyHeld=false;s_hotkeyBlocked=0;
        __atomic_store_n(&s_hotkeyPause,0u,__ATOMIC_RELEASE);
        return false;
    }
    const u32 keys=Controller::GetKeysDown(true), hotkey=__atomic_load_n(&s_hotkeyKeys,__ATOMIC_ACQUIRE);
    u32 buttons=0;
    for (u32 key : kHotkeyKeys) buttons |= keys & key;
    const bool held=hotkey!=0 && buttons==hotkey;
    s_hotkeyBlocked &= keys;
    if (held && !s_hotkeyHeld && s_hotkeyBlocked==0 && !__atomic_load_n(&s_pause,__ATOMIC_ACQUIRE)) {
        s_hotkeyBlocked |= hotkey;
        if (!CaptureCommand()) __atomic_store_n(&s_badCommand,1u,__ATOMIC_RELEASE);
    }
    s_hotkeyHeld=held;
    // Rを先に離しても、残ったBを取消として処理させない。
    __atomic_store_n(&s_hotkeyPause,s_hotkeyBlocked?1u:0u,__ATOMIC_RELEASE);
    return s_hotkeyBlocked!=0;
}
void Wire() {
    s_index=GuiMenu::FindItem(Cheats::kChatCommands);
}
void Shutdown() { s_wanted=false;SetActive(-1,false); }
void Tick(u16) {
    const bool wanted=s_index>=0 && GuiMenu::ItemApplied(s_index)!=0;
    if (wanted!=s_wanted) {s_wanted=wanted;SetActive(s_index,wanted);}
    const u16 mask=s_index>=0?GuiMenu::ItemAppliedHotkey(s_index):0;
    u32 hotkey=0;
    for (u32 i=0;i<GuiMenu::HB_COUNT;++i) if (mask & (1u<<i)) hotkey |= kHotkeyKeys[i];
    __atomic_store_n(&s_hotkeyKeys,hotkey,__ATOMIC_RELEASE);
    if(s_retiring && !GameList::Present()) {
        s_retiring=false;GameList::SetOverChat(false);
        __atomic_store_n(&s_pause,0u,__ATOMIC_RELEASE);
    }
    const u32 result=__atomic_exchange_n(&s_opResult,0u,__ATOMIC_ACQ_REL);
    if(result==2)Error(u8"画面が変わったか、操作できない状態です。");
    if(result==3)Error(u8"日時が範囲外か、通信中のため変更できません。日付は2012年から2050年です。");
    if(__atomic_exchange_n(&s_busyError,0u,__ATOMIC_ACQ_REL))Error(u8"前のコマンドを処理中です。");
    if(__atomic_exchange_n(&s_badCommand,0u,__ATOMIC_ACQ_REL))Error(u8"コマンドの書式が違います。listで一覧を開けます。");
    if(!IsActive(0)) {
        if(__atomic_load_n(&s_incoming,__ATOMIC_ACQUIRE)==1)__atomic_store_n(&s_incoming,0u,__ATOMIC_RELEASE);
        return;
    }
    if(s_afterClose!=Kind::None) {
        if(++s_waitFrames>180){s_afterClose=Kind::None;Error(u8"画面を切り替えられませんでした。");}
        else if(!ChatKanji::NormalChatOpen() && GameList::FieldShown() && !GuiMenu::IsVisible()
            && __atomic_load_n(&s_op,__ATOMIC_ACQUIRE)==NoOp) {
            const Kind next=s_afterClose;s_afterClose=Kind::None;
            if(next==Kind::Style) {if(!HhdScreen::Shown()&&!HhdScreen::Show())Error(HhdScreen::LastError());}
            else Post(OpenTimeMenu);
        }
    }
    if(s_list) {
        const u32 keys=GuiMenu::IsVisible()?0u:Controller::GetKeysDown(true), pressed=keys&~s_lastKeys;
        s_lastKeys=keys;
        GameList::FeedDpad(keys & (static_cast<u32>(Key::DPadUp)|static_cast<u32>(Key::DPadDown)
            |static_cast<u32>(Key::DPadLeft)|static_cast<u32>(Key::DPadRight)));
        GuiMenu::BlockGameDpad();
        const s32 selected=GameList::TakeDecided();
        if(pressed & static_cast<u32>(Key::B))CloseList();
        else if(selected>=0 && static_cast<u32>(selected)<s_count) {
            if(!s_commandList && (static_cast<u32>(selected)==s_previousRow || static_cast<u32>(selected)==s_nextRow)) {
                if(static_cast<u32>(selected)==s_previousRow)--s_page;else ++s_page;
                FillPage();GameList::SetItems(s_rowPointers,s_count);GameList::Select(0);
                return;
            }
            CloseList();
            if(s_commandList) {
                const char *text=kInsert[selected];
                std::memset(s_replace,0,sizeof(s_replace));
                for(size_t i=0;text[i]&&i<32;++i)s_replace[i]=static_cast<u16>(text[i]);
                Post(Replace);
            } else AddItem(s_ids[selected]);
        } else if(!ChatKanji::NormalChatOpen() || *GameList::LastError()) {
            CloseList();if(*GameList::LastError())Error(GameList::LastError());
        }
    }
    if(__atomic_load_n(&s_incoming,__ATOMIC_ACQUIRE)!=1)return;
    char text[100];std::memcpy(text,s_text,sizeof(text));
    __atomic_store_n(&s_incoming,0u,__ATOMIC_RELEASE);
    const Command command=Parse(text);
    ChatIme::DismissForCommand();
    if(s_list || s_retiring || s_afterClose!=Kind::None){Error(u8"前のコマンドを処理中です。");return;}
    switch(command.kind) {
    case Kind::List: OpenList(true);break;
    case Kind::ItemId: if(!ItemNames::LoadNormal())Error(u8"アイテム名を読めません。");else AddItem(command.item);break;
    case Kind::ItemName: OpenList(false,command.query);break;
    case Kind::Weather: if(!Cheats::SetWeather(command.weather))Error(u8"天気の処理が競合しています。");break;
    case Kind::Style:
    case Kind::TimeMenu:
        if(Post(CloseChat)){s_afterClose=command.kind;s_waitFrames=0;}break;
    case Kind::Time:
        if(__atomic_load_n(&s_op,__ATOMIC_ACQUIRE)==NoOp){s_time=command;Post(ChangeTime);}else Error(u8"処理中です。");break;
    default: Error(u8"コマンドの書式が違います。listで一覧を開けます。");break;
    }
}
}
