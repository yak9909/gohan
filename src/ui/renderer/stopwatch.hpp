#pragma once
// 帯の導入時の合成と毎フレームのコピーの時間を測る。実機はプラグインの時計、PC は steady_clock。
#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#else
#include <chrono>
#endif

namespace gohan::ui::renderer::detail {
class Stopwatch {
  public:
    unsigned ElapsedMicros() const {
#ifdef __3DS__
        return unsigned(clock_.GetElapsedTime().AsMicroseconds());
#else
        return unsigned(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_).count());
#endif
    }

  private:
#ifdef __3DS__
    CTRPluginFramework::Clock clock_;
#else
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
#endif
};
} // namespace gohan::ui::renderer::detail
