#include "ui/game_label.hpp"
namespace gohan::ui {
bool GameLabel::Declare(unsigned owner, std::vector<LabelLine> lines) {
    core::LockGuard lock(mutex_);
    if (stopping_ || owner >= owners_.size()) return false;
    unsigned count = lines.size();
    for (unsigned i = 0; i < owners_.size(); ++i) if (i != owner) count += owners_[i].size();
    if (count > 8) return false;
    owners_[owner] = std::move(lines); ++revision_; return true;
}
void GameLabel::Stop() {
    core::LockGuard lock(mutex_);
    stopping_ = true; for (auto &o : owners_) std::vector<LabelLine>().swap(o); panel_.Stop();
}
bool GameLabel::Drained() { core::LockGuard lock(mutex_); return panel_.Drained(); }
bool GameLabel::Cleanup() { core::LockGuard lock(mutex_); panel_.Request(false); return panel_.Drained(); }
void GameLabel::Step(const LeaseStatus &lease) {
    if (!mutex_.TryLock()) return;
    if (revision_ != applied_ && !panel_.Drained()) panel_.Request(false);
    if (!stopping_ && revision_ != applied_ && panel_.Drained()) {
        std::vector<LabelLine> lines;
        for (const auto &o : owners_) lines.insert(lines.end(), o.begin(), o.end());
        if (lines.empty()) { applied_ = revision_; panel_.Request(false); }
        else if (content_.Set(std::move(lines))) { applied_ = revision_; panel_.Request(true); }
    }
    panel_.Step(lease); mutex_.Unlock();
}
}
