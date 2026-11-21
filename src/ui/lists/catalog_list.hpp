#pragma once
#include "core/sync.hpp"
#include "game/catalog_content.hpp"
#include "ui/bottom_screen_panel.hpp"
#include "ui/lists/list_model.hpp"
namespace gohan::ui::lists {
class CatalogList {
  public:
    CatalogList() : panel_(content_, 1) {}
    bool Show(std::vector<Row>, std::string message_label = {}, bool extra_text = true);
    void Close();
    void Stop();
    bool Drained();
    bool Cleanup();
    int TakeDecision();
    void Step(const LeaseStatus &, unsigned directions, unsigned now, bool touching, int x, int y, bool confirm);
  private:
    core::SpinMutex mutex_;
    ListModel model_;
    game::CatalogContent content_;
    BottomScreenPanel panel_;
    DpadRepeat repeat_;
    game::NativeTouch touch_;
    std::string label_;
    unsigned generation_ = 0;
    bool wanted_ = false, extras_ = true, stopping_ = false, confirm_ = false;
    int decision_ = -1;
};
}
