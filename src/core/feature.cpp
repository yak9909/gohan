#include "core/feature.hpp"
#include <initializer_list>

namespace gohan::core {
bool FeatureRegistry::Add(const Feature &feature) {
    LockGuard lock(mutex_);
    if (started_ || stopped_ || feature.id.empty() || !feature.Wire || !feature.Shutdown ||
        count_ == limits::Features)
        return false;
    for (std::size_t i = 0; i < count_; ++i)
        if (features_[i].id == feature.id)
            return false;
    features_[count_++] = feature;
    return true;
}

bool FeatureRegistry::WireAll() {
    LockGuard lock(mutex_);
    if (stopped_)
        return false;
    bool success = true;
    started_ = true;
    for (std::size_t i = 0; i < count_; ++i) {
        if (!wired_[i])
            wired_[i] = features_[i].Wire();
        success = wired_[i] && success;
    }
    return success;
}

void FeatureRegistry::ShutdownAll() {
    LockGuard lock(mutex_);
    if (stopped_)
        return;
    stopped_ = true;
    // 同じ段は登録と逆の順。失敗した Wire は自分で原状に戻す契約。
    for (auto stage : {ShutdownStage::GameHooks, ShutdownStage::DrawHooks, ShutdownStage::Other}) {
        for (std::size_t i = count_; i > 0; --i) {
            if (wired_[i - 1] && features_[i - 1].stage == stage) {
                features_[i - 1].Shutdown();
                wired_[i - 1] = false;
            }
        }
    }
}
} // namespace gohan::core
