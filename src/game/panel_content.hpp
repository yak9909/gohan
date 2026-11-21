#pragma once
namespace gohan::game {
enum class PanelBuild { Pending, Ready, Failed };
// GPU の参照が無くなるまで Reset を遅らせるのは Panel の責任。
class PanelContent {
  public:
    virtual ~PanelContent() = default;
    virtual PanelBuild Build() = 0;
    virtual void Show(bool) = 0;
    virtual void Update() = 0;
    virtual bool Finished() const = 0;
    virtual void Draw() = 0;
    virtual void Detach() = 0;
    virtual void Reset() = 0;
};
}
