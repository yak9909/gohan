#include "ui/renderer/internal.hpp"
#include "ui/renderer/selection_frames.hpp"
#include <limits>

namespace gohan::ui::renderer {
namespace {
bool ValidSource(Rect r, Texture texture) {
    const auto index = static_cast<unsigned>(texture);
    if (index >= Fonts().textureCount || r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0)
        return false;
    const auto &t = Fonts().textures[index];
    return std::int64_t(r.x) + r.width <= t.width && std::int64_t(r.y) + r.height <= t.height;
}
bool ValidBounds(Rect r) {
    return r.width >= 0 && r.height >= 0 &&
           std::int64_t(r.x) + r.width <= std::numeric_limits<int>::max() &&
           std::int64_t(r.y) + r.height <= std::numeric_limits<int>::max();
}
} // namespace

Result Canvas::AddImage(Rect bounds, Rect source, Texture texture, unsigned scale,
                        std::uint32_t color, bool repeat) {
    if (scale < 1 || scale > 8 || !ValidBounds(bounds))
        return Result::Invalid;
    if (!detail::Intersect(bounds, clip_).width || !(color >> 24))
        return Result::Empty;
    auto &current = Current();
    const auto result = detail::Reserve(current, false, 0);
    if (result != Result::Added)
        return result;
    auto &command = current.commands[current.count];
    command.kind = repeat ? game::lyt::Kind::Repeat : game::lyt::Kind::Image;
    command.bounds = bounds;
    command.clip = clip_;
    command.color = color;
    command.source = source;
    command.texture = texture;
    command.scale = scale;
    command.length = 0;
    command.slot = 0;
    detail::Account(current, false, 0);
    return Result::Added;
}

Result Canvas::Image(int x, int y, Rect source, unsigned scale, std::uint32_t color, Texture texture) {
    if (!ValidSource(source, texture) || scale < 1 || scale > 8)
        return Result::Invalid;
    return AddImage({x, y, source.width * static_cast<int>(scale), source.height * static_cast<int>(scale)},
                    source, texture, scale, color, false);
}

Result Canvas::Image(int x, int y, Part part, unsigned scale, std::uint32_t color) {
    if (part >= Part::Count)
        return Result::Invalid;
    const auto &p = ui_data::Parts[static_cast<unsigned>(part)];
    return Image(x, y, {p.x, p.y, p.width, p.height}, scale, color);
}

Result Canvas::Repeat(Rect bounds, Texture texture, int phaseX, int phaseY, unsigned scale,
                      std::uint32_t color) {
    const auto index = static_cast<unsigned>(texture);
    if (texture == Texture::Ui || index >= Fonts().textureCount)
        return Result::Invalid;
    if (texture == Texture::TitleSelection) {
        namespace s = core::design::ca2::Titlebg::Selection;
        if (bounds.width > s::CellSize[0] || bounds.height > s::CellSize[1] || scale != 1)
            return Result::Invalid;
        auto &current = Current();
        const auto count = current.count, rects = current.rectangles, bytes = current.estimatedBytes;
        const auto first = AddImage(bounds, {0, 0, bounds.width, bounds.height}, texture, 1, color, false);
        if (first != Result::Added) return first;
        const auto second = AddImage(bounds, {0, 0, bounds.width, bounds.height}, texture, 1, color, false);
        if (second != Result::Added) {
            current.count = count; current.rectangles = rects; current.estimatedBytes = bytes;
            return second;
        }
        current.commands[count].slot = detail::SelectionFirst;
        current.commands[count + 1].slot = detail::SelectionSecond;
        current.commands[count + 1].bounds.x += bounds.width;
        current.commands[count + 1].bounds.width = 0;
        return Result::Added;
    }
    const auto &t = Fonts().textures[index];
    const int w = static_cast<int>(t.width), h = static_cast<int>(t.height);
    return AddImage(bounds, {(phaseX % w + w) % w, (phaseY % h + h) % h, w, h},
                    texture, scale, color, true);
}

Result Canvas::Slice(Rect bounds, Rect source, int cap, unsigned scale, std::uint32_t color, int mode) {
    if (!ValidSource(source, Texture::Ui) || !ValidBounds(bounds) || cap < 1 || scale < 1 || scale > 8 ||
        ((mode == 3 || mode == 9) && (cap > (source.width - 1) / 2 || bounds.width < 2 * cap * static_cast<int>(scale))) ||
        ((mode == 4 || mode == 9) && (cap > (source.height - 1) / 2 || bounds.height < 2 * cap * static_cast<int>(scale))) ||
        (mode == 3 && bounds.height != source.height * static_cast<int>(scale)) ||
        (mode == 4 && bounds.width != source.width * static_cast<int>(scale)))
        return Result::Invalid;
    auto &current = Current();
    const auto count = current.count, rects = current.rectangles, bytes = current.estimatedBytes;
    const int sc = static_cast<int>(scale), edge = cap * sc;
    const int sw[] = {cap, source.width - 2 * cap, cap};
    const int sh[] = {cap, source.height - 2 * cap, cap};
    const int dw[] = {edge, bounds.width - 2 * edge, edge};
    const int dh[] = {edge, bounds.height - 2 * edge, edge};
    int sy = source.y, dy = bounds.y;
    Result overall = Result::Empty;
    for (int y = 0; y < (mode == 3 ? 1 : 3); ++y) {
        int sx = source.x, dx = bounds.x;
        const int srcH = mode == 3 ? source.height : sh[y], dstH = mode == 3 ? bounds.height : dh[y];
        for (int x = 0; x < (mode == 4 ? 1 : 3); ++x) {
            const int srcW = mode == 4 ? source.width : sw[x], dstW = mode == 4 ? bounds.width : dw[x];
            const auto result = AddImage({dx, dy, dstW, dstH}, {sx, sy, srcW, srcH}, Texture::Ui, scale, color, false);
            if (result != Result::Added && result != Result::Empty) {
                current.count = count;
                current.rectangles = rects;
                current.estimatedBytes = bytes;
                return result;
            }
            if (result == Result::Added)
                overall = Result::Added;
            sx += srcW;
            dx += dstW;
        }
        sy += srcH;
        dy += dstH;
    }
    return overall;
}

Result Canvas::NineSlice(Rect b, Rect s, int cap, unsigned scale, std::uint32_t color) {
    return Slice(b, s, cap, scale, color, 9);
}
Result Canvas::ThreeSliceHorizontal(Rect b, Rect s, int cap, unsigned scale, std::uint32_t color) {
    return Slice(b, s, cap, scale, color, 3);
}
Result Canvas::ThreeSliceVertical(Rect b, Rect s, int cap, unsigned scale, std::uint32_t color) {
    return Slice(b, s, cap, scale, color, 4);
}
Result Canvas::PartBox(Rect bounds, Part part, unsigned scale, std::uint32_t color) {
    if (part >= Part::Count)
        return Result::Invalid;
    const auto &p = ui_data::Parts[static_cast<unsigned>(part)];
    if (!p.mode)
        return Result::Invalid;
    return Slice(bounds, {p.x, p.y, p.width, p.height}, p.cap, scale, color, p.mode);
}
} // namespace gohan::ui::renderer
