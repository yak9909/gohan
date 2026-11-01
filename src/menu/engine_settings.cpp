#include "menu/engine.hpp"

// 設定画面（START）・固定中の項目の並び・保存を全て消す・押し切るボタン（docs/design/menu-item-actions.md の 5 節・9 節）。
namespace gohan::menu {
namespace {
struct ButtonName {
    const char *name;
    u16 key;
};
// spec の選択肢の名前からボタンのビットを引く（大文字小文字は問わない。codegen の BUTTON_NAMES と同じ 14 個）。
constexpr ButtonName kButtonNames[] = {
    {"ZL", key::ZL},         {"L", key::L},       {"R", key::R},         {"ZR", key::ZR},   {"Up", key::Up},
    {"Down", key::Down},     {"Left", key::Left}, {"Right", key::Right}, {"A", key::A},     {"B", key::B},
    {"X", key::X},           {"Y", key::Y},       {"Select", key::Select}, {"Start", key::Start},
};

char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool SameName(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b)
        if (Lower(*a) != Lower(*b)) return false;
    return *a == *b;
}

u16 KeyOfName(const char *name) {
    for (const ButtonName &button : kButtonNames)
        if (SameName(button.name, name)) return button.key;
    return 0;
}
} // namespace

void Engine::BindSettings(const SettingsItems &items) {
    settings_.root = Has(items.root) ? items.root : kNoItem;
    settings_.erase = Has(items.erase) ? items.erase : kNoItem;
    settings_.keep = Has(items.keep) ? items.keep : kNoItem;
    settings_.press_through = Has(items.press_through) ? items.press_through : kNoItem;
    keep_toggles_ = settings_.keep != kNoItem && state_[settings_.keep].applied != 0;
}

bool Engine::InSettingsTree(u16 item) const {
    if (settings_.root == kNoItem) return false;
    for (u16 i = item; Has(i); i = defs_[i].parent)
        if (i == settings_.root) return true;
    return false;
}

// 押し切るボタンの適用値（選んだ選択肢の名前）を、ボタンのビットにする。設定が無い時は START だけ。
u16 Engine::DeferKeys() const {
    if (!Has(settings_.press_through)) return key::Start;
    const ItemDef &d = defs_[settings_.press_through];
    const auto mask = static_cast<std::uint32_t>(state_[settings_.press_through].applied);
    u16 keys = 0;
    for (std::size_t n = 0; n < d.option_count; ++n)
        if (((mask >> n) & 1u) != 0) keys = static_cast<u16>(keys | KeyOfName(d.options[n]));
    return keys;
}

void Engine::SavePosition(Position &to) const {
    to.depth = depth_;
    for (std::size_t n = 0; n < timing::kMaxDepth; ++n) {
        to.stack[n] = stack_[n];
        to.selection[n] = selection_[n];
    }
}

void Engine::LoadPosition(const Position &from) {
    depth_ = from.depth;
    for (std::size_t n = 0; n < timing::kMaxDepth; ++n) {
        stack_[n] = from.stack[n];
        selection_[n] = from.selection[n];
    }
}

// 一覧で START を押した時の要求を受けて、設定画面の根へ入る。一覧の位置は覚え、設定画面の中では前の位置から始める。
bool Engine::OpenSettings(u32 now) {
    (void)now;
    ui_ = UiRequest{};
    if (settings_.root == kNoItem || in_settings_ || !visible_ || dialog_ != DialogKind::None) return false;
    SavePosition(main_position_);
    if (settings_position_saved_) {
        LoadPosition(settings_position_);
    } else {
        depth_ = 0;
        stack_[0] = settings_.root; // スタックの底を設定画面の根に替える
        selection_[0] = kNoItem;
    }
    in_settings_ = true;
    RestorePosition();
    return true;
}

// 設定画面を閉じて、一覧の元の位置へ戻す。設定画面の中の位置は覚えておく（同じ起動中）。
void Engine::LeaveSettings(u32 now) {
    (void)now;
    if (!in_settings_) return;
    SavePosition(settings_position_);
    settings_position_saved_ = true;
    LoadPosition(main_position_);
    in_settings_ = false;
    RestorePosition();
}

// 固定中の項目の一覧の A: 固定を外す。行が消えるので、選択は次の行へ（最後の行なら前の行へ）。
void Engine::RemoveFixedRow(u16 item, u32 now) {
    const u16 folder = Folder();
    if (!IsFixedList(folder) || Selected() != item) return;
    u16 target = NextVisible(folder, item);
    if (target == kNoItem)
        for (u16 i = FirstVisible(folder); i != item && i != kNoItem; i = NextVisible(folder, i)) target = i;
    if (SetFixed(item, false, now)) selection_[depth_] = target;
    RestorePosition();
}

// 保存を全て消す: 固定・お気に入り・ホットキー・設定の値（設定画面の設定を含む）を最初に戻す。
// 今 ON のトグルの効果はその起動の間は残す（効果には触れない）。保存の書き込みは段の外（版の数と erase_count_ を増やす）。
void Engine::EraseAll(u32 now) {
    (void)now;
    for (u16 i = 0; i < count_; ++i) {
        const ItemDef &d = defs_[i];
        ItemState &st = state_[i];
        st.fixed = false;
        st.fixed_failed = false;
        st.fixed_value = 0;
        st.hotkey = st.applied_hotkey = d.hotkey;
        if (d.behavior == Behavior::Value && d.source == Source::Setting) {
            st.value = st.applied = d.def;
            if (is_bound_[i] && bound_[i].setting.applied) bound_[i].setting.applied(st.applied);
        }
    }
    fixed_count_ = 0;
    favorite_count_ = 0;
    keep_toggles_ = settings_.keep != kNoItem && state_[settings_.keep].applied != 0;
    ++revision_;
    ++erase_count_;
    RestorePosition();
    Notice notice;
    NoticeEraseAll(notice);
    Emit(notice);
}
} // namespace gohan::menu
