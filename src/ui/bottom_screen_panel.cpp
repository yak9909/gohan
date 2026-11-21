#include "ui/bottom_screen_panel.hpp"
namespace gohan::ui {
void BottomScreenPanel::Step(const LeaseStatus &status) {
    if (stage_ == Stage::Empty) {
        if (held_) {
            if (!releasing_) releasing_ = lease_.release(owner_);
            if (releasing_ && lease_.returned(owner_)) held_ = releasing_ = false;
            return;
        }
        if (!wanted_ || stopping_ || status.stopping) return;
        if (!lease_.acquire(owner_, top_)) return;
        held_ = true; stage_ = Stage::Waiting;
    }
    if (stage_ == Stage::Waiting) {
        if (!wanted_ || stopping_) { stage_ = Stage::Empty; return; }
        if (top_ ? (status.stage != LeaseStage::Shown && status.stage != LeaseStage::Hidden)
                 : !status.hidden) return;
        stage_ = Stage::Building;
    }
    if (stage_ == Stage::Building) {
        // 一覧は Setup 完了まで回す。組みかけの Finalize は呼ばない。
        const auto result = content_.Build();
        if (result == game::PanelBuild::Pending) return;
        if (result == game::PanelBuild::Failed) {
            wanted_ = false; content_.Detach(); stage_ = Stage::Retiring; idle_ = 0; return;
        }
        content_.Show(wanted_ && !stopping_); stage_ = Stage::Visible;
    }
    if (stage_ == Stage::Visible) {
        if (!top_ && !status.hidden) {
            content_.Detach(); stage_ = Stage::Retiring; idle_ = 0; return;
        }
        const bool show = wanted_ && !stopping_ && (top_ || status.hidden);
        content_.Show(show); content_.Update();
        if (!show && content_.Finished()) {
            content_.Detach(); stage_ = Stage::Retiring; idle_ = 0;
        } else content_.Draw();
        return;
    }
    if (stage_ == Stage::Retiring && ++idle_ >= 3) {
        content_.Reset(); stage_ = Stage::Empty;
    }
}
}
