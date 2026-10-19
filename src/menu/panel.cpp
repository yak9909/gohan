#include "menu/panel.hpp"
#include "core/format.hpp"
#include "menu/hotkey.hpp"
#include "menu/layout.hpp"
#include <cmath>
#include <cstring>

namespace gohan::menu {
namespace {
using namespace motion;
constexpr u32 kHoldMuted = style::Argb(0xFF, 0x74, 0x7B, 0x76);
constexpr float kTwoPi = 6.2831853f;

float Pulse(u32 now, float base, float swing, float low, float high) {
    const float phase = kTwoPi * static_cast<float>(now % timing::kPulseMs) / static_cast<float>(timing::kPulseMs);
    const float value = base + swing * std::sin(phase);
    return value < low ? low : (value > high ? high : value);
}

// 行の文字色。灰色（無効）・黄色（適用待ち）・白（選択中）・既定。
u32 RowColor(const RowView &view, bool selected) {
    return view.disabled ? style::kDisabled : view.pending ? style::kPending : selected ? style::kWhite : style::kText;
}

// 値の文字色。固定中は水色。ON・動作中・OK は緑、待機は青で、状態を色でも見分けられるようにする。
u32 ValueColor(const RowView &view, u32 row_color) {
    if (view.locked) return style::kLocked;
    if (view.disabled || view.pending) return row_color;
    const char *value = view.value;
    if (!std::strcmp(value, "ON") || !std::strcmp(value, "OK") || !std::strcmp(value, "動作中") ||
        !std::strcmp(value, "起動中"))
        return style::kAccent;
    if (!std::strcmp(value, "待機")) return style::kHotkey;
    return row_color;
}

bool NumericControl(const ItemDef &def) { return def.control == Control::Number || def.control == Control::Slider; }
} // namespace

void Panel::AddHit(Rect bounds, u16 key) {
    if (hit_count_ < kMaxHits) hits_[hit_count_++] = {bounds, key};
}

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

int Panel::RowAt(int y, u32 now) const {
    if (y < layout::kListTop || y >= layout::kListTop + rows_ * style::kItemHeight) return -1;
    const int index = static_cast<int>(std::floor(view_.At(now) + static_cast<float>(y - layout::kListTop) /
                                                                       static_cast<float>(style::kItemHeight)));
    return index >= 0 && index < count_ ? index : -1;
}

bool Panel::InPanel(int x, u32 now) const { return x >= MenuX(now) && x < MenuX(now) + style::kMenuWidth - 2; }

bool Panel::InList(int x, int y, u32 now) const {
    return InPanel(x, now) && y >= layout::kListTop && y < layout::kListTop + rows_ * style::kItemHeight;
}

void Panel::Draw(Surface &surface, const Engine &engine, u32 now, bool modal) {
    hit_count_ = 0;
    if (open_.At(now) <= 0.0f && open_.to <= 0.0f) return;
    const int menu_x = MenuX(now);
    DrawFrame(surface, engine, menu_x, now);
    DrawRows(surface, engine, menu_x, now);
    DrawScrollBar(surface, menu_x, now);
    if (modal) return;
    DrawFooter(surface, engine, menu_x);
    DrawHoldBar(surface, engine, menu_x, now);
}

void Panel::DrawFrame(Surface &surface, const Engine &engine, int menu_x, u32 now) {
    (void)now;
    surface.Fill({menu_x, 0, style::kMenuWidth, kScreenHeight}, style::kPanel);
    surface.Fill({menu_x + style::kMenuWidth - 2, 0, 2, kScreenHeight}, style::kBorder);
    surface.Fill({menu_x, 0, style::kMenuWidth - 2, layout::kHeaderHeight}, style::kHeader);
    surface.Fill({menu_x, layout::kFootTop, style::kMenuWidth - 2, kScreenHeight - layout::kFootTop}, style::kFoot);
    // 題と、今のフォルダの名前を 1 つの文字にする。
    constexpr const char *kTitle = "-* GOHAN *-";
    char text[kLineBytes];
    char trimmed[kLineBytes];
    core::TextWriter writer(text, sizeof(text));
    writer.Append(kTitle);
    if (engine.Folder() != kNoItem) {
        const int room = style::kMenuWidth - 2 - 14 - surface.Measure(kTitle) - surface.Measure(" ");
        writer.Append(" ").Append(Trim(surface, engine.Def(engine.Folder()).label, room, trimmed, sizeof(trimmed)));
    }
    surface.Text(menu_x + 7, 7, text, style::kWhite);
}

void Panel::DrawRows(Surface &surface, const Engine &engine, int menu_x, u32 now) {
    const float start = view_.At(now);
    const int last_row_y = layout::kFirstRowY + (rows_ - 1) * style::kItemHeight + 2;
    const float action = 3.0f * action_.At(now, timing::kActivationBounceMs, 0.30f);
    const float value = static_cast<float>(value_.direction) * 4.0f * value_.At(now, timing::kValueBounceMs, 0.28f);

    // 行の領域（帯の幅 x 行数）の外へは描かない。跳ねた帯や文字がはみ出しても切れる。
    const bool clipped = surface.BeginClip({menu_x, layout::kListTop, style::kMenuWidth - 2, rows_ * style::kItemHeight});
    // 選択の帯。窓の中に収める。
    if (count_ > 0) {
        const float span = static_cast<float>(rows_ - 1);
        const float row = Clamp01((selection_.At(now) - start) / span) * span;
        const int y = Round(static_cast<float>(layout::kFirstRowY) + row * static_cast<float>(style::kItemHeight));
        const Rect band{menu_x + 4 + Round(value + action), y - 3, style::kMenuWidth - 10, 15};
        surface.Fill(band, WithAlpha(style::kSelectionFill, Pulse(now, 0.48f, 0.07f, 0.0f, 1.0f)));
        surface.Outline(band, WithAlpha(style::kSelectionLine, Pulse(now, 0.7f, 0.2f, 0.5f, 0.9f)));
    }
    const int first = static_cast<int>(std::floor(start));
    for (int index = first < 0 ? 0 : first; index <= first + rows_ && index < count_; ++index) {
        const int y = Round(static_cast<float>(layout::kFirstRowY) + (static_cast<float>(index) - start) * static_cast<float>(style::kItemHeight));
        if (y < layout::kListTop || y > last_row_y) continue;
        const bool selected = items_[index] == engine.Selected();
        DrawRow(surface, engine, items_[index], menu_x, y, selected ? Round(action) : 0, selected);
    }
    if (clipped) surface.EndClip();
}

void Panel::DrawRow(Surface &surface, const Engine &engine, u16 item, int menu_x, int y, int offset, bool selected) {
    const RowView view = engine.View(item);
    if (!view.def) return;
    const ItemDef &def = *view.def;
    const u32 color = RowColor(view, selected);
    const bool numeric = NumericControl(def);
    const bool has_value = view.value[0] != '\0';
    const int value_width = has_value ? surface.Measure(view.value, numeric) : 0;
    const int value_x = menu_x + style::kMenuWidth - 8 - value_width + offset;
    const bool folder = def.control == Control::Folder;
    const int label_x = menu_x + (folder ? 8 : 12) + offset;
    // 余裕がある時だけ飾る（予算の最後まで残したい物を先に描いておく）。
    const bool spare = surface.RectsLeft() > 16;

    if (spare && def.control != Control::Folder && engine.State(item).hotkey != 0)
        surface.Fill({menu_x + 4 + offset, y - 2, 1, 12}, WithAlpha(style::kHotkey, 0.6f));
    char text[kLineBytes];
    char trimmed[kLineBytes];
    const int room = (has_value ? value_x - 8 : menu_x + style::kMenuWidth - 12 + offset) - label_x;
    core::TextWriter writer(text, sizeof(text));
    if (folder) writer.Append(">");
    writer.Append(Trim(surface, def.label, room - (folder ? surface.Measure(">") : 0), trimmed, sizeof(trimmed)));
    surface.Text(label_x, y, text, color);
    if (!has_value) return;
    if (spare && view.button) {
        const u32 edge = view.disabled ? style::kDisabled : view.pending ? style::kPending : selected ? style::kAccent : style::kPanelEdge;
        surface.Outline({value_x - 4, y - 3, value_width + 8, 14}, edge);
    }
    surface.Text(value_x, y, view.value, ValueColor(view, color), numeric);
}

void Panel::DrawScrollBar(Surface &surface, int menu_x, u32 now) {
    if (count_ <= rows_ || surface.RectsLeft() < 7) return;
    const int height = rows_ * style::kItemHeight - 4;
    int thumb = height * rows_ / count_;
    if (thumb < 10) thumb = 10;
    const int top = layout::kFirstRowY + Round(static_cast<float>(height - thumb) * Clamp01(view_.At(now) / MaxViewStart()));
    surface.Fill({menu_x + 154, layout::kFirstRowY, 2, height}, style::kBarTrack);
    surface.Fill({menu_x + 154, top, 2, thumb}, style::kBarThumb);
}

void Panel::DrawFooter(Surface &surface, const Engine &engine, int menu_x) {
    struct Segment {
        const char *text;
        u16 key;
    };
    constexpr Segment kSegments[] = {{"A決定", key::A}, {"X適用", key::X}, {"L戻し", key::L},
                                     {"B戻る", key::B}, {"Yキー", key::Y}};
    char line[kLineBytes];
    core::TextWriter writer(line, sizeof(line));
    const int left = menu_x + 6;
    const int gap = surface.Measure(" ");
    for (const Segment &segment : kSegments) {
        if (writer.Size() > 0) writer.Append(" ");
        const int offset = surface.Measure(std::string_view(line, writer.Size()));
        writer.Append(segment.text);
        AddHit({left + offset, layout::kFootTop, surface.Measure(segment.text) + gap, 12}, segment.key);
    }
    surface.Text(left, 220, line, style::kFootText);
    const std::size_t changed = engine.DirtyCount();
    char count[24];
    core::TextWriter(count, sizeof(count)).Unsigned(static_cast<u32>(changed)).Append("変更");
    surface.Text(left, 230, count, changed ? style::kPending : style::kFootOff);
}

void Panel::DrawHoldBar(Surface &surface, const Engine &engine, int menu_x, u32 now) {
    u32 permille = 0;
    bool muted = false;
    u16 held_key = 0;
    if (!engine.HoldProgress(now, &permille, &muted, &held_key)) return;
    if (surface.RectsLeft() < 7 || surface.TextsLeft() < 1) return;
    const Rect bar{menu_x + 5, layout::kHoldBarY, style::kMenuWidth - 12, 14};
    const u32 color = muted ? kHoldMuted : style::kPending;
    surface.Fill(bar, style::kHoldBackground);
    surface.Outline(bar, color);
    surface.Text(bar.x + 5, bar.y + 2, held_key == key::X ? "全適用" : "全戻し", style::kWhite);
    surface.Fill({bar.x + 5, bar.y + 10, bar.w - 10, 2}, style::kHoldTrack);
    surface.Fill({bar.x + 5, bar.y + 10, Round(static_cast<float>(bar.w - 10) * static_cast<float>(permille) / 1000.0f), 2}, color);
}

void Panel::DrawDescription(Surface &surface, const Engine &engine, u32 now) {
    const float amount = open_.At(now);
    const u16 selected = engine.Selected();
    if (amount <= 0.0f || selected == kNoItem || surface.TextsLeft() < 1 || surface.RectsLeft() < 5) return;
    const ItemDef &def = engine.Def(selected);
    const int width = layout::SideWidth(surface.Width());
    const int x = layout::kSideX;
    const int y = layout::kSideTop;

    char lines[style::kDescriptionLines][kLineBytes];
    int count = 0;
    const int room = surface.TextsLeft() - 1;
    if (room > 0 && def.desc && def.desc[0])
        count = Wrap(surface, def.desc, width - 16, lines, room < style::kDescriptionLines ? room : style::kDescriptionLines, nullptr);

    const Rect box{x, y, width, 27 + count * 11};
    surface.Fill(box, Fade(style::kBoxBackground, amount));
    surface.Outline(box, Fade(style::kPanelEdge, amount));
    char text[kLineBytes];
    core::TextWriter writer(text, sizeof(text));
    writer.Append("HOTKEY:");
    if (def.control == Control::Folder) writer.Append("--");
    else FormatHotkey(engine.State(selected).hotkey, text + writer.Size(), sizeof(text) - writer.Size());
    surface.Text(x + 8, y + 7, text, Fade(style::kHotkey, amount));
    for (int i = 0; i < count; ++i)
        surface.Text(x + 8, y + 20 + i * 11, lines[i], Fade(style::kBody, amount));
}
} // namespace gohan::menu
