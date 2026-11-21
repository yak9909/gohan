#pragma once
#include <cstdint>
namespace gohan::ui {
enum class LeaseStage : std::uint8_t { Shown, Exiting, Hidden, Restoring };
enum class LeaseCommand : std::uint8_t { None, HideMap, HideOther, HideTab, CloseMenu, FinishMenuClose, Restore };
struct LeaseProbe {
    std::uint32_t room = 0, owners = 0;
    bool chat_over = false, tab_settled = false, commands_consumed = false;
    bool map_present = false, other_present = false;
    bool menu_open = false, menu_entered = false, menu_gone = false;
    bool hide_done = false, restore_done = false, game_restored = false;
    bool resources_returned = false, available = true;
};
class LeaseMachine {
  public:
    static constexpr unsigned MinFrames = 2;
    static constexpr unsigned TimeoutFrames = 120;
    LeaseCommand Step(const LeaseProbe &p) {
        const bool moved = room_seen_ && p.room != room_;
        room_ = p.room; room_seen_ = true;
        if (frames_ < TimeoutFrames) ++frames_;
        const bool want = !stopping_ && p.owners && !p.chat_over;
        const bool active = stage_ != LeaseStage::Shown || closing_ || requested_ || recovering_;
        if (moved && !active) return Shown();
        // 退場直後の残像ではなく、命令を消費して静止した実物を見る。
        if (stage_ == LeaseStage::Restoring && p.restore_done) return Shown();
        if (active && p.game_restored && p.tab_settled && p.commands_consumed &&
            (stage_ != LeaseStage::Exiting || frames_ >= MinFrames))
            return Shown();
        if (active && (moved || !want || ((stage_ != LeaseStage::Hidden || closing_) && frames_ >= TimeoutFrames))) recovering_ = true;
        if (recovering_) return Recover(p);
        switch (stage_) {
        case LeaseStage::Shown:
            if (closing_) {
                if (!p.menu_gone) return LeaseCommand::None;
                closing_ = requested_ = false; restored_ = true;
                Enter(LeaseStage::Shown);
                return LeaseCommand::FinishMenuClose;
            }
            if (!want || frames_ < MinFrames) return LeaseCommand::None;
            if (p.menu_open) {
                if (!p.menu_entered || !p.commands_consumed) return LeaseCommand::None;
                closing_ = true; requested_ = true; restored_ = false; frames_ = 0;
                return LeaseCommand::CloseMenu;
            }
            if (!p.tab_settled || !p.commands_consumed) return LeaseCommand::None;
            requested_ = true; restored_ = false;
            Enter(LeaseStage::Exiting);
            return p.map_present ? LeaseCommand::HideMap : p.other_present ? LeaseCommand::HideOther : LeaseCommand::HideTab;
        case LeaseStage::Exiting:
            if (frames_ >= MinFrames && p.hide_done) { hidden_ = true; Enter(LeaseStage::Hidden); }
            break;
        case LeaseStage::Hidden:
            break;
        case LeaseStage::Restoring:
            if (p.restore_done) return Shown();
            break;
        }
        return LeaseCommand::None;
    }
    LeaseCommand Shutdown() { stopping_ = true; return LeaseCommand::None; }
    bool Drained() const { return !closing_ && (stage_ == LeaseStage::Shown || stage_ == LeaseStage::Restoring); }
    LeaseStage Stage() const { return stage_; }
    bool Requested() const { return requested_; }
    bool Recovering() const { return recovering_; }
    bool Hidden() const { return hidden_; }
    bool Restoring() const { return restoring_; }
    bool Restored() const { return restored_; }
  private:
    LeaseCommand Shown() {
        closing_ = requested_ = hidden_ = restoring_ = recovering_ = false;
        restored_ = true; Enter(LeaseStage::Shown); return LeaseCommand::None;
    }
    LeaseCommand Recover(const LeaseProbe &p) {
        // 先に貸し出しを取り消して、パネルの取り外しと資源返却を促す。
        requested_ = hidden_ = false;
        if (closing_ && p.menu_gone) {
            closing_ = false; return LeaseCommand::FinishMenuClose;
        }
        if (!p.resources_returned || !p.available) return LeaseCommand::None;
        if (stage_ == LeaseStage::Restoring && frames_ < TimeoutFrames) return LeaseCommand::None;
        closing_ = false; restoring_ = true; restored_ = false;
        Enter(LeaseStage::Restoring); return LeaseCommand::Restore;
    }
    void Enter(LeaseStage s) { stage_ = s; frames_ = 0; }
    LeaseStage stage_ = LeaseStage::Shown;
    unsigned frames_ = 0;
    std::uint32_t room_ = 0;
    bool room_seen_ = false, closing_ = false, requested_ = false, hidden_ = false;
    bool restoring_ = false, restored_ = true, stopping_ = false, recovering_ = false;
};
} // namespace gohan::ui
