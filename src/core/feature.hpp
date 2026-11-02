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
    void ShutdownAll();

  private:
    SpinMutex mutex_;
    Feature features_[limits::Features]{};
    bool wired_[limits::Features]{};
    std::size_t count_ = 0;
    bool started_ = false;
    bool stopped_ = false;
};
} // namespace gohan::core
