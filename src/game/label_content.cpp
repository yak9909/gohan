#include "game/label_content.hpp"
#include "game/panel_math.hpp"
#include "core/utf.hpp"
#include <algorithm>
namespace gohan::game {
namespace {
using namespace generated;
#define CALL(n) reinterpret_cast<n##_t>(n##_addr)
void Scale(NwLytPane *p, float x, float y) {
    if (!p) return;
    p->scale[0] = x; p->scale[1] = y; p->flags &= ~0x30u;
}
}
bool LabelContent::Set(std::vector<LabelLine> lines) {
    if (boxes_ || lines.size() > 8) return false;
    lines_ = std::move(lines); return true;
}
PanelBuild LabelContent::Build() {
    if (!arc_.LoadStep("Layout/Menu/time_bel_win.arc")) return PanelBuild::Pending;
    if (!boxes_) boxes_.reset(new Box[lines_.size()]);
    while (built_ < lines_.size()) {
        auto &b = boxes_[built_]; auto &layout = b.layout;
        if (!layout.Build(arc_, "time_bel_win.bclyt", 0x5510, 0x80) ||
            !b.motion.Load(layout, arc_, "time_bel_win_in.bclan", "time_bel_win_out.bclan", "G_bell_00"))
            return PanelBuild::Failed;
        auto *text = CALL(SsysMaLayoutNode_FindTextBox)(layout.Node(), "T_bell_00");
        auto *base = layout.Find("P_bell_base"), *shadow = layout.Find("P_bell_sh");
        auto *bell = layout.Find("N_bell"), *all = layout.Find("N_all");
        if (!text || !base || !shadow || !bell || !all) return PanelBuild::Failed;
        lyt::SetVisible(layout.Find("N_time"), false);
        for (const char *name : {"P_bell_icn_sh", "P_bell_icon", "P_mld_icn_sh", "P_mdl_icon",
                                 "P_cin_icn_sh", "P_cin_icon", "P_ticket_ico_sh", "P_ticket_icon"})
            lyt::SetVisible(layout.Find(name), false);
        const auto *mgr = *reinterpret_cast<g_FontMgr_t *>(g_FontMgr_addr);
        text->font = CALL(Font_GetLoadedFont)(const_cast<void *>(mgr), 0);
        std::u16string utf(lines_[built_].text.size() + 1, 0);
        const auto len = core::ToUtf16(lines_[built_].text, utf.data(), utf.size()).written;
        CALL(nwlyt_TextBox_AllocStringBuffer_0)(text, unsigned(len + 1), text->batch ? text->batch->flags : 0);
        CALL(nwlyt_TextBox_SetString)(text, reinterpret_cast<const std::uint16_t *>(utf.data()), 0, len);
        text->pane.base_position = (text->pane.base_position & 0xF0u) | 4u;
        text->text_position = 4; text->dirty |= 1;
        lyt::SetPosition(&text->pane, -43, 12);
        alignas(8) NwFontTextWriter writer{};
        CALL(nwfont_TextWriter_Ctor)(&writer);
        CALL(nwlyt_TextBox_ConfigureTextWriter)(text, &writer);
        const float measured = CALL(nwfont_TextWriter_CalcStringWidth)(&writer,
            reinterpret_cast<const std::uint16_t *>(utf.data()), int(len));
        CALL(nwfont_TextWriter_Dtor)(&writer);
        b.width = LabelWidth(measured);
        Scale(base, b.width / 98, 1); Scale(shadow, b.width / 98, 1);
        lyt::SetSize(&text->pane, b.width - 16, text->pane.height);
        // 入場し終えた位置を基準として左上に並べる。
        UiAnim pose{}; CALL(UiAnim_Ctor)(&pose);
        if (!CALL(UiAnim_Load)(&pose, "time_bel_win_in.bclan", arc_.Reader())) {
            CALL(UiAnim_Dtor)(&pose); return PanelBuild::Failed;
        }
        void *group = CALL(SsysMaLayoutNode_FindGroup)(layout.Node(), "G_bell_00");
        CALL(vc_ACSYSTEM_DATA1)(layout.Node(), &pose, group, false);
        CALL(UiAnim_SetFrame)(&pose, std::max(0.0f, pose.total_frames - 1));
        layout.Node()->vptr->calculate(layout.Node());
        b.bell_x = bell->translate[0]; b.bell_y = bell->translate[1];
        CALL(vc_DATA3)(layout.Node(), &pose, group, false); CALL(UiAnim_Dtor)(&pose);
        ++built_;
    }
    float left[8]; std::fill_n(left, 8, -192.0f);
    for (unsigned i = 0; i < built_; ++i) {
        auto &b = boxes_[i]; const unsigned row = std::min(lines_[i].row, 7u);
        auto *all = b.layout.Find("N_all"); Scale(all, 0.72f, 0.72f);
        const float cx = left[row] + b.width * 0.72f / 2;
        const float cy = 96 + 16 * (1 - 0.72f) - row * 36 * 0.72f;
        lyt::SetPosition(all, cx - 0.72f * (b.bell_x - 59), cy - 0.72f * (b.bell_y - 6));
        left[row] = LabelNext(left[row], b.width);
    }
    return PanelBuild::Ready;
}
void LabelContent::Show(bool shown) { for (unsigned i = 0; i < built_; ++i) boxes_[i].motion.Show(shown); }
void LabelContent::Update() { for (unsigned i = 0; i < built_; ++i) boxes_[i].motion.Step(); }
bool LabelContent::Finished() const {
    for (unsigned i = 0; i < built_; ++i) if (!boxes_[i].motion.Done()) return false;
    return true;
}
void LabelContent::Draw() { for (unsigned i = 0; i < built_; ++i) boxes_[i].layout.Commit(0); }
void LabelContent::Detach() { if (boxes_) for (unsigned i = 0; i < lines_.size(); ++i) boxes_[i].layout.Detach(); }
void LabelContent::Reset() {
    Detach();
    if (boxes_) for (unsigned i = 0; i < lines_.size(); ++i) {
        boxes_[i].motion.Reset(); boxes_[i].layout.Reset();
    }
    boxes_.reset(); arc_.Reset(); built_ = 0; std::vector<LabelLine>().swap(lines_);
}
#undef CALL
}
