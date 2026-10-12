#include "ClosetMenu.hpp"
#include "NativePortGuard.hpp"
#include "GridCursor.hpp"
#include "GameList.hpp"
#include "GuiMenu.hpp"
#include "GuiDialog.hpp"
#include "ChatKanji.hpp"
#include "Cheats.hpp"
#include <CTRPluginFramework.hpp>
#include <vector>
#include <cstring>
#include <algorithm>
using namespace CTRPluginFramework;

namespace ClosetMenu {
namespace {
using NativePortGuard::Read;
const NativePortGuard::Word kAnchors[] = {
    {0x1C42D4,0xE92D4070}, {0x1C3BCC,0xE92D41F0}, {0x297BDC,0xE92D4010},
    {0x71A0B8,0xE92D4070}, {0x297B28,0xE92D40F0}, {0x2978E8,0xE92D4010},
    {0x6D1330,0xE92D4010}, {0x13F718,0xE52D4004}, {0x13F84C,0xE5912008},
    {0x267A50,0xE59F20F4}, {0x26F030,0xE92D41F0}
};
const u32 kMenuList=0x9864FC, kManager=0x949D4C, kFlags=0x949D68;
const u32 kChestVt=0x8E5BB0, kCabinetVt=0x8EAB34, kChatVt=0x8E5120, kKeyboardVt=0x8F1D28;
const Item kEmpty={0x7FFE,0};
const u32 kSlots=10, kMaxSlots=0x7FFE;
enum Phase : u32 { Off, Starting, WaitField, Requested, Live, Returning };
u32 s_phase, s_publish, s_close, s_returnReleased;
bool s_returnChat, s_installed, s_armed, s_actorGone, s_hold;
u32 s_actor, s_child, s_room, s_frames, s_initialSlot, s_pendingInitialSlot;
const char *s_error;
Hook s_ctorHook;
std::vector<Item> s_pending, s_items;
u32 s_chestTable[27], s_childTable[19]; // Itanium header2+primary methods; secondary tables remain native
const char *const s_oneTab[4]={"G_btnA_00",nullptr,nullptr,nullptr};
struct ChatState {
    u32 parent, keyboard, manager, nodes[2], count, flags, binding, liveKeyboard;
    u32 labels[3];
    u8 argument, manager520;
    u32 parentTable[27], keyboardTable[27];
} s_chat;
std::vector<Item> s_configured(1,kEmpty);
int s_slotIndex=-1;
u32 Load(u32 *p) {return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
void Store(u32 *p,u32 v) {__atomic_store_n(p,v,__ATOMIC_RELEASE);}
void W(u32 a,u32 v) {*reinterpret_cast<volatile u32 *>(a)=v;}
u8 B(u32 a) {return *reinterpret_cast<const volatile u8 *>(a);}
void WB(u32 a,u8 v) {*reinterpret_cast<volatile u8 *>(a)=v;}
u16 Room() {return *reinterpret_cast<const u16 *>(0x95133A);}
int Idle(u32) {return 2;}
u32 Pages() {return std::max<u32>(1u,(static_cast<u32>(s_items.size())+kSlots-1u)/kSlots);}
void CopyTable(u32 *out,u32 original,u32 count) {
    std::memcpy(out,reinterpret_cast<const void *>(original-2u*sizeof(u32)),(count+2u)*sizeof(u32));
}
void ListRemove(u32 node) {reinterpret_cast<void (*)(u32,u32)>(0x13F718)(kMenuList,node);}
void ListAppend(u32 node) {reinterpret_cast<void (*)(u32,u32)>(0x13F84C)(kMenuList,node);}

int DestroyChat(u32 self,u32 table,bool deleting) {
    const u32 original=table==kChatVt?kChatVt:kKeyboardVt;
    W(self,original);
    // scene teardown can destroy paused actors; never reattach their old pointers.
    if(original==kChatVt) s_chat.parent=0; else s_chat.keyboard=0;
    return reinterpret_cast<int (*)(u32)>(Read(original+(deleting?4u:0u)))(self);
}
int ChatDtor(u32 self) {return DestroyChat(self,kChatVt,false);}
int ChatDelete(u32 self) {return DestroyChat(self,kChatVt,true);}
int KeyboardDtor(u32 self) {return DestroyChat(self,kKeyboardVt,false);}
int KeyboardDelete(u32 self) {return DestroyChat(self,kKeyboardVt,true);}
bool PauseChat() {
    std::memset(&s_chat,0,sizeof(s_chat));
    if(!s_returnChat) return Read(kMenuList+4u)==0;
    if(!ChatKanji::NormalChatOpen()) return false;
    s_chat.manager=Read(kManager);
    s_chat.parent=Read(s_chat.manager+492u);
    s_chat.keyboard=Read(s_chat.parent+220u);
    if(!NativePortGuard::Heap(s_chat.keyboard) || Read(s_chat.keyboard)!=kKeyboardVt) return false;
    for(u32 node=Read(kMenuList+4u);node;node=Read(node+8u)) {
        if(s_chat.count>=2u || (node!=s_chat.parent+20u && node!=s_chat.keyboard+20u)) return false;
        s_chat.nodes[s_chat.count++]=node;
    }
    if(s_chat.count!=2u) return false;
    s_chat.flags=Read(kFlags);s_chat.binding=Read(0x958108);s_chat.liveKeyboard=Read(0x94A654);
    for(u32 i=0;i<3u;++i)s_chat.labels[i]=Read(0x986508+i*sizeof(u32));
    s_chat.argument=B(0x949D20);s_chat.manager520=B(s_chat.manager+520u);
    CopyTable(s_chat.parentTable,kChatVt,25u);CopyTable(s_chat.keyboardTable,kKeyboardVt,25u);
    s_chat.parentTable[2u+0u]=reinterpret_cast<u32>(ChatDtor);
    s_chat.parentTable[2u+1u]=reinterpret_cast<u32>(ChatDelete);
    s_chat.keyboardTable[2u+0u]=reinterpret_cast<u32>(KeyboardDtor);
    s_chat.keyboardTable[2u+1u]=reinterpret_cast<u32>(KeyboardDelete);
    for(u32 i : {9u,12u}) {
        s_chat.parentTable[2u+i]=reinterpret_cast<u32>(Idle);
        s_chat.keyboardTable[2u+i]=reinterpret_cast<u32>(Idle);
    }
    W(s_chat.parent,reinterpret_cast<u32>(s_chat.parentTable+2u));
    W(s_chat.keyboard,reinterpret_cast<u32>(s_chat.keyboardTable+2u));
    // Primitive remove preserves the chat's event locks. Request arg1 also keeps them at closet detach.
    for(u32 i=0;i<s_chat.count;++i)ListRemove(s_chat.nodes[i]);
    // Native controls also consult the live keyboard/binding and menu key labels.
    // Keep the actors, but give the closet the same input registration as an ordinary menu.
    W(s_chat.manager+492u,0);W(0x94A654,0);W(0x958108,0);
    reinterpret_cast<void (*)()>(0x6D1A84)();
    W(kFlags,Read(kFlags)&~0x28u);
    return true;
}
void RestoreChat() {
    const bool same=Room()==s_room && Read(kManager)==s_chat.manager;
    if(s_chat.parent)W(s_chat.parent,kChatVt);
    if(s_chat.keyboard)W(s_chat.keyboard,kKeyboardVt);
    if(same && s_chat.parent && s_chat.keyboard && Read(kMenuList+4u)==0) {
        for(u32 i=0;i<s_chat.count;++i)ListAppend(s_chat.nodes[i]);
        W(kFlags,(Read(kFlags)&~0x888u)|(s_chat.flags&0x888u));
        W(s_chat.manager+492u,s_chat.parent);
        W(0x958108,s_chat.binding);W(0x94A654,s_chat.liveKeyboard);
        for(u32 i=0;i<3u;++i)W(0x986508+i*sizeof(u32),s_chat.labels[i]);
        WB(0x949D20,s_chat.argument);WB(s_chat.manager+520u,s_chat.manager520);
    }
    std::memset(&s_chat,0,sizeof(s_chat));
}
u32 Index(u32 self,u32 uiIndex) {
    const u32 base=Read(self+15524u), page=Read(self+6240u);
    if(uiIndex<base || uiIndex-base>=kSlots || !page || page>Pages())return 0xFFFFFFFFu;
    return (page-1u)*kSlots+uiIndex-base;
}
Item *GetItem(u32 self,u32 uiIndex) {
    const u32 index=Index(self,uiIndex);
    return index<s_items.size()?&s_items[index]:const_cast<Item *>(&kEmpty);
}
void *SetItem(u32 self,u32 uiIndex,const Item *item) {
    const u32 index=Index(self,uiIndex);
    if(item && index<s_items.size())s_items[index]=*item;
    return nullptr; // native callers ignore R0; no stack-local pointer escapes
}
int FindEmpty(u32 self) {
    const u32 base=Read(self+15524u);
    for(u32 i=0;i<kSlots;++i)if(GetItem(self,base+i)->id==kEmpty.id)return base+i;
    return -1;
}
void SetPages(u32 self) {
    W(self+6236u,0);W(self+6256u,Pages());
    for(u32 i=0;i<4u;++i)W(self+6240u+i*sizeof(u32),s_initialSlot/kSlots+1u);
}
int SetupSlots(u32 self,u32 base,u32 bindings) {
    SetPages(self);
    const int result=reinterpret_cast<int (*)(u32,u32,u32)>(0x297980)(self,base,bindings);
    if(result) {
        for(const char *name : {"B_btn_01","B_btn_02","B_btn_03"}) {
            const u32 pane=reinterpret_cast<u32 (*)(u32,const char *)>(0x567BAC)(self+624u,name);
            if(pane)WB(pane+183u,B(pane+183u)&~1u);
        }
    }
    return result;
}
void UpdatePocketLocks(u32 self) {
    if(Read(self+38704u)<3u)return;
    for(u32 i=0;i<16u;++i) {
        const Item *item=reinterpret_cast<const Item *(*)(u32,u32)>(0x723854)(self+80u,i);
        const bool occupied=item && item->id!=kEmpty.id;
        // Native LoadStep may already disable an empty but unavailable pocket slot; don't unlock it.
        const u32 window=self+80u;
        const u32 widget=B(window+33841u)?window+17424u+916u*i:window+2192u+876u*i;
        if(occupied && (B(widget+217u) || B(widget+301u)))
            reinterpret_cast<void (*)(u32,u32,int)>(0x26F030)(window,i,1);
    }
}
int LoadStep(u32 self,u32 allocator) {
    if(Load(&s_close) || Room()!=s_room) {
        reinterpret_cast<void (*)(u32)>(0x5200B8)(self);
        return 3;
    }
    if(!Read(self+76u)) {
        const u32 child=reinterpret_cast<u32 (*)(u32,u32)>(0x139FF4)(15528u,allocator);
        if(!child) {s_error=u8"クローゼットのメモリを確保できません。";Store(&s_close,1u);return 3;}
        reinterpret_cast<u32 (*)(u32)>(0x297BDC)(child);
        CopyTable(s_childTable,kCabinetVt,17u);
        s_childTable[2u+9u]=reinterpret_cast<u32>(SetupSlots);
        s_childTable[2u+13u]=reinterpret_cast<u32>(GetItem);
        s_childTable[2u+14u]=reinterpret_cast<u32>(SetItem);
        s_childTable[2u+16u]=reinterpret_cast<u32>(FindEmpty);
        W(child,reinterpret_cast<u32>(s_childTable+2u));
        W(child+2664u,reinterpret_cast<u32>(s_oneTab));
        W(self+76u,child);s_child=child;
    }
    const int result=reinterpret_cast<int (*)(u32,u32)>(0x1C3BCC)(self,allocator);
    UpdatePocketLocks(self);
    return result;
}
int Calc(u32 self) {
    UpdatePocketLocks(self);
    const int result=reinterpret_cast<int (*)(u32)>(0x1C3F54)(self);
    UpdatePocketLocks(self);
    return result;
}
int DestroyChest(u32 self,bool deleting) {
    W(self,kChestVt);
    const int result=reinterpret_cast<int (*)(u32)>(Read(kChestVt+(deleting?4u:0u)))(self);
    if(self==s_actor) {s_actor=0;s_child=0;s_actorGone=true;}
    return result;
}
int ChestDtor(u32 self) {return DestroyChest(self,false);}
int ChestDelete(u32 self) {return DestroyChest(self,true);}
__attribute__((noinline)) u32 Construct(u32 self) {
    const u32 result=HookContext::GetCurrent().OriginalFunction<u32>(self);
    // word948486 is UiScreen_OpenById's runtime registry ID, not menu61.
    const u16 registry=*reinterpret_cast<const u16 *>(0x9863E4+61u*sizeof(u16));
    if(s_armed && *reinterpret_cast<const u16 *>(0x948486)==registry && !s_actor) {
        CopyTable(s_chestTable,kChestVt,25u);
        s_chestTable[2u+0u]=reinterpret_cast<u32>(ChestDtor);
        s_chestTable[2u+1u]=reinterpret_cast<u32>(ChestDelete);
        s_chestTable[2u+3u]=reinterpret_cast<u32>(LoadStep);
        s_chestTable[2u+9u]=reinterpret_cast<u32>(Calc);
        W(self,reinterpret_cast<u32>(s_chestTable+2u));s_actor=self;
        Store(&s_phase,Live);
    }
    return result;
}
void BeginReturn() {
    s_armed=false;
    if(s_hold) {GameList::HoldField(false,GameList::kHoldCloset);s_hold=false;}
    // The tab's normal close state is bypassed while held; consume the finished menu notification.
    W(kFlags,Read(kFlags)&~0x28u);
    Store(&s_returnReleased,0u);Store(&s_phase,Returning);s_frames=0;
}
void Finish() {
    RestoreChat();s_items.clear();s_pending.clear();
    Store(&s_publish,0u);Store(&s_close,0u);Store(&s_phase,Off);
}
void FrameStep() {
    const u32 phase=Load(&s_phase);
    if(phase==Off)return;
    if(Load(&s_publish)) {
        s_items.swap(s_pending);s_initialSlot=s_pendingInitialSlot;Store(&s_publish,0u);
        if(s_child) {
            SetPages(s_child);
            if(B(s_child+15520u)) {
                reinterpret_cast<void (*)(u32)>(0x297BA0)(s_child);
                reinterpret_cast<void (*)(u32)>(0x267660)(s_child);
            }
        }
    }
    if(phase==Starting) {
        s_room=Room();s_frames=0;s_actorGone=false;
        if(!NativePortGuard::Heap(Read(kManager))
            || !NativePortGuard::Heap(reinterpret_cast<u32 (*)()>(0x2FB900)())) {
            s_error=u8"プレイヤーの持ち物を読めない状態です。";Finish();return;
        }
        if(!PauseChat()) {s_error=u8"通常チャットか、ほかの窓が閉じた状態で開いてください。";Finish();return;}
        if(!GameList::HoldField(true,GameList::kHoldCloset)) {s_error=GameList::LastError();Finish();return;}
        s_hold=true;Store(&s_phase,WaitField);return;
    }
    if(Room()!=s_room) {
        Store(&s_close,1u);
        if(phase!=Returning && !s_actor && B(0x949D1E)!=2u) {BeginReturn();return;}
    }
    if(phase==WaitField) {
        if(Load(&s_close) || ++s_frames>180u) {if(!Load(&s_close))s_error=u8"クローゼットを開けませんでした。";BeginReturn();return;}
        if(!GameList::FieldHidden())return;
        s_armed=true;
        if(!reinterpret_cast<int (*)(int,int)>(0x6D1330)(61,s_returnChat?1:0)) {
            s_error=u8"クローゼットの開始要求が受理されませんでした。";BeginReturn();return;
        }
        s_frames=0;Store(&s_phase,Requested);return;
    }
    if(phase==Requested) {
        if(!s_actor && B(0x949D1E)==1u) {s_error=u8"クローゼットを生成できませんでした。";BeginReturn();}
        // An accepted native request belongs to the menu manager; never resubmit or fake completion.
        return;
    }
    if(phase==Live) {
        if(s_actorGone) {BeginReturn();return;}
        if(s_actor && Read(s_actor+38704u)<3u && ++s_frames>600u) {
            s_error=u8"クローゼットの読み込みを完了できませんでした。";Store(&s_close,1u);
        }
        if(s_actor && Load(&s_close) && Read(s_actor+38704u)>=3u)W(kFlags,Read(kFlags)|0x20u);
        return;
    }
    if(phase==Returning && Load(&s_returnReleased) && (Room()!=s_room || GameList::FieldShown()))Finish();
}
bool Install() {
    if(s_installed)return true;
    if(!NativePortGuard::Matches(kAnchors) || !GridCursor::InstallFrameHook()
        || !GridCursor::AddExtraFrameStep(FrameStep))return false;
    s_ctorHook.InitializeForMitm(0x1C42D4,reinterpret_cast<u32>(Construct));
    if(s_ctorHook.Enable()!=HookResult::Success)return false;
    s_installed=true;return true;
}
void ConfigItem(int,s32 value) {
    const u32 slot=s_slotIndex>=0?static_cast<u32>(GuiMenu::ItemApplied(s_slotIndex)):0u;
    if(slot>=kMaxSlots)return;
    if(s_configured.size()<=slot)s_configured.resize(slot+1u,kEmpty);
    s_configured[slot]={static_cast<u16>(value),0};
}
void OpenConfigured(int) {
    if(Busy()) {Close();GuiMenu::Close();return;}
    if(!Begin(false)) {GuiDialog::ShowMessage(Cheats::kClosetOpen,TakeError());return;}
    const u32 slot=s_slotIndex>=0?static_cast<u32>(GuiMenu::ItemApplied(s_slotIndex)):0u;
    if(!Publish(s_configured.data(),s_configured.size(),std::min(slot,static_cast<u32>(s_configured.size()-1u))))Close();
    GuiMenu::Close();
}
}
bool Begin(bool returnToChat) {
    if(Busy() || GameList::Wanted() || GameList::Present()) {s_error=u8"別の一覧を使用中です。";return false;}
    if(!Install()) {s_error=u8"この版のクローゼット処理を確認できません。";return false;}
    s_error=nullptr;s_returnChat=returnToChat;s_armed=false;s_actor=0;s_child=0;s_hold=false;
    s_items.assign(1,kEmpty);s_initialSlot=0;Store(&s_close,0u);Store(&s_publish,0u);
    Store(&s_phase,Starting);return true;
}
bool Publish(const Item *items,u32 count,u32 initialSlot) {
    if(!Busy() || Load(&s_publish) || !items || !count || count>kMaxSlots || initialSlot>=count)return false;
    s_pending.assign(items,items+count);
    s_pending.resize(((count+kSlots-1u)/kSlots)*kSlots,kEmpty);
    s_pendingInitialSlot=initialSlot;
    Store(&s_publish,1u);return true;
}
bool PublishSlots(const Slot *slots,u32 count,u32 initialSlot) {
    if(!slots || !count)return false;
    u32 size=1;
    for(u32 i=0;i<count;++i) {if(slots[i].index>=kMaxSlots)return false;size=std::max(size,slots[i].index+1u);}
    std::vector<Item> items(size,kEmpty);
    for(u32 i=0;i<count;++i)items[slots[i].index]=slots[i].item;
    return Publish(items.data(),items.size(),initialSlot);
}
void Close() {Store(&s_close,1u);}
bool Busy() {return Load(&s_phase)!=Off;}
bool BlocksInput() {const u32 p=Load(&s_phase);return p!=Off && (p!=Live || !s_actor || Read(s_actor+38704u)<3u);}
const char *TakeError() {const char *result=s_error;s_error=nullptr;return result?result:"";}
void Tick() {
    if(!Busy())return;
    if(BlocksInput()) {GuiMenu::BlockGameAll();GuiMenu::BlockGameTouch();}
    if(Load(&s_phase)==Returning) {
        const u32 keys=Controller::GetKeysDown(true);
        if(!keys && !Controller::IsKeyDown(Key::Touchpad)) {
            GuiMenu::CaptureGameTouchUntilRelease();Store(&s_returnReleased,1u);
        }
    }
}
void Wire() {
    s_slotIndex=GuiMenu::FindItem(Cheats::kClosetSlot);
    const int item=GuiMenu::FindItem(Cheats::kClosetItem), open=GuiMenu::FindItem(Cheats::kClosetOpen);
    if(item>=0)GuiMenu::RegisterApply(item,ConfigItem);
    if(open>=0)GuiMenu::RegisterExecute(open,OpenConfigured);
}
void Shutdown() {Close();Store(&s_returnReleased,1u);}
}
