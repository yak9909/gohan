"""BDF の位置、タイルの並び、採用版の再現性を検査する。"""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
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
    assert (len(atlas), len(widths), len(main), len(numeric)) == (65536, 2085, 667, 27)
    # 旧版との全要素照合で一致した出力のハッシュ。旧版のファイルは試験に不要。
    payloads = [atlas, bytes(widths), b''.join(struct.pack('<HH', *p) for p in main),
                b''.join(struct.pack('<HH', *p) for p in numeric)]
    expected = ['8f8353059c2ec5dde6311a9d7a34afc309adf147e0e5b81e0e115c59b1cfd912',
                'cb2de4e997354959cdfc265e79731e4df486ea3538c0904b8404164a00ccf6c4',
                'a5d98ad670770c8339e1231954342f63cf911ba97bc138b53b1d1fc08a5ba376',
                'c5fe15b9c13bf9241d61cec059beafe35288c221b85d4ade682c6be5707d1faa']
    assert [hashlib.sha256(data).hexdigest() for data in payloads] == expected
    a, b = tmp_path / 'first.hpp', tmp_path / 'second.hpp'
    font.generate(ROOT, a)
    font.generate(ROOT, b)
    assert a.read_bytes() == b.read_bytes()
