#pragma once
#include "game/generated/api.hpp"
#include "game/lyt/renderer.hpp"

namespace gohan::game::lyt::detail {
struct CodeCheck {
    std::uintptr_t address;
    std::uint32_t bytes;
    std::uint32_t crc32;
};

// 資源を作る API も、登録・記録・再生と同じく導入前に照合する。
inline constexpr CodeCheck RendererChecks[] = {
    {generated::PictureDrawEntry_addr, generated::PictureDrawEntry_len, generated::PictureDrawEntry_crc32},
    {generated::FlushBatchEntry_addr, generated::FlushBatchEntry_len, generated::FlushBatchEntry_crc32},
    {generated::LayoutTopEntry_addr, generated::LayoutTopEntry_len, generated::LayoutTopEntry_crc32},
    {generated::LayoutBottomEntry_addr, generated::LayoutBottomEntry_len, generated::LayoutBottomEntry_crc32},
    {generated::GpuPropertyEntry_addr, generated::GpuPropertyEntry_len, generated::GpuPropertyEntry_crc32},
    {generated::GpuCreateEntry_addr, generated::GpuCreateEntry_len, generated::GpuCreateEntry_crc32},
    {generated::GpuSelectEntry_addr, generated::GpuSelectEntry_len, generated::GpuSelectEntry_crc32},
    {generated::GpuStorageEntry_addr, generated::GpuStorageEntry_len, generated::GpuStorageEntry_crc32},
    {generated::GpuDestroyEntry_addr, generated::GpuDestroyEntry_len, generated::GpuDestroyEntry_crc32},
    {generated::LayoutAllocateEntry_addr, generated::LayoutAllocateEntry_len, generated::LayoutAllocateEntry_crc32},
    {generated::LayoutFreeEntry_addr, generated::LayoutFreeEntry_len, generated::LayoutFreeEntry_crc32},
    {generated::LayoutRecordEntry_addr, generated::LayoutRecordEntry_len, generated::LayoutRecordEntry_crc32},
    {generated::LayoutReplayEntry_addr, generated::LayoutReplayEntry_len, generated::LayoutReplayEntry_crc32},
    {generated::LayoutAddEntry_addr, generated::LayoutAddEntry_len, generated::LayoutAddEntry_crc32},
};
InstallResult VerifyRenderer();
} // namespace gohan::game::lyt::detail
