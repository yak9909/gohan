#include "game/lyt/recording_resources.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace gohan::game::lyt::detail {
namespace {
constexpr unsigned ListBinding = 519;
constexpr int AtlasAlignment = 128;
// リスト生成・領域確保とも、メモリ種別65536のコールバックを使う。
// 描画の初期化で登録されるコールバックは描画メモリ管理者へ渡り、
// この種別は独立して作られる描画命令用ヒープを選ぶ。画像用とは別。
constexpr unsigned ListManagerBytes = 68, QueueEntryBytes = 28;

class BindingGuard {
  public:
    explicit BindingGuard(const ResourceApi &api) : api_(api) {
        api_.property(ListBinding, &saved_);
    }
    ~BindingGuard() { api_.select(saved_); }

  private:
    const ResourceApi &api_;
    unsigned saved_ = 0;
};
} // namespace

bool RecordingResources::Acquire(const FontResource &font) {
    if (!Empty() || !font.atlas || !font.atlasBytes ||
        font.atlasBytes > std::numeric_limits<unsigned>::max() ||
        font.textureCount > static_cast<unsigned>(Texture::Count) ||
        (font.textureCount && !font.textures))
        return false;
    for (std::size_t i = 0; i < font.textureCount; ++i)
        if (!font.textures[i].data || !font.textures[i].bytes ||
            font.textures[i].bytes > std::numeric_limits<unsigned>::max())
            return false;
    backgroundStatus_ = {};
    memoryStatus_ = {};
    const auto required = font.textureCount;
    std::uint64_t total = font.atlasBytes;
    if (required) {
        total = (total + 127) / 128 * 128;
        for (std::size_t i = 0; i < required; ++i)
            if (font.textures[i].gpuResident) total += (std::uint64_t(font.textures[i].bytes) + 127) / 128 * 128;
    }
    if (total + core::limits::RendererGameAllocationSlack > core::limits::RendererResidentHeapLimit) {
        backgroundStatus_.result = BackgroundResult::BudgetExceeded;
        return false;
    }
    allocator_ = api_.allocator();
    if (!allocator_) {
        backgroundStatus_.result = BackgroundResult::HeapUnavailable;
        return false;
    }
    // 確保が失敗するとゲームは null を参照して止まるので、先に空きを確かめる。
    // 総量と最大の塊を検査し、確保失敗をゲームへ渡さない。
    HeapSpace space;
    const auto bytes = static_cast<unsigned>(total);
    const bool measured = api_.heapSpace(allocator_, AtlasAlignment, &space);
    memoryStatus_.before = {space.freeBytes, space.largestBlock, measured};
    backgroundStatus_ = {BackgroundResult::NotAttempted, space.freeBytes, space.largestBlock, measured};
    if (!measured ||
        space.largestBlock < bytes + AtlasAlignment ||
        space.freeBytes < bytes + core::limits::RendererGameAllocationSlack) {
        backgroundStatus_.result = !measured ? BackgroundResult::HeapUnavailable :
            space.largestBlock < bytes + AtlasAlignment ? BackgroundResult::BlockTooSmall : BackgroundResult::InsufficientSpace;
        allocator_ = nullptr;
        return false;
    }
    atlas_ = api_.allocate(allocator_, bytes, AtlasAlignment);
    if (!atlas_) {
        backgroundStatus_.result = BackgroundResult::AllocateFailed;
        allocator_ = nullptr;
        return false;
    }
    memoryStatus_.gameBytes = bytes;
    memoryStatus_.gameUpperBytes = bytes + core::limits::RendererGameAllocationSlack;
    std::memcpy(atlas_, font.atlas, font.atlasBytes);
    for (std::size_t i = 0; i < required; ++i)
        if (font.textures[i].gpuResident) std::memcpy(static_cast<unsigned char *>(atlas_) + TextureOffset(font, i),
                    font.textures[i].data, font.textures[i].bytes);
    if (!api_.flush(atlas_, bytes)) {
        ReleaseIdle();
        backgroundStatus_.result = BackgroundResult::FlushFailed;
        return false;
    }
    memoryStatus_.fontBytes = static_cast<unsigned>(font.atlasBytes);
    for (std::size_t i = 0; i < required; ++i) {
        if (!font.textures[i].gpuResident) continue;
        const auto bytes = static_cast<unsigned>(font.textures[i].bytes);
        if (i < static_cast<unsigned>(Texture::TitleAlpha)) memoryStatus_.uiBytes += bytes;
        else if (i == static_cast<unsigned>(Texture::TitleWindow)) memoryStatus_.windowBytes += bytes;
        else if (i >= static_cast<unsigned>(Texture::TitleSelection)) memoryStatus_.selectionBytes += bytes;
    }
    backgroundStatus_.result = BackgroundResult::Ready;
    bool created = true;
    {
        BindingGuard binding(api_);
        for (auto &list : lists_) {
            api_.create(1, &list);
            if (!list) {
                created = false;
                break;
            }
            api_.select(list);
            api_.storage(core::limits::RendererRecordBytes, core::limits::RendererQueueEntries);
            memoryStatus_.commandBytes += ListManagerBytes + core::limits::RendererRecordBytes + QueueEntryBytes * core::limits::RendererQueueEntries;
        }
    }
    if (!created) {
        ReleaseIdle(); // まだ公開していないので、GPU からの参照は無い。
        backgroundStatus_.result = BackgroundResult::AllocateFailed;
    }
    space = {};
    const bool afterMeasured = api_.heapSpace(allocator_ ? allocator_ : api_.allocator(), AtlasAlignment, &space);
    memoryStatus_.after = {space.freeBytes, space.largestBlock, afterMeasured};
    memoryStatus_.current = memoryStatus_.after;
    if (created && !afterMeasured) {
        ReleaseIdle();
        backgroundStatus_.result = BackgroundResult::HeapUnavailable;
        return false;
    }
    // API内で追加の確保が増えても、実際の空きの差で上限違反を検出する。
    if (created && measured && afterMeasured && memoryStatus_.before.freeBytes > space.freeBytes) {
        memoryStatus_.gameUpperBytes = std::max(memoryStatus_.gameUpperBytes, memoryStatus_.before.freeBytes - space.freeBytes);
        if (memoryStatus_.gameUpperBytes > core::limits::RendererResidentHeapLimit) {
            ReleaseIdle();
            backgroundStatus_.result = BackgroundResult::BudgetExceeded;
            return false;
        }
    }
    return created;
}

bool RecordingResources::ReleaseIdle() {
    if (Empty())
        return true;
    unsigned bound = 0;
    api_.property(ListBinding, &bound);
    if (bound && (bound == lists_[0] || bound == lists_[1]))
        return false;
    for (auto &list : lists_) {
        if (list) {
            api_.destroy(1, &list);
            list = 0;
        }
    }
    if (atlas_) {
        api_.free(allocator_, atlas_);
        atlas_ = nullptr;
    }
    allocator_ = nullptr;
    memoryStatus_.gameBytes = memoryStatus_.gameUpperBytes = 0;
    memoryStatus_.commandBytes = 0;
    memoryStatus_.fontBytes = memoryStatus_.uiBytes = memoryStatus_.windowBytes = memoryStatus_.selectionBytes = 0;
    return true;
}
} // namespace gohan::game::lyt::detail
