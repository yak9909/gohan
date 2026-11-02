#pragma once
#include "core/limits.hpp"
#include "core/sync.hpp"
#include <cstddef>
#include <string_view>

namespace gohan::core {
enum class ShutdownStage { GameHooks, DrawHooks, Other };
struct Feature {
    std::string_view id;
    bool (*Wire)();
    void (*Shutdown)();
    ShutdownStage stage;
};

class FeatureRegistry {
  public:
    // 登録表は WireAll より前に構成する。呼び先は登録表に再入しない。
    bool Add(const Feature &feature);
    bool WireAll();
    // 登録表のロックを、取れるまで sleep_ms を挟んで attempts 回まで試す（WireAll の最中の終了で、永久に待たないため）。
    // 取れなければ何もせず偽を返す。済んでいる・今回済ませた時は真。
    bool ShutdownAll(void (*sleep_ms)() = nullptr, unsigned attempts = 1);

  private:
    SpinMutex mutex_;
    Feature features_[limits::Features]{};
    bool wired_[limits::Features]{};
    std::size_t count_ = 0;
    bool started_ = false;
    bool stopped_ = false;
};
} // namespace gohan::core
