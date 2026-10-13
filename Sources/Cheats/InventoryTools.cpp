#include "InventoryTools.hpp"
#include "Cheats.hpp"
#include "NativePortGuard.hpp"
#include "DecorLayout.hpp"
#include "InteriorChoice.hpp"
#include "GohanFiles.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include <CTRPluginFramework.hpp>
#include <cstring>
#include <algorithm>
using namespace CTRPluginFramework;

// IDA-gpt-6-F010: native6行の選択窓とDragIconGroupへ限定して追加する。
// USAのregister依存tail hookや共通textIDは移植せず、既存の行・cancel・wall actionを保つ。
namespace InventoryTools {
namespace {
using NativePortGuard::Read;
const NativePortGuard::Word kAnchors[] = {
    {0x19B7B0,0xE92D4FF0},{0x5E3D48,0xE92D4070},{0x5E3D1C,0xE3510000},
    {0x5E3D10,0xE3A01000},{0x19ED80,0xE92D41F0},{0x19CB74,0xE92D4FF0},
    {0x19FC58,0xE92D5FF0},{0x26DB1C,0xE92D4070},{0x303530,0xE92D4010},
    {0x303404,0xE92D4070},{0x305688,0xE92D41F0},{0x2D0660,0xE92D4010},
    {0x22C988,0xE92D47F0},{0x13F718,0xE52D4004},{0x13F84C,0xE5912008},
    {0x2B6EC0,0xE5912000},{0x22CA38,0xE92D4010},{0x22C88C,0xE92D4030},
    {0x29BC24,0xE92D4070},{0x29B364,0xE92D41F0},{0x815E8C,0xE92D47F0},
    {0x19FF4C,0xE92D4070}
};
const u32 kVtable=0x8E5220, kEmpty=0x7FFE, kPocketOffset=27600;
u32 s_trashOn, s_extraOn;
bool s_installed;
Hook s_buildHook,s_finalizeHook,s_copyHook,s_commitHook,s_dispatchHook,s_waitHook,s_updateHook,s_disposeHook;
u32 s_buildActor,s_extraActor,s_extraSlot,s_extraItem,s_originalCount,s_extraCount,s_extraKinds[4];
alignas(8) u8 s_cancel[32];
bool s_cancelCopied,s_committing;
const u16 *const kExtraText[] = {reinterpret_cast<const u16 *>(u"複製"),reinterpret_cast<const u16 *>(u"ラッピング"),
    reinterpret_cast<const u16 *>(u"収納する"),reinterpret_cast<const u16 *>(u"ローンを返す")};
u32 Load(u32 *p) {return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
void Store(u32 *p,u32 v) {__atomic_store_n(p,v,__ATOMIC_RELEASE);}
void W(u32 a,u32 v) {*reinterpret_cast<volatile u32 *>(a)=v;}
u8 B(u32 a) {return *reinterpret_cast<const volatile u8 *>(a);}
void WB(u32 a,u8 v) {*reinterpret_cast<volatile u8 *>(a)=v;}
u32 Player() {return reinterpret_cast<u32 (*)()>(0x2FB900)();}
u32 Actor() {
    const u32 holder=Read(0x986500);
    if(!NativePortGuard::Heap(holder))return 0;
    const u32 actor=Read(holder+12u);
    return NativePortGuard::Heap(actor) && Read(actor)==kVtable && !B(actor+36u)?actor:0u;
}
bool Owned(u32 actor) {return actor && actor==Actor();}
void Sound(u32 id) {reinterpret_cast<void (*)(u32)>(0x58C7D4)(id);}
void Error(const char *text) {GuiMenu::NotifyRed(Cheats::kInventoryOptions,text);}
void WritePocket(u32 actor,u32 player,u32 slot,u32 item,u8 flag=0) {
    W(player+kPocketOffset+slot*sizeof(u32),item);
    WB(player+kPocketOffset+64u+slot,flag);
    reinterpret_cast<void (*)(u32,u32)>(0x26DB1C)(actor+7872u,slot);
}
bool Checksum(u32 address) {
    const u32 lo=Read(address);
    return B(address+7u)==static_cast<u8>(lo+(lo>>8)+(lo>>16)+(lo>>24)+0xBAu);
}
bool ExtraAction(u32 actor,u32 kind) {
    const u32 player=Player(),slot=Read(actor+204u);
    if(!NativePortGuard::Heap(player) || slot>=16u || actor!=s_extraActor || slot!=s_extraSlot
        || Read(player+kPocketOffset+slot*sizeof(u32))!=s_extraItem)return false;
    const u32 item=s_extraItem;
    if((item&0x7FFFu)==kEmpty)return false;
    if(kind==0) {
        // 没品を空きとみなすnative検索は使わず、生の空き枠にだけ複製する。
        for(u32 i=0;i<16u;++i)if((Read(player+kPocketOffset+i*sizeof(u32))&0x7FFFu)==kEmpty) {
            WritePocket(actor,player,i,item,B(player+kPocketOffset+64u+slot));Sound(0x01000406);return true;
        }
        Error(u8"持ち物に空きがありません。");return false;
    }
    if(kind==1) {
        WritePocket(actor,player,slot,item,1);Sound(0x01000406);return true;
    }
    if(kind==2) {
        // native ChangeStockItemの保存領域。検索クローゼットの私有providerとは別。
        for(u32 i=0;i<180u;++i)if((Read(player+0x92F0u+i*sizeof(u32))&0x7FFFu)==kEmpty) {
            W(player+0x92F0u+i*sizeof(u32),item);WritePocket(actor,player,slot,kEmpty);Sound(0x0100038E);return true;
        }
        Error(u8"収納に空きがありません。");return false;
    }
    const u32 category=reinterpret_cast<u32 (*)(const u32 *)>(0x2FCB64)(&item);
    if(kind!=3 || category!=2u || !Checksum(player+0x6B94u) || !Checksum(player+0x6B8Cu))return false;
    const int money=reinterpret_cast<int (*)(const u32 *)>(0x305688)(&item);
    const int debt=reinterpret_cast<int (*)(u32)>(0x303530)(player+0x6B94u);
    const int bank=reinterpret_cast<int (*)(u32)>(0x303530)(player+0x6B8Cu);
    if(money<=0 || debt<=0 || bank<0 || bank>999999999)return false;
    const int paid=std::min(money,debt),extra=money-paid;
    // 返済を超えた分が銀行に入らない場合、ベル袋を消してしまわない。
    if(extra>999999999-bank) {Error(u8"余ったベルを貯金できません。");return false;}
    reinterpret_cast<void (*)(u32,int)>(0x303404)(player+0x6B94u,debt-paid);
    if(extra)reinterpret_cast<void (*)(u32,int)>(0x303404)(player+0x6B8Cu,bank+extra);
    WritePocket(actor,player,slot,kEmpty);Sound(0x01000492);return true;
}
__attribute__((noinline)) int BuildOptions(u32 actor,u32 slot) {
    auto &hook=HookContext::GetCurrent();
    const u32 saved=s_buildActor;
    s_buildActor=Load(&s_extraOn) && Owned(actor) && slot<16u?actor:0u;
    s_extraCount=0;s_cancelCopied=false;
    const int result=hook.OriginalFunction<int>(actor,slot);
    s_buildActor=saved;return result;
}
__attribute__((noinline)) int Finalize(u32 system,int count,int cancel) {
    auto &hook=HookContext::GetCurrent();
    if(s_buildActor && system==s_buildActor+224u && count>=1 && count<=6 && cancel==count-1) {
        const u32 player=Player(),slot=Read(s_buildActor+204u);
        if(NativePortGuard::Heap(player) && slot<16u) {
            s_originalCount=count;s_extraActor=s_buildActor;s_extraSlot=slot;
            s_extraItem=Read(player+kPocketOffset+slot*sizeof(u32));
            if((s_extraItem&0x7FFFu)!=kEmpty) {
                const u32 category=reinterpret_cast<u32 (*)(const u32 *)>(0x2FCB64)(&s_extraItem);
                // ベル袋では返済を先に入れる。元の項目で6行が埋まる品にもcancelを残す。
                const u32 order[4]={category==2u?3u:0u,category==2u?0u:1u,category==2u?1u:2u,category==2u?2u:3u};
                const u32 capacity=std::min<u32>(6u,Read(system+16u)),mask=Load(&s_extraOn);
                for(u32 kind : order)if(static_cast<u32>(count)+s_extraCount<capacity && (mask&(1u<<kind)) && (kind!=3u || category==2u))
                    s_extraKinds[s_extraCount++]=kind;
                count+=s_extraCount;cancel+=s_extraCount;
            }
        }
    }
    return hook.OriginalFunction<int>(system,count,cancel);
}
__attribute__((noinline)) int CopyOption(u32 system,int index,const void *descriptor) {
    auto &hook=HookContext::GetCurrent();
    if(s_buildActor && !s_committing && system==s_buildActor+224u && s_extraCount && index==static_cast<int>(s_originalCount)-1) {
        std::memcpy(s_cancel,descriptor,sizeof(s_cancel));s_cancelCopied=true;
    }
    return hook.OriginalFunction<int>(system,index,descriptor);
}
__attribute__((noinline)) int Commit(u32 system) {
    // COPYのhookを呼ぶとCTRPFのTLSがそちらへ切り替わる。元のCOMMITをここで保持する。
    auto &hook=HookContext::GetCurrent();
    if(s_buildActor && system==s_buildActor+224u && s_extraCount && s_cancelCopied) {
        s_committing=true;
        for(u32 i=0;i<s_extraCount;++i) {
            const u32 index=s_originalCount-1u+i,kind=s_extraKinds[i];
            alignas(8) u32 descriptor[8]={0,0,0,0,0,0,0x40u+kind,7};
            reinterpret_cast<void (*)(u32,u32,const void *)>(0x5E3D1C)(system,index,descriptor);
            const u32 word=Read(system+12u)+172u*index+8u;
            const u32 script[3]={0x904954,reinterpret_cast<u32>(kExtraText[kind]),0x3FFFFFFFu};
            reinterpret_cast<void (*)(u32,const void *)>(Read(Read(word)+24u))(word,script);
        }
        reinterpret_cast<void (*)(u32,u32,const void *)>(0x5E3D1C)(system,s_originalCount-1u+s_extraCount,s_cancel);
        s_committing=false;
    }
    return hook.OriginalFunction<int>(system);
}
__attribute__((noinline)) int Dispatch(u32 actor) {
    auto &hook=HookContext::GetCurrent();
    if(Owned(actor) && actor==s_extraActor && reinterpret_cast<int (*)(u32)>(0x71C610)(actor+444u)) {
        const u32 selected=reinterpret_cast<u32 (*)(u32)>(0x7592E0)(actor+224u);
        const u32 action=selected?Read(selected+168u):0;
        if(action>=0x40u && action<=0x43u) {
            reinterpret_cast<void (*)(u32)>(0x7592D4)(actor+224u);
            const u32 kind=action-0x40u;
            if(Load(&s_extraOn)&(1u<<kind))ExtraAction(actor,kind);
            return reinterpret_cast<int (*)(u32,u32,int)>(0x815E8C)(actor+164u,0x19CB74,0);
        }
    }
    return hook.OriginalFunction<int>(actor);
}

DecorLayout::Arc s_arc{};
DecorLayout::Layout s_layout{};
DecorLayout::Anim s_anim[6]{};
const char *const kAnimations[]={"hhd_trash_in.bclan","hhd_trash_out.bclan","hhd_trash_on.bclan", "hhd_trash_off.bclan","hhd_trash_ok.bclan","hhd_trash_loop.bclan"};
alignas(8) u8 s_node[304],s_group[320];
bool s_nodeMade,s_groupMade,s_linked,s_hover;
u32 s_choice,s_failed,s_retryFrames;
u32 s_trashActor,s_choicePlayer,s_idleFrames,s_hiddenPane;
bool s_oldVisible;
u8 s_oldEnabled;
int s_bound=-1;
float s_x,s_y;
u8 *s_file;u32 s_fileSize;
u32 Node() {return reinterpret_cast<u32>(s_node);}
u32 Group() {return reinterpret_cast<u32>(s_group);}
void TrashAnim(int index) {
    if(s_bound>=0)DecorLayout::Unbind(s_anim[s_bound]);
    s_bound=index;DecorLayout::Bind(s_anim[index],s_layout,"G_InOut");
}
void Detach(u32 actor) {
    // Dispose入口ではmanagerの登録や+36が既に変わる場合がある。まだ生きているthisを使う。
    if(s_linked && actor && actor==s_trashActor) {
        const u32 control=actor+41392u;
        if(Read(control+180u)==Node() || Read(control+260u)==Node())
            reinterpret_cast<void (*)(u32)>(Read(Read(control)+12u))(control);
        reinterpret_cast<void (*)(u32,u32)>(0x13F718)(control,Node());
        reinterpret_cast<void (*)(u32,u32)>(0x13F718)(control+228u,Group()+12u);
        if(s_hiddenPane)DecorLayout::SetVisible(reinterpret_cast<void *>(s_hiddenPane),s_oldVisible);
        const u32 letter=Read(actor+41768u);
        if(letter)WB(letter+8664u+217u,s_oldEnabled);
    }
    s_linked=false;s_hiddenPane=0;s_trashActor=0;s_hover=false;
}
void FreeTrash() {
    for(auto &anim:s_anim)DecorLayout::FreeAnim(anim);
    if(s_nodeMade)reinterpret_cast<void (*)(u32)>(Read(Read(Node())))(Node());
    if(s_groupMade)reinterpret_cast<void (*)(u32)>(Read(Read(Group())))(Group());
    s_nodeMade=s_groupMade=false;
    DecorLayout::Free(s_layout);DecorLayout::FreeArc(s_arc);
    s_bound=-1;
}
bool BuildTrash() {
    if(s_layout.built)return true;
    // native item_scrnのpriority=0x83。模様替えの6では持ち物背景の後ろに隠れる。
    if(!s_file || !DecorLayout::LoadArc(s_arc,s_file,s_fileSize)
        || !DecorLayout::Build(s_layout,s_arc,"hhd_trash.bclyt",0x2000,0x84))return false;
    for(u32 i=0;i<6u;++i)if(!DecorLayout::LoadAnim(s_anim[i],s_arc,kAnimations[i]))return false;
    if(!DecorLayout::Bind(s_anim[5],s_layout,"G_Loop"))return false;
    reinterpret_cast<void (*)(u32)>(0x22CA38)(Group());s_groupMade=true;
    reinterpret_cast<void (*)(u32)>(0x2D0660)(Node());s_nodeMade=true;
    const u32 groupParams[11]={reinterpret_cast<u32>(s_arc.holder),reinterpret_cast<u32>(s_layout.obj),0,0,0,0,0,0,0,0,0};
    reinterpret_cast<void (*)(u32,const void *)>(0x22C88C)(Group(),groupParams);
    // group+317だけがdrag開始を許す。クリック(+318)とitemの受け取り(+319)は残す。
    WB(Group()+317u,0);
    // 第2欄はアニメ群でありLayoutではない。LayoutはAddNodeがgroupから設定する。
    const u32 nodeParams[7]={reinterpret_cast<u32>(DecorLayout::Pane(s_layout,"B_Base_00")),0,0,0,0,19,0};
    if(!nodeParams[0])return false;
    reinterpret_cast<void (*)(u32,const void *)>(0x2B6EC0)(Node(),nodeParams);
    reinterpret_cast<void (*)(u32,u32,int)>(0x22C988)(Group(),Node(),0);
    WB(Node()+217u,1);WB(Node()+221u,0);TrashAnim(0);return true;
}
__attribute__((noinline)) int WaitInput(u32 actor) {
    auto &hook=HookContext::GetCurrent();
    if(s_linked && Owned(actor) && actor==s_trashActor) {
        const u32 control=actor+41392u;
        const bool decided=reinterpret_cast<int (*)(u32)>(0x722C34)(control)!=0;
        const u32 source=Read(control+180u),target=Read(control+260u);
        if(!Load(&s_trashOn) && decided && (source==Node() || target==Node()))
            return reinterpret_cast<int (*)(u32,int,u32)>(0x29B364)(control,1,0);
        if(decided && target==Node()) {
            const u32 slot=reinterpret_cast<u32 (*)(u32)>(0x71A288)(control),player=Player();
            if(slot<16u && NativePortGuard::Heap(player)) {
                WritePocket(actor,player,slot,kEmpty);TrashAnim(4);s_hover=false;
                return reinterpret_cast<int (*)(u32,int,u32)>(0x29B364)(control,1,0x01000962);
            }
            // 手紙など持ち物slot以外は捨てず、nativeの掴みだけ解いて戻す。
            return reinterpret_cast<int (*)(u32,int,u32)>(0x29B364)(control,1,0);
        } else if(decided && source==Node() && !target) {
            reinterpret_cast<void (*)(u32,int,u32)>(0x29B364)(control,1,0);
            if(InteriorChoice::Open(s_x,s_y,reinterpret_cast<const u16 *>(u"全て削除"),0x01000393)) {
                Store(&s_choice,1u);s_choicePlayer=Player();GuiMenu::CaptureGameTouchUntilRelease();
            }
            return 0;
        }
    }
    return hook.OriginalFunction<int>(actor);
}
__attribute__((noinline)) int Update(u32 actor) {
    auto &hook=HookContext::GetCurrent();
    if(s_choice && actor==s_trashActor && Owned(actor) && !(Read(0x949D68)&0x20u))return 2;
    return hook.OriginalFunction<int>(actor);
}
__attribute__((noinline)) int Dispose(u32 actor) {
    auto &hook=HookContext::GetCurrent();
    // letterのroot/drag controlが解放される前に外す。同じアドレスで次の持ち物が作られても再登録する。
    if(actor==s_trashActor)Detach(actor);
    return hook.OriginalFunction<int>(actor);
}
void FrameStep() {
    const u32 actor=Actor();
    const bool context=actor && Read(actor+176u)==0x19CB74 && !Read(actor+180u) && !GuiMenu::IsVisible();
    if(s_choice) {
        InteriorChoice::FrameStep(context && actor==s_trashActor && Player()==s_choicePlayer);
        if(!InteriorChoice::Busy()) {
            const int result=InteriorChoice::TakeResult();Store(&s_choice,0u);
            if(context && actor==s_trashActor && Player()==s_choicePlayer && result==0) {
                for(u32 i=0;i<16u;++i)WritePocket(actor,s_choicePlayer,i,kEmpty);
                TrashAnim(4);Sound(0x01000962);
            }
        }
    }
    const u32 letter=actor?Read(actor+41768u):0;
    const u32 pane=letter?Read(letter+8664u+12u):0;
    const bool valid=Load(&s_trashOn) && actor && letter && pane && B(actor+41780u)>=5u
        && Read(actor+176u)!=0x19AAC0 && !B(letter+9288u);
    if(!valid || (s_linked && actor!=s_trashActor)) {
        Detach(actor==s_trashActor?actor:0);
        s_retryFrames=0;
        if(++s_idleFrames>=3u && s_layout.made)FreeTrash();
        return;
    }
    if(s_retryFrames) {--s_retryFrames;return;}
    if(!BuildTrash()) {
        FreeTrash();s_retryFrames=30;
        // 入場時の一時的なheap不足で利用者のONを消さない。次の入場、または資源回復時に再試行する。
        if(!Load(&s_failed))GuiMenu::NotifyRed(Cheats::kPocketTrash,u8"ゴミ箱の資源を読み込めません。common/hhd_decorate.arcを確認してください。");
        Store(&s_failed,1u);return;
    }
    Store(&s_failed,0u);
    s_idleFrames=0;
    if(!s_linked) {
        s_trashActor=actor;
        s_hiddenPane=Read(letter+632u);
        if(!s_hiddenPane)return;
        s_oldVisible=DecorLayout::Visible(reinterpret_cast<void *>(s_hiddenPane));s_oldEnabled=B(letter+8664u+217u);
        // AppendIconGroupはgroupのnodeをcontrolへ移す。再入場時は空のgroupへnodeを戻してから登録する。
        if(!Read(Group()+4u))reinterpret_cast<void (*)(u32,u32)>(0x13F84C)(Group(),Node());
        reinterpret_cast<void (*)(u32,u32)>(0x29BC24)(actor+41392u,Group());s_linked=true;
        TrashAnim(0);
    }
    // alphaは子へ継承されないペインがあるため、手紙のsubtreeを可視flagで止める。
    WB(letter+8664u+217u,0);DecorLayout::SetVisible(reinterpret_cast<void *>(s_hiddenPane),false);
    // originが左寄せのHHDの部品も、native手紙ボタンの矩形中心へ合わせる。
    float nativeRect[4],trashRect[4],center[3],world[3];
    const u32 trashPane=reinterpret_cast<u32>(DecorLayout::Pane(s_layout,"B_Base_00"));
    if(!trashPane)return;
    const auto rect=reinterpret_cast<void (*)(float *,u32)>(0x73B43C);
    rect(nativeRect,pane);rect(trashRect,trashPane);
    center[0]=(nativeRect[0]+nativeRect[2])*0.5f;
    center[1]=(nativeRect[1]+nativeRect[3])*0.5f;center[2]=0;
    reinterpret_cast<void (*)(float *,u32,const float *)>(0x148CB0)(world,pane+128u,center);
    s_x=world[0];s_y=world[1];
    DecorLayout::SetPos(DecorLayout::Pane(s_layout,"N_Pos_00"),
        s_x-DecorLayout::PosX(reinterpret_cast<void *>(trashPane))-(trashRect[0]+trashRect[2])*0.5f,
        s_y-DecorLayout::PosY(reinterpret_cast<void *>(trashPane))-(trashRect[1]+trashRect[3])*0.5f);
    const u32 control=actor+41392u;
    const bool hover=Read(control+260u)==Node() || (Read(control+180u)==Node() && !B(control+248u));
    if(s_bound!=4 || DecorLayout::Done(s_anim[4])) {
        if(hover!=s_hover) {TrashAnim(hover?2:3);if(hover)Sound(0x0100080B);s_hover=hover;}
    }
    for(auto &anim:s_anim)DecorLayout::Step(anim);
    DecorLayout::Draw(s_layout,1);
}
bool Install() {
    if(s_installed)return true;
    if(!NativePortGuard::Matches(kAnchors) || !GridCursor::InstallFrameHook()
        || !GridCursor::AddExtraFrameStep(FrameStep))return false;
    const u32 sites[]={0x19B7B0,0x5E3D48,0x5E3D1C,0x5E3D10,0x19ED80,0x19CB74,0x19FC58,0x19FF4C};
    const u32 functions[]={reinterpret_cast<u32>(BuildOptions),reinterpret_cast<u32>(Finalize),reinterpret_cast<u32>(CopyOption),
        reinterpret_cast<u32>(Commit),reinterpret_cast<u32>(Dispatch),reinterpret_cast<u32>(WaitInput),reinterpret_cast<u32>(Update),reinterpret_cast<u32>(Dispose)};
    Hook *const hooks[]={&s_buildHook,&s_finalizeHook,&s_copyHook,&s_commitHook,&s_dispatchHook,&s_waitHook,&s_updateHook,&s_disposeHook};
    for(u32 i=0;i<8u;++i) {
        hooks[i]->InitializeForMitm(sites[i],functions[i]);
        if(hooks[i]->Enable()!=HookResult::Success) {for(auto h:hooks)h->Disable();return false;}
    }
    s_installed=true;return true;
}
bool TrashActive(int) {return Load(&s_trashOn)!=0;}
void SetTrash(int,bool enabled) {
    if(enabled && !Install()) {GuiMenu::NotifyRed(Cheats::kPocketTrash,u8"対象の持ち物処理を確認できません。");return;}
    if(enabled && !s_file) {
        char path[256];
        if(GohanFiles::CommonPath(path,sizeof(path),"hhd_decorate.arc"))s_file=GohanFiles::ReadAll(path,0x10000,s_fileSize);
        if(!s_file) {GuiMenu::NotifyRed(Cheats::kPocketTrash,u8"common/hhd_decorate.arcを読み込めません。");return;}
    }
    Store(&s_failed,0u);Store(&s_trashOn,enabled?1u:0u);
}
void SetExtra(int,s32 value) {
    const u32 mask=static_cast<u32>(value)&0xFu;
    if(mask && !Install()) {Error(u8"対象の持ち物処理を確認できません。");return;}
    Store(&s_extraOn,mask);
}
const GuiMenu::ToggleEffectFuncs kTrash={TrashActive,SetTrash};
}
void Wire() {
    const int trash=GuiMenu::FindItem(Cheats::kPocketTrash),extra=GuiMenu::FindItem(Cheats::kInventoryOptions);
    if(trash>=0)GuiMenu::RegisterToggleEffect(trash,&kTrash);
    if(extra>=0)GuiMenu::RegisterApply(extra,SetExtra);
    char path[256];
    if(!s_file && GohanFiles::CommonPath(path,sizeof(path),"hhd_decorate.arc"))s_file=GohanFiles::ReadAll(path,0x10000,s_fileSize);
}
void Tick() {if(Load(&s_choice)) {GuiMenu::BlockGameAll();GuiMenu::BlockGameTouch();}}
}
