"""同梱 BDF から UI 用の字形表と A4 アトラスを作る（外部依存なし）。"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

if __package__:
    from .glyph_overrides import pixels as override_pixels
else:
    from glyph_overrides import pixels as override_pixels


@dataclass(frozen=True)
class Glyph:
    advance: int
    width: int
    height: int
    x: int
    y: int
    rows: tuple[int, ...]


def read_bdf(path: Path) -> dict[int, Glyph]:
    """BBX のビット列は MSB が左。文字コードは Unicode または Latin-1。"""
    lines = iter(path.read_text(encoding='ascii').splitlines())
    glyphs = {}
    declared = None
    code = advance = box = rows = None
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        key, *values = parts
        if key == 'CHARS':
            declared = int(values[0])
        elif key == 'STARTCHAR':
            code = advance = box = rows = None
        elif key == 'ENCODING':
            code = int(values[0])
        elif key == 'DWIDTH':
            advance = int(values[0])
        elif key == 'BBX':
            box = tuple(map(int, values))
            if len(box) != 4 or not 0 <= box[0] <= 32 or not 0 <= box[1] <= 32:
                raise ValueError('BDF の BBX が不正です')
        elif key == 'BITMAP':
            if box is None:
                raise ValueError('BBX がありません')
            rows = tuple(int(next(lines), 16) for _ in range(box[1]))
        elif key == 'ENDCHAR':
            if None in (code, advance, box, rows):
                raise ValueError('BDF の字形の欄が不足しています')
            if code in glyphs:
                raise ValueError('BDF の文字コードが重複しています')
            glyphs[code] = Glyph(advance, *box, rows)
    if declared != len(glyphs):
        raise ValueError('BDF の字形数が一致しません')
    return glyphs


def raster(glyph: Glyph, baseline: int) -> tuple[list[list[int]], int]:
    pixels = [[0] * 7 for _ in range(8)]
    stride = (glyph.width + 7) // 8 * 8
    for row, bits in enumerate(glyph.rows):
        y = baseline - glyph.y - glyph.height + row
        for col in range(glyph.width):
            x = glyph.x + col
            if bits & (1 << (stride - col - 1)):
                if not 0 <= x < 7 or not 0 <= y < 8:
                    raise ValueError('字形が UI の 7x8 セルに入りません')
                pixels[y][x] = 255
    right = max((x + 1 for row in pixels for x, bit in enumerate(row) if bit), default=0)
    return pixels, right


def morton(x: int, y: int) -> int:
    return sum(((x >> bit) & 1) << (2 * bit) |
               ((y >> bit) & 1) << (2 * bit + 1) for bit in range(3))


def pixel_offset(x: int, y: int, width: int = 256) -> int:
    return ((y // 8) * (width // 8) + x // 8) * 64 + morton(x & 7, y & 7)


def build(root: Path) -> tuple[bytes, list[int], list[tuple[int, int]], list[tuple[int, int]]]:
    here = Path(__file__).resolve().parent
    sources = [root / 'third_party/fonts/misaki/misaki_gothic_2nd.bdf',
               root / 'third_party/fonts/pixelmplus/mplus_j10r-iso-W5.bdf']
    maps = []
    cells = []
    widths = []
    for source, name, baseline in zip(sources, ('misaki', 'numeric'), (7, 7)):
        bdf = read_bdf(source)
        characters = (here / (name + '_characters.txt')).read_text(encoding='utf-8').rstrip('\n')
        if len(set(characters)) != len(characters):
            raise ValueError('採用する文字が重複しています')
        mapping = []
        for char in characters:
            glyph = bdf[ord(char)]
            pixels, right = raster(glyph, baseline)
            fixed = override_pixels(char) if name == 'misaki' else None
            advance = glyph.advance
            if fixed:
                pixels, advance = fixed
                right = advance - 1
            mapping.append((ord(char), len(cells)))
            cells.append(pixels)
            # 数値用は字形の輪郭によらず 5px の等幅セルを指定する。
            widths.extend((0, 5 if name == 'numeric' else right, advance))
        maps.append(sorted(mapping))
    cells.append([[255] * 7 for _ in range(8)])
    widths.extend((0, 7, 7))
    if len(cells) > 32 * 28:
        raise ValueError('アトラスの容量を超えています')
    atlas = bytearray(256 * 256 // 2)
    for index, pixels in enumerate(cells):
        x0, y0 = (index % 32) * 8 + 1, (index // 32) * 9 + 1
        for y, row in enumerate(pixels):
            for x, value in enumerate(row):
                if value not in (0, 255):
                    raise ValueError('A4 への変換前の字形は二値である必要があります')
                offset = pixel_offset(x0 + x, y0 + y)
                atlas[offset // 2] |= (value // 17) << ((offset & 1) * 4)
    return bytes(atlas), widths, *maps


def generate(root: Path, output: Path) -> None:
    atlas, widths, misaki, numeric = build(root)
    lines = ['// 生成物。font_atlas.py が同梱 BDF から作る。', '#pragma once',
             '#include <cstdint>', '', 'namespace gohan::ui::renderer::font_data {',
             'inline constexpr unsigned SheetWidth = 256, SheetHeight = 256;',
             'inline constexpr unsigned CellWidth = 7, CellHeight = 8, Columns = 32;',
             f'inline constexpr unsigned SolidIndex = {len(widths) // 3 - 1};']
    def array(typ: str, name: str, values) -> None:
        lines.append(f'inline constexpr {typ} {name}[] = {{')
        for offset in range(0, len(values), 24):
            lines.append('    ' + ','.join(str(x) for x in values[offset:offset + 24]) + ',')
        lines.append('};')
    array('std::uint8_t', 'Widths', widths)
    for name, mapping in [('MisakiMap', misaki), ('NumericMap', numeric)]:
        array('std::uint16_t', name, [value for pair in mapping for value in pair])
    array('std::uint8_t', 'Atlas', atlas)
    lines.append('} // namespace gohan::ui::renderer::font_data')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--output', type=Path, default=Path('build/gen/font_atlas.hpp'))
    args = parser.parse_args()
    generate(args.root, args.output)


if __name__ == '__main__':
    main()
