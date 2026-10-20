"""CA2 の設計値と、自作の UI アトラスを生成する（ROM・追加ライブラリ不要）。"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import tomllib

if __package__:
    from . import ui_shapes as shapes
    from .font_atlas import pixel_offset
else:
    import ui_shapes as shapes
    from font_atlas import pixel_offset


def identifier(name):
    return ''.join(part.title() for part in name.split('_'))


def load(path):
    values = tomllib.loads(path.read_text(encoding='utf-8'))['ca2']
    for key, value in values.items():
        if not re.fullmatch(r'[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)*', key):
            raise ValueError(f'設計値の名前が不正です: {key}')
        if isinstance(value, list):
            if not value or any(type(x) not in (int, float, str) for x in value):
                raise ValueError(f'設計値の配列が不正です: {key}')
            if any(type(x) is str for x in value) and not all(type(x) is str for x in value):
                raise ValueError(f'設計値の型が混ざっています: {key}')
        elif type(value) not in (int, float, str, bool):
            raise ValueError(f'設計値の型が不正です: {key}')
    return values


def values_header(values):
    lines = ['// 生成物。spec/ui.toml を編集してください。', '#pragma once',
             '#include <array>', '#include <cstdint>', 'namespace gohan::core::design::ca2 {']
    def literal(value):
        if isinstance(value, str):
            return json.dumps(value, ensure_ascii=False)
        if type(value) is bool:
            return str(value).lower()
        return str(value)
    for key, value in sorted(values.items()):
        *parents, leaf = map(identifier, key.split('.'))
        if parents:
            lines.append('namespace ' + '::'.join(parents) + ' {')
        items = value if isinstance(value, list) else [value]
        typ = ('const char *' if isinstance(items[0], str) else
               'bool' if type(items[0]) is bool else
               'double' if any(type(x) is float for x in items) else
               'std::uint32_t' if any(x > 2147483647 for x in items) else 'int')
        if isinstance(value, list):
            lines.append(f'inline constexpr std::array<{typ}, {len(items)}> {leaf} = {{' +
                         ', '.join(map(literal, items)) + '};')
        else:
            lines.append(f'inline constexpr {typ} {leaf} = {literal(value)};')
        if parents:
            lines.append('}')
    return '\n'.join([*lines, '}']) + '\n'


def rgba(argb):
    return tuple((argb >> shift) & 255 for shift in (16, 8, 0, 24))


def box(w, h, radius, layers, fill):
    """ドット絵の円で角を落とす。縁からの距離で内縁を描く。"""
    pixels = [[(0, 0, 0, 0)] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            cx = min(max(x, radius), w - 1 - radius) if radius else x
            cy = min(max(y, radius), h - 1 - radius) if radius else y
            if (x != cx or y != cy) and radius:
                dist = math.hypot(x - cx, y - cy)
                if dist > radius + .2:
                    continue
                depth = radius + .5 - dist
            else:
                depth = min(x, y, w - 1 - x, h - 1 - y) + .5
            pixels[y][x] = next((color for i, color in enumerate(layers) if depth < i + 1), fill)
    return pixels


def ascii_image(rows, palette):
    return [[palette.get(c, (0, 0, 0, 0)) for c in row] for row in rows]


def sprites(v):
    result = {}
    white = (255,) * 4
    def color(key):
        return rgba(v['color.' + key])
    def add(name, pixels, mode=0, cap=0):
        result[name] = dict(pixels=pixels, mode=mode, cap=cap)
    def part(name, key, fill, layers, mode, radius=None):
        w, h, cap = v['part.' + key]
        image = box(w, h, cap if radius is None else radius, layers, fill)
        add(name, image, mode, cap)
        return image
    part('window', 'window', color('paper.fill'), [color('paper.edge'), color('paper.inner')], 9)
    part('notice.window', 'window', color('notice.paper'), [color('paper.edge'), color('paper.inner')], 9)
    for name, mode in [('band3', 3), ('band9', 9)]:
        part(name, name, color('band.fill'), [color('band.edge')], mode)
    rim = part('selrim', 'selrim', color('sel.band.fill'),
               [color('sel.band.edge'), color('sel.band.inner')], 3)
    for y in range(2, len(rim) - 2):
        rim[y][len(rim[0]) // 2] = (0, 0, 0, 0)
    part('disband', 'disband', color('disabled.sel.fill'), [color('disabled.sel.edge')], 3)
    part('switch.track', 'switch_track', white, [], 3)
    part('switch.inner', 'switch_inner', white, [], 3)
    for name, size in [('switch.knob', v['row.switch.knob']), ('switch.knob_inner', v['row.switch.knob_inner'])]:
        add(name, box(size, size, size // 2, [], white))
    for state in ('normal', 'reserved', 'ok'):
        fill, edge, _ = map(rgba, v['color.button.' + state])
        part('button.' + state, 'button', fill, [edge], 3)
    part('bar', 'bar', color('bar.fill'), [color('bar.edge')], 3)
    part('track', 'scroll', color('track.fill'), [color('track.fill')], 3)
    part('scroll.track', 'scroll', color('scroll.track'), [], 4)
    part('scroll.thumb', 'scroll', color('scroll.thumb'), [color('scroll.edge')], 4)
    knob_size = v['entry.number.thumb']
    add('knob', box(knob_size, knob_size, knob_size // 2, [color('track.edge')], color('track.knob')), 3, knob_size // 2)
    for name, mode in [('field', 3), ('box', 9)]:
        part(name, name, color('field.fill'), [color('field.edge')], mode)
    # 元の指の表を左右反転すると、指先が右になる。
    add('finger', ascii_image([row[::-1] for row in shapes.FINGER],
                             {'K': rgba(v['cursor.image.edge']), 'W': rgba(v['cursor.image.fill'])}))
    for name in ('CHEVRON', 'ARROW_L', 'ARROW_R', 'ARROW_U', 'ARROW_D', 'CHECK', 'BANG', 'LOCK', 'DOT'):
        add(name.lower(), ascii_image(getattr(shapes, name), {'W': white}))
    add('hotkey', ascii_image(['WWWWW', 'WWWW.', 'WWW..', 'WW...', 'W....'], {'W': white}))
    add('hotkey.selected', ascii_image(['WWWWWW', 'WBBBW.', 'WBBW..', 'WBW...', 'WW....', 'W.....'],
                                      {'W': white, 'B': rgba(v['hotkey.mark.color'])}))
    # 見本で使わない組合せも生成し、割り当て可能な全方向・全ボタンを揃える。
    for palette in ('bar', 'paper'):
        fill, ink, edge, accent = map(rgba, v['icon.color.' + palette])
        for key in 'ABXYLR':
            w, h, radius = v['icon.button.round' if key in 'ABXY' else 'icon.button.shoulder']
            image = box(w, h, radius, [edge], fill)
            for y, row in enumerate(shapes.LETTER[key]):
                for x, c in enumerate(row):
                    if c == 'W':
                        image[y + 2][(3 if w == 11 else 4) + x] = ink
            add(f'key.{palette}.{key.lower()}', image)
        for name, arms in [('up', 'u'), ('down', 'd'), ('left', 'l'), ('right', 'r'), ('ud', 'ud'), ('lr', 'lr')]:
            size, _, arm, _, darken = v['icon.dpad']
            lo, hi = (size - arm) // 2, (size + arm) // 2
            def inside(x, y):
                return 0 <= x < size and 0 <= y < size and (lo <= x < hi or lo <= y < hi)
            image = [[(0, 0, 0, 0)] * size for _ in range(size)]
            for y in range(size):
                for x in range(size):
                    if not inside(x, y):
                        continue
                    region = 'u' if y < lo else 'd' if y >= hi else 'l' if x < lo else 'r' if x >= hi else 'c'
                    border = any(not inside(x + dx, y + dy) for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1)])
                    image[y][x] = ((tuple(max(0, c - darken) for c in accent[:3]) + (255,) if border else accent)
                                   if region in arms else edge if border else fill)
            add(f'key.{palette}.{name}', image)
        add(f'key.{palette}.plus', ascii_image(shapes.PLUS, {'W': fill}))
    return result


def pack(images, width, height):
    """最も低い置き場に詰める。ニアレストなので部品間の余白は不要。"""
    free = [(0, 0, width, height)]
    for name, item in sorted(images.items(), key=lambda p: (-len(p[1]['pixels']), -len(p[1]['pixels'][0]), p[0])):
        h, w = len(item['pixels']), len(item['pixels'][0])
        candidates = [(min(fw - w, fh - h), max(fw - w, fh - h), y, x)
                      for x, y, fw, fh in free if fw >= w and fh >= h]
        if not candidates:
            raise ValueError('UI アトラスの容量を超えています')
        _, _, y, x = min(candidates)
        item['rect'] = [x, y, w, h]
        remaining = []
        for fx, fy, fw, fh in free:
            if x >= fx + fw or x + w <= fx or y >= fy + fh or y + h <= fy:
                remaining.append((fx, fy, fw, fh))
                continue
            if x > fx:
                remaining.append((fx, fy, x - fx, fh))
            if x + w < fx + fw:
                remaining.append((x + w, fy, fx + fw - x - w, fh))
            if y > fy:
                remaining.append((fx, fy, fw, y - fy))
            if y + h < fy + fh:
                remaining.append((fx, y + h, fw, fy + fh - y - h))
        remaining = list(dict.fromkeys(remaining))
        free = [r for r in remaining if not any(r != q and q[0] <= r[0] and q[1] <= r[1] and
                q[0] + q[2] >= r[0] + r[2] and q[1] + q[3] >= r[1] + r[3] for q in remaining)]


def encode(pixels):
    h, w = len(pixels), len(pixels[0])
    data = bytearray(w * h * 4)
    for y, row in enumerate(pixels):
        for x, (r, g, b, a) in enumerate(row):
            # RGBA8: メモリの先頭から A, B, G, R。8×8 タイル内は Morton 順。
            offset = pixel_offset(x, y, w) * 4
            data[offset:offset + 4] = bytes((a, b, g, r))
    return bytes(data)


def build(values):
    images = sprites(values)
    w, h = values['atlas.size']
    pack(images, w, h)
    sheet = [[(0, 0, 0, 0)] * w for _ in range(h)]
    for item in images.values():
        x, y, _, _ = item['rect']
        for j, row in enumerate(item['pixels']):
            sheet[y + j][x:x + len(row)] = row
    dw, dh, dots = values['part.dash']
    dash = [[(255,) * 4 if x < dots else (0,) * 4 for x in range(dw)] for _ in range(dh)]
    return images, encode(sheet), encode(dash), encode(list(map(list, zip(*dash))))


def generate(spec, out):
    values = load(spec)
    images, atlas, dash_h, dash_v = build(values)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'ui_values.hpp').write_text(values_header(values), encoding='utf-8')
    lines = ['// 生成物。ui.py が自作の型紙から作る。', '#pragma once',
             'namespace gohan::ui::renderer::ui_data {', 'enum class Part : unsigned {']
    names = sorted(images)
    for name in names:
        lines.append('    ' + identifier(name.replace('.', '_')) + ',')
    lines += ['    Count', '};', 'struct PartInfo { int x, y, width, height, mode, cap; };',
              'inline constexpr PartInfo Parts[] = {']
    for name in names:
        i = images[name]
        lines.append('    {' + ', '.join(map(str, [*i['rect'], i['mode'], i['cap']])) + '}, // ' + name)
    lines += ['};', 'inline constexpr const char *PartNames[] = {']
    lines += ['    ' + json.dumps(name) + ',' for name in names]
    lines += ['};', '}']
    (out / 'ui_parts.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    lines = ['// 生成物。RGBA8 の自作 UI と繰り返し画像。', '#pragma once', '#include <cstdint>',
             'namespace gohan::ui::renderer::ui_data {']
    for name, data in [('Atlas', atlas), ('DashH', dash_h), ('DashV', dash_v)]:
        lines.append(f'inline constexpr std::uint8_t {name}[] = {{')
        lines.extend('    ' + ','.join(map(str, data[i:i + 32])) + ',' for i in range(0, len(data), 32))
        lines.append('};')
    lines.append('}')
    (out / 'ui_atlas.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spec', type=Path, default=Path('spec/ui.toml'))
    parser.add_argument('--out', type=Path, default=Path('build/gen'))
    args = parser.parse_args()
    generate(args.spec, args.out)
