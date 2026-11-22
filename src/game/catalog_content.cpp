#include "game/catalog_content.hpp"
#include "game/catalog_content_selection.hpp"
#include "game/hook_batch.hpp"
#include "game/heap.hpp"
#include "game/panel_math.hpp"
#include "game/catalog_layout.hpp"
#include "core/utf.hpp"
#include <CTRPluginFramework.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace gohan::game {
namespace {
using namespace generated;
#define CALL(n) reinterpret_cast<n##_t>(n##_addr)
CatalogContent *owner = nullptr;
HookBatch *drag_hook = nullptr;
std::u16string Utf(const std::string &s) {
    std::u16string out(s.size() + 1, 0);
    const auto n = core::ToUtf16(s, out.data(), out.size()).written;
    out.resize(n); return out;
}
void DetachNode(SsysMaLayoutNode &node) {
    auto *mgr = *reinterpret_cast<g_LayoutMgr_t *>(g_LayoutMgr_addr);
    if (!mgr) return;
    for (auto *list : {&mgr->top, &mgr->bottom})
        for (auto *p = list->head; p; p = p->next) if (p == &node) {
            if (node.prev) node.prev->next = node.next; else list->head = node.next;
            if (node.next) node.next->prev = node.prev; else list->tail = node.prev;
            node.prev = node.next = nullptr; break;
        }
}
bool BuildData(SsysMaLayoutNode &node, ArcResAccReader &reader, const std::vector<std::uint8_t> &data) {
    unsigned previous = 0;
    CALL(GpuCmd_GetProperty)(519, &previous);
    CALL(GpuCmd_CreateLists)(1, &node.command_list_id);
    CALL(GpuCmd_SelectList)(node.command_list_id);
    node.command_capacity_bytes = 0xB000; node.replay_state_mask = 0; node.replay_option = 1;
    CALL(GpuCmd_AllocateListStorage)(node.command_capacity_bytes, 1);
    CALL(GpuCmd_SelectList)(previous);
    if (!CALL(nwlyt_Layout_Build)(node.unk_D + 3, data.data(), &reader.accessor_vptr)) return false;
    node.vptr->calculate(&node); return true;
}
std::vector<std::uint8_t> ExtraLayout(ArcResAccReader &reader) {
    void *accessor = reader.resource_accessor;
    if (!accessor) return {};
    const auto *vt = *static_cast<const std::uintptr_t *const *>(accessor);
    unsigned bytes = 0;
    using Get = const std::uint8_t *(*)(void *, unsigned, const char *, unsigned *);
    const auto *src = reinterpret_cast<Get>(vt[2])(accessor, 0x626C7974, "ctlg_cntnt_00.bclyt", &bytes);
    if (!src) return {};
    if (!bytes) std::memcpy(&bytes, src + 12, 4);
    return MakeCatalogExtraLayout(src, bytes);
}
}
bool CatalogContent::Wire() {
    static const CodeCheck checks[] = {
#define CHECK(i) {PanelCall##i##_addr, PanelCall##i##_len, PanelCall##i##_crc32}
        CHECK(0), CHECK(1), CHECK(2), CHECK(3), CHECK(4), CHECK(5), CHECK(6),
        CHECK(7), CHECK(8), CHECK(9), CHECK(10)
#undef CHECK
    };
    if (!VerifyNative(checks)) return false;
    static const HookSpec spec{{CatalogScrollDrag_addr, CatalogScrollDrag_len, CatalogScrollDrag_crc32},
                               reinterpret_cast<std::uintptr_t>(Drag), HookKind::Mitm};
    static HookBatch hook(NativeHookBackend(), {&spec, 1});
    drag_hook = &hook;
    return hook.Wire() == Result::Ok;
}
void CatalogContent::Shutdown() { if (drag_hook) drag_hook->Shutdown(); drag_hook = nullptr; }
void CatalogContent::Drag(void *scroll) {
    auto *s = static_cast<CommonScrollBar *>(scroll);
    if (!OwnedDrag(s, owner && owner->setup_ ? &owner->list_->scroll_bar : nullptr,
                   s->drag_active, s->drag_blocked_this_frame, s->input_mode)) {
        CTRPluginFramework::HookContext::GetCurrent().OriginalFunction<void>(scroll); return;
    }
    const auto *touch = reinterpret_cast<const AcnlTouchState *>(g_TouchState_addr);
    if (!CALL(Touch_IsDragHoldReady)(touch)) return;
    const AcnlUiPoint2 pixels{float(touch->start_x), float(touch->start_y)};
    AcnlUiPoint2 start{}, now{};
    CALL(Layout_TransformTouchPixelsToLocal)(&s->layout, &pixels, &start);
    CALL(Layout_TransformTouchPixelsToLocal)(&s->layout, CALL(GameInput_GetTouchPixelPosition)(), &now);
    const float point[] = {HalfDrag(start.x, now.x), HalfDrag(start.y, now.y)};
    CALL(CommonScrollBar_ApplyDragPoint)(s, point);
}
bool CatalogContent::Set(std::vector<CatalogRow> rows, std::string label, bool extras) {
    if (!Empty() || rows.size() > 512) return false;
    rows_ = std::move(rows); label_ = std::move(label); extras_ = extras; return true;
}
int CatalogContent::BuildItems(InstSelect8 *list) {
    if (!owner || list != owner->list_) return 0;
    list->item_count = int(owner->rows_.size()); return 1;
}
void *CatalogContent::WordAt(InstSelect8 *list, int index) {
    if (!owner || list != owner->list_ || index < 0 || unsigned(index) >= owner->words_.size()) return nullptr;
    return &owner->words_[index].fixed;
}
void CatalogContent::SetSelect(InstSelect8 *list, int index, int before) {
    CALL(InstSelect_SetSelectVisual)(list, index, before);
}
void CatalogContent::WaitCalc(InstSelect8 *list) {
    // 決定時は元の待機処理が index を通知する。所有する表の他の欄は元のまま使う。
    if (owner && owner->list_ == list) {
        void *control = list->unk_A00;
        if (CALL(ButtonActionControl_IsDecided)(control)) {
            const int value = list->scroll_bar.top_row + CALL(ButtonActionControl_GetDecidedIndex)(control);
            if (value >= 0 && unsigned(value) < owner->rows_.size()) owner->decision_ = value;
        }
    }
    CALL(SelectBase_WaitCalc)(list);
}
PanelBuild CatalogContent::Build() {
    if (!arc_.LoadStep("Layout/catalog/catalogue.arc")) return PanelBuild::Pending;
    if (!frame_.Ready()) {
        if (!frame_.Build(arc_, "ctlg_list.bclyt", 0x8000) ||
            !motion_.Load(frame_, arc_, "ctlg_list_in.bclan", "ctlg_list_out.bclan")) return PanelBuild::Failed;
        CALL(UiAnim_Ctor)(&kind_); kind_made_ = true;
        if (!CALL(UiAnim_Load)(&kind_, "ctlg_list_kind.bclan", arc_.Reader())) return PanelBuild::Failed;
        CALL(Layout_BindAnim)(frame_.Node(), &kind_);
        CALL(UiAnim_SetFrame)(&kind_, reinterpret_cast<const float *>(kCatalogListKindFrameTable_addr)[6]);
        frame_.Node()->vptr->calculate(frame_.Node());
        CALL(Layout_UnbindAnim)(frame_.Node(), &kind_);
    }
    if (!list_) {
        if (owner && owner != this) return PanelBuild::Failed;
        auto *alloc = *reinterpret_cast<g_UiLayoutAllocator_t *>(g_UiLayoutAllocator_addr);
        if (!alloc || !alloc->heap || !heap::Measure(heap::Kind::Layout).Allows(65536, 8192))
            return PanelBuild::Pending;
        words_.resize(rows_.size());
        auto *message = *reinterpret_cast<vc_DATAPOINTER_t *>(vc_DATAPOINTER_addr);
        for (unsigned i = 0; i < rows_.size(); ++i) {
            auto &w = words_[i]; CALL(script_WordFix38_Ctor)(&w.fixed);
            w.text = Utf(rows_[i].text);
            core::ToUtf16(rows_[i].text, reinterpret_cast<char16_t *>(w.fixed.text), 38);
            w.extra = Utf(rows_[i].prefix + rows_[i].extra);
            if (message && rows_[i].message && !label_.empty())
                CALL(vc_NPC_2_SETUP)(message, &w.fixed, label_.c_str(), rows_[i].message);
        }
        void *mem = CALL(Heap_Alloc)(sizeof(InstSelect8), alloc->heap, 4);
        if (!mem) return PanelBuild::Failed;
        list_ = CALL(InstSelect8_Ctor)(static_cast<InstSelect8 *>(mem)); owner = this;
        const auto *base = reinterpret_cast<const std::uintptr_t *>(InstSelect_8_vtable);
        if (!table_.Install(reinterpret_cast<std::uintptr_t *>(&list_->vptr), base) ||
            !table_.Set(13, reinterpret_cast<std::uintptr_t>(BuildItems)) ||
            !table_.Set(16, reinterpret_cast<std::uintptr_t>(SetSelect)) ||
            !table_.Set(24, reinterpret_cast<std::uintptr_t>(WaitCalc)) ||
            !table_.Set(25, reinterpret_cast<std::uintptr_t>(WordAt)) || !table_.Activate()) return PanelBuild::Failed;
        list_->text_capacity = 128;
        if (extras_) {
            layout_data_ = ExtraLayout(*arc_.Reader());
            if (layout_data_.empty() || !BuildData(list_->layout, *arc_.Reader(), layout_data_)) return PanelBuild::Failed;
        }
    }
    if (!setup_) {
        auto *alloc = *reinterpret_cast<g_UiLayoutAllocator_t *>(g_UiLayoutAllocator_addr);
        if (!CALL(SelectBase_SetupStep)(list_, arc_.Reader(), alloc->heap)) return PanelBuild::Pending;
        setup_ = true;
        auto *pane = frame_.Find("N_scrl_pos_00");
        if (!pane) return PanelBuild::Failed;
        CALL(CommonScrollBar_AttachTo)(&list_->scroll_bar, pane);
        if (extras_) for (unsigned i = 0; i < 8; ++i) {
            char name[16]; auto &e = extra_[i];
            std::snprintf(name, sizeof(name), "T_itm_%02u", i);
            e.name = CALL(SsysMaLayoutNode_FindTextBox)(&list_->layout, name);
            std::snprintf(name, sizeof(name), "T_id_%02u", i);
            e.box = CALL(SsysMaLayoutNode_FindTextBox)(&list_->layout, name);
            if (!e.name || !e.box) return PanelBuild::Failed;
            CALL(nwlyt_TextBox_AllocStringBuffer_0)(e.box, 128, e.box->batch ? e.box->batch->flags : 0);
            e.x = e.box->pane.translate[0]; e.y = e.box->pane.translate[1];
            e.name_x = e.name->pane.translate[0]; e.name_y = e.name->pane.translate[1];
        }
    }
    return PanelBuild::Ready;
}
void CatalogContent::Show(bool shown) {
    if (!setup_ || (started_ && shown == shown_)) return;
    auto &from = shown ? list_->leave_anim : list_->enter_anim;
    auto &to = shown ? list_->enter_anim : list_->leave_anim;
    const float total = from.total_frames - 1;
    const float progress = started_ && total > 0 ? std::clamp(from.cur_time / total, 0.0f, 1.0f) : 1;
    if (started_) CALL(Layout_UnbindAnim)(&list_->layout, &from);
    if (shown) CALL(SelectBase_StartEnter)(list_); else CALL(SelectBase_StartLeave)(list_);
    if (started_) CALL(UiAnim_SetFrame)(&to, (1 - progress) * std::max(to.total_frames - 1, 0.0f));
    motion_.Show(shown); shown_ = shown; started_ = true;
}
void CatalogContent::ExtraRows() {
    if (!extras_) return;
    for (unsigned i = 0; i < 8; ++i) {
        auto &e = extra_[i]; if (!e.box || !e.name) continue;
        const int index = list_->scroll_bar.top_row + int(i);
        const std::u16string empty;
        const auto &text = index >= 0 && unsigned(index) < words_.size() ? words_[index].extra : empty;
        CALL(nwlyt_TextBox_SetString)(e.box, reinterpret_cast<const std::uint16_t *>(text.c_str()), 0, text.size());
        lyt::MirrorPane(e.name, e.box);
        lyt::SetPosition(&e.box->pane, e.x + e.name->pane.translate[0] - e.name_x,
                         e.y + e.name->pane.translate[1] - e.name_y);
        lyt::SetSize(&e.box->pane, 47, e.name->pane.height);
        e.box->font_width = e.name->font_width * 0.65f;
        e.box->font_height = e.name->font_height * 0.65f; e.box->dirty |= 1;
    }
}
void CatalogContent::Update() {
    if (!setup_) return;
    NativeInputInjection inject;
    if (inject.Begin(touch_)) CALL(SelectBase_Update)(list_);
    motion_.Step(); ExtraRows();
}
bool CatalogContent::Finished() const {
    return !setup_ || (!list_->animation_active && motion_.Done());
}
void CatalogContent::Draw() {
    if (!setup_) return;
    frame_.Commit(); // 毎フレーム計算し、子に付けたつまみの位置を反映する。
    CALL(SelectBase_Draw)(list_);
}
void CatalogContent::Detach() {
    frame_.Detach();
    if (list_) { DetachNode(list_->layout); DetachNode(list_->scroll_bar.layout); }
}
void CatalogContent::Reset() {
    Detach();
    if (list_) {
        if (setup_) CALL(SelectBase_Finalize)(list_);
        table_.Restore();
        const auto *vt = static_cast<const std::uintptr_t *>(list_->vptr);
        reinterpret_cast<void (*)(void *)>(vt[1])(list_); list_ = nullptr;
    }
    if (owner == this) owner = nullptr;
    motion_.Reset();
    if (kind_made_) CALL(UiAnim_Dtor)(&kind_);
    frame_.Reset(); arc_.Reset();
    std::vector<Word>().swap(words_); std::vector<CatalogRow>().swap(rows_);
    std::vector<std::uint8_t>().swap(layout_data_); std::string().swap(label_);
    for (auto &e : extra_) e = {};
    setup_ = kind_made_ = started_ = shown_ = false; decision_ = -1;
}
int CatalogContent::Selected() const { return list_ && setup_ ? list_->selected_index : 0; }
int CatalogContent::VisibleRows() const {
    return list_ && setup_ ? std::clamp(int(rows_.size()) - list_->scroll_bar.max_top_row, 1, 8) : 6;
}
void CatalogContent::Select(int index) {
    if (!setup_ || rows_.empty()) return;
    SelectCatalogRow(int(rows_.size()), list_->selected_index, index, [this](int next, int before) {
        int top = list_->scroll_bar.top_row;
        if (next < top) top = next; else if (next >= top + VisibleRows()) top = next - VisibleRows() + 1;
        CALL(CommonScrollBar_SetPos)(&list_->scroll_bar, top);
        SetSelect(list_, next, before);
    }, [] {
        // 一覧の行フォーカス音。生成された列挙には値がないため、ここで型を合わせる。
        constexpr auto list_active = static_cast<AcnlCatalogSoundId_F015>(0x010003B0);
        CALL(Game_PlaySound)(list_active);
    });
}
int CatalogContent::TakeDecision() { const int value = decision_; decision_ = -1; return value; }
#undef CALL
}
