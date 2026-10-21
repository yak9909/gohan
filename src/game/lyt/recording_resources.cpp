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
    const auto required = std::min(font.textureCount, font.requiredTextureCount);
    std::uint64_t total = font.atlasBytes;
    if (required) {
        total = (total + 127) / 128 * 128;
        for (std::size_t i = 0; i < required; ++i)
            if (font.textures[i].gpuResident) total += (std::uint64_t(font.textures[i].bytes) + 127) / 128 * 128;
    }
    if (total + core::limits::RendererGameAllocationSlack > core::limits::RendererResidentHeapLimit)
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
        if (font.textures[i].gpuResident) std::memcpy(static_cast<unsigned char *>(atlas_) + TextureOffset(font, i),
                    font.textures[i].data, font.textures[i].bytes);
    if (!api_.flush(atlas_, bytes)) {
        ReleaseIdle();
        return false;
    }
    memoryStatus_.residentBytes = memoryStatus_.gameBytes;
    memoryStatus_.residentUpperBytes = memoryStatus_.gameUpperBytes;
    // 背景はメニューが描画を始める時まで借りない。
    backgroundStatus_.result = required < font.textureCount ? BackgroundResult::NotAttempted : BackgroundResult::NotNeeded;
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
    if (created && !afterMeasured) {
        ReleaseIdle();
        return false;
    }
    // API内で追加の確保が増えても、実際の空きの差で上限違反を検出する。
    if (created && measured && afterMeasured && memoryStatus_.before.freeBytes > space.freeBytes) {
        memoryStatus_.gameUpperBytes = std::max(memoryStatus_.gameUpperBytes, memoryStatus_.before.freeBytes - space.freeBytes);
        if (memoryStatus_.gameUpperBytes > core::limits::RendererResidentHeapLimit ||
            space.freeBytes < core::limits::RendererHeapReserve) {
            ReleaseIdle();
            return false;
        }
    }
    memoryStatus_.residentUpperBytes = memoryStatus_.gameUpperBytes;
    return created;
}

bool RecordingResources::AcquireBackground(const FontResource &font) {
    if (!atlas_ || background_) return background_ != nullptr;
    unsigned bound = 0;
    api_.property(ListBinding, &bound);
    if (bound && (bound == lists_[0] || bound == lists_[1])) return false;
    const auto required = std::min(font.textureCount, font.requiredTextureCount);
    backgroundStatus_ = {};
    if (required == font.textureCount) {
        backgroundStatus_.result = BackgroundResult::NotNeeded;
        return false;
    }
    std::uint64_t bytes = 0;
    for (std::size_t i = required; i < font.textureCount; ++i)
        if (font.textures[i].gpuResident) bytes += (std::uint64_t(font.textures[i].bytes) + 127) / 128 * 128;
    if (!bytes) {
        backgroundStatus_.result = BackgroundResult::NotNeeded;
        return false;
    }
    HeapSpace space;
    backgroundStatus_.measured = api_.heapSpace(allocator_, AtlasAlignment, &space);
    backgroundStatus_.freeBytes = space.freeBytes;
    backgroundStatus_.largestBlock = space.largestBlock;
    if (!backgroundStatus_.measured) {
        backgroundStatus_.result = BackgroundResult::HeapUnavailable;
        return false;
    }
    // 管理・整列の余裕まで含めて予備を残す。引き算のアンダーフローも避ける。
    if (memoryStatus_.residentUpperBytes + bytes + core::limits::RendererGameAllocationSlack > core::limits::RendererGameHeapLimit) {
        backgroundStatus_.result = BackgroundResult::BudgetExceeded;
        return false;
    }
    if (space.largestBlock < bytes + AtlasAlignment) {
        backgroundStatus_.result = BackgroundResult::BlockTooSmall;
        return false;
    }
    if (space.freeBytes < bytes + core::limits::RendererGameAllocationSlack + core::limits::RendererHeapReserve) {
        backgroundStatus_.result = BackgroundResult::InsufficientSpace;
        return false;
    }
    background_ = api_.allocate(allocator_, static_cast<unsigned>(bytes), AtlasAlignment);
    if (!background_) {
        backgroundStatus_.result = BackgroundResult::AllocateFailed;
        return false;
    }
    memoryStatus_.openBytes = static_cast<unsigned>(bytes);
    memoryStatus_.openUpperBytes = static_cast<unsigned>(bytes) + core::limits::RendererGameAllocationSlack;
    for (std::size_t i = required; i < font.textureCount; ++i)
        if (font.textures[i].gpuResident) std::memcpy(static_cast<unsigned char *>(background_) + TextureOffset(font, i), font.textures[i].data, font.textures[i].bytes);
    if (!api_.flush(background_, static_cast<unsigned>(bytes))) {
        ReleaseBackgroundIdle(); // まだ木に公開していない。
        backgroundStatus_.result = BackgroundResult::FlushFailed;
        return false;
    }
    HeapSpace after;
    const bool measured = api_.heapSpace(allocator_, AtlasAlignment, &after);
    memoryStatus_.current = {after.freeBytes, after.largestBlock, measured};
    if (measured && space.freeBytes > after.freeBytes)
        memoryStatus_.openUpperBytes = std::max(memoryStatus_.openUpperBytes, space.freeBytes - after.freeBytes);
    const bool overBudget = memoryStatus_.residentUpperBytes + memoryStatus_.openUpperBytes > core::limits::RendererGameHeapLimit;
    if (!measured || overBudget ||
        after.freeBytes < core::limits::RendererHeapReserve) {
        ReleaseBackgroundIdle();
        backgroundStatus_.result = !measured ? BackgroundResult::HeapUnavailable :
            overBudget ? BackgroundResult::BudgetExceeded : BackgroundResult::InsufficientSpace;
        return false;
    }
    memoryStatus_.gameBytes = memoryStatus_.residentBytes + memoryStatus_.openBytes;
    memoryStatus_.gameUpperBytes = memoryStatus_.residentUpperBytes + memoryStatus_.openUpperBytes;
    backgroundStatus_.result = BackgroundResult::Ready;
    return true;
}

bool RecordingResources::ReleaseBackgroundIdle() {
    unsigned bound = 0;
    api_.property(ListBinding, &bound);
    if (bound && (bound == lists_[0] || bound == lists_[1])) return false;
    if (background_) {
        api_.free(allocator_, background_);
        background_ = nullptr;
        backgroundStatus_.result = BackgroundResult::Released;
    }
    memoryStatus_.openBytes = memoryStatus_.openUpperBytes = 0;
    memoryStatus_.gameBytes = memoryStatus_.residentBytes;
    memoryStatus_.gameUpperBytes = memoryStatus_.residentUpperBytes;
    return true;
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
    ReleaseBackgroundIdle();
    if (atlas_) {
        api_.free(allocator_, atlas_);
        atlas_ = nullptr;
    }
    allocator_ = nullptr;
    memoryStatus_.gameBytes = memoryStatus_.gameUpperBytes = 0;
    memoryStatus_.commandBytes = 0;
    memoryStatus_.residentBytes = memoryStatus_.residentUpperBytes = 0;
    return true;
}
} // namespace gohan::game::lyt::detail
