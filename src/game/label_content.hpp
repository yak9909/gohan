#pragma once
#include "game/panel_content.hpp"
#include "game/lyt/arc_layout.hpp"
#include <memory>
#include <string>
#include <vector>
namespace gohan::game {
struct LabelLine { std::string text; unsigned row = 0; };
class LabelContent final : public PanelContent {
  public:
    bool Set(std::vector<LabelLine>);
    PanelBuild Build() override;
    void Show(bool) override;
    void Update() override;
    bool Finished() const override;
    void Draw() override;
    void Detach() override;
    void Reset() override;
    bool Empty() const { return !boxes_; }
  private:
    struct Box {
        lyt::ArcLayout layout;
        lyt::InOut motion;
        float width = 0, bell_x = 0, bell_y = 0;
    };
    lyt::Arc arc_;
    std::unique_ptr<Box[]> boxes_;
    std::vector<LabelLine> lines_;
    unsigned built_ = 0;
};
}
