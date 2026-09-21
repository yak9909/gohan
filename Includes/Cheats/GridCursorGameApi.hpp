// Every game function and global the grid cursor touches, in one place.
//
// All of these were established on hardware or by disassembly during the 2026-09-21
// session and are recorded in work/FINDINGS.md (IDA-opus-5-F004 .. F029). They are burned
// into the build, so this is for the JPN 無印 update build only -- the same build gohan's
// other cheats are written against.
//
// The declarations are written as ordinary C++ function pointers so the compiler applies
// the same AAPCS-VFP the game was compiled with. That matters for the two calls that take
// a float: passing those by hand as stack words happens to work in a debugger harness and
// is wrong here.
#pragma once

#include <3ds/types.h>

namespace GridCursor {
namespace Game {

// ---- globals -------------------------------------------------------------------------

// The heap every child heap of ours comes out of.
static void** const kParentHeap = reinterpret_cast<void**>(0x0094CC48);
// The player object; its world position is three floats at +0x14.
static void** const kPlayer = reinterpret_cast<void**>(0x00AA7994);
static const u32 kPlayerPositionOffset = 0x14;
// Room id. 0 is the village outdoors.
static u8* const kRoomId = reinterpret_cast<u8*>(0x0095133A);
// Per-room flag word; bit 1 (value 2) means the world is bent into a cylinder and a
// position has to go through FieldPosition_ToRenderSpace before it can be used in a matrix.
static const u32* const kRoomFlags = reinterpret_cast<const u32*>(0x00883328);
static const u32 kRoomFlagCurved = 2;
// Who owns the scene right now. Changes at every room change, which is how the frame
// callback knows to stop drawing (IDA-opus-5-F024).
static void** const kSceneOwner = reinterpret_cast<void**>(0x00948E70);
static const u32 kSceneOwnerResourceOffset = 0x34;

static const u32 kSafeStringVtable = 0x008FEC00;
static const u32 kHeapAllocatorVtable = 0x0090053C;
static const u32 kResourceLoaderVtable = 0x008FCD18;
static const u32 kNodeHolderVtable = 0x008FCCE8;
static const u32 kSkeletalModelVtable = 0x008FB848;  // UnitCursor is a SkeletalModel
static const u32 kMaterialAnimVtable = 0x008FCC74;
static const u32 kSkeletalModelTypeInfo = 0x40000092;

// A sead::SafeString is a vtable and a pointer to the text. The game's loader takes these
// by address, so we keep ours in plugin memory and hand over the pointer.
struct SafeString {
    u32 vtable;
    const char* text;
};

// ---- sizes the game's own structures need ---------------------------------------------

static const u32 kResourceHolderBytes = 264;  // what G3dResHolder_Ctor writes
static const u32 kNodeHolderBytes = 16;
static const u32 kMaterialAnimBytes = 32;

// ---- node fields we read to check our own work ------------------------------------------

static const u32 kNodeVtable = 0x00;
static const u32 kNodeAnimBinding = 0x28;     // SceneNode::m_AnimBinding
static const u32 kNodeLocalMatrix = 0x4C;
static const u32 kNodeMeshArrayBegin = 0x170; // equal to +0x174 means the array is empty,
static const u32 kNodeMeshArrayEnd = 0x174;   // and then Destroy writes into the resource
static const u32 kNodeBufferOption = 0x188;
static const u32 kNodeMaterialActivator = 0x1EC;  // zero means the build ran out of heap

// ---- functions ---------------------------------------------------------------------

// Heaps and allocators.
typedef void (*HeapAllocatorCtorFn)(void* allocator);
// ★第 6 引数を float で宣言してはいけない。-mfloat-abi=hard では s0 に載るが、
//   ゲームは `vldr s16, [sp, #44]`（呼び出し元 SP+4）から読む（0x00317878-0x0031787C）。
//   float のままだとスタックの語が書かれず、ゴミが fill として渡る。
//   語として渡す。0 は 0.0f のビット表現で、ハーネスが実機で通した値（IDA-opus-5-F026）。
typedef int (*HeapCreateNamedFn)(void* allocator, u32 size, void* parent,
                                 const SafeString* name, int flag, u32 fillBits);
typedef void (*HeapAllocatorDestroyHeapFn)(void* allocator);
typedef u32 (*HeapGetFreeSizeFn)(void* heap);

static const HeapAllocatorCtorFn HeapAllocatorCtor =
    reinterpret_cast<HeapAllocatorCtorFn>(0x005667E8);
static const HeapCreateNamedFn HeapCreateNamed =
    reinterpret_cast<HeapCreateNamedFn>(0x0031785C);
static const HeapAllocatorDestroyHeapFn HeapAllocatorDestroyHeap =
    reinterpret_cast<HeapAllocatorDestroyHeapFn>(0x002F73F4);
static const HeapGetFreeSizeFn HeapGetFreeSize =
    reinterpret_cast<HeapGetFreeSizeFn>(0x0074D744);

// Loading a bcres.
typedef void (*ResHolderCtorFn)(void* holder);
typedef int (*ResHolderRequestLoadFn)(void* holder, const SafeString* path, void* heap,
                                      int align);
typedef int (*ResHolderSetupFn)(void* holder, void* allocator, int a3, int a4);
typedef void (*ResHolderDtorFn)(void* holder);
typedef void* (*FindModelByNameFn)(void* resourceSet, const char* name);

static const ResHolderCtorFn ResHolderCtor = reinterpret_cast<ResHolderCtorFn>(0x004ED860);
static const ResHolderRequestLoadFn ResHolderRequestLoad =
    reinterpret_cast<ResHolderRequestLoadFn>(0x00317920);
static const ResHolderSetupFn ResHolderSetup = reinterpret_cast<ResHolderSetupFn>(0x00317958);
// Must be called BEFORE the resource heap is destroyed, or it follows a dangling pointer
// and the thread executes into the symbol table (IDA-opus-5-F027).
static const ResHolderDtorFn ResHolderDtor = reinterpret_cast<ResHolderDtorFn>(0x00317CFC);
static const FindModelByNameFn FindModelByName =
    reinterpret_cast<FindModelByNameFn>(0x004A5F20);

// Instances.
typedef void (*NodeHolderCtorFn)(void* holder);
typedef int (*ModelInstanceCreateFn)(void* holder, void* model, void* allocatorA,
                                     void* allocatorB, u32 bufferOption,
                                     u32 isAnimationEnabled, u32 maxAnimObjectsPerGroup);
typedef void (*ModelInstanceDestroyFn)(void* holder);
typedef void (*SetMatrix3x4Fn)(void* holder, const float* matrix3x4);
typedef void (*UpdateWorldAndSkeletonFn)(void* holder);
typedef void (*SubmitFn)(void* holder, int sceneIndex);

static const NodeHolderCtorFn NodeHolderCtor = reinterpret_cast<NodeHolderCtorFn>(0x004ED008);
static const ModelInstanceCreateFn ModelInstanceCreate =
    reinterpret_cast<ModelInstanceCreateFn>(0x004F048C);
static const ModelInstanceDestroyFn ModelInstanceDestroy =
    reinterpret_cast<ModelInstanceDestroyFn>(0x004F053C);
static const SetMatrix3x4Fn SetMatrix3x4 = reinterpret_cast<SetMatrix3x4Fn>(0x004ED794);
// Fills the node's three matrices AND the skeleton's per-bone arrays. Skipping it is why
// twelve instances once submitted every frame and drew nothing (IDA-opus-5-F020).
static const UpdateWorldAndSkeletonFn UpdateWorldAndSkeleton =
    reinterpret_cast<UpdateWorldAndSkeletonFn>(0x004ECFEC);
static const SubmitFn Submit = reinterpret_cast<SubmitFn>(0x004ED630);

// Animations. The game gives every UnitCursor three slots: 0 is the looping normal
// animation, 1 is the "cannot place here" one, 2 holds a frozen frame that turns the
// hatching to match the furniture's facing (IDA-opus-5-F028, F029).
typedef void (*MaterialAnimCtorFn)(void* anim);
typedef int (*MaterialAnimBuildFromResFn)(void* anim, void* holder, void* canm,
                                          void* allocatorHolder, char flag);
typedef void (*MaterialAnimReleaseBuiltFn)(void* anim);
typedef void (*BindAnimSlotFn)(void* holder, void* anim, int slot);
typedef void (*ClearAnimSlotFn)(void* holder, int slot);
typedef void (*AnimSetFrameFn)(void* anim, float frame);
typedef void (*AnimSetRateFn)(void* anim, float rate);
typedef void (*EvaluateAndApplyAnimsFn)(void* holder);

static const MaterialAnimCtorFn MaterialAnimCtor =
    reinterpret_cast<MaterialAnimCtorFn>(0x004EBE4C);
static const MaterialAnimBuildFromResFn MaterialAnimBuildFromRes =
    reinterpret_cast<MaterialAnimBuildFromResFn>(0x004EBC14);
// vtable slot 5, not the destructor: the destructor leaves the animation object and its
// child heap allocated (IDA-opus-5-F023).
static const MaterialAnimReleaseBuiltFn MaterialAnimReleaseBuilt =
    reinterpret_cast<MaterialAnimReleaseBuiltFn>(0x004EBDF8);
static const BindAnimSlotFn BindAnimSlot = reinterpret_cast<BindAnimSlotFn>(0x004F0300);
static const ClearAnimSlotFn ClearAnimSlot = reinterpret_cast<ClearAnimSlotFn>(0x004F03D0);
static const AnimSetFrameFn AnimSetFrame = reinterpret_cast<AnimSetFrameFn>(0x004EDC98);
static const AnimSetRateFn AnimSetRate = reinterpret_cast<AnimSetRateFn>(0x004EDC74);
// The game's own per-frame call: walks the binding's slots, gives each animation object
// its vtable[5], then applies phase 0 and phase 1 (IDA-opus-5-F029).
static const EvaluateAndApplyAnimsFn EvaluateAndApplyAnims =
    reinterpret_cast<EvaluateAndApplyAnimsFn>(0x004F0560);

// Placing something in the village. Outdoors the world is bent into a cylinder, so a plain
// world position has to be converted first and the resulting angle folded into the matrix.
typedef u16 (*FieldPositionToRenderSpaceFn)(float* out3, const float* in3);
typedef void (*AppendRotationX16Fn)(float* matrix3x4, u16 angle);
typedef void (*AppendRotationY16Fn)(float* matrix3x4, u16 angle);

static const FieldPositionToRenderSpaceFn FieldPositionToRenderSpace =
    reinterpret_cast<FieldPositionToRenderSpaceFn>(0x0052CDD8);
static const AppendRotationX16Fn AppendRotationX16 =
    reinterpret_cast<AppendRotationX16Fn>(0x0056689C);
static const AppendRotationY16Fn AppendRotationY16 =
    reinterpret_cast<AppendRotationY16Fn>(0x005669A0);

}  // namespace Game
}  // namespace GridCursor
