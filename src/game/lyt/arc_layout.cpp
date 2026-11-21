#include "game/lyt/arc_layout.hpp"
#include "game/lyt/bclyt_edit.hpp"
#include <algorithm>
#include <cstring>

namespace gohan::game::lyt {
namespace {
using namespace generated;
#define CALL(name) reinterpret_cast<name##_t>(name##_addr)
template <class T> T Global(std::uintptr_t address) { return *reinterpret_cast<T *>(address); }
template <class T> bool Put(T *p, const T &v, PaneLog *log) {
    if (log) return log->Write(p, v);
    std::memcpy(p, &v, sizeof(T)); return true;
}
bool Space(PaneLog *log, unsigned count) { return !log || log->Count() + count <= log->Capacity(); }
bool Dirty(NwLytPane *p, PaneLog *log) { return Put(&p->flags, std::uint8_t(p->flags & ~0x30u), log); }
void *Accessor(ArcResAccReader &r) { return &r.accessor_vptr; }
}
bool Arc::Register() {
    auto *manager = Global<g_FontMgr_t>(g_FontMgr_addr);
    if (!manager) return false;
    for (int kind : {0, 1, 3}) {
        auto *font = CALL(Font_GetLoadedFont)(manager, kind);
        if (!font) { if (!kind) return false; else continue; }
        auto *name = static_cast<const std::uintptr_t *>(CALL(Font_GetNameString)(manager, kind));
        if (!name || !name[0]) return false;
        const auto *vt = reinterpret_cast<const std::uintptr_t *>(name[0]);
        reinterpret_cast<void (*)(const void *)>(vt[2])(name);
        if (!CALL(nwlyt_ArcResourceAccessor_RegisterFont)(Accessor(reader_), reinterpret_cast<const char *>(name[1]), font))
            return false;
    }
    return CALL(ArcResAccReader_RegisterTextures)(&reader_);
}
bool Arc::LoadStep(const char *path) {
    if (!path || copy_) return false;
    if (ready_) return true;
    if (!made_) { CALL(ArcResAccReader_Ctor)(&reader_); made_ = true; }
    if (!CALL(ArcResAccReader_LoadArcStep)(&reader_, path)) return false;
    ready_ = Register();
    return ready_;
}
bool Arc::Copy(const void *data, std::size_t bytes) {
    if (!data || !bytes || bytes > 0xFFFFFFFFu || made_) return false;
    owner_ = Global<g_UiResourceAllocator_t>(g_UiResourceAllocator_addr);
    const int alignment = Global<g_UiResourceLoadAlignment_t>(g_UiResourceLoadAlignment_addr);
    if (!owner_ || alignment <= 0 || (alignment & (alignment - 1))) return false;
    copy_ = CALL(ssys_ma_Allocator_Alloc)(owner_, unsigned(bytes), alignment);
    if (!copy_) { owner_ = nullptr; return false; }
    std::memcpy(copy_, data, bytes);
    CALL(GpuCache_QueueFlushRange)(copy_, unsigned(bytes));
    CALL(ArcResAccReader_Ctor)(&reader_); made_ = true;
    reader_.arc_data = reader_.loaded_arc = copy_;
    if (!CALL(nwlyt_ArcResourceAccessor_Attach)(Accessor(reader_), copy_, ".") || !Register()) {
        Reset(); return false;
    }
    ready_ = true; return true;
}
bool Arc::Reset() {
    if (users_) return false;
    if (made_) CALL(ArcResAccReader_Dtor)(&reader_);
    if (copy_ && owner_) CALL(ssys_ma_Allocator_Free)(owner_, copy_);
    reader_ = {}; owner_ = nullptr; copy_ = nullptr; made_ = ready_ = false;
    return true;
}
bool ArcLayout::Build(Arc &arc, const char *name, unsigned bytes, std::uint8_t priority) {
    if (made_ || !arc.Ready() || !name || !bytes) return false;
    CALL(ssys_ma_lyt_Layout_Ctor)(&node_); made_ = true;
    arc_ = &arc; ++arc.users_;
    built_ = CALL(ssys_ma_lyt_Layout_Build)(&node_, name, arc.Reader(), bytes);
    if (!built_) { Reset(); return false; }
    node_.priority = priority;
    return true;
}
bool ArcLayout::BuildData(Arc &arc, std::vector<std::uint8_t> data, unsigned bytes, std::uint8_t priority) {
    if (made_ || !arc.Ready() || !bytes) return false;
    BclytEdit check;
    if (!check.Load(data.data(), data.size())) return false;
    CALL(ssys_ma_lyt_Layout_Ctor)(&node_); made_ = true;
    arc_ = &arc; ++arc.users_; data_ = std::move(data);
    unsigned previous = 0;
    CALL(GpuCmd_GetProperty)(519, &previous);
    CALL(GpuCmd_CreateLists)(1, &node_.command_list_id);
    CALL(GpuCmd_SelectList)(node_.command_list_id);
    node_.command_capacity_bytes = bytes;
    node_.replay_state_mask = 0; node_.replay_option = 1;
    CALL(GpuCmd_AllocateListStorage)(bytes, 1);
    CALL(GpuCmd_SelectList)(previous);
    // 内包する nw::lyt::Layout は、生成型の未公開プレフィクス内から始まる。
    built_ = CALL(nwlyt_Layout_Build)(node_.unk_D + 3, data_.data(), Accessor(*arc.Reader()));
    if (!built_) { Reset(); return false; }
    node_.priority = priority; node_.vptr->calculate(&node_);
    return true;
}
bool ArcLayout::Commit(unsigned screen) {
    if (!built_ || screen > 1) return false;
    auto *mgr = Global<g_LayoutMgr_t>(g_LayoutMgr_addr);
    if (!mgr) return false;
    if (manager_ && (manager_ != mgr || screen_ != screen)) Detach();
    node_.vptr->calculate(&node_);
    auto &list = screen ? mgr->bottom : mgr->top;
    // LayoutMgr は毎フレーム登録を捨てる。残っていれば重ねて登録しない。
    bool linked = false;
    for (auto *p = list.head; p; p = p->next) if (p == &node_) { linked = true; break; }
    if (!linked) {
        node_.prev = node_.next = nullptr;
        CALL(ssys_ma_lyt_LayoutMgr_AddLayout)(mgr, &node_, screen);
    }
    manager_ = mgr; screen_ = screen; return true;
}
void ArcLayout::Detach() {
    if (!manager_) return;
    auto &list = screen_ ? manager_->bottom : manager_->top;
    for (auto *p = list.head; p; p = p->next) {
        if (p != &node_) continue;
        if (node_.prev) node_.prev->next = node_.next; else list.head = node_.next;
        if (node_.next) node_.next->prev = node_.prev; else list.tail = node_.prev;
        break;
    }
    node_.prev = node_.next = nullptr; manager_ = nullptr;
}
void ArcLayout::Reset() {
    Detach();
    if (made_) {
        if (built_) CALL(ssys_ma_lyt_Layout_UnbindAllAnimations)(&node_);
        CALL(ssys_ma_lyt_Layout_Dtor)(&node_);
    }
    if (arc_) --arc_->users_;
    arc_ = nullptr; node_ = {}; made_ = built_ = false;
    std::vector<std::uint8_t>().swap(data_);
}
NwLytPane *ArcLayout::Find(const char *name) {
    return built_ && name ? CALL(SsysMaLayoutNode_FindPane)(&node_, name) : nullptr;
}
bool InOut::Load(ArcLayout &layout, Arc &arc, const char *in, const char *out, const char *group) {
    if (made_ || !layout.Ready() || !arc.Ready() || !in || !out) return false;
    layout_ = layout.Node();
    group_ = group ? CALL(SsysMaLayoutNode_FindGroup)(layout_, group) : nullptr;
    if (group && !group_) { layout_ = nullptr; return false; }
    const char *names[] = {in, out};
    for (unsigned i = 0; i < 2; ++i) {
        CALL(UiAnim_Ctor)(&anim_[i]); ++made_;
        if (!CALL(UiAnim_Load)(&anim_[i], names[i], arc.Reader())) { Reset(); return false; }
    }
    return true;
}
void InOut::Show(bool shown) {
    if (made_ != 2) return;
    const int next = shown ? 0 : 1;
    if (active_ == next) return;
    float start = 0;
    if (active_ >= 0) {
        auto &from = anim_[active_];
        const float total = from.total_frames - 1;
        const float progress = total > 0 ? std::clamp(from.cur_time / total, 0.0f, 1.0f) : 1;
        start = (1 - progress) * std::max(anim_[next].total_frames - 1, 0.0f);
        if (group_) CALL(vc_DATA3)(layout_, &from, group_, false);
        else CALL(Layout_UnbindAnim)(layout_, &from);
    }
    active_ = next;
    if (group_) CALL(vc_ACSYSTEM_DATA1)(layout_, &anim_[next], group_, false);
    else CALL(Layout_BindAnim)(layout_, &anim_[next]);
    CALL(UiAnim_SetFrame)(&anim_[next], start);
}
bool InOut::Step() {
    if (active_ < 0 || Done()) return false;
    CALL(UiAnim_Step)(&anim_[active_]); return true;
}
bool InOut::Done() const { return active_ < 0 || CALL(UiAnim_IsFinished)(&anim_[active_]); }
void InOut::Reset() {
    if (active_ >= 0) {
        if (group_) CALL(vc_DATA3)(layout_, &anim_[active_], group_, false);
        else CALL(Layout_UnbindAnim)(layout_, &anim_[active_]);
    }
    while (made_) CALL(UiAnim_Dtor)(&anim_[--made_]);
    layout_ = nullptr; group_ = nullptr; active_ = -1;
}
bool SetPosition(NwLytPane *p, float x, float y, PaneLog *log) {
    return p && Space(log, 3) && Put(&p->translate[0], x, log) && Put(&p->translate[1], y, log) && Dirty(p, log);
}
bool SetSize(NwLytPane *p, float w, float h, PaneLog *log) {
    return p && Space(log, 3) && Put(&p->width, w, log) && Put(&p->height, h, log) && Dirty(p, log);
}
bool SetVisible(NwLytPane *p, bool shown, PaneLog *log) {
    return p && Space(log, 1) && Put(&p->flags, std::uint8_t((p->flags & ~0x31u) | (shown ? 1u : 0u)), log);
}
bool Reparent(NwLytPane *p, NwLytPane *parent, PaneLog *log) {
    if (!p || p == parent || !Space(log, 8)) return false;
    for (auto *a = parent; a; a = a->parent) if (a == p) return false;
    if (parent && !parent->children.prev) return false;
    auto &link = p->sibling_link;
    if (p->parent) {
        if (!link.prev || !link.next) return false;
        Put(&link.prev->next, link.next, log);
        Put(&link.next->prev, link.prev, log);
    }
    if (parent) {
        auto *tail = parent->children.prev;
        if (!tail) return false;
        Put(&link.prev, tail, log); Put(&link.next, &parent->children, log);
        Put(&tail->next, &link, log); Put(&parent->children.prev, &link, log);
    } else { Put(&link.next, static_cast<NwLytPaneLink *>(nullptr), log); Put(&link.prev, static_cast<NwLytPaneLink *>(nullptr), log); }
    Put(&p->parent, parent, log); Dirty(p, log); return true;
}
bool MirrorPane(const NwLytPane *src, NwLytPane *dst, PaneLog *log) {
    if (!src || !dst || !Space(log, 14)) return false;
    for (unsigned i = 0; i < 3; ++i) { Put(&dst->translate[i], src->translate[i], log); Put(&dst->rotate[i], src->rotate[i], log); }
    for (unsigned i = 0; i < 2; ++i) Put(&dst->scale[i], src->scale[i], log);
    Put(&dst->width, src->width, log); Put(&dst->height, src->height, log);
    Put(&dst->alpha, src->alpha, log); Put(&dst->base_position, src->base_position, log);
    return Put(&dst->flags, std::uint8_t(src->flags & ~0x30u), log);
}
bool MirrorPane(const NwLytTextBox *src, NwLytTextBox *dst, PaneLog *log) {
    if (!src || !dst || !Space(log, 24)) return false;
    MirrorPane(&src->pane, &dst->pane, log);
    Put(&dst->top_color, src->top_color, log);
    Put(&dst->bottom_color, src->bottom_color, log);
    Put(&dst->font_width, src->font_width, log);
    Put(&dst->font_height, src->font_height, log);
    Put(&dst->dirty, std::uint8_t(dst->dirty | 1u), log);
    if (src->material && dst->material) {
        Put(&dst->material->colors, src->material->colors, log);
        Put(&dst->material->flags, std::uint8_t(dst->material->flags & ~4u), log);
    }
    return true;
}
bool SetTexture(NwLytPicture *picture, Arc &arc, const char *name) {
    if (!picture || !picture->material || !arc.Ready() || !name) return false;
    auto *mat = picture->material;
    if (!mat->variable_data || !(mat->count_bits & NwLytMaterialCountMask_TexMaps)) return false;
    unsigned info[5]{};
    CALL(nwlyt_ArcResourceAccessor_GetTexture)(info, Accessor(*arc.Reader()), name);
    if (!info[1]) return false;
    // TextureInfo と TexMap は同じ先頭4語。形式だけ TexMap の旗へ配置する。
    auto *texture = mat->variable_data;
    std::memcpy(texture, info, 4 * sizeof(unsigned));
    texture->sampler_bits = (texture->sampler_bits & ~0xF00u) | ((info[4] << 8) & 0xF00u);
    CALL(nwlyt_TexMap_UpdateGpuRegs)(mat->variable_data);
    mat->flags &= ~4u;
    return true;
}
#undef CALL
} // namespace gohan::game::lyt
