#include "game/romfs.hpp"
#include <limits>
#include <new>
#ifdef __3DS__
#include <3ds.h>
#else
#include <cstdlib>
#include <filesystem>
#include <fstream>
#endif

namespace gohan::game::romfs {
struct Session::Impl {
    struct Source {
#ifdef __3DS__
        Handle handle = 0;
        Index index;
        std::uint64_t size = 0;
        std::uint32_t result = 0;
        static bool Read(void *p, std::uint64_t at, void *out, std::size_t size) {
            auto &s = *static_cast<Source *>(p);
            if (size > std::numeric_limits<u32>::max()) return false;
            u32 got = 0;
            const auto r = FSFILE_Read(s.handle, &got, at, out, static_cast<u32>(size));
            if (R_FAILED(r)) s.result = static_cast<std::uint32_t>(r);
            return R_SUCCEEDED(r) && got == size;
        }
        Failure Open(unsigned type, Attempt &attempt) {
            attempt.attempted = true;
            if (handle) return Failure::None;
            const u32 data[3] = {type, 0, 0};
            const FS_Path archive{PATH_EMPTY, 1, ""}, file{PATH_BINARY, sizeof(data), data};
            auto r = FSUSER_OpenFileDirectly(&handle, ARCHIVE_ROMFS, archive, file, FS_OPEN_READ, 0);
            if (R_FAILED(r)) { handle = 0; attempt.result = static_cast<std::uint32_t>(r); return Failure::Open; }
            r = FSFILE_GetSize(handle, &size);
            Failure failure;
            if (R_FAILED(r)) { result = static_cast<std::uint32_t>(r); failure = Failure::Read; }
            else failure = index.Open({this, size, Read});
            if (failure != Failure::None) { attempt.result = result; Close(); }
            return failure;
        }
        void Close() { if (handle) FSFILE_Close(handle); handle = 0; }
#else
        std::ifstream file;
        std::uint64_t size = 0;
        std::uint32_t result = 0;
        static bool Read(void *p, std::uint64_t at, void *out, std::size_t size) {
            auto &s = *static_cast<Source *>(p);
            s.file.clear();
            s.file.seekg(static_cast<std::streamoff>(at));
            return bool(s.file.read(static_cast<char *>(out), static_cast<std::streamsize>(size)));
        }
        Failure Open(unsigned type, const char *path, Attempt &attempt) {
#ifdef _WIN32
            const auto *root = _wgetenv(type == 5 ? L"GOHAN_PREVIEW_ROMFS_UPDATE" : L"GOHAN_PREVIEW_ROMFS_BASE");
#else
            const auto *root = std::getenv(type == 5 ? "GOHAN_PREVIEW_ROMFS_UPDATE" : "GOHAN_PREVIEW_ROMFS_BASE");
#endif
            if (!root || !*root) return Failure::Open;
            attempt.attempted = true;
            const auto relative = std::filesystem::path(path);
            if (relative.is_absolute()) return Failure::Missing;
            for (auto &part : relative) if (part == "..") return Failure::Missing;
            file.close(); file.clear();
            file.open(std::filesystem::path(root) / relative, std::ios::binary | std::ios::ate);
            if (!file) return Failure::Open;
            const auto end = file.tellg();
            if (end < 0) return Failure::Read;
            size = static_cast<std::uint64_t>(end);
            return Failure::None;
        }
        void Close() { file.close(); }
#endif
    } update, base;
    bool Try(Source &source, unsigned type, const char *path, Consumer consumer, void *context, Attempt &attempt) {
#ifdef __3DS__
        attempt.failure = source.Open(type, attempt);
        if (attempt.failure != Failure::None) return false;
        Region region;
        attempt.failure = source.index.Find(path, region);
        if (attempt.failure != Failure::None) { attempt.result = source.result; return false; }
        struct Slice { Source &source; Region region; } slice{source, region};
        const Reader reader{&slice, region.size, [](void *p, std::uint64_t at, void *out, std::size_t size) {
            const auto &s = *static_cast<Slice *>(p);
            return Source::Read(&s.source, s.region.offset + at, out, size);
        }};
#else
        attempt.failure = source.Open(type, path, attempt);
        if (attempt.failure != Failure::None) return false;
        const Reader reader{&source, source.size, Source::Read};
#endif
        attempt.failure = consumer(reader, context);
        attempt.result = source.result;
#ifndef __3DS__
        // PC は展開済みの個別ファイル。呼び出しの外へ開いたまま持ち出さない。
        source.Close();
#endif
        return attempt.failure == Failure::None;
    }
};
Session::Session() : impl_(new (std::nothrow) Impl) {}
Session::~Session() { Close(); }
void Session::Close() { if (impl_) { impl_->update.Close(); impl_->base.Close(); } }
Status Session::Visit(const char *path, Consumer consumer, void *context) {
    Status status;
    if (!impl_ || !path || !consumer) return status;
    if (impl_->Try(impl_->update, 5, path, consumer, context, status.update)) status.ready = status.fromUpdate = true;
    else status.ready = impl_->Try(impl_->base, 0, path, consumer, context, status.base);
    return status;
}
core::files::Buffer ReadAll(const char *path, std::size_t limit) {
    // 共通の作業のスレッドだけが使う。起動時の背景は独立の Session を持つ。
    static Session session;
    struct Request { core::files::Buffer buffer; std::size_t limit; } request{{}, limit};
    session.Visit(path, [](const Reader &r, void *p) {
        auto &request = *static_cast<Request *>(p);
        if (!r.size || r.size > request.limit || r.size >= std::numeric_limits<std::size_t>::max()) return Failure::Read;
        core::files::Buffer staged;
        staged.data.reset(new (std::nothrow) std::uint8_t[static_cast<std::size_t>(r.size) + 1]);
        if (!staged.data || !r.Read(0, staged.data.get(), static_cast<std::size_t>(r.size))) return Failure::Read;
        staged.size = static_cast<std::size_t>(r.size);
        staged.data[staged.size] = 0;
        request.buffer = std::move(staged);
        return Failure::None;
    }, &request);
    return std::move(request.buffer);
}
} // namespace gohan::game::romfs
