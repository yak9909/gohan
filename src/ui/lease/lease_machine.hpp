#pragma once
// 下画面の借り受けの段の移り変わり（ゲームを呼ばない純粋な判断）。
// Shown → Exiting → Hidden → Restoring → Shown。各段は最低 2 フレーム見てから次へ進む。
#include <cstdint>

namespace gohan::ui {
enum class LeaseStage : std::uint8_t { Shown, Exiting, Hidden, Restoring };
enum class LeaseCommand : std::uint8_t {
    None,
    HideMap,     // 地図の命令バイトに退場
    HideOther,   // 他の画面の命令バイトに 0、タブの命令バイトに 7
    HideTab,     // タブだけ 7
    CloseMenu,   // 下画面のメニューを、地図とタブを出し直さずに閉じさせる
    Restore,     // 命令 2（出し直し）
};
struct LeaseProbe {
    std::uint32_t room = 0;
    std::uint32_t owners = 0;         // 持ち主のビットの OR。0 でなければ隠したい
    bool chat_over = false;           // チャットの上に出す: 元の UI は隠さない
    bool tab_settled = false;         // タブが「タブ選択」で落ち着いている
    bool commands_consumed = false;   // 命令バイトがどれも消費済み
    bool map_present = false, other_present = false;
    bool menu_open = false;           // 下画面のメニュー（持ち物など）が開いている
    bool menu_entered = false;        // 入場し終えた（MENU_FLAGS 0x08|0x10）
    bool hide_done = false;           // 隠れきった
    bool restore_done = false;        // 戻りきった
    bool game_restored = false;       // ゲーム自身がタブ・地図を戻した
    bool resources_returned = false;  // 持ち主の下画面の資源が全部返った
};
class LeaseMachine {
  public:
    static constexpr unsigned MinFrames = 2;
    LeaseCommand Step(const LeaseProbe &p) {
        if (!room_seen_ || p.room != room_) {
            const bool changed = room_seen_;
            room_ = p.room; room_seen_ = true;
            if (changed) { Enter(LeaseStage::Shown); closing_ = false; requested_ = false; hidden_ = false; restoring_ = false; return LeaseCommand::None; }
        }
        ++frames_;
        const bool want = p.owners != 0 && !p.chat_over;
        switch (stage_) {
        case LeaseStage::Shown:
            if (restoring_ && frames_ >= MinFrames) restored_ = true;
            if (!want) { return LeaseCommand::None; }
            if (p.menu_open) {
                if (closing_) return LeaseCommand::None;
                if (!p.menu_entered) return LeaseCommand::None;
                closing_ = true;
                return LeaseCommand::CloseMenu;
            }
            closing_ = false;
            if (!p.tab_settled || !p.commands_consumed) return LeaseCommand::None;
            requested_ = true; restoring_ = false; restored_ = false;
            Enter(LeaseStage::Exiting);
            return p.map_present ? LeaseCommand::HideMap
                 : p.other_present ? LeaseCommand::HideOther : LeaseCommand::HideTab;
        case LeaseStage::Exiting:
            if (p.game_restored) { Enter(LeaseStage::Shown); return LeaseCommand::None; }
            if (frames_ >= MinFrames && p.hide_done) { hidden_ = true; Enter(LeaseStage::Hidden); }
            return LeaseCommand::None;
        case LeaseStage::Hidden:
            if (p.game_restored) { hidden_ = false; Enter(LeaseStage::Shown); return LeaseCommand::None; }
            if (frames_ >= MinFrames && p.owners == 0 && p.resources_returned) {
                hidden_ = false; restoring_ = true; requested_ = false;
                Enter(LeaseStage::Restoring);
                return LeaseCommand::Restore;
            }
            return LeaseCommand::None;
        case LeaseStage::Restoring:
            if (frames_ >= MinFrames && p.restore_done) { Enter(LeaseStage::Shown); restored_ = true; }
            return LeaseCommand::None;
        }
        return LeaseCommand::None;
    }
    // Shutdown 用: 隠している最中・隠れたままなら出し直しを 1 回だけ出す。戻りきりは待たない。
    LeaseCommand Shutdown() {
        if (stage_ == LeaseStage::Exiting || stage_ == LeaseStage::Hidden) {
            hidden_ = false; restoring_ = true; requested_ = false;
            Enter(LeaseStage::Restoring);
            return LeaseCommand::Restore;
        }
        return LeaseCommand::None;
    }
    LeaseStage Stage() const { return stage_; }
    bool Requested() const { return requested_; }   // 隠す命令を出した
    bool Hidden() const { return hidden_; }         // 隠れきった
    bool Restoring() const { return restoring_; }   // 戻す命令を出した（戻っている）
    bool Restored() const { return restored_; }     // 戻りきった
  private:
    void Enter(LeaseStage s) { stage_ = s; frames_ = 0; }
    LeaseStage stage_ = LeaseStage::Shown;
    unsigned frames_ = 0;
    std::uint32_t room_ = 0;
    bool room_seen_ = false, closing_ = false, requested_ = false, hidden_ = false,
         restoring_ = false, restored_ = false;
};
} // namespace gohan::ui
