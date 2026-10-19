#pragma once
#include "game/generated/api.hpp"
#include "game/lyt/drawing.hpp"

namespace gohan::game::lyt::detail {
// ゲーム API の境界。PC 試験でも生成されたものと同じ関数型を使う。
struct ResourceApi {
    generated::SsysMaAllocator *(*allocator)();
    generated::ssys_ma_Allocator_Alloc_t allocate;
    generated::ssys_ma_Allocator_Free_t free;
    generated::GpuCmd_GetProperty_t property;
    generated::GpuCmd_CreateLists_t create;
    generated::GpuCmd_SelectList_t select;
    generated::GpuCmd_AllocateListStorage_t storage;
    generated::GpuCmd_DestroyLists_t destroy;
    bool (*flush)(void *, std::size_t);
};
// VerifyRenderer が成功し、必要な欄の公開を確認した後にだけ使用する。
const ResourceApi &GameResourceApi();

// 描画スレッドだけで所有する。GPU の使用終了を確認する前に自動解放しない。
// ノード・Pane 木の公開が揃った後、この所有者を導入トランザクションに組み込む。
class RecordingResources {
  public:
    explicit RecordingResources(const ResourceApi &api) : api_(api) {}
    RecordingResources(const RecordingResources &) = delete;
    RecordingResources &operator=(const RecordingResources &) = delete;
    bool Acquire(const FontResource &font);
    // 呼び出し元がノードを外し、記録列と GPU の参照を排出してから呼ぶ。
    // 所有するリストが束縛中なら、資源と ID を保ったまま false を返す。
    bool ReleaseIdle();
    const void *Atlas() const { return atlas_; }
    unsigned List(Screen screen) const { return lists_[static_cast<unsigned>(screen)]; }
    bool Empty() const { return !atlas_ && !lists_[0] && !lists_[1]; }

  private:
    const ResourceApi &api_;
    generated::SsysMaAllocator *allocator_ = nullptr;
    void *atlas_ = nullptr;
    unsigned lists_[2]{};
};
} // namespace gohan::game::lyt::detail
