#pragma once
#include "game/panel_content.hpp"
#include "game/lyt/arc_layout.hpp"
#include "game/native_input.hpp"
#include "game/vtable_override.hpp"
#include <string>
#include <vector>
namespace gohan::game {
struct CatalogRow {
    std::string text, prefix, extra;
    unsigned message = 0;
};
class CatalogContent final : public PanelContent {
  public:
    bool Set(std::vector<CatalogRow>, std::string label, bool extras);
    PanelBuild Build() override;
    void Show(bool) override;
    void Update() override;
    bool Finished() const override;
    void Draw() override;
    void Detach() override;
    void Reset() override;
    void Input(const NativeTouch &t) { touch_ = t; }
    void Select(int);
    int Selected() const;
    int VisibleRows() const;
    int TakeDecision();
    bool Empty() const { return !list_ && !frame_.Ready(); }
    static bool Wire();
    static void Shutdown();
  private:
    static int BuildItems(generated::InstSelect8 *);
    static void *WordAt(generated::InstSelect8 *, int);
    static void SetSelect(generated::InstSelect8 *, int, int);
    static void WaitCalc(generated::InstSelect8 *);
    static void Drag(void *);
    void ExtraRows();
    lyt::Arc arc_;
    lyt::ArcLayout frame_;
    lyt::InOut motion_;
    generated::UiAnim kind_{};
    generated::InstSelect8 *list_ = nullptr;
    VtableOverride<26, 4> table_;
    struct Word {
        generated::ScriptWordFix38 fixed{};
        std::u16string text, extra;
        bool use_fixed = false;
    };
    std::vector<CatalogRow> rows_;
    std::vector<Word> words_;
    std::vector<std::uint8_t> layout_data_;
    std::string label_;
    NativeTouch touch_;
    struct Extra {
        generated::NwLytTextBox *name = nullptr, *box = nullptr;
        float x = 0, y = 0, name_x = 0, name_y = 0;
    } extra_[8];
    bool extras_ = false, setup_ = false, kind_made_ = false, shown_ = false, started_ = false;
    int decision_ = -1;
};
}
