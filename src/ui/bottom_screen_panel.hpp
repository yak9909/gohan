#pragma once
#include "game/panel_content.hpp"
#include "ui/lease/bottom_screen_lease.hpp"
namespace gohan::ui {
struct PanelLease {
    bool (*acquire)(std::uint32_t, bool);
    bool (*release)(std::uint32_t);
    bool (*returned)(std::uint32_t);
};
class BottomScreenPanel {
  public:
    enum class Stage { Empty, Waiting, Building, Visible, Retiring };
    BottomScreenPanel(game::PanelContent &content, std::uint32_t owner, bool top = false,
                      PanelLease lease = {bottom_screen_lease::Acquire, bottom_screen_lease::Release,
                                          bottom_screen_lease::ResourcesReturned})
        : content_(content), lease_(lease), owner_(owner), top_(top) {}
    // 呼び手が要求の受け渡しを同期する。Step はゲームのフレームだけ。
    void Request(bool shown) { if (!stopping_) wanted_ = shown; }
    void Stop() { wanted_ = false; stopping_ = true; }
    void Step(const LeaseStatus &);
    bool Drained() const { return stage_ == Stage::Empty && !held_; }
    bool Visible() const { return stage_ == Stage::Visible; }
    Stage State() const { return stage_; }
  private:
    game::PanelContent &content_;
    PanelLease lease_;
    std::uint32_t owner_;
    Stage stage_ = Stage::Empty;
    unsigned idle_ = 0;
    bool top_, wanted_ = false, held_ = false, releasing_ = false, stopping_ = false;
};
}
