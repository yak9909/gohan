#pragma once
#include "game/lyt/drawing.hpp"
#include "game/lyt/resource_api.hpp"

namespace gohan::game::lyt::detail {
// 描画スレッドだけで所有する。GPU の使用終了を確認する前に自動解放しない。
// 上・下それぞれの記録用コマンドリストと、GPU が読む字形のアトラス（ゲームのヒープから借りる）。
class RecordingResources {
  public:
    explicit RecordingResources(const ResourceApi &api) : api_(api) {}
    RecordingResources(const RecordingResources &) = delete;
    RecordingResources &operator=(const RecordingResources &) = delete;
    // 字形・UI・窓・帯の全画像と記録列を確保する。失敗したら何も残さない。
    bool Acquire(const FontResource &font);
    // 呼び出し元がノードを外し、記録列と GPU の参照を排出してから呼ぶ。
    // 所有するリストが束縛中なら、資源と ID を保ったまま false を返す。
    bool ReleaseIdle();
    void *Atlas() const { return atlas_; }
    const BackgroundAllocation &BackgroundStatus() const { return backgroundStatus_; }
    RendererMemory MemoryStatus() const {
        auto result = memoryStatus_;
        result.background = backgroundStatus_;
        return result;
    }
    unsigned List(Screen screen) const { return lists_[static_cast<unsigned>(screen)]; }
    bool Empty() const { return !atlas_ && !lists_[0] && !lists_[1]; }

  private:
    const ResourceApi &api_;
    generated::SsysMaAllocator *allocator_ = nullptr;
    void *atlas_ = nullptr;
    BackgroundAllocation backgroundStatus_{};
    RendererMemory memoryStatus_{};
    unsigned lists_[2]{};
};
} // namespace gohan::game::lyt::detail
