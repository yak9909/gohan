#include "game/scene_native.hpp"
#include "game/generated/api.hpp"
#include "game/memory.hpp"
#include "game/draw_slots.hpp"
#include "game/code_ownership.hpp"
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <algorithm>
#include <cstring>
#include <new>

namespace gohan::game {
namespace {
using namespace generated;
using U = std::uint32_t;
using A = std::uintptr_t;
template<class F> F *Call(A address) { return reinterpret_cast<F *>(address); }
bool Read(A a, std::size_t n = 4) { return a && memory::Readable(a, n); }
U Word(A a) { U x = 0; if (Read(a)) std::memcpy(&x, reinterpret_cast<void *>(a), 4); return x; }
A Rel(A a) { const U v = Word(a); return v ? static_cast<U>(a + v) : 0; }
template<class T> A At(T *p) { return reinterpret_cast<A>(p); }
void Put(A a, U v) { if (Read(a)) std::memcpy(reinterpret_cast<void *>(a), &v, 4); }
bool Heap(A a) { return memory::IsHeapPointer(a) && Read(a); }
std::array<A, 2> Safe(const char *text) { return {sead_SafeStringBase_char_vtable, At(text)}; }
U Free(A heap) {
    return Heap(heap) ? Call<U(void *)>(sead_ExpHeap_getFreeSize_addr)(reinterpret_cast<void *>(heap)) : 0;
}
U Contiguous(A heap) {
    return Heap(heap) ? Call<U(void *, int)>(sead_ExpHeap_getMaxAllocatableSize_addr)(reinterpret_cast<void *>(heap), 128) : 0;
}
void EmptyCallback(A, A) {}
}

namespace scene {
struct Native::State {
    alignas(32) std::array<U, 4> resource_allocator{}, instance_allocator{}, holder{}, drawer{};
    struct alignas(32) Resource { std::array<U, 66> holder{}; };
    struct alignas(32) Animation { std::array<U, 8> words{}; bool built = false; };
    std::vector<Resource> resources;
    std::vector<std::string> paths;
    std::vector<std::array<A, 2>> safe_paths;
    std::vector<std::pair<std::size_t, std::size_t>> links;
    std::array<A, 2> resource_name{}, instance_name{};
    Animation normal, rotate;
    A model = 0;
    std::size_t model_file = 0;
    bool constructed = false, instance_safe = false, cursor_animations = false;
};
Identity Native::Current() {
    const A proc = Word(g_FgobjProc_addr);
    const U room = Read(g_CurrentRoomId_addr, 1) ? *reinterpret_cast<const unsigned char *>(g_CurrentRoomId_addr) : 255;
    return {Heap(proc) ? reinterpret_cast<void *>(proc) : nullptr,
        Heap(proc) ? reinterpret_cast<void *>(Word(proc + 52)) : nullptr, room};
}
heap::Room Native::Room() {
    const A parent = Word(g_ParentHeap_addr);
    if (!Heap(parent) || Word(parent) != vtbl_sead_ExpHeap_addr) return {};
    return {Free(parent), Contiguous(parent), true};
}
bool Native::Create(const Plan &plan, U bytes) {
    if (state_) return false;
    for (auto link : plan.links) if (link.first >= plan.files.size() || link.second >= plan.files.size()) return false;
    state_ = new (std::nothrow) State;
    if (!state_) return false;
    auto &s = *state_;
    s.model_file = plan.model_file; s.cursor_animations = plan.cursor_animations; s.links = plan.links;
    s.resources.resize(plan.files.size()); s.paths.reserve(plan.files.size());
    for (const auto &f : plan.files) s.paths.push_back(f.path);
    for (const auto &path : s.paths) s.safe_paths.push_back(Safe(path.c_str()));
    s.resource_name = Safe("gohan SceneResource"); s.instance_name = Safe("gohan SceneInstance");
    Call<void(void *)>(HeapAllocator_Ctor_addr)(s.resource_allocator.data());
    Call<void(void *)>(HeapAllocator_Ctor_addr)(s.instance_allocator.data());
    const auto parent = reinterpret_cast<void *>(Word(g_ParentHeap_addr));
    using CreateHeap = int(void *, U, void *, const void *, int, U);
    // 第6引数は、ゲームが SP+4 から読む float のビット列。
    if (Call<CreateHeap>(Heap_CreateNamed_0_addr)(s.resource_allocator.data(), bytes, parent, s.resource_name.data(), 1, 0) != 1 ||
        !Heap(s.resource_allocator[1])) return false;
    if (Call<CreateHeap>(Heap_CreateNamed_0_addr)(s.instance_allocator.data(), plan.instance_bytes, parent, s.instance_name.data(), 1, 0) != 1 ||
        !Heap(s.instance_allocator[1])) return false;
    for (auto &r : s.resources) Call<void(void *)>(G3dResHolder_Ctor_addr)(r.holder.data());
    s.constructed = true;
    return true;
}
int Native::Load(std::size_t file) {
    auto &s = *state_;
    return Call<int(void *, const void *, void *, int)>(G3dResHolder_RequestLoad_addr)(
        s.resources[file].holder.data(), s.safe_paths[file].data(), reinterpret_cast<void *>(s.resource_allocator[1]), 128);
}
bool Native::Setup(std::size_t file) {
    auto &s = *state_; auto &r = s.resources[file];
    if (!Read(r.holder[2], 4) || !IsCgfx(reinterpret_cast<void *>(r.holder[2]), 4)) return false;
    auto setup = Call<int(void *, void *, A, int)>(G3dResHolder_Setup_addr);
    setup(r.holder.data(), s.resource_allocator.data(), 0, 1);
    for (auto link : s.links) if (std::max(link.first, link.second) == file)
        setup(s.resources[link.first].holder.data(), s.resource_allocator.data(), At(s.resources[link.second].holder.data()), 1);
    return true;
}
bool Native::Find(const Plan &plan) {
    auto &s = *state_;
    s.model = At(Call<void *(void *, const char *)>(G3dResource_FindModelByName_addr)(
        s.resources[plan.model_file].holder.data() + 1, plan.model.c_str()));
    if (!Heap(s.model)) return false;
    const U type = Word(s.model);
    return AcceptsModel(plan.kind, type);
}
bool Native::Build() {
    auto &s = *state_;
    // 生成関数は確保失敗をすべて検査するとは限らない。連続した空きを先に見る。
    if (Contiguous(s.instance_allocator[1]) < 6144) return false;
    Call<void(void *)>(g3d_SkeletalModel_Ctor_addr)(s.holder.data());
    const int result = Call<int(void *, void *, void *, void *, U, U, U)>(ModelInstance_Create_addr)(
        s.holder.data(), reinterpret_cast<void *>(s.model), s.instance_allocator.data(), s.instance_allocator.data(), 0x834, 1, 3);
    const A node = s.holder[1];
    if (!Heap(node) || !Read(node, 0x1F0)) return false;
    s.instance_safe = Word(node + 0x170) != Word(node + 0x174) &&
        (memory::IsClass(node, "N2nw3gfx13SkeletalModelE") || memory::IsClass(node, "N2nw3gfx5ModelE"));
    if (result != 1 || !s.instance_safe || !Heap(Word(node + 0x1EC))) return false;
    if (s.cursor_animations) {
        const A res = s.resources[s.model_file].holder[2];
        const U offsets[] = {0x1148, 0x161C};
        State::Animation *anims[] = {&s.normal, &s.rotate};
        for (unsigned i = 0; i < 2; ++i) {
            if (Contiguous(s.instance_allocator[1]) < 2048 || !Read(res + offsets[i], 8)) return false;
            auto &a = *anims[i];
            Call<void(void *)>(g3d_MaterialAnim_Ctor_addr)(a.words.data());
            a.built = Call<int(void *, void *, void *, void *, char)>(g3d_MaterialAnim_BuildFromRes_addr)(
                a.words.data(), s.holder.data(), reinterpret_cast<void *>(res + offsets[i]), s.instance_allocator.data(), 1) != 0;
            if (!a.built) return false;
            Call<void(void *, void *, int)>(ModelInstance_BindAnimSlot_addr)(s.holder.data(), a.words.data(), i ? 2 : 0);
            Call<void(void *, float)>(Anim_SetRate_addr)(a.words.data(), i ? 0.0f : 1.0f);
            Call<void(void *, float)>(Anim_SetFrame_addr)(a.words.data(), 0.0f);
        }
    }
    if (Contiguous(s.instance_allocator[1]) < 2048) return false;
    Call<void(void *, void *)>(g3d_FuncNode_Create_addr)(s.drawer.data(), s.instance_allocator.data());
    if (!Heap(s.drawer[1])) return false;
    Call<void(void *, draw::Callback, draw::Callback, A)>(g3d_FuncNode_SetCallbacks_addr)(
        s.drawer.data(), EmptyCallback, EmptyCallback, 0);
    return true;
}
void Native::Destroy() {
    if (!state_) return;
    auto &s = *state_;
    ReleaseInOrder([&] {
        if (s.drawer[1]) Call<void(void *)>(SceneNodeHolder_Destroy_addr)(s.drawer.data());
        if (s.instance_safe && s.holder[1]) Call<void(void *)>(ModelInstance_Destroy_addr)(s.holder.data());
        if (s.normal.built) Call<void(void *)>(g3d_MaterialAnim_ReleaseBuilt_addr)(s.normal.words.data());
        if (s.rotate.built) Call<void(void *)>(g3d_MaterialAnim_ReleaseBuilt_addr)(s.rotate.words.data());
    }, [&] {
        if (s.constructed) for (auto i = s.resources.size(); i-- > 0;)
            Call<void(void *)>(G3dResHolder_Dtor_addr)(s.resources[i].holder.data());
    }, [&] {
        if (s.instance_allocator[1]) Call<void(void *)>(HeapAllocator_DestroyHeap_addr)(s.instance_allocator.data());
        if (s.resource_allocator[1]) Call<void(void *)>(HeapAllocator_DestroyHeap_addr)(s.resource_allocator.data());
    });
    delete state_; state_ = nullptr;
}
void Native::Wait() { svcSleepThread(33333334); }
void Native::Pause() { CTRPluginFramework::Process::Pause(); }
void Native::Resume() { CTRPluginFramework::Process::Play(); }
void Native::Animate() {
    if (state_ && state_->holder[1]) Call<void(void *)>(ModelInstance_EvaluateAndApplyAnims_addr)(state_->holder.data());
}
bool Native::Prepare(A ctx, const instance::Matrix &matrix) {
    if (!state_ || !Heap(state_->holder[1]) || !Read(ctx, 12) || !Read(Word(ctx + 8) + 28)) return false;
    const A ptr = Word(g_GpuCmdPtr_addr), end = Word(g_GpuCmdEnd_addr);
    if (!ptr || end < ptr || end - ptr < 4096 || !Read(g_FgobjRenderCamera_addr + 164, 48)) return false;
    auto &s = *state_;
    Call<void(void *, const float *)>(TransformNodeHolder_SetMatrix3x4_addr)(s.holder.data(), matrix.data());
    Call<void(void *)>(TransformNodeHolder_UpdateWorldAndSkeleton_addr)(s.holder.data());
    alignas(8) std::array<U, 68> fake{};
    fake[66] = s.holder[1]; // obj+264 = 体、obj+268 = アニメ無し
    Call<void(const void *, const float *)>(fgobj_Object_SetModelView_addr)(
        fake.data(), reinterpret_cast<const float *>(g_FgobjRenderCamera_addr + 164));
    return true;
}
void Native::ResetMatrixCache(A ctx) {
    instance::ResetCache(ctx, Word, Read, Put);
}
void Native::Meshes(A ctx) {
    const A node = state_->holder[1], res = Word(node + 8);
    A it = Word(node + 0x170), end = Word(node + 0x174);
    if (it == end) { it = Rel(res + 184); end = it + 4 * Word(res + 180); }
    if (end < it || (end - it) % 4 || (end - it) / 4 > 1024 || !Read(it, end - it)) return;
    for (; it != end; it += 4) {
        const A mesh = Rel(it);
        if (!Read(mesh, 40) || !*reinterpret_cast<const unsigned char *>(mesh + 36)) continue;
        std::int16_t vi; std::memcpy(&vi, reinterpret_cast<void *>(mesh + 38), 2);
        if (vi >= 0) {
            const A begin = Word(node + 0x17C), finish = Word(node + 0x180);
            A entry = 0;
            if (begin == finish) { const A dict = Rel(res + 208); if (dict) entry = Rel(dict + 16 * vi + 40); }
            else if (finish >= begin && static_cast<U>(vi) < (finish - begin) / 8) entry = begin + 8 * vi;
            if (!Read(entry + 4, 1) || !*reinterpret_cast<const unsigned char *>(entry + 4)) continue;
        }
        Call<void(A, A, A)>(gfx_DrawMesh_addr)(ctx, mesh, node);
    }
}
bool Native::Material(material::View &view) {
    if (!state_) return false;
    const A array = Word(state_->holder[1] + 0x164);
    return Read(array) && material::Resolve(Word(array), view);
}
bool Native::PositionMatrix(const std::array<float, 3> &position, instance::Matrix &m) {
    auto p = position;
    p[1] = Call<float(const float *, U)>(Field_GetGroundHeightAtWorld_addr)(p.data(), 0);
    auto out = p; std::uint16_t angle = 0;
    const U room = Read(g_CurrentRoomId_addr, 1) ? *reinterpret_cast<const unsigned char *>(g_CurrentRoomId_addr) : 255;
    if (Word(g_RoomFlagTable_addr + room * 4) & 2)
        angle = Call<std::uint16_t(float *, const float *)>(FieldPosition_ToRenderSpace_addr)(out.data(), p.data());
    m = {}; m[0] = m[5] = m[10] = 1; m[3] = out[0]; m[7] = out[1]; m[11] = out[2];
    if (angle) Call<void(float *, std::uint16_t)>(Matrix3x4_AppendRotationX16_addr)(m.data(), angle);
    return true;
}
bool Native::PlayerPosition(std::array<float, 3> &p) {
    const bool was_paused = CTRPluginFramework::Process::IsPaused();
    if (!was_paused) Pause();
    const bool result = [&] {
    const U index = Call<U()>(vc_A_GETONLINEPLAYERINDEX_addr)();
    if (index >= 4) return false;
    const A actor = Word(g_PlayerActorSlots_addr + 4 * index);
    if (!Heap(actor) || !Read(actor + 20, 12)) return false;
    std::memcpy(p.data(), reinterpret_cast<void *>(actor + 20), 12); return true;
    }();
    if (!was_paused) Resume();
    return result;
}
} // namespace scene

namespace material {
bool Resolve(A m, View &v) {
    v = {};
    if (!memory::IsClass(m, "N2nw3gfx8MaterialE") || !Read(m, 84)) return false;
    const A colour = Word(m + 48), tevres = Word(m + 72), frag = Word(m + 80);
    if (!Read(colour, 256) || !Read(tevres, 716)) return false;
    const A tev = Rel(tevres + 648);
    if (!Read(tev, sizeof(Tev))) return false;
    const A lut = Rel(tev + 40);
    if (lut && !Read(lut, 4)) return false;
    v.tev = reinterpret_cast<Tev *>(tev); v.palette = reinterpret_cast<U *>(colour + 36);
    v.tev_key = reinterpret_cast<U *>(tevres + 712); v.lut_valid = true;
    if (Read(frag, 724) && frag == colour && Word(frag + 320) == 0x00E40100u && Word(frag + 324) == 0x803F0100u) {
        v.fb_read = reinterpret_cast<U *>(frag + 300); v.blend_colour = reinterpret_cast<U *>(frag + 336);
        v.frag_key = reinterpret_cast<U *>(frag + 720);
    }
    if (Read(frag, 332)) v.blend = reinterpret_cast<U *>(frag + 328);
    return true;
}
}
namespace draw {
namespace { int owner; }
Slots CurrentSlots() {
    const A proc = Word(g_FgobjProc_addr);
    if (!Heap(proc)) return {};
    const A node = Word(proc + 0x45B0 + 4);
    if (!Read(node, 0x154) || Word(node) != g3d_FuncNode_vtable) return {};
    return {reinterpret_cast<A *>(node + 0x148), reinterpret_cast<A *>(node + 0x14C),
        reinterpret_cast<void *>(proc), reinterpret_cast<void *>(Word(proc + 52))};
}
Callback Original0() { return Call<void(A, A)>(fgobj_DrawCallbackLayer0_addr); }
Callback Original1() { return Call<void(A, A)>(fgobj_DrawCallbackLayer1_addr); }
bool Wire() {
    for (const A start : {fgobj_DrawCallbackLayer0_addr, fgobj_DrawCallbackLayer1_addr})
        for (unsigned i = 0; i < 4; ++i) if (!code_ownership::Take(&owner, start + 4 * i)) {
            code_ownership::Release(&owner); return false;
        }
    return true;
}
void Unwire() { code_ownership::Release(&owner); }
void Suspend() { scene::Native::Pause(); }
void Resume() { scene::Native::Resume(); }
}
} // namespace gohan::game
