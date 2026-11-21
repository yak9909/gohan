#include "ui/lists/catalog_list.hpp"
namespace gohan::ui::lists {
bool CatalogList::Show(std::vector<Row> rows, std::string label, bool extras) {
    core::LockGuard lock(mutex_);
    if (stopping_ || rows.empty() || rows.size() > 512) return false;
    model_.Set(std::move(rows)); label_ = std::move(label); extras_ = extras; wanted_ = true;
    return true;
}
void CatalogList::Close() { core::LockGuard lock(mutex_); wanted_ = false; }
void CatalogList::Stop() { core::LockGuard lock(mutex_); wanted_ = false; stopping_ = true; panel_.Stop(); }
bool CatalogList::Drained() { core::LockGuard lock(mutex_); return panel_.Drained(); }
bool CatalogList::Cleanup() { Close(); return Drained(); }
int CatalogList::TakeDecision() { core::LockGuard lock(mutex_); const int v = decision_; decision_ = -1; return v; }
void CatalogList::Step(const LeaseStatus &lease, unsigned dirs, unsigned now, bool touching, int x, int y, bool confirm) {
    if (!mutex_.TryLock()) return;
    const bool changed = generation_ != model_.Generation();
    if (changed && !panel_.Drained()) panel_.Request(false);
    if (wanted_ && panel_.Drained() && (changed || generation_ == 0)) {
        std::vector<game::CatalogRow> rows;
        for (unsigned i = 0; i < model_.Size(); ++i) {
            const auto &r = model_.At(i); rows.push_back({r.text, r.prefix, r.extra, r.message});
        }
        if (content_.Set(std::move(rows), label_, extras_)) {
            generation_ = model_.Generation(); panel_.Request(true);
            repeat_ = DpadRepeat{}; touch_ = {}; confirm_ = confirm;
        }
    }
    if (!wanted_) panel_.Request(false);
    touch_.Step(x, y, touching && wanted_ && !stopping_);
    content_.Input(touch_);
    if (panel_.Visible()) {
        const int move = repeat_.Step(dirs, now, content_.VisibleRows());
        if (move) content_.Select(content_.Selected() + move);
        if (confirm && !confirm_) decision_ = content_.Selected();
        const int picked = content_.TakeDecision(); if (picked >= 0) decision_ = picked;
    } else repeat_.Step(0, now, 6);
    confirm_ = confirm;
    panel_.Step(lease);
    if (!wanted_ && panel_.Drained()) { model_.Close(); generation_ = 0; label_.clear(); }
    mutex_.Unlock();
}
}
