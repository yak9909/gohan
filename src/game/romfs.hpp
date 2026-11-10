#pragma once
#include "core/files.hpp"
#include <cstdint>

namespace gohan::game::romfs {
struct Reader {
    void *context;
    std::uint64_t size;
    bool (*read)(void *, std::uint64_t, void *, std::size_t);
    bool Read(std::uint64_t offset, void *out, std::size_t bytes) const;
};
struct Region { std::uint64_t offset = 0, size = 0; };
// 消費側が返す形式の失敗も、更新→基本の再試行の結果に残す。
enum class Failure { None, Open, Read, Romfs, Missing, Darc, Clim };
struct Attempt { Failure failure = Failure::Open; std::uint32_t result = 0; bool attempted = false; };
struct Status { Attempt update{}, base{}; bool ready = false, fromUpdate = false; };
bool Fits(std::uint64_t offset, std::uint64_t size, std::uint64_t capacity);
class Index {
  public:
    Failure Open(Reader reader); // 見出しはここで一度だけ読む。
    Failure Find(const char *path, Region &out) const;
  private:
    Reader reader_{};
    std::uint64_t level_ = 0;
    std::uint32_t header_[10]{};
    bool ready_ = false;
};
using Consumer = Failure (*)(const Reader &, void *);
// ハンドルと見出しを保持。開く→大きさ→同じ Reader で読む。更新が無い／読めない時は基本へ。
class Session {
  public:
    Session();
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Status Visit(const char *path, Consumer consumer, void *context);
    void Close();
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
core::files::Buffer ReadAll(const char *path, std::size_t limit);
} // namespace gohan::game::romfs
