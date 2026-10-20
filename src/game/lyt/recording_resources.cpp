#include "game/lyt/recording_resources.hpp"
#include <cstring>
#include <limits>

namespace gohan::game::lyt::detail {
namespace {
constexpr unsigned ListBinding = 519;
constexpr int AtlasAlignment = 128;

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
    std::uint64_t total = font.atlasBytes;
    if (font.textureCount) {
        total = (total + 127) / 128 * 128;
        for (std::size_t i = 0; i < font.textureCount; ++i)
            total += (std::uint64_t(font.textures[i].bytes) + 127) / 128 * 128;
    }
    if (total > std::numeric_limits<unsigned>::max() - AtlasAlignment - core::limits::RendererHeapReserve)
        return false;
    allocator_ = api_.allocator();
    if (!allocator_)
        return false;
    // 確保が失敗するとゲームは null を参照して止まるので、先に空きを確かめる。
    // 導入後もゲームが大きな資源を取れるよう、空きを残す。
    HeapSpace space;
    const auto bytes = static_cast<unsigned>(total);
    if (!api_.heapSpace(allocator_, AtlasAlignment, &space) ||
        space.largestBlock < bytes + AtlasAlignment ||
        space.freeBytes < bytes + AtlasAlignment + core::limits::RendererHeapReserve) {
        allocator_ = nullptr;
        return false;
    }
    atlas_ = api_.allocate(allocator_, bytes, AtlasAlignment);
    if (!atlas_) {
        allocator_ = nullptr;
        return false;
    }
    std::memcpy(atlas_, font.atlas, font.atlasBytes);
    for (std::size_t i = 0; i < font.textureCount; ++i)
        std::memcpy(static_cast<unsigned char *>(atlas_) + TextureOffset(font, i),
                    font.textures[i].data, font.textures[i].bytes);
    if (!api_.flush(atlas_, bytes)) {
        ReleaseIdle();
        return false;
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
        }
    }
    if (!created)
        ReleaseIdle(); // まだ公開していないので、GPU からの参照は無い。
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
    return true;
}
} // namespace gohan::game::lyt::detail
