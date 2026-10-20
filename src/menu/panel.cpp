#include "menu/panel.hpp"
#include "core/format.hpp"
#include "menu/hotkey.hpp"
#include "menu/layout.hpp"
#include "menu/value_text.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace gohan::menu {
namespace {
using namespace motion;
bool NumericControl(const ItemDef &def) { return def.control == Control::Number || def.control == Control::Slider; }
} // namespace

float Panel::TargetFor(int index) const {
    const int high = count_ > rows_ ? count_ - rows_ : 0;
    int target = index - rows_ / 2;
    target = target < 0 ? 0 : (target > high ? high : target);
    return static_cast<float>(target);
}

void Panel::Sync(const Engine &engine, u32 now) {
    count_ = 0;
    for (u16 item = engine.FirstVisible(engine.Folder()); item != kNoItem && count_ < static_cast<int>(core::limits::MenuItems);
         item = engine.NextVisible(item))
        items_[count_++] = item;
    int index = 0;
    while (index < count_ && items_[index] != engine.Selected()) ++index;
    if (index >= count_) index = 0;

    const bool visible = engine.Visible();
    if (visible != was_visible_) {
        open_.Go(visible ? 1.0f : 0.0f, now, timing::kEnterMs);
        was_visible_ = visible;
        if (visible) {
            selection_.Jump(static_cast<float>(index), now);
            view_.Jump(TargetFor(index), now);
            folder_ = engine.Folder();
            selected_ = engine.Selected();
        }
    }
    if (!visible) return;
    if (engine.Folder() != folder_) {
        // フォルダの出入りは、動かさずに新しい一覧の選択へ合わせる。
        selection_.Jump(static_cast<float>(index), now);
        view_.Jump(TargetFor(index), now);
    } else if (engine.Selected() != selected_) {
        selection_.Go(static_cast<float>(index), now, timing::kSelectionMoveMs);
        const float start = view_.to;
        if (!keep_view_ || static_cast<float>(index) < start || static_cast<float>(index) >= start + static_cast<float>(rows_))
            view_.Go(TargetFor(index), now, timing::kScrollMs);
    }
    keep_view_ = false;
    folder_ = engine.Folder();
    selected_ = engine.Selected();
}

void Panel::SetRows(int rows, u32 now) {
    if (rows == rows_) return;
    rows_ = rows;
    int index = 0;
    while (index < count_ && items_[index] != selected_) ++index;
    if (index >= count_) index = 0;
    view_.Jump(TargetFor(index), now);
}

bool Panel::Animating(u32 now) const {
    return !open_.Done(now) || !selection_.Done(now) || !view_.Done(now) || value_.Active(now, timing::kValueBounceMs) ||
           action_.Active(now, timing::kActivationBounceMs);
}

int Panel::MenuX(u32 now) const {
    return Round(-static_cast<float>(style::kMenuWidth) + static_cast<float>(style::kMenuWidth) * open_.At(now));
}

void Panel::Draw(Surface &surface, const Engine &engine, u32 now, bool modal) {
    if (open_.At(now) <= 0.0f && open_.to <= 0.0f) return;
    const int menu_x = MenuX(now);
    DrawFrame(surface, engine, menu_x, now);
    DrawRows(surface, engine, menu_x, now);
    DrawScrollBar(surface, menu_x, now);
    (void)modal;

}


void Panel::DrawFrame(Surface &s, const Engine &e, int dx, u32) {
    Paper(s, Bounds(ca::List::Rect, dx));
    const Rect title = Bounds(ca::List::Title::Rect, dx + ca::List::Rect[0], ca::List::Rect[1], ca::List::Rect[2]);
    s.Box(title, "band3");
    int index = 0;
    while (index < count_ && items_[index] != e.Selected()) ++index;
    char pos[24]; core::TextWriter(pos, sizeof(pos)).Unsigned(count_ ? index + 1 : 0).Append("/").Unsigned(count_);
    const int right = dx + ca::List::Rect[0] + ca::List::Rect[2] + ca::List::Title::Pos::Right;
    const int py = title.y + ca::List::Title::Text[1];
    s.Text(right - s.Measure(pos, true), py, pos, ca::Color::Band::Text, true);
    u16 ancestors[timing::kMaxDepth]{}; int depth = 0;
    for (u16 at = e.Folder(); at != kNoItem && depth < static_cast<int>(timing::kMaxDepth); at = e.Def(at).parent) ancestors[depth++] = at;
    char path[kLineBytes], trimmed[kLineBytes];
    core::TextWriter w(path, sizeof(path)); w.Append(ca::Text::Title);
    for (int n = depth - 1; n >= 0; --n) w.Append(ca::Text::PathSeparator).Append(e.Def(ancestors[n]).label);
    const int x = title.x + ca::List::Title::Text[0];
    const int room = right - s.Measure(pos, true) - ca::Row::Gap::Min - x;
    if (s.Measure(path) > room && depth) {
        core::TextWriter(path, sizeof(path)).Append(ca::Trim::Ellipsis).Append(ca::Text::PathSeparator).Append(e.Def(e.Folder()).label);
    }
    s.Text(x, py, Trim(s, path, room, trimmed, sizeof(trimmed)), ca::Color::Band::Text);
}

void Panel::DrawRows(Surface &s, const Engine &e, int dx, u32 now) {
    const float start = view_.At(now);
    const int action = Round(ca::Time::Bounce::Confirm[1] * action_.At(now, timing::kActivationBounceMs, .30f));
    const int value = Round(value_.direction * ca::Time::Bounce::Value[1] * value_.At(now, timing::kValueBounceMs, .28f));
    const int clipTop = ca::Row::Top + ca::Hotkey::Mark::SelectedOffset[1];
    const int clipBottom = ca::List::Rect[1] + ca::List::Rect[3] - ca::Margin;
    const bool clipped = s.BeginClip({dx + ca::List::Rect[0], clipTop, ca::List::Rect[2], clipBottom - clipTop});
    if (count_) {
        const float row = std::clamp(selection_.At(now) - start, 0.f, float(rows_ - 1));
        const int y = Round(ca::Row::Top + row * ca::Row::Pitch);
        const Rect band{dx + ca::Row::X + value + action, y, ca::Row::W, ca::Row::H};
        Selected(s, band, e.View(e.Selected()).disabled);
        s.Sprite(band.x + ca::Cursor::Offset[0] + ca::Cursor::Sway[(now % ca::Time::Cursor) >= unsigned(ca::Time::Cursor / 2)],
                 band.y + ca::Cursor::Offset[1], "finger");
    }
    const int first = int(std::floor(start));
    for (int n = first < 0 ? 0 : first; n <= first + rows_ && n < count_; ++n) {
        const int y = Round(ca::Row::Top + (n - start) * ca::Row::Pitch);
        if (y + ca::Row::H < clipTop || y >= clipBottom) continue;
        const bool selected = items_[n] == e.Selected();
        DrawRow(s, e, items_[n], dx, y, selected ? action + value : 0, selected);
        s.Pattern({dx + ca::Row::Rule[0], y + ca::Row::H + ca::Row::Rule[2],
                   ca::Row::Rule[1] - ca::Row::Rule[0] + 1, ca::Row::Rule[3]}, "dash", ca::Color::Rule);
    }
    if (clipped) s.EndClip();
}

void Panel::DrawRow(Surface &s, const Engine &e, u16 item, int dx, int y, int offset, bool selected) {
    const RowView v = e.View(item); if (!v.def) return;
    const auto &d = *v.def;
    const int x = dx + ca::Row::X + offset;
    const int right = x + ca::Row::W + ca::Row::Value::Right;
    const bool numeric = NumericControl(d) || d.control == Control::Multiselect;
    const bool pill = d.control == Control::Toggle || v.button;
    const int vw = pill ? ca::Row::Pill::Size[0] : s.Measure(v.value, numeric);
    const int vx = pill ? right - vw : right + ca::Row::Value::Text[0] - vw;
    const int lx = x + ca::Row::Label[0], ly = y + ca::Row::Label[1];
    const u32 ink = selected ? (v.disabled ? ca::Color::Disabled::Sel::Text : ca::Color::Sel::Text) :
                    v.disabled ? ca::Color::Disabled::Text : v.pending ? ca::Color::Pending::Text : ca::Color::Text;
    char trimmed[kLineBytes];
    const auto label = Trim(s, d.label, (v.value[0] ? vx : right - ca::Mark::Other::Chevron[0]) - lx - ca::Row::Gap::Min -
                           (v.pending ? ca::Row::Pending::Mark[2] + ca::Row::Pending::Mark[0] : 0), trimmed, sizeof(trimmed));
    Ink(s, lx, ly, label, selected, ink, false, v.disabled ? ca::Color::Disabled::Sel::Shadow : ca::Color::Sel::Shadow);
    if (v.pending) s.Sprite(lx + s.Measure(label) + ca::Row::Pending::Mark[0], y + ca::Row::Pending::Mark[1], "dot",
                             selected ? ca::Color::Pending::SelectedDot : ca::Color::Pending::Dot);
    if (e.State(item).hotkey && d.control != Control::Folder)
        s.Sprite(x + (selected ? ca::Hotkey::Mark::SelectedOffset[0] : 0), y + (selected ? ca::Hotkey::Mark::SelectedOffset[1] : 0),
                 selected ? "hotkey.selected" : "hotkey", selected ? 0xFFFFFFFFu : ca::Hotkey::Mark::Color);
    if (d.control == Control::Folder) {
        s.Sprite(right + ca::Row::Folder::Mark[0], y + ca::Row::Folder::Mark[1], "chevron", ink); return;
    }
    if (pill) {
        std::string part;
        u32 text = ca::Color::Switch::Text;
        int tx = vx;
        if (v.button) {
            const int state = !std::strcmp(v.value, "予約") ? 1 : (!std::strcmp(v.value, "OK") || !std::strcmp(v.value, "起動中")) ? 2 : 0;
            part = state == 1 ? "button.reserved" : state == 2 ? "button.ok" : "button.normal";
            text = state == 1 ? ca::Color::Button::Reserved[2] : state == 2 ? ca::Color::Button::Ok[2] : ca::Color::Button::Normal[2];
            tx += (vw - s.Measure(v.value)) / 2;
        } else {
            const bool off = !std::strcmp(v.value, "OFF");
            part = !std::strcmp(v.value, "待機") ? "pill.wait" : !std::strcmp(v.value, "動作中") ? "pill.run" :
                   v.pending ? (off ? "pill.pending_off" : "pill.pending_on") : off ? "pill.off" : "pill.on";
            tx += ca::Row::Pill::Text[off ? 0 : 1] + (ca::Row::Pill::Text[2] - s.Measure(v.value)) / 2;
        }
        const float alpha = v.disabled && !selected ? ca::Color::Disabled::Alpha : 1.f;
        s.Box({vx, y + ca::Row::Pill::Y, vw, ca::Row::Pill::Size[1]}, part, Fade(0xFFFFFFFFu, alpha));
        s.Text(tx, y + ca::Row::Pill::Y + ca::Row::Pill::Text[3], v.value, Fade(text, alpha));
    } else {
        Ink(s, vx, y + ca::Row::Value::Text[1], v.value, selected, v.locked ? ca::Color::Locked : ink, numeric, v.disabled ? ca::Color::Disabled::Sel::Shadow : ca::Color::Sel::Shadow);
        if (selected) {
            s.Sprite(right + ca::Row::Value::ArrowRight[0], y + ca::Row::Value::ArrowRight[1], "arrow_r", ink);
            s.Sprite(vx + ca::Row::Value::ArrowLeft, y + ca::Row::Value::ArrowRight[1], "arrow_l", ink);
        }
    }
    if (v.locked) s.Sprite(vx + ca::Row::Lock::Mark[0], y + ca::Row::Lock::Mark[1], "lock", ca::Color::Locked);
}

void Panel::DrawScrollBar(Surface &s, int dx, u32 now) {
    if (count_ <= rows_) return;
    const auto &g = ca::Scroll::Groove;
    const int h = g[1] - g[0] + 1;
    const int thumb = std::max(ca::Scroll::ThumbMin, h * rows_ / count_);
    const int top = g[0] + Round((h - thumb) * Clamp01(view_.At(now) / MaxViewStart()));
    s.Box({dx + ca::Scroll::X, g[0], g[2], h}, "scroll.track");
    s.Box({dx + ca::Scroll::X, top, g[2], thumb}, "scroll.thumb");
    s.Sprite(dx + ca::Scroll::X, ca::Scroll::Arrows[0], "arrow_u", ca::Color::Scroll::Arrow);
    s.Sprite(dx + ca::Scroll::X, ca::Scroll::Arrows[1], "arrow_d", ca::Color::Scroll::Arrow);
}

void Panel::DrawFooter(Surface &s, const Engine &e, int dx) {
    const Rect bar = Bounds(ca::Bar::Rect, dx);
    s.Box(bar, "bar");
    struct Hint { u16 key; const char *label; };
    Hint hints[6]; int count = 0;
    auto hint = [&](u16 key, const char *label) { hints[count++] = {key, label}; };
    const auto kind = e.Ui().kind;
    char fine[24], coarse[24];
    if (e.Dialog() != DialogKind::None) {
        hint(key::Left | key::Right, "えらぶ"); hint(key::A, "決定"); hint(key::B, "取消");
    } else if (kind == UiKind::Key) {
        hint(key::A, "押してはなすと決定");
    } else if (kind == UiKind::List || kind == UiKind::CheckList) {
        hint(key::Up | key::Down, "えらぶ"); hint(key::A, kind == UiKind::CheckList ? "切替" : "決定");
        if (kind == UiKind::CheckList) hint(key::X, "決定");
        hint(key::B, "取消");
    } else if (kind != UiKind::None) {
        const auto &d = e.Def(e.Ui().item);
        char number[16]; FormatNumber(d.format, d.step > 0 ? d.step : 1, number, sizeof(number));
        core::TextWriter(fine, sizeof(fine)).Append("±").Append(number);
        FormatNumber(d.format, (d.step > 0 ? d.step : 1) * (d.format == Format::Hex ? 16 : 10), number, sizeof(number));
        core::TextWriter(coarse, sizeof(coarse)).Append("±").Append(number);
        hint(key::Left | key::Right, fine); hint(key::Up | key::Down, coarse); hint(key::A, "決定"); hint(key::B, "取消");
    } else {
        hint(key::A, "決定");
        if (e.Selected() != kNoItem) {
            const auto control = e.Def(e.Selected()).control;
            if (control == Control::Number || control == Control::Slider || control == Control::Select) hint(key::Left | key::Right, "値");
        }
        hint(key::B, "戻る"); hint(key::X, "適用"); hint(key::L, "戻し"); hint(key::Y, "キー");
    }
    int x = bar.x + ca::Bar::Hint::Icon[0];
    int last = 0;
    for (int n = 0; n < count; ++n) {
        const auto &h = hints[n];
        x = KeyIcons(s, x, bar.y + ca::Bar::Hint::Icon[1], h.key, true) + ca::Bar::Hint::LabelGap;
        const int align = ca::Bar::Hint::Alignment;
        if (last) x += (align - (x - last) % align) % align;
        s.Text(x, bar.y + ca::Bar::Changed[1], h.label, ca::Color::Bar::Text);
        last = x; x += s.Measure(h.label) + ca::Bar::Hint::NextGap;
    }
    char changed[24]; core::TextWriter(changed, sizeof(changed)).Unsigned(e.DirtyCount()).Append(ca::Text::ChangedSuffix);
    s.Text(bar.x + bar.w + ca::Bar::Changed[0] - s.Measure(changed), bar.y + ca::Bar::Changed[1], changed,
           e.DirtyCount() ? ca::Color::Bar::Changed : ca::Color::Bar::Unchanged);
}

void Panel::DrawBar(Surface &s, const Engine &e, u32 now) {
    const int dx = Visible(now) ? MenuX(now) : 0;
    u32 progress; bool muted; u16 key;
    if (e.HoldProgress(now, &progress, &muted, &key)) DrawHoldBar(s, e, dx, now);
    else DrawFooter(s, e, dx);
}

void Panel::DrawHoldBar(Surface &s, const Engine &e, int dx, u32 now) {
    u32 progress = 0; bool muted = false; u16 key = 0;
    if (!e.HoldProgress(now, &progress, &muted, &key)) return;
    (void)muted;
    const Rect bar = Bounds(ca::Bar::Rect, dx);
    s.Box(bar, "bar");
    s.Text(bar.x + ca::Bar::Hold::Label[0], bar.y + ca::Bar::Hold::Label[1], key == key::X ? "全適用" : "全戻し", ca::Color::Bar::Text);
    const Rect track = Bounds(ca::Bar::Hold::Track, bar.x, bar.y, bar.w);
    s.Fill(track, ca::Color::Hold::Track);
    s.Fill({track.x + ca::Bar::Hold::Fill[0], track.y + ca::Bar::Hold::Fill[1], Round((track.w - 2) * progress / 1000.f), ca::Bar::Hold::Fill[2]},
            progress <= unsigned(ca::Time::Hold[1] * 1000 / ca::Time::Hold[0]) ? ca::Color::Hold::Early : ca::Color::Hold::Late);
    s.Text(track.x + track.w + ca::Bar::Hold::Cancel[0], bar.y + ca::Bar::Hold::Cancel[1], ca::Text::HoldCancel, ca::Color::Bar::Text);
}

void Panel::DrawDescription(Surface &s, const Engine &e, u32 now) {
    const float alpha = open_.At(now); const u16 item = e.Selected();
    if (alpha <= 0 || item == kNoItem) return;
    const auto &d = e.Def(item); const auto row = e.View(item);
    char title[ca::Desc::Title::Lines][kLineBytes], body[ca::Desc::Body[4]][kLineBytes];
    const int nt = Wrap(s, d.label, ca::Desc::Rect[2] - ca::Desc::Title::Text[0] * 2, title, ca::Desc::Title::Lines, nullptr);
    const int nb = Wrap(s, d.desc ? d.desc : "", ca::Desc::Body[5], body, ca::Desc::Body[4], nullptr);
    const int th = ca::Desc::Title::Rect[3] + ca::Desc::Title::Line * (nt > 0 ? nt - 1 : 0);
    const bool hotkey = d.control != Control::Folder && e.State(item).hotkey != 0;
    const char *state = row.disabled ? ca::Text::State::Disabled : !std::strcmp(row.value, "予約") ? ca::Text::State::Reserved :
                        row.pending ? ca::Text::State::Pending : row.locked ? ca::Text::State::Locked : !std::strcmp(row.value, "待機") ? ca::Text::State::Wait :
                        !std::strcmp(row.value, "ON") ? ca::Text::State::On : "";
    char states[ca::Desc::State[1]][kLineBytes];
    const int ns = Wrap(s, state, ca::Desc::Body[5], states, ca::Desc::State[1], nullptr);
    const bool extra = hotkey || state[0];
    const auto &h = ca::Desc::Height;
    const int height = h[0] + th + h[1] + h[2] * nb + (extra ? h[3] + (hotkey ? h[4] : 0) + ns * h[5] : 0) + h[6];
    Rect box = Bounds(ca::Desc::Rect); box.h = height;
    box.x += Round((1 - alpha) * (ca::Screen[0] - box.x));
    Paper(s, box, alpha);
    Rect band = Bounds(ca::Desc::Title::Rect, box.x, box.y, box.w); band.h = th;
    s.Box(band, "band9", Fade(0xFFFFFFFFu, alpha));
    for (int n = 0; n < nt; ++n) s.Text(box.x + ca::Desc::Title::Text[0], box.y + ca::Desc::Title::Text[1] + n * ca::Desc::Title::Line, title[n], Fade(ca::Color::Band::Text, alpha));
    int y = box.y + ca::Desc::Body[1] + th + ca::Desc::Body[2];
    for (int n = 0; n < nb; ++n) s.Text(box.x + ca::Desc::Body[0], y + n * ca::Desc::Body[3], body[n], Fade(ca::Color::Text, alpha));
    y += nb * ca::Desc::Body[3];
    if (extra) {
        s.Pattern({box.x + ca::Desc::Rule[0], y + ca::Desc::Rule[2], ca::Desc::Rule[1] - ca::Desc::Rule[0] + 1, 1}, "dash", Fade(ca::Color::Rule, alpha));
        y += h[3];
        if (hotkey) {
            s.Text(box.x + ca::Desc::Hotkey::Label[0], y + ca::Desc::Hotkey::Label[1], ca::Text::HotkeyLabel, Fade(ca::Color::Sub, alpha));
            if (e.State(item).hotkey) KeyIcons(s, box.x + ca::Desc::Hotkey::Icon[0], y + ca::Desc::Hotkey::Icon[1], e.State(item).hotkey);
            else s.Text(box.x + ca::Desc::Hotkey::Icon[0], y + ca::Desc::Hotkey::Label[1], "--", Fade(ca::Color::Sub, alpha));
            y += ca::Desc::Hotkey::Pitch;
        }
        const u32 stateColor = row.disabled ? ca::Color::Disabled::Text : row.locked ? ca::Color::Locked : row.pending ? ca::Color::Pending::Text : !std::strcmp(row.value, "ON") ? ca::Color::Green : ca::Color::Hotkey;
        for (int n = 0; n < ns; ++n) s.Text(box.x + ca::Desc::Body[0], y + n * ca::Desc::State[0], states[n], Fade(stateColor, alpha));
    }
}
} // namespace gohan::menu
