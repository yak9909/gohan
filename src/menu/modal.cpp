#include "menu/modal.hpp"
#include "core/format.hpp"
#include "menu/hotkey.hpp"
#include "menu/layout.hpp"
#include "menu/value_text.hpp"

namespace gohan::menu {
namespace {
using namespace motion;


constexpr int kMaxListRows = 10;
constexpr int kMaxCheckRows = 10;
Rect Place(int, int height, bool) { auto r = Bounds(ca::Entry::Rect); r.h = height; return r; }
Rect Track(const Rect &b) { return Bounds(ca::Entry::Number::Track, b.x, b.y, b.w); }

s32 Clamp(s32 v, s32 lo, s32 hi) { return v < lo ? lo : v > hi ? hi : v; }
constexpr u16 kDirections[4] = {key::Up, key::Down, key::Left, key::Right};
void Title(Surface &s, Rect b, const char *label, float alpha) {
    Paper(s, b, alpha);
    s.Box(Bounds(ca::Entry::Title::Rect, b.x, b.y, b.w), "band3", Fade(0xFFFFFFFFu, alpha));
    char text[kLineBytes];
    s.Text(b.x + ca::Entry::Title::Text[0], b.y + ca::Entry::Title::Text[1],
           Trim(s, label, b.w - 2 * ca::Entry::Title::Text[0], text, sizeof(text)), Fade(ca::Color::Band::Text, alpha));
}
} // namespace
Rect DialogBox(int, bool) { return Bounds(ca::Dialog::Rect); }
Rect DialogOption(const Rect &b, int index) {
    return {b.x + b.w / 2 + ca::Dialog::Button::Offset[index], b.y + ca::Dialog::Button::Offset[2], ca::Dialog::Button::Size[0], ca::Dialog::Button::Size[1]};
}
int DialogOptionAt(int, bool, int, int) { return -1; }
void DrawDialog(Surface &s, const Engine &e, float, float appear) {
    if (appear <= 0 || e.Dialog() == DialogKind::None) return;
    s.Fill({0, 0, ca::Screen[0], ca::Screen[1]}, Fade(ca::Color::Dim, appear));
    Rect b = Bounds(ca::Dialog::Rect); b.y += Round((1 - appear) * ca::Time::Dialog[1]);
    Title(s, b, ca::Text::DialogTitle, appear);
    const char *yes = "適用";
    char question[kLineBytes]; core::TextWriter w(question, sizeof(question));
    switch (e.Dialog()) {
    case DialogKind::Close: w.Append("変更を適用しますか？"); break;
    case DialogKind::ApplyAll: w.Append("全ての変更を適用しますか？"); break;
    case DialogKind::RevertAll: w.Append("全ての変更を戻しますか？"); yes = "戻す"; break;
    case DialogKind::Action:
        if (e.DialogItem() != kNoItem) w.Append(e.Def(e.DialogItem()).label);
        w.Append("を実行しますか？"); yes = "実行"; break;
    case DialogKind::None: break;
    }
    char lines[2][kLineBytes]; const int n = Wrap(s, question, b.w - ca::Margin * 2, lines, 2, nullptr);
    for (int i = 0; i < n; ++i) s.Text(b.x + (b.w - s.Measure(lines[i])) / 2, b.y + ca::Dialog::QuestionY + i * ca::Font::Line, lines[i], Fade(ca::Color::Text, appear));
    const char *labels[] = {yes, "キャンセル"};
    for (int i = 0; i < 2; ++i) {
        const Rect option = DialogOption(b, i); const bool selected = (e.DialogYes() ? 0 : 1) == i;
        if (selected) {
            Selected(s, {option.x, option.y - 1, option.w, ca::Row::H}, false, appear);
            s.Sprite(option.x + ca::Cursor::Offset[0], option.y + ca::Cursor::Offset[1] - 1, "finger", Fade(0xFFFFFFFFu, appear));
        }
        s.Text(option.x + (option.w - s.Measure(labels[i])) / 2, option.y + ca::Row::Pill::Text[3], labels[i], Fade(selected ? ca::Color::Sel::Text : ca::Color::Text, appear));
    }
}
Rect Entry::Box(bool menu_open) const {
    const auto &h = ca::Entry::Heights;
    return Place(screen_width_, kind_ == UiKind::Key ? h[4] : kind_ == UiKind::List || kind_ == UiKind::CheckList ? h[1] + h[2] * Rows() + h[3] : h[0], menu_open);
}
int Entry::Rows() const { const int max = kind_ == UiKind::CheckList ? kMaxCheckRows : kMaxListRows; return count_ < max ? count_ : max; }

void Entry::Begin(const Engine &engine, const Input &input, int screen_width) {
    const UiRequest request = engine.Ui();
    active_ = request.kind != UiKind::None;
    kind_ = request.kind;
    item_ = request.item;
    screen_width_ = screen_width;
    prev_held_ = input.held;
    for (u32 &at : repeat_at_) at = 0;
    if (!active_) return;
    def_ = &engine.Def(item_);
    const ItemState &state = engine.State(item_);
    switch (kind_) {
    case UiKind::Number:
    case UiKind::Slider:
        value_ = state.value;
        min_ = def_->min;
        max_ = def_->max;
        step_ = def_->step > 0 ? def_->step : 1;
        break;
    case UiKind::TriggerInput:
        value_ = def_->def;
        min_ = def_->min;
        max_ = def_->max;
        if (max_ <= min_) {
            min_ = 0;
            max_ = def_->input == InputKind::Hex ? 0xFFFF : 99999;
        }
        step_ = 1;
        break;
    case UiKind::List:
        count_ = def_->option_count;
        index_ = state.value >= 0 && state.value < count_ ? state.value : 0;
        break;
    case UiKind::CheckList:
        count_ = def_->option_count;
        mask_ = static_cast<u32>(state.value);
        index_ = 0;
        break;
    case UiKind::Key:
        captured_ = 0;
        phase_ = Phase::Release;
        break;
    case UiKind::None: break;
    }
    top_ = 0;
    KeepVisible();
}

void Entry::Finish(Engine &engine, s32 value, u32 now) {
    active_ = false;
    engine.SubmitUi(value, now);
}

void Entry::Cancel(Engine &engine) {
    active_ = false;
    engine.CancelUi();
}

void Entry::SetValue(s32 value) { value_ = Clamp(value, min_, max_); }

Format Entry::DisplayFormat() const {
    return kind_ == UiKind::TriggerInput && def_->input == InputKind::Hex ? Format::Hex : def_->format;
}

void Entry::KeepVisible() {
    const int rows = Rows();
    if (index_ < top_) top_ = index_;
    if (index_ >= top_ + rows) top_ = index_ - rows + 1;
    if (top_ < 0) top_ = 0;
}

unsigned Entry::Fired(u16 held, u16 pressed, u32 now, bool *repeated) {
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

void Entry::Step(Engine &engine, u32 now, const Input &input, bool /*menu_open*/) {
    if (!active_) return;
    if (engine.Ui().kind == UiKind::None) {
        active_ = false;
        return;
    }
    const u16 pressed = static_cast<u16>(input.held & ~prev_held_);
    switch (kind_) {
    case UiKind::Number:
    case UiKind::Slider:
    case UiKind::TriggerInput: StepNumber(engine, now, input, pressed); break;
    case UiKind::List:
    case UiKind::CheckList: StepList(engine, now, input, pressed); break;
    case UiKind::Key: StepKey(engine, now, input); break;
    case UiKind::None: break;
    }
    prev_held_ = input.held;
}

void Entry::StepNumber(Engine &engine, u32 now, const Input &input, u16 pressed) {
    bool repeated[4];
    const unsigned fired = Fired(input.held, pressed, now, repeated);
    const s32 coarse = step_ * (DisplayFormat() == Format::Hex ? 16 : 10);
    if (fired & 1u) SetValue(value_ + coarse);       // 上
    if (fired & 2u) SetValue(value_ - coarse);       // 下
    if (fired & 4u) SetValue(value_ - step_);        // 左
    if (fired & 8u) SetValue(value_ + step_);        // 右
    if ((pressed & key::A) != 0) return Finish(engine, value_, now);
    if ((pressed & key::B) != 0) return Cancel(engine);

}

void Entry::StepList(Engine &engine, u32 now, const Input &input, u16 pressed) {
    const bool check = kind_ == UiKind::CheckList;
    bool repeated[4];
    const unsigned fired = Fired(input.held, pressed, now, repeated);
    if (count_ > 0) {
        if (fired & 1u) index_ = index_ > 0 ? index_ - 1 : (repeated[0] ? 0 : count_ - 1);
        if (fired & 2u) index_ = index_ + 1 < count_ ? index_ + 1 : (repeated[1] ? count_ - 1 : 0);
    }
    KeepVisible();
    if ((pressed & key::A) != 0) {
        if (!check) return Finish(engine, index_, now);
        mask_ ^= 1u << index_;
    }
    if (check && (pressed & key::X) != 0) return Finish(engine, static_cast<s32>(mask_), now);
    if ((pressed & key::B) != 0) return Cancel(engine);

}

void Entry::StepKey(Engine &engine, u32 now, const Input &input) {
    if (input.held & key::Select) {
        if (!(prev_held_ & key::Select)) select_at_ = now;
        if (now - select_at_ >= timing::kHoldMs) { select_at_ = 0; return Finish(engine, 0, now); }
        return;
    }
    if (prev_held_ & key::Select) return Cancel(engine);
    // メニューの開閉に使うボタンは割り当てに含めない。
    const u16 held = static_cast<u16>(input.held & ~key::Select);
    switch (phase_) {
    case Phase::Release:
        if (held == 0) phase_ = Phase::Wait;
        break;
    case Phase::Wait:
        if (held != 0) {
            phase_ = Phase::Record;
            captured_ = held;
        }
        break;
    case Phase::Record:
        captured_ = static_cast<u16>(captured_ | held);
        if (held == 0) return Finish(engine, captured_, now);
        break;
    }

}


void Entry::Draw(Surface &s, float, float appear, bool) const {
    if (!active_ || appear <= 0) return;
    Rect b = Box(true); b.y += Round((1 - appear) * ca::Time::Dialog[1]);
    switch (kind_) {
    case UiKind::Key: DrawKey(s, b, appear); break;
    case UiKind::List:
    case UiKind::CheckList: DrawList(s, b, appear); break;
    case UiKind::None: break;
    default: DrawNumber(s, b, appear); break;
    }
}
void Entry::DrawNumber(Surface &s, const Rect &b, float alpha) const {
    Title(s, b, def_->label, alpha);
    const Rect field = Bounds(ca::Entry::Number::Field, b.x, b.y, b.w);
    s.Box(field, "field", Fade(0xFFFFFFFFu, alpha));
    char text[kLineBytes]; FormatNumber(DisplayFormat(), value_, text, sizeof(text));
    s.Text(b.x + (b.w - s.Measure(text, true)) / 2, b.y + ca::Entry::Number::ValueY, text, Fade(ca::Color::Text, alpha), true);
    s.Sprite(b.x + ca::Entry::Number::Arrows[0], b.y + ca::Entry::Number::Arrows[2], "arrow_l", Fade(ca::Color::Green, alpha));
    s.Sprite(b.x + b.w + ca::Entry::Number::Arrows[1], b.y + ca::Entry::Number::Arrows[2], "arrow_r", Fade(ca::Color::Green, alpha));
    const Rect track = Track(b);
    const float progress = max_ > min_ ? float(value_ - min_) / float(max_ - min_) : 0;
    s.Box(track, "track", Fade(0xFFFFFFFFu, alpha));
    s.Fill({track.x, track.y + 1, Round(track.w * progress), track.h - 2}, Fade(ca::Color::Track::Filled, alpha));
    const int size = ca::Entry::Number::Thumb;
    s.Sprite(track.x + Round((track.w - size) * progress), track.y + (track.h - size) / 2, "knob", Fade(0xFFFFFFFFu, alpha));
    FormatNumber(DisplayFormat(), min_, text, sizeof(text));
    s.Text(track.x, b.y + ca::Entry::Number::RangeY, text, Fade(ca::Color::Sub, alpha), true);
    FormatNumber(DisplayFormat(), max_, text, sizeof(text));
    s.Text(track.x + track.w - s.Measure(text, true), b.y + ca::Entry::Number::RangeY, text, Fade(ca::Color::Sub, alpha), true);
    s.Text(b.x + ca::Entry::Number::Field[0], b.y + ca::Entry::Number::PendingY, ca::Text::NumberPending, Fade(ca::Color::Sub, alpha));
}
void Entry::DrawList(Surface &s, const Rect &b, float alpha) const {
    Title(s, b, def_->label, alpha);
    for (int row = 0; row < Rows(); ++row) {
        const int option = top_ + row; if (option >= count_) break;
        const int y = b.y + ca::Entry::List::Top + row * ca::Entry::List::Pitch;
        const bool selected = option == index_;
        if (selected) {
            Rect band = Bounds(ca::Entry::List::Band, b.x, y, b.w); Selected(s, band, false, alpha);
            s.Sprite(b.x + ca::Entry::List::Cursor[0], y + ca::Entry::List::Cursor[1], "finger", Fade(0xFFFFFFFFu, alpha));
        }
        char text[kLineBytes], trimmed[kLineBytes]; core::TextWriter w(text, sizeof(text));
        if (kind_ == UiKind::CheckList) w.Append((mask_ >> option) & 1 ? "[X] " : "[ ] ");
        w.Append(def_->options[option]);
        s.Text(b.x + ca::Entry::List::Text[0], y + ca::Entry::List::Text[1], Trim(s, text, b.w - ca::Entry::List::Text[0] - ca::Margin, trimmed, sizeof(trimmed)), Fade(selected ? ca::Color::Sel::Text : ca::Color::Text, alpha));
    }
}
void Entry::DrawKey(Surface &s, const Rect &b, float alpha) const {
    Title(s, b, def_->label, alpha);
    s.Text(b.x + ca::Entry::Key::Prompt[0], b.y + ca::Entry::Key::Prompt[1], ca::Text::KeyPrompt, Fade(ca::Color::Text, alpha));
    s.Box(Bounds(ca::Entry::Key::Field, b.x, b.y, b.w), "box", Fade(0xFFFFFFFFu, alpha));
    if (captured_) KeyIcons(s, b.x + ca::Entry::Key::Icon[0], b.y + ca::Entry::Key::Icon[1], captured_);
    s.Text(b.x + ca::Entry::Key::Prompt[0], b.y + ca::Entry::Key::HintsY[0], ca::Text::KeyCancel, Fade(ca::Color::Sub, alpha));
    s.Text(b.x + ca::Entry::Key::Prompt[0], b.y + ca::Entry::Key::HintsY[1], ca::Text::KeyDisable, Fade(ca::Color::Sub, alpha));
}
} // namespace gohan::menu
