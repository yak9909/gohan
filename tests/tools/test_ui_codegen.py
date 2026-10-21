"""CA2 の設計表・自作の型紙・RGBA8・部品の範囲を検査する。"""
from pathlib import Path
import re
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from gohan_tools.codegen import ui
from gohan_tools.codegen.font_atlas import pixel_offset


def test_design_covers_all_ca2_tables():
    values = ui.load(ROOT / 'spec/ui.toml')
    section = (ROOT / 'docs/design/menu-ui.md').read_text(encoding='utf-8').split('### D.2 ')[1].split('### D.10 ')[0]
    # 各表の名前は直接の値、または複合値の子を持つ。
    for line in section.splitlines():
        if line.startswith('| `ca2.'):
            name = re.match(r'\| `ca2\.([^`]+)`', line)[1]
            assert name in values or any(k.startswith(name + '.') for k in values), name
    assert values['list.rect'] == [4, 4, 228, 210]
    assert values['budget.plan'] == [269, 269, 76024, 64, 64, 123132, 124518]
    assert len(values['glyph.override']) == 23


def test_ui_is_bounded_reproducible_and_contains_all_icons(tmp_path):
    values = ui.load(ROOT / 'spec/ui.toml')
    images, atlas, dash_h, dash_v = ui.build(values)
    assert len(atlas) == values['atlas.bytes'] == 32768
    occupied = set()
    for name, item in images.items():
        x, y, w, h = item['rect']
        assert 0 <= x < x + w <= 256 and 0 <= y < y + h <= 32
        area = {(xx, yy) for yy in range(y, y + h) for xx in range(x, x + w)}
        assert occupied.isdisjoint(area), name
        occupied |= area
    for palette in ('bar', 'paper'):
        assert all(f'key.{palette}.{key}' in images for key in ('a', 'b', 'x', 'y', 'l', 'r', 'up', 'down', 'left', 'right', 'ud', 'lr', 'plus'))
    for destination in (tmp_path / 'a', tmp_path / 'b'):
        ui.generate(ROOT / 'spec/ui.toml', destination)
    for filename in ('ui_values.hpp', 'ui_parts.hpp', 'ui_atlas.hpp'):
        assert (tmp_path / 'a' / filename).read_bytes() == (tmp_path / 'b' / filename).read_bytes()
    def pixel(data, x, y):
        p = pixel_offset(x, y, 8) * 4
        return int.from_bytes(data[p:p + 4], 'little')
    assert all(pixel(dash_h, x, y) == (0xFFFFFFFF if x < 4 else 0) for y in range(8) for x in range(8))
    assert all(pixel(dash_v, x, y) == pixel(dash_h, y, x) for y in range(8) for x in range(8))


def test_dpad_center_and_finger_direction():
    images = ui.sprites(ui.load(ROOT / 'spec/ui.toml'))
    center = images['key.bar.ud']['pixels'][5][5]
    assert center == ui.rgba(0xFFF4F8EC)
    assert images['key.bar.ud']['pixels'][1][5] == ui.rgba(0xFFE08A00)
    assert images['key.bar.ud']['pixels'][9][5] == ui.rgba(0xFFE08A00)
    # 指先は右側の長い突き出し。元の表の左右反転を保持する。
    from gohan_tools.codegen.ui_shapes import FINGER
    assert images['finger']['pixels'][0] == ui.ascii_image([FINGER[0][::-1]], {
        'K': ui.rgba(0xFF5A3A1E), 'W': ui.rgba(0xFFFFFFFF)})[0]


def test_overflow_is_an_error():
    with pytest.raises(ValueError, match='容量'):
        ui.pack({'large': dict(pixels=[[(0,) * 4] * 9 for _ in range(9)])}, 8, 8)


def part_color_keys(name):
    """型紙の実装とは別に、部品が使う設計表の色を列挙する。"""
    fixed = {
        'window': ['color.paper.edge', 'color.paper.inner', 'color.paper.fill'],
        'notice.window': ['color.paper.edge', 'color.paper.inner', 'color.notice.paper'],
        'band3': ['color.band.edge', 'color.band.fill'],
        'band9': ['color.band.edge', 'color.band.fill'],
        'selrim': ['color.sel.band.edge', 'color.sel.band.inner', 'color.sel.band.fill'],
        'disband': ['color.disabled.sel.edge', 'color.disabled.sel.fill'],
        'bar': ['color.bar.edge', 'color.bar.fill'],
        'track': ['color.track.fill'],
        'scroll.track': ['color.scroll.track'],
        'scroll.thumb': ['color.scroll.edge', 'color.scroll.thumb'],
        'knob': ['color.track.edge', 'color.track.knob'],
        'field': ['color.field.edge', 'color.field.fill'],
        'box': ['color.field.edge', 'color.field.fill'],
        'finger': ['cursor.image.edge', 'cursor.image.fill'],
        'hotkey.selected': ['color.switch.knob', 'hotkey.mark.color'],
    }
    if name in fixed:
        return fixed[name]
    if name.startswith('pill.'):
        state = name.split('.')[1]
        pending = state.startswith('pending_')
        state = state.removeprefix('pending_')
        return ([f'color.switch.{state}', 'color.switch.run_knob' if state == 'run' else 'color.switch.knob'] +
                (['color.pending.edge', 'color.pending.inner'] if pending else [f'color.switch.{state}_edge']))
    if name.startswith('button.'):
        return ['color.' + name]
    if name.startswith('key.'):
        return ['icon.color.' + name.split('.')[1]]
    # 単色の印は白い型に頂点色を掛ける。
    return ['color.switch.knob']


VALUES = ui.load(ROOT / 'spec/ui.toml')


@pytest.mark.parametrize('name', sorted(ui.sprites(VALUES)))
def test_every_part_color_survives_device_format(name):
    images, data, _, _ = ui.build(VALUES)
    palette = set()
    for key in part_color_keys(name):
        value = VALUES[key]
        # ボタンの配列の末尾は字色で、型紙には描き込まない。
        if key.startswith('color.button.'):
            value = value[:2]
        palette.update(map(ui.rgba, value if isinstance(value, list) else [value]))
    if name.startswith('key.'):
        fill, ink, edge, accent = map(ui.rgba, VALUES['icon.color.' + name.split('.')[1]])
        if name.endswith('.plus'):
            palette = {fill}
        elif name.split('.')[-1] in ('a', 'b', 'x', 'y', 'l', 'r'):
            palette = {fill, ink, edge}
        else:
            palette = {fill, edge, accent, tuple(max(0, c - VALUES['icon.dpad'][4]) for c in accent[:3]) + (255,)}
    palette.add((0, 0, 0, 0))
    x, y, w, h = images[name]['rect']
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            # 生成器から独立した Morton / ABGR の復号。元の型紙同士の比較で済ませない。
            morton = sum(((xx >> i) & 1) << (2 * i) | ((yy >> i) & 1) << (2 * i + 1) for i in range(3))
            offset = ((yy // 8 * (VALUES['atlas.size'][0] // 8) + xx // 8) * 64 + morton) * 4
            actual = tuple(data[offset:offset + 4][::-1])
            assert any(all(abs(a - b) <= 2 for a, b in zip(actual, expected)) for expected in palette), (name, xx, yy, actual)
            assert actual == images[name]['pixels'][yy - y][xx - x], (name, xx, yy)


def test_rgba8_keeps_asymmetric_channels_and_alpha():
    pixels = [[(0, 0, 0, 0)] * 8 for _ in range(8)]
    pixels[1][2] = (248, 244, 230, 242)
    data = ui.encode(pixels)
    assert data[24:28] == bytes((242, 230, 244, 248))
