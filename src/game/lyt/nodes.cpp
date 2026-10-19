#include "game/lyt/nodes.hpp"
#include <cstring>

namespace gohan::game::lyt::detail {
namespace {
using namespace generated;

static_assert(sizeof(void *) != 4 || sizeof(SsysMaLayoutNode) == 0x14C);

// 呼ばれても何もしない仮想表の枠。ゲームは記録・再生・投影の設定だけを呼ぶ。
void Nothing(SsysMaLayoutNode *) {}
bool NothingBuilt(SsysMaLayoutNode *, const char *, void *, unsigned) { return false; }

constexpr std::uint8_t PriorityLast = 0xFF;
constexpr std::uint8_t ReplayOption = 1;
constexpr int ScreenHeight = 240;

SsysMaLayoutList *ListOf(SsysMaLayoutMgr *manager, Screen screen) {
    return screen == Screen::Bottom ? &manager->bottom : &manager->top;
}
} // namespace

void LayoutNodes::Build(const ResourceApi &api, NwLytPane *root[2], const unsigned list[2]) {
    api_ = &api;
    auto &table = table_.value;
    std::memset(&table, 0, sizeof(table));
    table.unk_0 = reinterpret_cast<void *>(&Nothing);
    table.unk_4 = reinterpret_cast<void *>(&Nothing);
    table.replay_recorded_list = reinterpret_cast<decltype(table.replay_recorded_list)>(
        ssys_ma_lyt_Layout_ReplayRecordedList_addr);
    table.calculate = reinterpret_cast<decltype(table.calculate)>(&Nothing);
    table.build = &NothingBuilt;
    table.record_pane_tree = reinterpret_cast<decltype(table.record_pane_tree)>(
        ssys_ma_lyt_Layout_RecordPaneTree_addr);
    table.setup_projection_matrix = reinterpret_cast<decltype(table.setup_projection_matrix)>(
        ssys_ma_lyt_Layout_SetupProjectionMatrix_addr);
    for (unsigned s = 0; s < 2; ++s) {
        auto &node = nodes_[s].value;
        std::memset(&node, 0, sizeof(node));
        node.vptr = &table;
        node.priority = PriorityLast;
        node.root = root[s];
        node.width = s ? 320.0f : 400.0f;
        node.height = static_cast<float>(ScreenHeight);
        // 内包する描画情報は、ゲームの Layout が作るときと同じ初期値にする。
        node.draw_info.vptr = reinterpret_cast<NwLytDrawInfoVtable *>(vtbl_nw_lyt_DrawInfo_addr);
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
                node.draw_info.view_mtx[row][column] = row == column ? 1.0f : 0.0f;
        node.draw_info.location_adjust_scale[0] = node.draw_info.location_adjust_scale[1] = 1.0f;
        node.draw_info.global_alpha = 1.0f;
        // 視点の Z と、Pane のアニメーションの印も、ゲームの Layout の初期値に合わせる。
        node.view_eye[2] = 1.0f;
        node.animation_flags = 0;
        node.command_list_id = list[s];
        node.replay_state_mask = 0;
        node.command_capacity_bytes = static_cast<unsigned>(core::limits::RendererRecordBytes);
        node.replay_option = ReplayOption;
        node.record_dirty = 1;
    }
}

void LayoutNodes::MarkDirty(Screen screen) {
    nodes_[static_cast<unsigned>(screen)].value.record_dirty = 1;
}

bool LayoutNodes::Linked(Screen screen) const {
    const SsysMaLayoutMgr *manager = api_ ? api_->manager() : nullptr;
    if (!manager)
        return false;
    const auto &node = nodes_[static_cast<unsigned>(screen)].value;
    const SsysMaLayoutList *list = ListOf(const_cast<SsysMaLayoutMgr *>(manager), screen);
    // 空のリストなら、前の登録の名残の前後の印は無効。
    if (!list->head)
        return false;
    return list->tail == &node || node.prev || node.next;
}

bool LayoutNodes::Register(Screen screen) {
    SsysMaLayoutMgr *manager = api_ ? api_->manager() : nullptr;
    if (!manager)
        return false;
    auto &node = nodes_[static_cast<unsigned>(screen)].value;
    SsysMaLayoutList *list = ListOf(manager, screen);
    if (!list->head) {
        node.prev = nullptr;
        node.next = nullptr;
    } else if (Linked(screen)) {
        return true;
    }
    node.draw_info.text_work = &manager->text_work;
    api_->addLayout(manager, &node, static_cast<unsigned>(screen));
    return true;
}
} // namespace gohan::game::lyt::detail
