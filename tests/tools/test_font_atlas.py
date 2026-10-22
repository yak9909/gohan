"""BDF の位置、タイルの並び、採用版の再現性を検査する。"""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/gohan_tools/codegen'))
SPEC = importlib.util.spec_from_file_location(
    'gohan_font_atlas', ROOT / 'tools/gohan_tools/codegen/font_atlas.py')
font = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = font
SPEC.loader.exec_module(font)


def test_bdf_position_and_negative_bearing(tmp_path):
    source = tmp_path / 'tiny.bdf'
    source.write_text('''STARTFONT 2.1
CHARS 2
STARTCHAR space
ENCODING 32
DWIDTH 4 0
BBX 0 0 0 0
BITMAP
ENDCHAR
STARTCHAR mark
ENCODING 65
DWIDTH 4 0
BBX 2 2 1 -1
BITMAP
80
C0
ENDCHAR
ENDFONT
''')
    glyphs = font.read_bdf(source)
    pixels, width = font.raster(glyphs[65], baseline=7)
    assert width == 3
    assert [(x, y) for y, row in enumerate(pixels) for x, bit in enumerate(row) if bit] == [
        (1, 6), (1, 7), (2, 7)]
    assert font.raster(glyphs[32], 7)[1] == 0
    source.write_text(source.read_text().replace('CHARS 2', 'CHARS 3'))
    with pytest.raises(ValueError, match='字形数'):
        font.read_bdf(source)


def test_glyph_overflow_is_rejected():
    glyph = font.Glyph(8, 1, 1, 7, 0, (128,))
    with pytest.raises(ValueError, match='セル'):
        font.raster(glyph, baseline=7)


def test_tile_boundaries_and_layout():
    assert [font.pixel_offset(x, 0) for x in range(9)] == [0, 1, 4, 5, 16, 17, 20, 21, 64]
    assert font.pixel_offset(0, 8) == 2048
    assert len({font.pixel_offset(x, y) for y in range(256) for x in range(256)}) == 65536


def test_adopted_fonts_are_reproducible(tmp_path):
    atlas, widths, main, numeric = font.build(ROOT)
    assert (len(atlas), len(widths), len(main), len(numeric)) == (32768, 2109, 674, 28)
    # CA2 の 23 字の上書きと追加の字を含む、採用した出力。
    # A4 を元の LA4 へ戻した全65,536画素のハッシュを比較する。
    expanded = bytes(((atlas[i // 2] >> ((i & 1) * 4)) & 15) * 17 for i in range(65536))
    assert set(expanded) == {0, 255}
    payloads = [expanded, bytes(widths), b''.join(struct.pack('<HH', *p) for p in main),
                b''.join(struct.pack('<HH', *p) for p in numeric)]
    expected = ['351971e45bcec98e931db89d8f0506d5f1209ef5104a5a158c921fe29945ed3b',
                '29b08546080d19e2e0c0e647fd748fd3f904def058338ab1c41425fd6d0ac4a8',
                'd6fe2bc0559ada7d7add5461f2cade3b9bf69ea411b888870529a9b17e68b65a',
                'bb968ac4507c5e7a7ee99de5d8863e0d36db0d5c4c49daf401abee9b7d77a7c2']
    assert [hashlib.sha256(data).hexdigest() for data in payloads] == expected
    a, b = tmp_path / 'first.hpp', tmp_path / 'second.hpp'
    font.generate(ROOT, a)
    font.generate(ROOT, b)
    assert a.read_bytes() == b.read_bytes()


def test_ca2_overrides_are_in_the_atlas():
    from glyph_overrides import OVERRIDES, pixels
    atlas, widths, main, numeric = font.build(ROOT)
    mapping = dict(main)
    assert len(OVERRIDES) == 23
    for char in OVERRIDES:
        glyph = mapping[ord(char)]
        expected, advance = pixels(char)
        assert widths[glyph * 3:glyph * 3 + 3] == [0, advance - 1, advance]
        x0, y0 = glyph % 32 * 8 + 1, glyph // 32 * 9 + 1
        assert [[((atlas[font.pixel_offset(x0 + x, y0 + y) // 2] >> ((font.pixel_offset(x0 + x, y0 + y) & 1) * 4)) & 15) * 17 for x in range(7)]
                for y in range(8)] == expected
    assert all(ord(c) in mapping for c in '±棒発省')
    slash = dict(numeric)[ord('/')]
    assert widths[slash * 3 + 2] == 5
