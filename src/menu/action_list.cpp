#include "menu/action_list.hpp"
#include "menu/hotkey.hpp"
#include "menu/paint.hpp"
#include "menu/timing.hpp"

// R の操作のリスト。開閉の動き・入力・描画（D.12.1）。並べ替えと設定画面の要求は Shell が取り消す。
namespace gohan::menu {
namespace {
constexpr u16 kDirections[4] = {key::Up, key::Down, key::Left, key::Right};
constexpr u32 kOpenMs = static_cast<u32>(ca::Time::Actions[0]);

// 印。使える行は白にして右下 1 画素の影を付ける。灰色は不透明度だけ下げる。
void Mark(Surface &s, const char *name, int x, int y, u32 color, bool lit, float alpha) {
    if (lit) {
        s.Sprite(x + 1, y + 1, name, ca::Color::Sel::Shadow);
        s.Sprite(x, y, name, 0xFFFFFFFFu);
    } else s.Sprite(x, y, name, Fade(color, alpha));
}

// 並べ替えの ▲▼（重ねて 5 × 7）。使える行の選択中は白と影。
void DrawReorder(Surface &s, int x, int y, bool lit, float alpha) {
    const u32 color = lit ? 0xFFFFFFFFu : Fade(ca::Color::Band::Fill, alpha);
    const int down = ca::Actions::Icon::ReorderDown;
    if (lit) {
        s.Sprite(x + 1, y + 1, "arrow_u", ca::Color::Sel::Shadow);
        s.Sprite(x + 1, y + 1 + down, "arrow_d", ca::Color::Sel::Shadow);
    }
    s.Sprite(x, y, "arrow_u", color);
    s.Sprite(x, y + down, "arrow_d", color);
}

void DrawIcon(Surface &s, ActionKind kind, int x, int ry, bool lit, float alpha) {
    const int wide = ca::Actions::Icon::Wide;
    const int ix = x + ca::Actions::Icon::Pos[0], iy = ry + ca::Actions::Icon::Pos[1];
    switch (kind) {
    case ActionKind::Fix:
    case ActionKind::Unfix: Mark(s, "lock", ix + wide, iy, ca::Color::Locked, lit, alpha); break;
    case ActionKind::AddFavorite:
    case ActionKind::RemoveFavorite: Mark(s, lit ? "star.white" : "star", ix, iy, 0xFFFFFFFFu, lit, alpha); break;
    case ActionKind::Reorder: DrawReorder(s, ix + wide, iy, lit, alpha); break;
    case ActionKind::SetHotkey:
    case ActionKind::RemoveHotkey:
        Mark(s, "hotkey", ix, ry + ca::Actions::Icon::HotkeyY, ca::Hotkey::Mark::Color, lit, alpha);
        break;
    }
}
} // namespace

void ActionList::Begin(const Engine &engine, u16 held, u32 now) {
    item_ = engine.Ui().item;
    title_ = engine.Def(item_).label;
    count_ = engine.Actions(item_, actions_);
    // 開くとカーソルは最初の使える操作。使える物が無ければ先頭。
    index_ = 0;
    for (std::size_t n = 0; n < count_; ++n)
        if (!actions_[n].disabled) {
            index_ = n;
            break;
        }
    prev_held_ = held; // 開く時に押したボタン（R）を新しい入力にしない。十字キーのリピートは押してから始める
    for (u32 &at : repeat_at_) at = now + timing::kRepeatDelayMs;
    open_ = true;
    // 閉じる途中なら今の位相から開き直す（Tween は今の値から続ける）。
    appear_.Go(1.0f, now, kOpenMs);
}

void ActionList::Close(u32 now) {
    open_ = false;
    appear_.Go(0.0f, now, kOpenMs);
}

unsigned ActionList::Fired(u16 held, u16 pressed, u32 now, bool *repeated) {
    unsigned fired = 0;
    for (unsigned n = 0; n < 4; ++n) {
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

void ActionList::Step(Engine &engine, u32 now, u16 held) {
    if (!open_) return;
    // 要求がエンジンから消えていたら（他で片付いた時）、取り消さずに閉じる。
    if (engine.Ui().kind != UiKind::Actions) {
        Close(now);
        return;
    }
    const u16 pressed = static_cast<u16>(held & ~prev_held_);
    prev_held_ = held;
    bool repeated[4];
    const unsigned fired = Fired(held, pressed, now, repeated);
    if (count_ > 0) {
        // 押した瞬間は端で折り返し、リピートでは端で止まる。
        if (fired & 1u) index_ = index_ > 0 ? index_ - 1 : (repeated[0] ? 0 : count_ - 1);
        if (fired & 2u) index_ = index_ + 1 < count_ ? index_ + 1 : (repeated[1] ? count_ - 1 : 0);
    }
    if ((pressed & (key::B | key::R)) != 0) {
        engine.CancelUi();
        Close(now);
        return;
    }
    // 灰色の操作では何もしない（閉じない）。実行は押した時に 1 回だけ。
    if ((pressed & key::A) != 0 && !actions_[index_].disabled) {
        engine.RunAction(item_, actions_[index_].kind, now);
        Close(now);
        return;
    }
    if ((pressed & key::Y) != 0) {
        for (std::size_t n = 0; n < count_; ++n) {
            if (actions_[n].kind != ActionKind::SetHotkey) continue;
            if (!actions_[n].disabled) {
                engine.RunAction(item_, ActionKind::SetHotkey, now);
                Close(now);
            }
            break;
        }
    }
}

void ActionList::Draw(Surface &surface, u32 now, int row_y) const {
    const float appear = appear_.At(now);
    if (appear <= 0.0f || count_ == 0) return;
    const int width = ca::Actions::Rect[2];
    const int height = ca::Actions::Heights[0] + ca::Actions::Heights[1] * static_cast<int>(count_) + ca::Actions::Heights[2];
    // 左（行の側）から動きながら現れる。閉じる時は逆。
    const int x = ca::Actions::Rect[0] - motion::Round((1.0f - appear) * ca::Time::Actions[1]);
    // 窓の y は選んだ行の y − 3 を、上の限り（4）と下の限り（214 − 高さ）の間に収める。
    int y = row_y + ca::Actions::Offset;
    if (y + height > ca::Actions::Rect[3]) y = ca::Actions::Rect[3] - height;
    if (y < ca::Actions::Rect[1]) y = ca::Actions::Rect[1];
    FadedSurface f(surface, appear);
    // 通知より上に描くので、地は通知と同じ不透明度（notice.window）。普通の窓では下の通知の字が透ける。
    Paper(f, {x, y, width, height}, 1, "notice.window");
    // 選んだ行への指し（窓と窓の間）。行が窓の縦の範囲に入る時だけ。
    const int pointer_top = row_y + ca::Actions::Pointer[1];
    if (pointer_top >= y + 3 && pointer_top + 9 <= y + height - 3)
        f.Sprite(x + ca::Actions::Pointer[0], pointer_top, "pointer", ca::Color::Band::Fill);
    f.Box(Bounds(ca::Actions::Title::Rect, x, y, width), "band3");
    char trimmed[kLineBytes];
    f.Text(x + ca::Actions::Title::Text[0], y + ca::Actions::Title::Text[1],
           Trim(f, title_, width - 2 * ca::Actions::Title::Text[0], trimmed, sizeof(trimmed)), ca::Color::Band::Text);
    for (std::size_t i = 0; i < count_; ++i) {
        const ItemAction &action = actions_[i];
        const int ry = y + ca::Actions::Heights[0] + ca::Actions::Heights[1] * static_cast<int>(i);
        const bool selected = i == index_;
        if (selected) {
            Selected(f, Bounds(ca::Actions::Row::Band, x, ry, width), action.disabled);
            f.Sprite(x + ca::Actions::Row::Cursor[0], ry + ca::Actions::Row::Cursor[1], "finger");
        }
        const float ia = action.disabled ? ca::Color::Disabled::Alpha : 1.f;
        const bool lit = selected && !action.disabled;
        DrawIcon(f, action.kind, x, ry, lit, ia);
        const u32 ink = action.disabled ? (selected ? ca::Color::Disabled::Sel::Text : ca::Color::Disabled::Text)
                                        : selected ? ca::Color::Sel::Text : ca::Color::Text;
        Ink(f, x + ca::Actions::Row::Text[0], ry + ca::Actions::Row::Text[1], action.label, lit, ink, false,
            ca::Color::Sel::Shadow);
        // ホットキーの行には、Y で実行できる近道の印を右に出す。
        if (action.kind == ActionKind::SetHotkey)
            f.Sprite(x + width + ca::Actions::Shortcut[0], ry + ca::Actions::Shortcut[1],
                     selected ? "key.bar.y" : "key.paper.y", Fade(0xFFFFFFFFu, ia));
    }
}
} // namespace gohan::menu
