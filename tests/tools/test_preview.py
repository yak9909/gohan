"""自作の小画像で、向き・LA4・合成・切り抜き・比較・台本の境界を確かめる。"""
import base64
import json
from pathlib import Path
import struct
import sys
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from gohan_tools.preview.comparison import compare
from gohan_tools.preview.gallery import gallery
from gohan_tools.preview.images import Image, read, read_bmp, read_png, screen_image
from gohan_tools.preview.raster import Raster
from gohan_tools.preview.script import protocol


def model(**changes):
    return dict(offset=[0, 0], alpha_gain=1., rgb_gain=[1., 1., 1.],
                rounding='nearest', swap_red_blue=False, **changes)


def atlas(tmp_path):
    # 8×8 タイル内の左上だけに、輝度15/alpha8の非対称な画素を置く。
    (tmp_path / 'fonts.json').write_text(json.dumps(dict(
        sheet=[8, 8], cell=[2, 2], columns=1, widths=[0, 2, 3], main=[65, 0], numeric=[49, 0])))
    data = bytearray(64)
    data[3] = 0xF8  # (1,1) の Morton 番号
    data[6] = 0x8F  # (2,1)
    (tmp_path / 'atlas.la4').write_bytes(data)
    return Raster(tmp_path, model())


@pytest.mark.parametrize(('bits', 'top_down'), [(24, False), (24, True), (32, False), (32, True)])
def test_bmp_bgr_padding_and_vertical_direction(bits, top_down):
    width, height = 3, 2
    stride = ((width * bits + 31) // 32) * 4
    expected = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 1, 2, 3, 7, 8, 9, 11, 12, 13])
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            i = (y * width + x) * 3
            row.extend(expected[i:i + 3][::-1])
            if bits == 32:
                row.append(0)  # BI_RGB の予約バイトを alpha と解釈しない
        rows.append(row + bytes(stride - len(row)))
    dib = struct.pack('<IiiHHIIiiII', 40, width, -height if top_down else height, 1, bits, 0,
                      stride * height, 0, 0, 0, 0)
    data = b'BM' + struct.pack('<IHHI', 54 + stride * height, 0, 0, 54) + dib
    image = read_bmp(data + b''.join(rows if top_down else rows[::-1]))
    assert bytes(image.pixels) == expected
    with pytest.raises(ValueError, match='不足'):
        read_bmp(data)


def test_png_roundtrip_crc_and_crop(tmp_path):
    image = Image(2, 2, bytearray(range(12)))
    path = tmp_path / 'screen.png'
    image.save(path)
    assert read(path) == image
    assert image.crop(1, 0, 1, 2).pixels == bytearray([3, 4, 5, 9, 10, 11])
    bad = bytearray(image.png())
    bad[-1] ^= 1
    with pytest.raises(ValueError, match='CRC'):
        read_png(bad)
    with pytest.raises(ValueError):
        image.crop(-1, 0, 1, 1)


@pytest.mark.parametrize('mode', range(5))
def test_png_external_filters(mode):
    # 書き出し器が使わない Sub/Up/Average/Paeth も外部 PNG から読める。
    rows = [bytes([12, 34, 56, 45, 67, 89]), bytes([90, 80, 70, 60, 50, 40])]
    encoded = bytearray()
    previous = bytes(6)
    for row in rows:
        encoded.append(mode)
        for i, value in enumerate(row):
            a, b, c = row[i - 3] if i >= 3 else 0, previous[i], previous[i - 3] if i >= 3 else 0
            p = a + b - c
            predictor = (0, a, b, (a + b) // 2, min((a, b, c), key=lambda v: abs(p - v)))[mode]
            encoded.append((value - predictor) & 255)
        previous = row
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    data = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(encoded)) + chunk(b'IEND', b''))
    assert read_png(data).pixels == bytearray(b''.join(rows))


def test_screen_extraction():
    image = Image.solid(400, 480, (0, 0, 0))
    image.pixels[(240 * 400 + 40) * 3:(240 * 400 + 40) * 3 + 3] = bytes([1, 2, 3])
    assert screen_image(image, 'bottom').pixels[:3] == bytes([1, 2, 3])
    assert screen_image(image, 'top').height == 240
    with pytest.raises(ValueError):
        screen_image(Image.solid(800, 240, (0, 0, 0)), 'top')


def command(**changes):
    result = dict(kind=1, bounds=[0, 0, 3, 2], clip=[0, 0, 8, 8], color=0xFFFF0000,
                  scale=1, font=0, text=[65])
    result.update(changes)
    return result


def test_la4_channels_alpha_and_vertex_color(tmp_path):
    raster = atlas(tmp_path)
    image = raster.draw([command()], Image.solid(8, 8, (0, 0, 0)))
    assert image.pixels[:6] == bytes([136, 0, 0, 136, 0, 0])
    assert image.pixels[8 * 3:8 * 3 + 6] == bytes(6)  # 向き・拡大補間の誤りを検出
    image = raster.draw([command(kind=0, bounds=[0, 0, 1, 1], color=0x800000FF)], Image.solid(8, 8, (255, 0, 0)))
    assert image.pixels[:3] == bytes([127, 0, 128])


def test_glyph_clipping_and_numeric_scale(tmp_path):
    raster = atlas(tmp_path)
    background = Image.solid(8, 8, (0, 0, 0))
    assert raster.draw([command(clip=[0, 0, 2, 8])], background) == background
    assert raster.draw([command(clip=[0, 1, 8, 7])], background) == background
    image = raster.draw([command(bounds=[-3, 0, 6, 2], text=[65, 65])], background)
    assert image.pixels[:3] == bytes([136, 0, 0])  # 左に欠ける字だけ捨て、次の字を残す
    image = raster.draw([command(font=1, text=[49], scale=2, bounds=[0, 0, 6, 4])], background)
    assert image.pixels[:12] == bytes([136, 0, 0] * 4)
    assert image.pixels[8 * 3:8 * 3 + 12] == bytes([136, 0, 0] * 4)


def test_shift_and_luminance_metrics():
    a = Image.solid(12, 12, (0, 0, 0))
    b = Image.solid(12, 12, (0, 0, 0))
    a.pixels[(5 * 12 + 5) * 3:(5 * 12 + 5) * 3 + 3] = bytes([255, 32, 64])
    b.pixels[(4 * 12 + 7) * 3:(4 * 12 + 7) * 3 + 3] = bytes([255, 32, 64])
    report, diff, side = compare(a, b, 2)
    assert report['shift_capture'] == [2, -1]
    assert report['mean'] == 0 and report['mean_unshifted'] > 0
    assert not any(diff.pixels) and (side.width, side.height) == (36, 12)
    a = Image.solid(8, 8, (10, 10, 10))
    b = Image.solid(8, 8, (20, 20, 20))
    report, _, _ = compare(a, b, 1, luma=True)
    assert report['shift_capture'] == [0, 0]
    assert report['mean'] == pytest.approx(10)
    assert report['over_8_percent'] == 100 and report['over_24_percent'] == 0
    assert report['signed_luma_mean_capture_minus_preview'] == pytest.approx(10)
    with pytest.raises(ValueError):
        compare(a, b, 4)


def test_script_exact_times_and_persistent_inputs():
    scenario = dict(name='example', events=[dict(at=5, buttons=['Select']), dict(at=20, buttons=[])],
                    captures=[dict(at=13, name='half'), dict(at=32, name='end')])
    rows = [list(map(int, line.split())) for line in protocol(scenario, 16).splitlines()]
    assert [r[0] for r in rows] == [0, 5, 13, 16, 20, 32]
    assert [r[1] for r in rows] == [0, 4, 4, 4, 0, 0]
    assert rows[2][-1] == 0 and rows[-1][-1] == 1
    scenario['events'][0]['buttons'] = ['Typo']
    with pytest.raises(ValueError, match='ボタン'):
        protocol(scenario, 16)


def test_gallery_is_standalone_and_escaped():
    image = Image.solid(1, 1, (12, 34, 56))
    page = gallery([dict(label='<test>', at=123, top=image, bottom=image)], 'bottom')
    assert '&lt;test&gt;' in page and '<test>' not in page
    assert 'width="400"' in page and 'width="800"' in page
    assert 'width="320"' in page and 'width="640"' in page
    assert page.count('data:image/png;base64,') == 4 and 'https://' not in page
    encoded = page.split('data:image/png;base64,', 1)[1].split('"', 1)[0]
    assert read_png(base64.b64decode(encoded)) == image


def test_shipped_scenarios_are_valid():
    script = json.loads((ROOT / 'tools/preview/scenarios.json').read_text(encoding='utf-8'))
    for scenario in script['scenarios']:
        assert protocol(scenario, 16)
