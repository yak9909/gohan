#pragma once
#include "core/sync.hpp"
#include "game/label_content.hpp"
#include "ui/bottom_screen_panel.hpp"
#include <array>
namespace gohan::ui {
using LabelLine = game::LabelLine;
class GameLabel {
  public:
    GameLabel() : panel_(content_, 2, true) {}
    // owner の宣言をまとめて置換する。全体で8箱まで。空の宣言でその持ち主を返す。
    bool Declare(unsigned owner, std::vector<LabelLine>);
    void Stop();
    bool Drained();
    bool Cleanup();
    void Step(const LeaseStatus &);
  private:
    core::SpinMutex mutex_;
    std::array<std::vector<LabelLine>, 8> owners_;
    game::LabelContent content_;
    BottomScreenPanel panel_;
    unsigned revision_ = 0, applied_ = 0;
    bool stopping_ = false;
};
}
