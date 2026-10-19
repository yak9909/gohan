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
    assert (len(atlas), len(widths), len(main), len(numeric)) == (65536, 2094, 670, 27)
    # 旧版との全要素照合で一致した出力に、メニューが使う「予」「約」「機」を足した後のハッシュ。
    # 足した字は文字表の末尾に置くので、既存の字形の番号は変わらない（数字用の写像だけ字形番号がずれる）。
    payloads = [atlas, bytes(widths), b''.join(struct.pack('<HH', *p) for p in main),
                b''.join(struct.pack('<HH', *p) for p in numeric)]
    expected = ['e464d528539c4a1e39a9c46296965301aa0563b606a5e4f41d2ce0f30b880898',
                'eba3b76820e7481669c08ab2cb59eaabb8ec9be67b42ef14dc3cf2b97f5dc208',
                'b26ffe9dd09854f12d86857b9013abe09f963f5144faf8cf005f6a24de9004e7',
                '877ad38556212e2ec345ad113c76c49cee449e0f2291fd5576a277ef89dbd5c2']
    assert [hashlib.sha256(data).hexdigest() for data in payloads] == expected
    a, b = tmp_path / 'first.hpp', tmp_path / 'second.hpp'
    font.generate(ROOT, a)
    font.generate(ROOT, b)
    assert a.read_bytes() == b.read_bytes()
