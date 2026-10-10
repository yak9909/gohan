#include "InteriorTools.hpp"
#include "InteriorSnapshot.hpp"
#include "InteriorPlan.hpp"
#include "InteriorStorage.hpp"
#include "InteriorChoice.hpp"
#include "DecorCatalog.hpp"
#include "DecorTrashTable.h"
#include "DecorLayout.hpp"
#include "GohanFiles.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include "GuiDialog.hpp"
#include "Cheats.hpp"
#include <CTRPluginFramework.hpp>
#include <3ds.h>
#include <cstdio>
#include <cstring>

// IDA-gpt-6.1-sol-F016. File IO on menu thread; native calls on game thread.
namespace InteriorTools {
namespace {
using namespace CTRPluginFramework;
using InteriorSnapshot::Snapshot;
using InteriorSnapshot::Record;
using InteriorSnapshot::Item;
using InteriorPlan::Bounds;
enum State : u32 { Idle, CaptureSave, Prepare, Confirm, ConfirmWait, Approved, Cancelled,
    Backup, BackupWait, BackupOK, IOFailed, Remove, EmptyWait, Spawn, ReadyWait,
    Exchange, ExchangeWait, ExchangeOK, ExchangeFailed, Saved, Done };
u32 s_state, s_choiceBusy;
Snapshot s_target, s_before;
InteriorPlan::Plan s_plan;
Bounds s_bounds;
u32 s_scene, s_data, s_indoor, s_editor, s_room, s_frames, s_stable, s_next;
u32 s_freezeEditor, s_freezeScene, s_oldCalc, s_oldAdj;
bool s_swap, s_rollback, s_changed, s_chipsReset, s_hooked;
int s_slot = 1, s_operationSlot;
const char *s_message;
bool s_error;
u32 s_holdChip, s_holdFrames;
Record s_duplicate;
bool s_duplicateWaiting;
const char *const kTitle = u8"部屋の内装";
u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }
void W32(u32 a, u32 v) { *reinterpret_cast<volatile u32 *>(a) = v; }
bool Heap(u32 a) { return a >= 0x30000000 && a < 0x40000000 && !(a & 3); }
State Status(void) { return static_cast<State>(__atomic_load_n(&s_state, __ATOMIC_ACQUIRE)); }
void Set(State state) { __atomic_store_n(&s_state, static_cast<u32>(state), __ATOMIC_RELEASE); }
bool ChoiceBusy(void) { return __atomic_load_n(&s_choiceBusy, __ATOMIC_ACQUIRE) != 0; }
void ChoiceBusy(bool value) { __atomic_store_n(&s_choiceBusy, value ? 1u : 0u, __ATOMIC_RELEASE); }
int WaitCalc(u32) { return 0; }
bool FrozenValid(void) {
    if (!s_freezeEditor || R32(0x00948E70) != s_freezeScene || !s_indoor
        || !Process::CheckAddress(s_freezeEditor + 56, MEMPERM_WRITE)) return false;
    const u32 owner = R32(s_indoor + DecorTrashTable::kIndoorEditorPtrLiteral);
    return Process::CheckAddress(owner, MEMPERM_READ) && R32(owner) == s_freezeEditor;
}
bool Freeze(u32 editor, u32 indoor) {
    if (!editor) return true;
    if (s_freezeEditor) return s_freezeEditor == editor && FrozenValid();
    if (R32(editor + 52) != indoor + DecorTrashTable::kIndoorEditorNeutralCalc) return false;
    s_indoor = indoor;
    s_freezeEditor = editor;
    s_freezeScene = R32(0x00948E70);
    s_oldCalc = R32(editor + 52); s_oldAdj = R32(editor + 56);
    W32(editor + 56, 0);
    W32(editor + 52, reinterpret_cast<u32>(WaitCalc));
    return true;
}
void Unfreeze(void) {
    if (FrozenValid() && R32(s_freezeEditor + 52) == reinterpret_cast<u32>(WaitCalc)) {
        W32(s_freezeEditor + 56, s_oldAdj);
        W32(s_freezeEditor + 52, s_oldCalc);
    }
    s_freezeEditor = 0;
}
bool NativeReady(void) {
    static const u32 checks[][2] = {
        {0x004E9314,0xE59F0000},{0x0056FD10,0xE92D4070},{0x0068FA84,0xE92D40F8},
        {0x006920CC,0xE92D4FFF},{0x004E8880,0xE92D4070},{0x00744BDC,0xE5D00000},
        {0x00744BFC,0xE59F3034},{0x006A53DC,0xE59F0004},{0x002FEE38,0xE92D4FF0},
        {0x00747B34,0xE5D00011},{0x0074798C,0xE5D01011},{0x00768448,0xE92D4010},
        {0x00535188,0xE92D4010},{0x002FCB64,0xE92D4010},{0x0076A390,0xE92D4010},
        {0x002BAFC0,0xE59F20F4},{0x002BB13C,0xE92D4010},{0x002BAC48,0xE92D4010},
        {0x002BA8B0,0xE92D41F0},{0x002BAF48,0xE59FC04C},{0x002BABAC,0xE92D4070},
        {0x00818528,0xE92D47F0},{0x004E8650,0xE92D4070},{0x00750A38,0xE92D4FF0},
    };
    for (const auto &c : checks) if (R32(c[0]) != c[1]) return false;
    return true;
}
bool IndoorReady(u32 base) {
    static const u32 checks[][2] = {
        {0x43294,0xE92D4010},{0x2FC50,0xE92D4070},{0x4355C,0xE59F1034},
        {0x433E4,0xE59F2114},{0x3CC08,0xE92D41F0},{0x2F77C,0xE92D4070},
        {0x432DC,0xE92D4010},{0x429BC,0xE92D47F0},
    };
    for (const auto &c : checks)
        if (!Process::CheckAddress(base + c[0], MEMPERM_READ) || R32(base + c[0]) != c[1]) return false;
    return true;
}
u32 Field(void) { return reinterpret_cast<u32 (*)(void)>(0x006A53DC)(); }
Item *At(u32 field, int x, int z, int layer) {
    return reinterpret_cast<Item *(*)(u32,int,int,int)>(0x002FEE38)(field,x,z,layer);
}
u32 Param(const Item &item) { return reinterpret_cast<u32 (*)(const Item *)>(0x00535188)(&item); }
bool Furniture(u32 param) { return param && reinterpret_cast<int (*)(u32)>(0x00747B34)(param); }
bool Background(const Item &item, bool floor) {
    const u32 cat = reinterpret_cast<u32 (*)(const Item *)>(0x002FCB64)(&item);
    return cat == (floor ? 4u : 3u) || reinterpret_cast<int (*)(const Item *)>(0x0076A390)(&item);
}
bool Geometry(const Record &r, InteriorPlan::Geometry &g) {
    const u32 param = Param(r.item);
    if (!Furniture(param)) return false;
    g.wall = reinterpret_cast<int (*)(u32)>(0x0074798C)(param) != 0;
    g.supports = reinterpret_cast<int (*)(const Item *)>(0x00768448)(&r.item) != 0;
    u8 info[2] = {};
    reinterpret_cast<void (*)(void *,const Item *,u32)>(0x004E8880)(info,&r.item,r.rotation);
    g.count = reinterpret_cast<u32 (*)(const void *)>(0x00744BDC)(info);
    if (!g.count || g.count > 9) return false;
    for (u32 n = 0; n < g.count; ++n) {
        const s32 *off = reinterpret_cast<const s32 *(*)(const void *,u32)>(0x00744BFC)(info,n);
        if (!off) return false;
        g.x[n] = r.x + off[0]; g.z[n] = r.z + off[1];
    }
    return true;
}
bool NativeWall(int x, int z, unsigned rot) {
    const u32 side = reinterpret_cast<u32 (*)(int,int,int)>(0x005CD50C)(x,z,0);
    const u8 rotations[5] = {255,2,3,0,1};
    return side > 0 && side < 5 && rotations[side] == rot;
}
void GetBounds(Bounds &b) {
    reinterpret_cast<void (*)(int *,int *,int *,int *)>(0x002853A8)(&b.minX,&b.minZ,&b.maxX,&b.maxZ);
}
u32 Table(void) { return reinterpret_cast<u32 (*)(void)>(0x004E9314)(); }
u32 Capacity(void) { return reinterpret_cast<u32 (*)(u32)>(0x005B371C)(R8(0x0095133A)); }
bool ActorReady(u32 actor) {
    if (!Heap(actor) || !Process::CheckAddress(actor + 1972, MEMPERM_READ)
        || R8(actor + 15) || !(R8(actor + 102) & 2) || R8(actor + 1528) != 2) return false;
    const u32 vt = R32(actor);
    return Process::CheckAddress(vt + 144, MEMPERM_READ)
        && reinterpret_cast<int (*)(u32)>(R32(vt + 144))(actor) != 0;
}
bool Actors(unsigned &count, bool ready) {
    count = 0;
    const u32 table = Table(), capacity = Capacity();
    if (!table || !capacity || capacity > 48) return false;
    for (u32 i = 0; i < capacity; ++i) {
        const u32 actor = R32(table + 4 * i);
        if (!actor) continue;
        ++count;
        if (ready && !ActorReady(actor)) return false;
    }
    return true;
}
bool EditorIdle(u32 editor, u32 indoor) {
    return !editor || (R32(editor + 52) == indoor + DecorTrashTable::kIndoorEditorNeutralCalc
        && !R32(editor + 129100) && !R32(editor + 128640)
        && !R32(editor + 126796) && !R32(editor + 129288));
}
bool ContextStart(void) {
    if (!NativeReady() || DecorCatalog::PlacementBusy()) return false;
    s_data = DecorCatalog::RoomDataForInterior();
    if (!s_data || !DecorCatalog::InteriorBackgroundReady()) return false;
    s_scene = R32(0x00948E70); s_room = R8(0x0095133A);
    u32 indoor, editor;
    const bool live = DecorCatalog::EditorContext(indoor,editor);
    s_editor = live ? editor : 0; s_indoor = indoor;
    if ((editor && !live) || (s_editor && !IndoorReady(indoor)) || !EditorIdle(s_editor,indoor)) return false;
    unsigned count;
    if (!Actors(count,true)) return false;
    GetBounds(s_bounds);
    return s_scene && Freeze(s_editor,indoor);
}
bool ContextSame(void) {
    if (!s_scene || R32(0x00948E70) != s_scene || R8(0x0095133A) != s_room
        || DecorCatalog::RoomDataForInterior() != s_data) return false;
    u32 indoor, editor;
    DecorCatalog::EditorContext(indoor,editor);
    if (s_editor) return indoor == s_indoor && editor == s_editor && FrozenValid()
        && R32(editor + 52) == reinterpret_cast<u32>(WaitCalc);
    return !editor;
}
bool Capture(Snapshot &s) {
    s = {};
    s.minX=s_bounds.minX; s.minZ=s_bounds.minZ; s.maxX=s_bounds.maxX; s.maxZ=s_bounds.maxZ;
    std::memcpy(&s.wallpaper,reinterpret_cast<void *>(s_data + 754),sizeof(Item));
    std::memcpy(&s.flooring,reinterpret_cast<void *>(s_data + 758),sizeof(Item));
    s.wallVariant=R8(s_data+32); s.floorVariant=R8(s_data+33);
    if (!Background(s.wallpaper,false) || !Background(s.flooring,true)) return false;
    const u32 field = Field();
    if (!field) return false;
    for (int layer=0;layer<2;++layer) for (int z=0;z<16;++z) for (int x=0;x<16;++x) {
        Item *item = At(field,x,z,layer);
        if (!item || !Furniture(Param(*item))) continue;
        if (s.count >= 48) return false;
        Record &r=s.records[s.count++];
        r.item=*item; r.rotation=static_cast<u8>(item->flags >> 14);
        r.item.flags &= 0x3FFF; r.x=x; r.z=z; r.layer=layer;
    }
    unsigned count;
    if (!Actors(count,true) || count != s.count) return false;
    InteriorSnapshot::Seal(s);
    InteriorPlan::Plan plan;
    return InteriorPlan::Build(s,s_bounds,Geometry,NativeWall,plan);
}
void Finish(const char *message, bool error=false) {
    Unfreeze(); s_message=message; s_error=error; Set(Done);
}
void ReleaseChips(void) {
    if (!s_editor) return;
    for (u32 i=0;i<112;++i) {
        const u32 chip=s_editor+1176+1120*i;
        if (R8(chip+1117)) reinterpret_cast<void (*)(u32)>(s_indoor+0x2FC50)(chip);
    }
    for (u32 i=0;i<112;++i) {
        const u32 chip=s_editor+1176+1120*i;
        if (R8(chip+1117)) reinterpret_cast<void (*)(u32)>(s_indoor+0x43294)(chip);
    }
}
void ResetChips(void) {
    if (!s_editor) return;
    for (u32 i=0;i<112;++i) {
        const u32 chip=s_editor+1176+1120*i;
        reinterpret_cast<void (*)(u32)>(s_indoor+0x4355C)(chip);
        reinterpret_cast<void (*)(u32)>(s_indoor+0x433E4)(chip);
    }
}
void CreateChips(void) {
    if (!s_editor) return;
    reinterpret_cast<void (*)(u32)>(s_indoor+0x3CC08)(s_editor);
    for (u32 i=0;i<112;++i) {
        const u32 chip=s_editor+1176+1120*i;
        if (R8(chip+1117)) reinterpret_cast<void (*)(u32)>(s_indoor+0x2F77C)(chip);
    }
    DecorCatalog::RefreshChipTracking();
}
bool RequestRemove(void) {
    const u32 table=Table(), capacity=Capacity();
    if (!table || !capacity || capacity>48) return false;
    // The whole table is checked before the first destructive call.
    for (u32 i=0;i<capacity;++i) {
        const u32 actor=R32(table+4*i);
        if (actor && (!Heap(actor) || !Process::CheckAddress(actor+1528,MEMPERM_READ))) return false;
    }
    for (u32 i=0;i<capacity;++i) {
        const u32 actor=R32(table+4*i);
        if (actor && !R8(actor+15)) reinterpret_cast<int (*)(u32,u32)>(0x0056FD10)(actor,7);
    }
    return true;
}
void Fail(void) {
    if (!s_changed) { Finish(u8"内装を変更できませんでした。",true); return; }
    if (s_rollback) { Finish(u8"復元できませんでした。変更前の内装はSDに保存してあります。",true); return; }
    s_rollback=true; s_target=s_before;
    if (!InteriorPlan::Build(s_target,s_bounds,Geometry,NativeWall,s_plan)) {
        Finish(u8"復元データを確認できませんでした。",true); return;
    }
    s_frames=0; s_stable=0; s_next=0; Set(Remove);
}
void BackgroundApply(const Snapshot &s) {
    reinterpret_cast<void (*)(u32,const Item *)>(0x002D52D4)(s_data,&s.wallpaper);
    reinterpret_cast<void (*)(u32,const Item *)>(0x002D5728)(s_data,&s.flooring);
    *reinterpret_cast<volatile u8 *>(s_data+32)=s.wallVariant;
    *reinterpret_cast<volatile u8 *>(s_data+33)=s.floorVariant;
}
bool SpawnOne(const Record &r) {
    Record record={};
    const int result=reinterpret_cast<int (*)(Record *,int,int,int,const Item *,int,int,int,int,int)>(0x006920CC)
        (&record,r.x,r.z,r.rotation,&r.item,r.layer,-1,-1,0,0);
    if (result != 0) return false;
    record.reserved=0;                         // Same immediate request kind as stock room population.
    return reinterpret_cast<u32 (*)(Record *)>(0x0068FA84)(&record) != 0;
}
bool SlotPath(char *out,u32 cap,bool rollback=false) {
    char name[48];
    if (rollback) std::snprintf(name,sizeof(name),"interior_rollback.bin");
    else std::snprintf(name,sizeof(name),"interior_%d.bin",s_operationSlot);
    return GohanFiles::TitlePath(out,cap,name);
}
bool ReadFile(const char *path,Snapshot &s) {
    File f;
    if (File::Open(f,path,File::READ) != File::SUCCESS) return false;
    const bool ok=f.GetSize()==sizeof(s) && f.Read(&s,sizeof(s))==File::SUCCESS;
    f.Close();
    return ok && InteriorSnapshot::Valid(s);
}
struct NativeFiles {
    bool Exists(const char *path) { return File::Exists(path)==1; }
    bool Rename(const char *from,const char *to) { return File::Rename(from,to)==File::SUCCESS; }
    bool Remove(const char *path) { return File::Remove(path)==File::SUCCESS; }
    bool Read(const char *path,Snapshot &s) { return ReadFile(path,s); }
    bool Write(const char *path,const Snapshot &s) {
        File f;
        if (File::Open(f,path,File::WRITE|File::CREATE|File::TRUNCATE|File::SYNC)!=File::SUCCESS) return false;
        const bool ok=f.Write(&s,sizeof(s))==File::SUCCESS && f.Flush()==File::SUCCESS;
        f.Close();
        return ok;
    }
};
bool WriteFile(const char *path,const Snapshot &s) {
    Directory::Create("/luma/plugins/gohan");
    char parent[192]; std::snprintf(parent,sizeof(parent),"%s",path);
    char *slash=std::strrchr(parent,'/'); if (!slash) return false;
    *slash=0; Directory::Create(parent);
    NativeFiles files;
    return InteriorStorage::Write(path,s,files);
}
void ConfirmResult(bool yes) { if (Status()==ConfirmWait) Set(yes ? Approved : Cancelled); }
void Execute(int index) {
    if (!s_hooked || InputBusy()) { GuiMenu::NotifyRed(kTitle,u8"今は使えません。"); return; }
    s_operationSlot=s_slot;
    s_swap=index==GuiMenu::FindItem(Cheats::kInteriorSwap);
    s_rollback=s_changed=s_chipsReset=false;
    if (index==GuiMenu::FindItem(Cheats::kInteriorSave)) { Set(CaptureSave); return; }
    char path[192];
    if (!SlotPath(path,sizeof(path)) || !ReadFile(path,s_target)) {
        GuiMenu::NotifyRed(kTitle,u8"保存した内装を読めませんでした。"); return;
    }
    Set(Prepare);
}
bool SlotRead(int,s32 *value) { *value=s_slot; return true; }
void SlotWrite(int,s32 value) { if (value>=1 && value<=8) s_slot=value; }
void LongPress(void) {
    u32 indoor,editor;
    const bool live=DecorCatalog::EditorContext(indoor,editor);
    if (ChoiceBusy()) {
        const bool context=live && indoor==s_indoor && editor==s_editor && FrozenValid();
        InteriorChoice::FrameStep(context);
        if (InteriorChoice::Busy()) return;
        const int result=InteriorChoice::TakeResult();
        Unfreeze(); ChoiceBusy(false);
        if (context && result==0) s_duplicateWaiting=true;
        else s_duplicateWaiting=false;
        return;
    }
    if (s_duplicateWaiting) {
        if (!live || editor!=s_editor || indoor!=s_indoor || GuiMenu::IsVisible()) { s_duplicateWaiting=false; return; }
        if (!EditorIdle(editor,indoor)) return;
        s_duplicateWaiting=false;
        if (!DecorCatalog::DuplicateItem(s_duplicate.item.id,s_duplicate.item.flags,s_duplicate.rotation,s_duplicate.x,s_duplicate.z))
            GuiMenu::NotifyRed(kTitle,u8"複製を置ける場所や空きがありません。");
        return;
    }
    if (!live || !DecorCatalog::DuplicateReady() || GuiMenu::IsVisible()
        || !NativeReady() || !IndoorReady(indoor)
        || !(Controller::GetKeysDown() & static_cast<u32>(Key::Touchpad))) {
        s_holdChip=s_holdFrames=0; return;
    }
    const u32 chip=R32(editor+129100);
    if (!chip || chip<editor+1176 || chip>=editor+1176+1120*112
        || (chip-editor-1176)%1120 || !R8(chip+1117) || R8(chip+1114)
        || R32(chip+12)!=indoor+0x429BC || R32(chip+16)) { s_holdChip=s_holdFrames=0; return; }
    const float dx=*reinterpret_cast<const float *>(chip+1084)-*reinterpret_cast<const float *>(chip+1068);
    const float dz=*reinterpret_cast<const float *>(chip+1088)-*reinterpret_cast<const float *>(chip+1072);
    if (!(dx*dx+dz*dz<576.0f)) { s_holdChip=s_holdFrames=0; return; }
    if (s_holdChip!=chip) { s_holdChip=chip; s_holdFrames=0; }
    W32(chip+1092,0);                           // Native short-tap path is unchanged on release.
    if (++s_holdFrames<18) return;
    s_holdChip=s_holdFrames=0;
    const u32 slot=R32(DecorTrashTable::kFtrSlot);
    if (slot==DecorTrashTable::kUnloadedSlotWord || slot<=DecorTrashTable::kFtrSlotOffset) return;
    const u32 ftr=slot-DecorTrashTable::kFtrSlotOffset;
    if (R32(ftr+DecorTrashTable::kFtrRecGet)!=DecorTrashTable::kFtrRecGetWord) return;
    const s16 recordId=*reinterpret_cast<const s16 *>(chip+1032);
    const u32 rec=reinterpret_cast<u32 (*)(const s16 *)>(ftr+DecorTrashTable::kFtrRecGet)(&recordId);
    if (!Heap(rec)) return;
    u32 actorIndex=R32(rec+4);
    const u32 actor=reinterpret_cast<u32 (*)(u32 *)>(0x004E8650)(&actorIndex);
    if (!ActorReady(actor)) return;
    int x=-1,z=-1; const u16 rotation[4]={0};
    if (!reinterpret_cast<int (*)(u32,int *,int *,u32,const u16 *)>(0x00750A38)(actor,&x,&z,0,rotation)
        || x<0 || x>=16 || z<0 || z>=16) return;
    const int layer=R8(actor+1972);
    Item *item=layer<2 ? At(Field(),x,z,layer) : nullptr;
    if (!item || !Furniture(Param(*item))) return;
    s_duplicate={*item,static_cast<u8>(x),static_cast<u8>(z),static_cast<u8>(item->flags>>14),0,0};
    s_duplicate.item.flags &= 0x3FFF;
    const float touchX=*reinterpret_cast<const float *>(chip+1084);
    const float touchY=*reinterpret_cast<const float *>(chip+1088);
    reinterpret_cast<void (*)(u32)>(indoor+0x432DC)(chip);
    reinterpret_cast<void (*)(u32)>(indoor+0x3CEA8)(editor);
    if (R32(editor+129100)) return;
    s_editor=editor; s_indoor=indoor;
    if (!Freeze(editor,indoor)) return;
    GuiMenu::CaptureGameTouchUntilRelease();
    if (InteriorChoice::Open(touchX,touchY)) ChoiceBusy(true);
    else Unfreeze();
}
void FrameStep(void) {
    DecorCatalog::StepDuplicate();
    const State state=Status();
    if (state==Idle) { LongPress(); return; }
    if (state==Done || state==Saved) return;
    if (state==CaptureSave || state==Prepare) {
        if (!ContextStart() || !Capture(s_before)) { Finish(u8"家具の操作を終え、自分の部屋で使ってください。",true); return; }
        if (state==CaptureSave) { Unfreeze(); Set(Saved); return; }
        if (!Background(s_target.wallpaper,false) || !Background(s_target.flooring,true)
            || !InteriorPlan::Build(s_target,s_bounds,Geometry,NativeWall,s_plan)) {
            Finish(u8"保存した内装の配置が正しくありません。",true); return;
        }
        const bool different=s_target.minX!=s_bounds.minX || s_target.minZ!=s_bounds.minZ
            || s_target.maxX!=s_bounds.maxX || s_target.maxZ!=s_bounds.maxZ;
        Set(different ? Confirm : Backup);
        return;
    }
    if (!ContextSame()) { Finish(u8"部屋が変わったため処理を中止しました。",true); return; }
    if (state==Cancelled || state==IOFailed) { Finish(state==Cancelled ? u8"内装の変更をやめました。" : u8"変更前の内装を保存できませんでした。",state==IOFailed); return; }
    if (state==Approved) { Set(Backup); return; }
    if (state==Confirm || state==ConfirmWait || state==Backup || state==BackupWait || state==Exchange || state==ExchangeWait) return;
    if (state==BackupOK) { s_frames=s_stable=s_next=0; Set(Remove); return; }
    if (state==ExchangeOK) { Finish(u8"保存枠と部屋の内装を入れ替えました。"); return; }
    if (state==ExchangeFailed) { Fail(); return; }
    if (++s_frames>1800) { Fail(); return; }
    if (state==Remove) {
        ReleaseChips(); s_chipsReset=false; s_changed=true;
        if (!RequestRemove()) { Fail(); return; }
        s_frames=s_stable=0; Set(EmptyWait); return;
    }
    if (state==EmptyWait) {
        unsigned count;
        if (!Actors(count,false)) { Fail(); return; }
        if (count) { s_stable=0; return; }
        if (++s_stable<3) return;
        ResetChips(); s_chipsReset=true; s_next=s_frames=0; Set(Spawn); return;
    }
    if (state==Spawn) {
        if (s_next<s_plan.count) {
            if (!SpawnOne(s_target.records[s_plan.order[s_next++]])) { Fail(); return; }
        } else { s_frames=s_stable=0; Set(ReadyWait); }
        return;
    }
    if (state==ReadyWait) {
        unsigned count;
        if (!Actors(count,true) || count!=s_plan.count) { s_stable=0; return; }
        if (++s_stable<2) return;
        BackgroundApply(s_target); CreateChips();
        if (s_rollback) { Finish(u8"変更できなかったため、元の内装へ戻しました。",true); return; }
        if (s_swap) Set(Exchange);
        else Finish(u8"保存した内装を部屋へ反映しました。");
    }
}
}
bool InputBusy(void) { return Status()!=Idle || ChoiceBusy(); }
bool IsFrozenEditorCalc(u32 calc) { return calc==reinterpret_cast<u32>(WaitCalc) && FrozenValid(); }
void Tick(void) {
    if (InputBusy()) {
        GuiMenu::BlockGameAll();
        if (!ChoiceBusy()) GuiMenu::BlockGameTouch();
    }
    const State state=Status();
    if (state==Confirm) {
        char text[256];
        std::snprintf(text,sizeof(text),u8"部屋のサイズが違います。\n範囲外の家具%d個を削って配置しますか？",static_cast<int>(s_plan.clipped));
        Set(ConfirmWait);
        if (!GuiDialog::ShowConfirm(kTitle,text,ConfirmResult)) Set(Cancelled);
    } else if (state==Backup) {
        Set(BackupWait);
        char path[192];
        const bool ok=SlotPath(path,sizeof(path),true) && WriteFile(path,s_before);
        if (Status()==BackupWait) Set(ok ? BackupOK : IOFailed);
    } else if (state==Exchange) {
        Set(ExchangeWait);
        char path[192];
        const bool ok=SlotPath(path,sizeof(path)) && WriteFile(path,s_before);
        if (Status()==ExchangeWait) Set(ok ? ExchangeOK : ExchangeFailed);
    } else if (state==Saved) {
        char path[192];
        const bool ok=SlotPath(path,sizeof(path)) && WriteFile(path,s_before);
        if (ok) GuiMenu::Notify(kTitle,u8"部屋の内装を保存しました。");
        else GuiMenu::NotifyRed(kTitle,u8"内装を保存できませんでした。");
        Set(Idle);
    } else if (state==Done) {
        if (s_error) GuiMenu::NotifyRed(kTitle,s_message);
        else GuiMenu::Notify(kTitle,s_message);
        Set(Idle);
    }
}
void Wire(void) {
    s_hooked=GridCursor::InstallFrameHook() && GridCursor::AddExtraFrameStep(FrameStep);
    const char *const actions[]={Cheats::kInteriorSave,Cheats::kInteriorLoad,Cheats::kInteriorSwap};
    for (const char *label:actions) {
        const int index=GuiMenu::FindItem(label);
        if (index>=0) GuiMenu::RegisterExecute(index,Execute);
    }
    const int slot=GuiMenu::FindItem(Cheats::kInteriorSlot);
    if (slot>=0) GuiMenu::RegisterLinked(slot,SlotRead,SlotWrite);
}
}
