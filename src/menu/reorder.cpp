#include "menu/reorder.hpp"
#include "menu/paint.hpp"
#include "menu/timing.hpp"
#include <algorithm>

// お気に入りの並べ替え。写しの入れ替えと、持ち上げ・下ろしの動き（D.12.3）。描画は Panel が行う。
namespace gohan::menu {
namespace {
constexpr u16 kDirections[2] = {key::Up, key::Down};
constexpr float kLift = static_cast<float>(ca::Fav::Reorder::Lift);
} // namespace

void Reorder::Begin(const Engine &engine, u16 held, u32 now) {
    item_ = engine.Ui().item;
    count_ = engine.FavoriteCount();
    std::copy_n(engine.FavoriteOrder(), count_, items_);
    index_ = 0;
    for (std::size_t n = 0; n < count_; ++n)
        if (items_[n] == item_) {
            index_ = n;
            break;
        }
    prev_held_ = held; // 開く時に押したボタン（A）を新しい入力にしない。十字キーのリピートは押してから始める
    for (u32 &at : repeat_at_) at = now + timing::kRepeatDelayMs;
    open_ = true;
    // 持ち上げる。閉じる途中に開き直す時は、今の位相から続く（Tween は今の値から動く）。
    lift_.Go(kLift, now, timing::kSelectionMoveMs);
}

void Reorder::Close(const Engine &engine, u32 now) {
    open_ = false;
    // 写しは捨てる（A の後はエンジンの並びと同じ。B の後は並べ替えの前の並び）。
    count_ = engine.FavoriteCount();
    std::copy_n(engine.FavoriteOrder(), count_, items_);
    lift_.Go(0.0f, now, timing::kSelectionMoveMs);
}

void Reorder::Swap(std::size_t to) {
    std::swap(items_[index_], items_[to]);
    index_ = to;
}

unsigned Reorder::Fired(u16 held, u16 pressed, u32 now, bool *repeated) {
    unsigned fired = 0;
    for (unsigned n = 0; n < 2; ++n) {
        repeated[n] = false;
        if ((pressed & kDirections[n]) != 0) {
            repeat_at_[n] = now + timing::kRepeatDelayMs;
            fired |= 1u << n;
        } else if ((held & kDirections[n]) != 0 && static_cast<s32>(now - repeat_at_[n]) >= 0) {
            repeat_at_[n] += timing::kRepeatIntervalMs;
            if (static_cast<s32>(now - repeat_at_[n]) > 0) repeat_at_[n] = now + timing::kRepeatIntervalMs;
            fired |= 1u << n;
            repeated[n] = true;
        }
    }
    return fired;
}

void Reorder::Step(Engine &engine, u32 now, u16 held) {
    if (!open_) return;
    // 要求がエンジンから消えていたら（他で片付いた時）、決めずに下ろす。
    if (engine.Ui().kind != UiKind::Reorder) {
        Close(engine, now);
        return;
    }
    const u16 pressed = static_cast<u16>(held & ~prev_held_);
    prev_held_ = held;
    bool repeated[2];
    const unsigned fired = Fired(held, pressed, now, repeated);
    // 上下は写しの中のとなりと入れ替える。端では止まる（折り返さない。押した瞬間もリピートも同じ）。
    if ((fired & 1u) != 0 && index_ > 0) Swap(index_ - 1);
    if ((fired & 2u) != 0 && index_ + 1 < count_) Swap(index_ + 1);
    if ((pressed & key::B) != 0) {
        engine.CancelUi();
        Close(engine, now);
        return;
    }
    if ((pressed & key::A) != 0) {
        // 決める。版の数は並びが変わった時だけ増える（エンジンの規則）。
        engine.SetFavoriteOrder(items_, count_);
        engine.CancelUi();
        Close(engine, now);
    }
}
} // namespace gohan::menu
