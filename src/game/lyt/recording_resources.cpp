#include "game/lyt/recording_resources.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace gohan::game::lyt::detail {
namespace {
constexpr unsigned ListBinding = 519;
constexpr int AtlasAlignment = 128;
// 記録列は画像用アロケーターとは別の描画命令用コールバックで確保される。
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
    const auto required = std::min(font.textureCount, font.requiredTextureCount);
    std::uint64_t total = font.atlasBytes;
    if (required) {
        total = (total + 127) / 128 * 128;
        for (std::size_t i = 0; i < required; ++i)
            total += (std::uint64_t(font.textures[i].bytes) + 127) / 128 * 128;
    }
    if (total + core::limits::RendererGameAllocationSlack > core::limits::RendererGameHeapLimit)
        return false;
    allocator_ = api_.allocator();
    if (!allocator_)
        return false;
    // 確保が失敗するとゲームは null を参照して止まるので、先に空きを確かめる。
    // 導入後もゲームが大きな資源を取れるよう、空きを残す。
    HeapSpace space;
    const auto bytes = static_cast<unsigned>(total);
    const bool measured = api_.heapSpace(allocator_, AtlasAlignment, &space);
    memoryStatus_.before = {space.freeBytes, space.largestBlock, measured};
    if (!measured ||
        space.largestBlock < bytes + AtlasAlignment ||
        space.freeBytes < bytes + core::limits::RendererGameAllocationSlack + core::limits::RendererHeapReserve) {
        allocator_ = nullptr;
        return false;
    }
    atlas_ = api_.allocate(allocator_, bytes, AtlasAlignment);
    if (!atlas_) {
        allocator_ = nullptr;
        return false;
    }
    memoryStatus_.gameBytes = bytes;
    memoryStatus_.gameUpperBytes = bytes + core::limits::RendererGameAllocationSlack;
    std::memcpy(atlas_, font.atlas, font.atlasBytes);
    for (std::size_t i = 0; i < required; ++i)
        std::memcpy(static_cast<unsigned char *>(atlas_) + TextureOffset(font, i),
                    font.textures[i].data, font.textures[i].bytes);
    if (!api_.flush(atlas_, bytes)) {
        ReleaseIdle();
        return false;
    }
    // 必須画像が確保できた後の空きを改めて測る。背景を取れなくても導入を続ける。
    backgroundStatus_.result = BackgroundResult::NotNeeded;
    if (required < font.textureCount) {
        std::uint64_t backgroundBytes = 0;
        for (std::size_t i = required; i < font.textureCount; ++i)
            backgroundBytes += (std::uint64_t(font.textures[i].bytes) + 127) / 128 * 128;
        space = {};
        backgroundStatus_.measured = api_.heapSpace(allocator_, AtlasAlignment, &space);
        backgroundStatus_.freeBytes = space.freeBytes;
        backgroundStatus_.largestBlock = space.largestBlock;
        if (!backgroundStatus_.measured) {
            backgroundStatus_.result = BackgroundResult::HeapUnavailable;
        } else if (total + backgroundBytes + 2 * core::limits::RendererGameAllocationSlack > core::limits::RendererGameHeapLimit ||
                   space.largestBlock < backgroundBytes + AtlasAlignment ||
                   space.freeBytes < backgroundBytes + core::limits::RendererGameAllocationSlack + core::limits::RendererHeapReserve) {
            backgroundStatus_.result = BackgroundResult::InsufficientSpace;
        } else {
            background_ = api_.allocate(allocator_, static_cast<unsigned>(backgroundBytes), AtlasAlignment);
            backgroundStatus_.result = background_ ? BackgroundResult::Ready : BackgroundResult::AllocateFailed;
            if (background_) {
                memoryStatus_.gameBytes += static_cast<unsigned>(backgroundBytes);
                memoryStatus_.gameUpperBytes += static_cast<unsigned>(backgroundBytes) + core::limits::RendererGameAllocationSlack;
                for (std::size_t i = required; i < font.textureCount; ++i)
                    std::memcpy(static_cast<unsigned char *>(background_) + TextureOffset(font, i),
                                font.textures[i].data, font.textures[i].bytes);
                if (!api_.flush(background_, static_cast<unsigned>(backgroundBytes))) {
                    api_.free(allocator_, background_);
                    background_ = nullptr;
                    memoryStatus_.gameBytes -= static_cast<unsigned>(backgroundBytes);
                    memoryStatus_.gameUpperBytes -= static_cast<unsigned>(backgroundBytes) + core::limits::RendererGameAllocationSlack;
                    backgroundStatus_.result = BackgroundResult::FlushFailed;
                }
            }
        }
    }
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
    if (!created)
        ReleaseIdle(); // まだ公開していないので、GPU からの参照は無い。
    space = {};
    const bool afterMeasured = api_.heapSpace(allocator_ ? allocator_ : api_.allocator(), AtlasAlignment, &space);
    memoryStatus_.after = {space.freeBytes, space.largestBlock, afterMeasured};
    memoryStatus_.current = memoryStatus_.after;
    // API内で追加の確保が増えても、実際の空きの差で上限違反を検出する。
    if (created && measured && afterMeasured && memoryStatus_.before.freeBytes > space.freeBytes) {
        memoryStatus_.gameUpperBytes = std::max(memoryStatus_.gameUpperBytes, memoryStatus_.before.freeBytes - space.freeBytes);
        if (memoryStatus_.gameUpperBytes > core::limits::RendererGameHeapLimit ||
            space.freeBytes < core::limits::RendererHeapReserve) {
            ReleaseIdle();
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
    if (background_) {
        api_.free(allocator_, background_);
        background_ = nullptr;
    }
    if (atlas_) {
        api_.free(allocator_, atlas_);
        atlas_ = nullptr;
    }
    allocator_ = nullptr;
    memoryStatus_.gameBytes = memoryStatus_.gameUpperBytes = 0;
    memoryStatus_.commandBytes = 0;
    return true;
}
} // namespace gohan::game::lyt::detail
