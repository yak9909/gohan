#pragma once
#include <cstdint>
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using Handle = u32;
using Result = std::int32_t;
enum FS_ArchiveID { ARCHIVE_ROMFS = 3 };
enum FS_PathType { PATH_EMPTY = 1, PATH_BINARY = 2 };
struct FS_Path { FS_PathType type; u32 size; const void *data; };
inline constexpr u32 FS_OPEN_READ = 1;
inline bool R_FAILED(Result result) { return result < 0; }
inline bool R_SUCCEEDED(Result result) { return result >= 0; }
Result FSUSER_OpenFileDirectly(Handle *, FS_ArchiveID, FS_Path, FS_Path, u32, u32);
Result FSFILE_GetSize(Handle, u64 *);
Result FSFILE_Read(Handle, u32 *, u64, void *, u32);
Result FSFILE_Close(Handle);
