#include "game/title_background.hpp"
#include "ui_values.hpp"
#ifdef __3DS__
#include <3ds.h>
#else
#include <cstdlib>
#include <filesystem>
#include <fstream>
#endif

namespace gohan::game::titlebg {
namespace {
#ifdef __3DS__
struct Slice { const Reader &reader; Region region; };
bool ReadSlice(void *context, std::uint64_t offset, void *out, std::size_t bytes) {
    const auto &s = *static_cast<Slice *>(context);
    return s.reader.Read(s.region.offset + offset, out, bytes);
}
struct File { Handle handle; Attempt &attempt; };
bool ReadFile(void *context, std::uint64_t offset, void *out, std::size_t bytes) {
    auto &file = *static_cast<File *>(context);
    u32 got = 0;
    const Result result = FSFILE_Read(file.handle, &got, offset, out, bytes);
    if (R_FAILED(result)) file.attempt.result = static_cast<std::uint32_t>(result);
    return R_SUCCEEDED(result) && got == bytes;
}
bool Try(unsigned type, Sources &out, Attempt &attempt) {
    attempt.attempted = true;
    const u32 data[3] = {type, 0, 0};
    const FS_Path archivePath{PATH_EMPTY, 1, ""};
    const FS_Path filePath{PATH_BINARY, sizeof(data), data};
    Handle handle = 0;
    Result result = FSUSER_OpenFileDirectly(&handle, ARCHIVE_ROMFS, archivePath, filePath, FS_OPEN_READ, 0);
    if (R_FAILED(result)) {
        attempt.result = static_cast<std::uint32_t>(result);
        attempt.failure = Failure::Open;
        return false;
    }
    u64 size = 0;
    result = FSFILE_GetSize(handle, &size);
    bool ready = false;
    if (R_FAILED(result)) {
        attempt.result = static_cast<std::uint32_t>(result);
        attempt.failure = Failure::Read;
    } else {
        File file{handle, attempt};
        Reader reader{&file, size, ReadFile};
        Region region;
        attempt.failure = FindArchive(reader, region);
        if (attempt.failure == Failure::None) {
            Slice slice{reader, region};
            attempt.failure = DecodeArchive({&slice, region.size, ReadSlice}, out);
        }
        ready = attempt.failure == Failure::None;
    }
    FSFILE_Close(handle);
    return ready;
}
#else
bool ReadFile(void *context, std::uint64_t offset, void *out, std::size_t bytes) {
    auto &file = *static_cast<std::ifstream *>(context);
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(static_cast<char *>(out), static_cast<std::streamsize>(bytes));
    return file.good();
}
bool Try(unsigned type, Sources &out, Attempt &attempt) {
#ifdef _WIN32
    const auto *root = _wgetenv(type == 5 ? L"GOHAN_PREVIEW_ROMFS_UPDATE" : L"GOHAN_PREVIEW_ROMFS_BASE");
#else
    const char *root = std::getenv(type == 5 ? "GOHAN_PREVIEW_ROMFS_UPDATE" : "GOHAN_PREVIEW_ROMFS_BASE");
#endif
    if (!root || !*root) return false;
    attempt.attempted = true;
    const auto path = std::filesystem::path(root) / core::design::ca2::Titlebg::Source::Archive;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = file.tellg();
    if (size < 0) { attempt.failure = Failure::Read; return false; }
    Reader reader{&file, static_cast<std::uint64_t>(size), ReadFile};
    attempt.failure = DecodeArchive(reader, out);
    return attempt.failure == Failure::None;
}
#endif
} // namespace
Status LoadSelf(Sources &out) {
    Status status;
    if (Try(5, out, status.update)) {
        status.ready = status.fromUpdate = true;
    } else {
        status.ready = Try(0, out, status.base);
    }
    return status;
}
} // namespace gohan::game::titlebg
