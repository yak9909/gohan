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
    {0x267A50,0xE59F20F4}, {0x26F030,0xE92D41F0},
    {0x193988,0xE59000DC}, {0x57B238,0xE92D4010}, {0x57F944,0xE92D5FF0},
    {0x6D5510,0xE92D41F0}, {0x6D6214,0xE92D4010}, {0x6D72C0,0xE92D4070},
    {0x81B41C,0xE92D47F0}, {0x723878,0xE92D4038}, {0x1C4238,0xE92D4070},
    {0x26DB1C,0xE92D4070}, {0x2B9470,0xE92D4070}
};
const u32 kMenuList=0x9864FC, kManager=0x949D4C, kFlags=0x949D68;
const u32 kChestVt=0x8E5BB0, kCabinetVt=0x8EAB34;
const Item kEmpty={0x7FFE,0};
const u32 kSlots=10, kMaxSlots=0x7FFE;
enum Phase : u32 { Off, Starting, WaitField, Requested, Live, Returning, WaitChat, ReopeningChat };
u32 s_phase, s_publish, s_close, s_returnReleased, s_cancelReleased;
bool s_returnChat, s_installed, s_armed, s_actorGone, s_hold;
u32 s_actor, s_child, s_room, s_frames, s_initialSlot, s_pendingInitialSlot, s_originManager, s_nativeDepth;
const char *s_error;
Hook s_ctorHook, s_pocketReadHook, s_itemWriteHook;
struct NativeScope {
    NativeScope() {++s_nativeDepth;}
    ~NativeScope() {--s_nativeDepth;}
};
std::vector<Item> s_pending, s_items;
u32 s_chestTable[27], s_childTable[19]; // Itanium header2+primary methods; secondary tables remain native
const char *const s_oneTab[4]={"G_btnA_00",nullptr,nullptr,nullptr};
std::vector<Item> s_configured(1,kEmpty);
int s_slotIndex=-1;
u32 Load(u32 *p) {return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
void Store(u32 *p,u32 v) {__atomic_store_n(p,v,__ATOMIC_RELEASE);}
void W(u32 a,u32 v) {*reinterpret_cast<volatile u32 *>(a)=v;}
u8 B(u32 a) {return *reinterpret_cast<const volatile u8 *>(a);}
void WB(u32 a,u8 v) {*reinterpret_cast<volatile u8 *>(a)=v;}
u16 Room() {return *reinterpret_cast<const u16 *>(0x95133A);}
u32 Pages() {return std::max<u32>(1u,(static_cast<u32>(s_items.size())+kSlots-1u)/kSlots);}
void CopyTable(u32 *out,u32 original,u32 count) {
    std::memcpy(out,reinterpret_cast<const void *>(original-2u*sizeof(u32)),(count+2u)*sizeof(u32));
}
bool RequestChatClose() {
    if(!ChatKanji::NormalChatOpen()) return false;
    const u32 parent=Read(s_originManager+492u), keyboard=Read(parent+220u);
    u32 count=0;
    for(u32 node=Read(kMenuList+4u);node;node=Read(node+8u)) {
        if(++count>2u || (node!=parent+20u && node!=keyboard+20u)) return false;
    }
    if(!GameList::HoldFieldFromMenu(GameList::kHoldCloset)) return false;
    s_hold=true;
    // BsSkb's Cancel branch notifies both the parent and the tab waiting for chat.
    // Publishing result1 alone leaves the tab's completion signal unchanged.
    if(B(keyboard+4424u))WB(0x949D3B,0);
    W(keyboard+4580u,1);
    reinterpret_cast<void (*)(u32,int)>(0x522D40)(1,1);
    return true;
}
bool ChatGone() {
    return Read(s_originManager+492u)==0 && Read(0x94A654)==0
        && reinterpret_cast<bool (*)()>(0x6D1074)();
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
void *SetItem(u32,u32,const Item *) {
    // このクローゼットは在庫ではなく複製元。nativeのswap/stack後も検索結果を残す。
    return nullptr;
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
void HideExtraTabs(u32 self) {
    if(B(self+15520u)) {
        // B_btnは当たり判定だけ。絵を含む親と、別の兄弟にある影を隠す。
        for(const char *name : {"N_btn_01","N_btn_02","P_btn_sh_01","P_btn_sh_02"}) {
            const u32 pane=reinterpret_cast<u32 (*)(u32,const char *)>(0x567BAC)(self+624u,name);
            if(pane)WB(pane+183u,B(pane+183u)&~1u);
        }
    }
}
int SetupSlots(u32 self,u32 base,u32 bindings) {
    SetPages(self);
    const int result=reinterpret_cast<int (*)(u32,u32,u32)>(0x297980)(self,base,bindings);
    if(result)HideExtraTabs(self);
    return result;
}
void UpdatePocketInput(u32 self) {
    if(Read(self+38704u)<3u)return;
    for(u32 i=0;i<16u;++i) {
        const u32 window=self+80u;
        const u32 widget=B(window+33841u)?window+17424u+916u*i:window+2192u+876u*i;
        if(!B(widget+301u))
            reinterpret_cast<void (*)(u32,u32,int)>(0x26F030)(window,i,0);
    }
}
u32 Pocket() {
    const u32 player=reinterpret_cast<u32 (*)()>(0x2FB900)();
    return NativePortGuard::Heap(player)?player+27600u:0u;
}
__attribute__((noinline)) Item *ReadPocket(u32 container,u32 slot,u8 *outFlag,u32 fallback) {
    if(s_nativeDepth && slot<16u && container==Pocket()) {
        if(outFlag)*outFlag=B(container+64u+slot);
        return reinterpret_cast<Item *>(container+slot*sizeof(Item));
    }
    return HookContext::GetCurrent().OriginalFunction<Item *>(container,slot,outFlag,fallback);
}
__attribute__((noinline)) int WriteItem(u32 self,u32 uiIndex,const Item *item) {
    if(self!=s_actor || !s_actor)return HookContext::GetCurrent().OriginalFunction<int>(self,uiIndex,item);
    NativeScope scope;
    if(uiIndex<16u && item) {
        const u32 pocket=Pocket();
        if(!pocket)return 0;
        const u32 controls=self+33976u;
        const int from=reinterpret_cast<int (*)(u32)>(0x71A288)(controls);
        const int to=reinterpret_cast<int (*)(u32)>(0x71A23C)(controls);
        const Item *value=item;
        // 持ち物を複製元へ送る操作は破棄。通常swapの相手側の品を持ち物へ戻さない。
        if(from==static_cast<int>(uiIndex) && s_child && Index(s_child,static_cast<u32>(to))!=0xFFFFFFFFu)value=&kEmpty;
        // native pocket setterの正規化は低ID/没品を空や別IDへ変えるため、4byteをそのまま保つ。
        W(pocket+uiIndex*sizeof(Item),Read(reinterpret_cast<u32>(value)));
        WB(pocket+64u+uiIndex,0);
        return reinterpret_cast<int (*)(u32,u32)>(0x26DB1C)(self+80u,uiIndex);
    }
    return HookContext::GetCurrent().OriginalFunction<int>(self,uiIndex,item);
}
int LoadStep(u32 self,u32 allocator) {
    NativeScope scope;
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
    UpdatePocketInput(self);
    return result;
}
int Calc(u32 self) {
    NativeScope scope;
    UpdatePocketInput(self);
    const int result=reinterpret_cast<int (*)(u32)>(0x1C3F54)(self);
    UpdatePocketInput(self);
    if(s_child)HideExtraTabs(s_child);
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
    s_items.clear();s_pending.clear();
    Store(&s_publish,0u);Store(&s_close,0u);Store(&s_phase,Off);
}
void WaitForField() {
    if(!GameList::HoldField(true,GameList::kHoldCloset)) {s_error=GameList::LastError();Finish();return;}
    s_hold=true;s_frames=0;Store(&s_phase,WaitField);
}
bool ReopenChat() {
    if(!GameList::FieldReady() || !reinterpret_cast<bool (*)()>(0x6D1074)())return false;
    const u32 tab=Read(s_originManager+480u);
    if(!NativePortGuard::Heap(tab))return false;
    // The native chat button selects index6 and enters "chat tabs out".
    // Its following state issues menu5 itself, including field/background transitions.
    WB(tab+6427u,6);
    reinterpret_cast<void (*)(u32,u32,u32)>(0x81B41C)(tab+20u,0x6D6214,0);
    Store(&s_phase,ReopeningChat);s_frames=0;return true;
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
        s_originManager=Read(kManager);
        if(!NativePortGuard::Heap(s_originManager)
            || !NativePortGuard::Heap(reinterpret_cast<u32 (*)()>(0x2FB900)())) {
            s_error=u8"プレイヤーの持ち物を読めない状態です。";Finish();return;
        }
        if(s_returnChat) {
            if(!RequestChatClose()) {s_error=u8"通常チャットから開いてください。";Finish();return;}
            Store(&s_phase,WaitChat);
        } else if(Read(kMenuList+4u)) {s_error=u8"ほかの窓を閉じてから開いてください。";Finish();}
        else WaitForField();
        return;
    }
    if(Room()!=s_room || Read(kManager)!=s_originManager) {
        s_returnChat=false;
        Store(&s_close,1u);
        if(phase!=Returning && !s_actor && B(0x949D1E)!=2u) {BeginReturn();return;}
    }
    if(phase==WaitChat) {
        if(ChatGone()) {if(Load(&s_close))BeginReturn();else {s_frames=0;Store(&s_phase,WaitField);}}
        else if(++s_frames>180u) {
            s_error=u8"チャットの退場を完了できませんでした。";
            if(s_hold) {GameList::HoldField(false,GameList::kHoldCloset);s_hold=false;}
            Finish();
        }
        return;
    }
    if(phase==WaitField) {
        if(Load(&s_close) || ++s_frames>180u) {if(!Load(&s_close))s_error=u8"クローゼットを開けませんでした。";BeginReturn();return;}
        if(!GameList::FieldHidden())return;
        s_armed=true;
        if(!reinterpret_cast<int (*)(int,int)>(0x6D1330)(61,0)) {
            s_error=u8"クローゼットの開始要求が受理されませんでした。";BeginReturn();return;
        }
        s_frames=0;Store(&s_phase,Requested);return;
    }
    if(phase==Requested) {
        if(!s_actor && B(0x949D1E)==1u) {if(!Load(&s_close))s_error=u8"クローゼットを生成できませんでした。";BeginReturn();}
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
    if(phase==Returning && Load(&s_returnReleased)) {
        if(!s_returnChat) {if(Room()!=s_room || GameList::FieldShown())Finish();}
        else if(GameList::FieldShown())ReopenChat();
    }
    if(phase==ReopeningChat) {
        const u32 parent=Read(s_originManager+492u);
        if(ChatKanji::NormalChatOpen() && NativePortGuard::Heap(parent)
            && Read(parent+52u)==0x193764 && Read(parent+56u)==0)Finish();
        else if(++s_frames>180u) {
            s_error=u8"チャットを開き直せませんでした。";
            if(ChatGone())WB(0x949D24,2); // native map/tab restoration after a failed creation
            Finish();
        }
    }
}
bool Install() {
    if(s_installed)return true;
    if(!NativePortGuard::Matches(kAnchors) || !GridCursor::InstallFrameHook()
        || !GridCursor::AddExtraFrameStep(FrameStep))return false;
    s_ctorHook.InitializeForMitm(0x1C42D4,reinterpret_cast<u32>(Construct));
    s_pocketReadHook.InitializeForMitm(0x723878,reinterpret_cast<u32>(ReadPocket));
    s_itemWriteHook.InitializeForMitm(0x1C4238,reinterpret_cast<u32>(WriteItem));
    if(s_ctorHook.Enable()!=HookResult::Success || s_pocketReadHook.Enable()!=HookResult::Success
        || s_itemWriteHook.Enable()!=HookResult::Success) {
        s_itemWriteHook.Disable();s_pocketReadHook.Disable();s_ctorHook.Disable();return false;
    }
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
    Store(&s_cancelReleased,0u);
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
    const u32 keys=Controller::GetKeysDown(true), phase=Load(&s_phase);
    if(!(keys&static_cast<u32>(Key::B)))Store(&s_cancelReleased,1u);
    else if(Load(&s_cancelReleased) && phase!=Live && phase!=Returning && phase!=ReopeningChat)Close();
    if(Load(&s_phase)==Returning) {
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
