#include "core/files.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#else
#include <fstream>
#endif

namespace gohan::core::files {
Buffer ReadAll(const char *path, std::size_t limit) {
    if (!path) return {};
#ifdef __3DS__
    CTRPluginFramework::File file;
    if (CTRPluginFramework::File::Open(file, path, CTRPluginFramework::File::READ)) return {};
    const auto size = file.GetSize();
#else
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto end = file.tellg();
    if (end < 0) return {};
    const auto size = static_cast<std::uint64_t>(end);
#endif
    Buffer result;
    if (size && size <= limit && size < std::numeric_limits<std::size_t>::max()) {
        result.data.reset(new (std::nothrow) std::uint8_t[static_cast<std::size_t>(size) + 1]);
        if (result.data) {
#ifdef __3DS__
            const bool read = file.Read(result.data.get(), static_cast<std::uint32_t>(size)) == 0;
#else
            file.seekg(0);
            const bool read = bool(file.read(reinterpret_cast<char *>(result.data.get()), static_cast<std::streamsize>(size)));
#endif
            if (read) { result.size = static_cast<std::size_t>(size); result.data[result.size] = 0; }
            else result = {};
        }
    }
#ifdef __3DS__
    file.Close();
#endif
    return result;
}
bool CommonPath(char *out, std::size_t capacity, const char *name) {
    if (!out || !capacity || !name) return false;
    const auto n = std::snprintf(out, capacity, "/luma/plugins/gohan/common/%s", name);
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}
bool TitlePath(char *out, std::size_t capacity, std::uint64_t title, const char *name) {
    if (!out || !capacity || !name) return false;
    const auto n = std::snprintf(out, capacity, "/luma/plugins/gohan/%016llX/%s", static_cast<unsigned long long>(title), name);
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}
bool TitlePath(char *out, std::size_t capacity, const char *name) {
#ifdef __3DS__
    return TitlePath(out, capacity, CTRPluginFramework::Process::GetTitleID(), name);
#else
    return TitlePath(out, capacity, 0, name);
#endif
}
struct AsyncLoad::Context {
    AsyncLoad *owner;
    char path[256];
    std::size_t limit;
    Loader loader;
    Buffer result;
    static void Work(void *p) { auto &c = *static_cast<Context *>(p); c.result = c.loader(c.path, c.limit); }
    static void Done(void *p) {
        auto &c = *static_cast<Context *>(p);
        if (c.owner) {
            c.owner->result_ = std::move(c.result);
            c.owner->state_ = State::Ready;
            c.owner->context_ = nullptr;
        }
        delete &c;
    }
};
AsyncLoad::~AsyncLoad() {
    // 作業は owner に触れない。完了と破棄はゲーム側なので、ここで切り離せる。
    if (context_) context_->owner = nullptr;
}
bool AsyncLoad::Start(const char *path, std::size_t limit, Loader loader, jobs::Queue &queue) {
    if (!path || !loader || std::strlen(path) >= sizeof(Context::path) || GetState() != State::Empty) return false;
    auto *c = new (std::nothrow) Context{this, {}, limit, loader, {}};
    if (!c) return false;
    std::strcpy(c->path, path);
    if (!queue.Submit({c, Context::Work, Context::Done, Context::Done})) { delete c; return false; }
    context_ = c;
    state_ = State::Reading;
    return true;
}
AsyncLoad::State AsyncLoad::GetState() const { return state_; }
bool AsyncLoad::Take(Buffer &out) {
    if (GetState() != State::Ready) return false;
    out = std::move(result_);
    state_ = State::Empty;
    return true;
}
} // namespace gohan::core::files
