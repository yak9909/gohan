#include "menu/engine.hpp"

// 構築・結び付け・照会・フォルダの移動と選択。
namespace gohan::menu {
Engine::Engine(const ItemDef *defs, std::size_t count)
    : defs_(defs), count_(count < core::limits::MenuItems ? count : core::limits::MenuItems) {
    for (std::size_t i = 0; i < count_; ++i) {
        ItemState &st = state_[i];
        const ItemDef &d = defs_[i];
        st.value = st.applied = d.def;
        st.hotkey = st.applied_hotkey = d.hotkey;
        // ゲームから読む項目は、読めるまで編集できない。
        st.available = d.source != Source::Game;
    }
    stack_[0] = kNoItem;
    selection_[0] = kNoItem;
}

bool Engine::Bind(const Bound &bound) {
    if (bound.item >= count_ || is_bound_[bound.item]) return false;
    const ItemDef &d = defs_[bound.item];
    if (bound.behavior != d.behavior || bound.source != d.source) return false;
    bound_[bound.item] = bound;
    is_bound_[bound.item] = true;
    state_[bound.item].seen = EffectOn(bound.item); // 結び付けただけでは通知しない
    return true;
}

bool Engine::BindAll(const Bound *bounds, std::size_t count) {
    bool ok = true;
    for (std::size_t i = 0; i < count; ++i) ok = Bind(bounds[i]) && ok;
    return ok;
}

void Engine::Emit(const Notice &notice) {
    if (sink_.emit) sink_.emit(sink_.user, notice);
}

// 値（編集値と適用値）を持つ項目か。実行（その場）と画面は値を持たない。
bool Engine::HasValue(u16 item) const {
    const ItemDef &d = defs_[item];
    if (d.control == Control::Folder || d.behavior == Behavior::Mode) return false;
    if (d.control == Control::Action) return d.menu == MenuKind::OnApply;
    return true;
}

bool Engine::CondHolds(const Cond &cond) const {
    if (cond.item == kNoItem || cond.op == CondOp::None || cond.item >= count_) return true;
    const s32 lhs = state_[cond.item].value;
    switch (cond.op) {
    case CondOp::Eq: return lhs == cond.rhs;
    case CondOp::Ne: return lhs != cond.rhs;
    case CondOp::Lt: return lhs < cond.rhs;
    case CondOp::Le: return lhs <= cond.rhs;
    case CondOp::Gt: return lhs > cond.rhs;
    case CondOp::Ge: return lhs >= cond.rhs;
    case CondOp::None: break;
    }
    return true;
}

bool Engine::Hidden(u16 item) const {
    for (u16 i = item; i != kNoItem && i < count_; i = defs_[i].parent)
        if (defs_[i].dev && !dev_visible_) return true;
    return false;
}

// 灰色（カーソルは置ける）。ゲームの値を読めない、または有効の条件が成り立たない。
bool Engine::Disabled(u16 item) const {
    if (!Has(item)) return true;
    const ItemDef &d = defs_[item];
    if (d.source == Source::Game && !state_[item].available) return true;
    return !CondHolds(d.cond);
}

bool Engine::Dirty(u16 item) const {
    if (!Has(item) || defs_[item].control == Control::Folder) return false;
    const ItemState &st = state_[item];
    return (HasValue(item) && st.value != st.applied) || st.hotkey != st.applied_hotkey;
}

// 適用できる（隠れても灰色でもない）適用待ちの数。
std::size_t Engine::DirtyCount() const {
    std::size_t n = 0;
    for (u16 i = 0; i < count_; ++i)
        if (Dirty(i) && !Hidden(i) && !Disabled(i)) ++n;
    return n;
}

bool Engine::EffectOn(u16 item) const {
    if (!Has(item) || defs_[item].behavior != Behavior::State) return false;
    if (is_bound_[item] && bound_[item].patch.is_active) return bound_[item].patch.is_active();
    return state_[item].effect;
}

u16 Engine::FirstVisible(u16 folder) const {
    u16 i = folder == kNoItem ? 0 : defs_[folder].first_child;
    if (folder == kNoItem && count_ == 0) return kNoItem;
    while (i != kNoItem && Hidden(i)) i = defs_[i].next;
    return i;
}

u16 Engine::NextVisible(u16 item) const {
    u16 i = defs_[item].next;
    while (i != kNoItem && Hidden(i)) i = defs_[i].next;
    return i;
}

u16 Engine::PrevVisible(u16 item) const {
    u16 previous = kNoItem;
    for (u16 i = FirstVisible(defs_[item].parent); i != kNoItem && i != item; i = NextVisible(i)) previous = i;
    return previous;
}

void Engine::Move(int direction, bool wrap) {
    const u16 current = Selected();
    if (current == kNoItem) return;
    u16 target = direction > 0 ? NextVisible(current) : PrevVisible(current);
    if (target == kNoItem) {
        if (!wrap) return;
        target = FirstVisible(Folder());
        if (direction < 0)
            for (u16 i = target; i != kNoItem; i = NextVisible(i)) target = i;
    }
    selection_[depth_] = target;
}

// ゲームの現在値を読む。読めなければ編集できなくする。値の固定中は固定した値を見せ続ける。
bool Engine::ReadGame(u16 item) {
    ItemState &st = state_[item];
    s32 value = 0;
    const bool ok = is_bound_[item] && bound_[item].game.read && bound_[item].game.read(&value);
    st.available = ok;
    if (ok) st.value = st.applied = st.fixed ? st.fixed_value : value;
    return ok;
}

// 開いたその時に 1 回だけ読む（開いている間は同期しない）。
void Engine::ReadGameValues() {
    for (u16 i = 0; i < count_; ++i)
        if (defs_[i].source == Source::Game) ReadGame(i);
}

// フォルダの経路を上から検査し、選択は次の有効な兄弟、無ければ手前の兄弟へ寄せる。
// 全て灰色なら先頭の見える行に置く。空になったフォルダからは親へ戻る。
void Engine::RestorePosition() {
    for (std::size_t level = 1; level <= depth_; ++level) {
        const u16 folder = stack_[level];
        if (!Has(folder) || defs_[folder].control != Control::Folder || defs_[folder].parent != stack_[level - 1] ||
            Hidden(folder) || Disabled(folder) || FirstVisible(folder) == kNoItem) {
            depth_ = level - 1;
            break;
        }
    }
    for (std::size_t level = 0; level <= depth_; ++level) {
        const u16 selected = selection_[level];
        if (Has(selected) && defs_[selected].parent == stack_[level] && !Hidden(selected) && !Disabled(selected)) continue;
        u16 target = kNoItem;
        for (u16 item = FirstVisible(stack_[level]); item != kNoItem; item = NextVisible(item)) {
            if (Disabled(item)) continue;
            target = item;
            if (selected == kNoItem || item >= selected) break;
        }
        selection_[level] = target != kNoItem ? target : FirstVisible(stack_[level]);
    }
}

void Engine::Open(u32 now) {
    (void)now;
    visible_ = true;
    ui_ = UiRequest{};
    hold_active_ = false;
    ReadGameValues(); // 開いたその時に 1 回だけ読む（開いている間は同期しない）
    RestorePosition();
}

bool Engine::RequestClose(u32 now) {
    (void)now;
    if (dialog_ != DialogKind::None) return false;
    ui_ = UiRequest{};
    hold_active_ = false;
    if (DirtyCount() > 0) {
        OpenDialog(DialogKind::Close, kNoItem);
        return false;
    }
    visible_ = false;
    return true;
}

bool Engine::Select(u16 item) {
    if (!visible_ || dialog_ != DialogKind::None || ui_.kind != UiKind::None) return false;
    if (!Has(item) || Hidden(item) || defs_[item].parent != Folder()) return false;
    selection_[depth_] = item;
    return true;
}

void Engine::Back(u32 now) {
    if (depth_ > 0) {
        --depth_;
        return;
    }
    RequestClose(now);
}

void Engine::Activate(u16 item, u32 now) {
    if (item == kNoItem) return;
    const ItemDef &d = defs_[item];
    if (d.control == Control::Folder) {
        if (depth_ + 1 >= timing::kMaxDepth) return;
        ++depth_;
        stack_[depth_] = item;
        selection_[depth_] = FirstVisible(item);
        return;
    }
    if (Disabled(item)) return;
    ItemState &st = state_[item];
    switch (d.control) {
    case Control::Toggle: st.value = st.value ? 0 : 1; break;
    case Control::Action:
        if (d.behavior == Behavior::Mode) {
            ToggleMode(item, now);
        } else if (d.menu == MenuKind::OnApply) {
            st.value = st.value ? 0 : 1; // 予約。適用で実行する
        } else if (d.input != InputKind::None) {
            ui_ = UiRequest{UiKind::TriggerInput, item};
        } else {
            RunTrigger(item, 0, now);
        }
        break;
    case Control::Number: ui_ = UiRequest{UiKind::Number, item}; break;
    case Control::Slider: ui_ = UiRequest{UiKind::Slider, item}; break;
    case Control::Select: ui_ = UiRequest{UiKind::List, item}; break;
    case Control::Multiselect: ui_ = UiRequest{UiKind::CheckList, item}; break;
    case Control::Key: ui_ = UiRequest{UiKind::Key, item}; break;
    case Control::Folder: break;
    }
}

// 左右キー。数値は step ずつ、選択は 1 つずつ（端で止まる）。
void Engine::Adjust(u16 item, int direction) {
    if (item == kNoItem || Disabled(item)) return;
    const ItemDef &d = defs_[item];
    const ItemState &st = state_[item];
    if (d.control == Control::Number || d.control == Control::Slider)
        SetValue(item, st.value + direction * d.step);
    else if (d.control == Control::Select)
        SetValue(item, st.value + direction);
}
} // namespace gohan::menu
