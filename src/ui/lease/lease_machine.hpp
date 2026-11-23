#pragma once
#include <cstdint>
namespace gohan::ui {
enum class LeaseStage : std::uint8_t { Shown, Exiting, Hidden, Restoring };
enum class LeaseCommand : std::uint8_t { None, HideMap, HideOther, HideTab, CloseMenu, FinishMenuClose, Restore };
struct LeaseProbe {
    std::uint32_t room = 0, owners = 0;
    bool chat_over = false, tab_settled = false, commands_consumed = false;
    bool tab_command_consumed = false;
    bool map_present = false, other_present = false;
    bool menu_open = false, menu_entered = false, menu_gone = false;
    bool hide_done = false, restore_done = false, game_restored = false;
    bool resources_returned = false;
};
class LeaseMachine {
  public:
    static constexpr unsigned MinFrames = 2;
    LeaseCommand Step(const LeaseProbe &p) {
        if (!room_seen_ || p.room != room_) {
            const bool changed = room_seen_;
            room_ = p.room; room_seen_ = true;
            // Shown のメニュー終了処理は部屋が変わっても最後まで続ける。
            if (changed && stage_ != LeaseStage::Shown) {
                Enter(LeaseStage::Shown); requested_ = hidden_ = restoring_ = false;
                restored_ = true; return LeaseCommand::None;
            }
        }
        if (frames_ < MinFrames) ++frames_;
        const bool want = !stopping_ && p.owners && !p.chat_over;
        switch (stage_) {
        case LeaseStage::Shown:
            // 一度閉じ始めたら、持ち主が消えても必ず後始末をする。
            if (closing_) {
                if (!p.menu_gone) return LeaseCommand::None;
                closing_ = false;
                if (!want) { requested_ = false; restored_ = true; }
                return LeaseCommand::FinishMenuClose;
            }
            if (!want) return LeaseCommand::None;
            if (p.menu_open) {
                if (!p.menu_entered || !p.tab_command_consumed) return LeaseCommand::None;
                closing_ = true; requested_ = true; restored_ = false;
                return LeaseCommand::CloseMenu;
            }
            if (!p.tab_settled || !p.commands_consumed) return LeaseCommand::None;
            requested_ = true; restored_ = false;
            Enter(LeaseStage::Exiting);
            return p.map_present ? LeaseCommand::HideMap : p.other_present ? LeaseCommand::HideOther : LeaseCommand::HideTab;
        case LeaseStage::Exiting:
            // 退場中にはまだ地図がある。再出現の判定は Hidden で行う。
            if (frames_ >= MinFrames && p.hide_done) { hidden_ = true; Enter(LeaseStage::Hidden); }
            return LeaseCommand::None;
        case LeaseStage::Hidden:
            if (p.game_restored) {
                hidden_ = requested_ = false; restored_ = true;
                Enter(LeaseStage::Shown); return LeaseCommand::None;
            }
            if (!want && p.resources_returned && p.tab_settled && p.commands_consumed) {
                hidden_ = requested_ = false; restoring_ = true;
                Enter(LeaseStage::Restoring); return LeaseCommand::Restore;
            }
            return LeaseCommand::None;
        case LeaseStage::Restoring:
            if (frames_ >= MinFrames && p.restore_done) {
                restoring_ = false; restored_ = true; Enter(LeaseStage::Shown);
            }
            return LeaseCommand::None;
        }
        return LeaseCommand::None;
    }
    // 排出中も Step を続ける。退場と資源返却が済む前には出し直さない。
    LeaseCommand Shutdown() { stopping_ = true; return LeaseCommand::None; }
    bool Drained() const { return !closing_ && stage_ == LeaseStage::Shown; }
    LeaseStage Stage() const { return stage_; }
    bool Requested() const { return requested_; }
    bool Hidden() const { return hidden_; }
    bool Restoring() const { return restoring_; }
    bool Restored() const { return restored_; }
  private:
    void Enter(LeaseStage s) { stage_ = s; frames_ = 0; }
    LeaseStage stage_ = LeaseStage::Shown;
    unsigned frames_ = 0;
    std::uint32_t room_ = 0;
    bool room_seen_ = false, closing_ = false, requested_ = false, hidden_ = false;
    bool restoring_ = false, restored_ = true, stopping_ = false;
};
} // namespace gohan::ui
