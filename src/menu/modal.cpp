#include "menu/modal.hpp"
#include "core/format.hpp"
#include "menu/hotkey.hpp"
#include "menu/layout.hpp"
#include "menu/value_text.hpp"

namespace gohan::menu {
namespace {
using namespace motion;
constexpr int kDialogHeight = 96;
constexpr int kNumberHeight = 112;
constexpr int kKeyHeight = 106;
constexpr int kRowHeight = 20;
constexpr int kButtonHeight = 19;
constexpr int kSlideDistance = 24;
constexpr int kMaxListRows = 5;
constexpr int kMaxCheckRows = 3;

Rect Place(int screen_width, int height, bool menu_open) {
    const int width = layout::SideWidth(screen_width);
    return {menu_open ? layout::kSideX : (screen_width - width) / 2, (kScreenHeight - height) / 2, width, height};
}

void DrawBox(Surface &surface, const Rect &box, u32 fill, u32 edge, float appear) {
    surface.Fill(box, Fade(fill, appear));
    surface.Outline(box, Fade(edge, appear));
}

void DrawButton(Surface &surface, const Rect &bounds, const char *label, u32 fill, float appear, unsigned scale = 1) {
    surface.Fill(bounds, Fade(fill, appear));
    const int width = surface.Measure(label, false, scale);
    surface.Text(bounds.x + (bounds.w - width) / 2, bounds.y + (bounds.h - 8 * static_cast<int>(scale)) / 2, label,
                 Fade(style::kKeyText, appear), false, scale);
}

// 数値の枠の部品。0 引く・1 足す・2 決定・3 取消。
Rect NumberButton(const Rect &box, int which) {
    const int width = (box.w - 16 - 4) / 2;
    return {box.x + 8 + (which % 2) * (width + 4), box.y + 60 + (which / 2) * 24, width, kButtonHeight};
}
Rect Track(const Rect &box) { return {box.x + 8, box.y + 40, box.w - 16, 4}; }

// 選択の枠の部品。
Rect ListRow(const Rect &box, int row) { return {box.x + 4, box.y + 6 + row * kRowHeight, box.w - 8, kRowHeight - 2}; }
Rect ListButton(const Rect &box, int which) {
    const int width = (box.w - 16 - 4) / 2;
    return {box.x + 8 + which * (width + 4), box.y + box.h - 26, width, kButtonHeight};
}

// 割り当ての枠の部品。0 無効・1 取消。
Rect KeyButton(const Rect &box, int which) {
    const int width = (box.w - 16 - 4) / 2;
    return {box.x + 8 + which * (width + 4), box.y + 70, width, kButtonHeight};
}

bool Touched(const Rect &bounds, const Pointer &pointer) { return pointer.down && bounds.Contains(pointer.x, pointer.y); }

s32 Clamp(s32 value, s32 low, s32 high) { return value < low ? low : value > high ? high : value; }

constexpr u16 kDirections[4] = {key::Up, key::Down, key::Left, key::Right};
} // namespace

// ---- 確認のダイアログ ----
Rect DialogBox(int screen_width, bool menu_open) { return Place(screen_width, kDialogHeight, menu_open); }
Rect DialogOption(const Rect &box, int index) { return {box.x + 8, box.y + 38 + index * 23 - 5, box.w - 16, kButtonHeight}; }

int DialogOptionAt(int screen_width, bool menu_open, int x, int y) {
    const Rect box = DialogBox(screen_width, menu_open);
    for (int i = 0; i < 2; ++i)
        if (DialogOption(box, i).Contains(x, y)) return i;
    return -1;
}

void DrawDialog(Surface &surface, const Engine &engine, float menu_amount, float appear) {
    if (appear <= 0.0f || engine.Dialog() == DialogKind::None) return;
    Rect box = DialogBox(surface.Width(), true);
    const Rect centered = DialogBox(surface.Width(), false);
    box.x = Round(Mix(static_cast<float>(centered.x), static_cast<float>(box.x), menu_amount)) +
            Round((1.0f - appear) * static_cast<float>(kSlideDistance));

    const char *yes = "適用";
    char title[kLineBytes];
    core::TextWriter writer(title, sizeof(title));
    switch (engine.Dialog()) {
    case DialogKind::Close: writer.Append("変更を適用しますか？"); break;
    case DialogKind::ApplyAll: writer.Append("全ての変更を適用しますか？"); break;
    case DialogKind::RevertAll:
        writer.Append("全ての変更を戻しますか？");
        yes = "戻す";
        break;
    case DialogKind::Action:
        if (engine.DialogItem() != kNoItem) writer.Append(engine.Def(engine.DialogItem()).label);
        writer.Append("を実行しますか？");
        yes = "実行";
        break;
    case DialogKind::None: break;
    }
    DrawBox(surface, box, style::kDialogBackground, style::kPending, appear);
    char lines[2][kLineBytes];
    const int count = Wrap(surface, title, box.w - 20, lines, 2, nullptr);
    for (int i = 0; i < count; ++i) surface.Text(box.x + 10, box.y + 10 + i * 11, lines[i], Fade(style::kWhite, appear));
    const char *labels[2] = {yes, "キャンセル"};
    const int selected = engine.DialogYes() ? 0 : 1;
    for (int i = 0; i < 2; ++i) {
        const Rect option = DialogOption(box, i);
        if (i == selected) {
            surface.Fill(option, Fade(style::kDialogSelection, appear));
            surface.Outline(option, Fade(style::kPending, appear));
        }
        surface.Text(option.x + 10, option.y + 5, labels[i], Fade(i == selected ? style::kWhite : style::kText, appear));
    }
}

// ---- 入力の枠 ----
Rect Entry::Box(bool menu_open) const {
    switch (kind_) {
    case UiKind::Key: return Place(screen_width_, kKeyHeight, menu_open);
    case UiKind::List:
    case UiKind::CheckList: {
        const bool check = kind_ == UiKind::CheckList;
        return Place(screen_width_, 16 + Rows() * kRowHeight + (check ? 28 : 0), menu_open);
    }
    default: return Place(screen_width_, kNumberHeight, menu_open);
    }
}

int Entry::Rows() const {
    const int limit = kind_ == UiKind::CheckList ? kMaxCheckRows : kMaxListRows;
    return count_ < limit ? count_ : limit;
}

void Entry::Begin(const Engine &engine, const Input &input, int screen_width, bool touch_enabled) {
    const UiRequest request = engine.Ui();
    active_ = request.kind != UiKind::None;
    kind_ = request.kind;
    item_ = request.item;
    screen_width_ = screen_width;
    touch_ = touch_enabled;
    prev_held_ = input.held;
    prev_touch_ = input.touch.down;
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

void Entry::Step(Engine &engine, u32 now, const Input &input, bool menu_open) {
    if (!active_) return;
    if (engine.Ui().kind == UiKind::None) {
        active_ = false;
        return;
    }
    const u16 pressed = static_cast<u16>(input.held & ~prev_held_);
    const bool touch_down = touch_ && input.touch.down && !prev_touch_;
    switch (kind_) {
    case UiKind::Number:
    case UiKind::Slider:
    case UiKind::TriggerInput: StepNumber(engine, now, input, pressed, touch_down, menu_open); break;
    case UiKind::List:
    case UiKind::CheckList: StepList(engine, now, input, pressed, touch_down, menu_open); break;
    case UiKind::Key: StepKey(engine, now, input, touch_down, menu_open); break;
    case UiKind::None: break;
    }
    prev_held_ = input.held;
    prev_touch_ = input.touch.down;
}

void Entry::StepNumber(Engine &engine, u32 now, const Input &input, u16 pressed, bool touch_down, bool menu_open) {
    bool repeated[4];
    const unsigned fired = Fired(input.held, pressed, now, repeated);
    const s32 coarse = step_ * (DisplayFormat() == Format::Hex ? 16 : 10);
    if (fired & 1u) SetValue(value_ + coarse);       // 上
    if (fired & 2u) SetValue(value_ - coarse);       // 下
    if (fired & 4u) SetValue(value_ - step_);        // 左
    if (fired & 8u) SetValue(value_ + step_);        // 右
    if ((pressed & key::A) != 0) return Finish(engine, value_, now);
    if ((pressed & key::B) != 0) return Cancel(engine);
    if (!touch_) return;
    const Rect box = Box(menu_open);
    const Rect track = Track(box);
    const Rect grab{track.x, track.y - 14, track.w, 32};
    if (Touched(grab, input.touch) && max_ > min_) {
        // 触れている間、指の位置に合わせる。刻みに丸める。
        const float ratio = Clamp01(static_cast<float>(input.touch.x - track.x) / static_cast<float>(track.w));
        const s32 raw = min_ + Round(ratio * static_cast<float>(max_ - min_) / static_cast<float>(step_)) * step_;
        SetValue(raw);
    }
    if (!touch_down) return;
    if (Touched(NumberButton(box, 0), input.touch)) SetValue(value_ - step_);
    else if (Touched(NumberButton(box, 1), input.touch)) SetValue(value_ + step_);
    else if (Touched(NumberButton(box, 2), input.touch)) Finish(engine, value_, now);
    else if (Touched(NumberButton(box, 3), input.touch)) Cancel(engine);
}

void Entry::StepList(Engine &engine, u32 now, const Input &input, u16 pressed, bool touch_down, bool menu_open) {
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
    if (!touch_ || !touch_down) return;
    const Rect box = Box(menu_open);
    for (int row = 0; row < Rows(); ++row) {
        if (!Touched(ListRow(box, row), input.touch)) continue;
        const int target = top_ + row;
        if (check) {
            index_ = target;
            mask_ ^= 1u << target;
        } else if (target == index_) {
            return Finish(engine, index_, now);
        } else {
            index_ = target;
        }
        return;
    }
    if (check && Touched(ListButton(box, 0), input.touch)) Finish(engine, static_cast<s32>(mask_), now);
    else if (check && Touched(ListButton(box, 1), input.touch)) Cancel(engine);
}

void Entry::StepKey(Engine &engine, u32 now, const Input &input, bool touch_down, bool menu_open) {
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
    if (!touch_ || !touch_down) return;
    const Rect box = Box(menu_open);
    if (Touched(KeyButton(box, 0), input.touch)) Finish(engine, 0, now);
    else if (Touched(KeyButton(box, 1), input.touch)) Cancel(engine);
}

void Entry::Draw(Surface &surface, float menu_amount, float appear, bool menu_open) const {
    if (!active_ || appear <= 0.0f) return;
    (void)menu_open;
    Rect box = Box(true);
    const Rect centered = Box(false);
    box.x = Round(Mix(static_cast<float>(centered.x), static_cast<float>(box.x), menu_amount)) +
            Round((1.0f - appear) * static_cast<float>(kSlideDistance));
    switch (kind_) {
    case UiKind::Key: DrawKey(surface, box, appear); break;
    case UiKind::List:
    case UiKind::CheckList: DrawList(surface, box, appear); break;
    case UiKind::None: break;
    default: DrawNumber(surface, box, appear); break;
    }
}

void Entry::DrawNumber(Surface &surface, const Rect &box, float appear) const {
    DrawBox(surface, box, style::kBoxBackground, style::kAccent, appear);
    char text[kLineBytes];
    FormatNumber(DisplayFormat(), value_, text, sizeof(text));
    surface.Text(box.x + 8, box.y + 10, text, Fade(style::kPending, appear), true, 2);
    const Rect track = Track(box);
    const float progress = max_ > min_ ? static_cast<float>(value_ - min_) / static_cast<float>(max_ - min_) : 0.0f;
    surface.Fill(track, Fade(style::kSliderTrack, appear));
    surface.Fill({track.x, track.y, Round(static_cast<float>(track.w) * progress), track.h}, Fade(style::kSliderFill, appear));
    surface.Fill({track.x + Round(static_cast<float>(track.w) * progress) - 3, track.y - 5, 7, 14}, Fade(style::kSliderKnob, appear));
    DrawButton(surface, NumberButton(box, 0), "-", style::kKeyBackground, appear, 2);
    DrawButton(surface, NumberButton(box, 1), "+", style::kKeyBackground, appear, 2);
    DrawButton(surface, NumberButton(box, 2), "決定", style::kDialogSelection, appear);
    DrawButton(surface, NumberButton(box, 3), "取消", style::kKeyBackground, appear);
}

void Entry::DrawList(Surface &surface, const Rect &box, float appear) const {
    const bool check = kind_ == UiKind::CheckList;
    DrawBox(surface, box, style::kBoxBackground, style::kAccent, appear);
    char line[kLineBytes];
    char trimmed[kLineBytes];
    for (int row = 0; row < Rows(); ++row) {
        const int option = top_ + row;
        if (option >= count_) break;
        const Rect bounds = ListRow(box, row);
        if (option == index_) surface.Fill(bounds, Fade(style::kListSelection, appear));
        core::TextWriter writer(line, sizeof(line));
        if (check) writer.Append((mask_ >> option) & 1u ? "[X] " : "[ ] ");
        writer.Append(def_->options[option]);
        surface.Text(bounds.x + 8, bounds.y + 5, Trim(surface, line, bounds.w - 16 - (count_ > Rows() ? 6 : 0), trimmed, sizeof(trimmed)),
                     Fade(option == index_ ? style::kWhite : style::kText, appear));
    }
    if (count_ > Rows()) {
        const int height = Rows() * kRowHeight - 4;
        const int thumb = height * Rows() / count_ < 10 ? 10 : height * Rows() / count_;
        const float where = static_cast<float>(top_) / static_cast<float>(count_ - Rows());
        surface.Fill({box.x + box.w - 7, box.y + 8, 2, height}, Fade(style::kBarTrack, appear));
        surface.Fill({box.x + box.w - 7, box.y + 8 + Round(static_cast<float>(height - thumb) * where), 2, thumb}, Fade(style::kBarThumb, appear));
    }
    if (check) {
        DrawButton(surface, ListButton(box, 0), "決定", style::kDialogSelection, appear);
        DrawButton(surface, ListButton(box, 1), "取消", style::kKeyBackground, appear);
    }
}

void Entry::DrawKey(Surface &surface, const Rect &box, float appear) const {
    DrawBox(surface, box, style::kBoxBackground, style::kAccent, appear);
    const char *status = phase_ == Phase::Release ? "全てのボタンを離す" : phase_ == Phase::Wait ? "ボタンを押す" : "離すと決定";
    surface.Text(box.x + 8, box.y + 10, status, Fade(style::kWhite, appear));
    char text[kLineBytes];
    if (captured_ != 0) FormatHotkey(captured_, text, sizeof(text));
    else core::TextWriter(text, sizeof(text)).Append("--");
    surface.Text(box.x + 8, box.y + 28, text, Fade(style::kHotkey, appear));
    DrawButton(surface, KeyButton(box, 0), "無効", style::kKeyBackground, appear);
    DrawButton(surface, KeyButton(box, 1), "取消", style::kKeyBackground, appear);
}
} // namespace gohan::menu
