#pragma once
#include "core/jobs.hpp"
#include <cstdint>
#include <memory>

namespace gohan::core::files {
struct Buffer {
    std::unique_ptr<std::uint8_t[]> data;
    std::size_t size = 0;
    explicit operator bool() const { return data != nullptr; }
};
// 成功時は size の直後に 1 B のゼロを付ける。空ファイル・上限超過・読み取り失敗は空を返す。
Buffer ReadAll(const char *path, std::size_t limit);
bool CommonPath(char *out, std::size_t capacity, const char *name);
bool TitlePath(char *out, std::size_t capacity, std::uint64_t title, const char *name);
bool TitlePath(char *out, std::size_t capacity, const char *name);
class AsyncLoad {
  public:
    enum class State { Empty, Reading, Ready };
    using Loader = Buffer (*)(const char *, std::size_t);
    AsyncLoad() = default;
    ~AsyncLoad();
    AsyncLoad(const AsyncLoad &) = delete;
    AsyncLoad &operator=(const AsyncLoad &) = delete;
    // 呼び出しと照会はゲームのスレッドだけ。破棄しても、受理済みの I/O の文脈は作業が保持する。
    bool Start(const char *path, std::size_t limit, Loader loader = ReadAll, jobs::Queue &queue = jobs::Global());
    State GetState() const;
    bool Take(Buffer &out); // 失敗した読み込みも Ready になり、空を返して Empty へ戻る。
  private:
    struct Context;
    Context *context_ = nullptr;
    State state_ = State::Empty;
    Buffer result_;
};
} // namespace gohan::core::files
