"""自作の PNG で縮小・ETC1A4・DARC・SD 配布を試す。"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from gohan_tools import darc, etc1a4
from gohan_tools.hidden_icons import bclim, psnr
from gohan_tools.sd import build


def image():
    return [[(x * 3, y * 3, 90, 0 if x < 8 else 255) for x in range(64)] for y in range(64)]


def test_etc1a4_roundtrip(tmp_path):
    path = tmp_path / 'sample.png'
    source = image()
    etc1a4.write_png(path, source)
    w, h, back = etc1a4.read_png(path)
    assert (w, h, back) == (64, 64, source)
    small = etc1a4.downscale2(back)
    blob = etc1a4.encode(small, 32, 32)
    assert len(blob) == 1024
    decoded = etc1a4.decode(blob, 32, 32)
    assert psnr(small, decoded) >= 29
    assert all(a[3] == b[3] for row_a, row_b in zip(small, decoded) for a, b in zip(row_a, row_b))
    # 完全に透明な色を、見える色に混ぜない。
    assert etc1a4.downscale2([[(200, 20, 40, 255), (0, 250, 0, 0)],
                             [(0, 0, 250, 0), (250, 0, 250, 0)]]) == [[(200, 20, 40, 64)]]
    translucent = [[(50, 80, 120, (x + y) * 2) for x in range(32)] for y in range(32)]
    decoded = etc1a4.decode(etc1a4.encode(translucent, 32, 32), 32, 32)
    assert all(abs(a[3] - b[3]) <= 15 for ra, rb in zip(translucent, decoded) for a, b in zip(ra, rb))


def test_bclim():
    blob = bclim(bytes(1024))
    assert len(blob) == 1064
    assert struct.unpack_from('<4sHHIII4sIHHII', blob, 1024) == (
        b'CLIM', 0xFEFF, 20, 0x02020000, 1064, 1, b'imag', 16, 32, 32, 11, 1024)
    with pytest.raises(ValueError):
        bclim(bytes(1023))


def test_darc():
    files = {'timg/12.bclim': bclim(bytes([12]) * 1024), 'timg/3.bclim': bclim(bytes([3]) * 1024),
             'other/nested/a.txt': b'example'}
    blob = darc.pack(files)
    assert darc.read(blob) == files
    assert blob == darc.pack(dict(reversed(list(files.items()))))
    assert darc.read(darc.pack({})) == {}
    for offset in (0, 4, 6, 8, 12, 16, 20, 24, 28, 32, 36):
        bad = bytearray(blob)
        struct.pack_into('<I', bad, offset, 0xFFFFFFFF)
        with pytest.raises(ValueError):
            darc.read(bad)
    for size in range(28):
        with pytest.raises(ValueError):
            darc.read(blob[:size])


def fixture_root(tmp_path):
    (tmp_path / 'spec').mkdir()
    table = 'id\ticon\tkana\tname\n0x0001\t9\tあ\tあ\n0x0002\t3\tい\tい\n0x0003\t9\tう\tう\n0x0004\t0\tえ\tえ\n0x0005\t8\tお\tお\n'
    (tmp_path / 'spec/hidden_items.tsv').write_text(table, encoding='utf-8')
    return tmp_path


def test_no_png_setting(tmp_path, capsys):
    root = fixture_root(tmp_path)
    build(root)
    assert '未指定' in capsys.readouterr().out
    assert not list((root / 'out').rglob('*.arc'))
    assert len(json.loads((root / 'out/manifest.json').read_text())['files']) == 1


def test_only_tsv_icons(tmp_path, capsys):
    root = fixture_root(tmp_path)
    icons = root / 'icons'
    icons.mkdir()
    for number in (0, 3, 9, 100):
        etc1a4.write_png(icons / f'{number}.png', image())
    local = root / 'gohan.local.toml'
    local.write_text('[hidden_icons]\npng_directory = "icons"\n', encoding='utf-8')
    build(root)
    arc = root / 'out/sdmc/luma/plugins/gohan/common/hidden_item_icons.arc'
    files = darc.read(arc.read_bytes())
    assert set(files) == {'timg/3.bclim', 'timg/9.bclim'}
    assert all(len(blob) == 1064 for blob in files.values())
    row = json.loads((root / 'out/manifest.json').read_text(encoding='utf-8'))['files'][1]
    assert (row['count'], row['wanted'], row['missing']) == (2, 3, [8])
    assert row['bytes'] == arc.stat().st_size and row['worst_psnr'] >= 29
    assert row['output_sha256'] == hashlib.sha256(arc.read_bytes()).hexdigest()
    assert len(row['inputs']) == 2
    assert 'PNG が無い番号 [8]' in capsys.readouterr().out
    # 設定を外して作り直した時は、前回の arc も配布しない。
    local.unlink()
    build(root)
    assert not arc.exists()


def test_wrong_png_size(tmp_path):
    root = fixture_root(tmp_path)
    (root / 'icons').mkdir()
    etc1a4.write_png(root / 'icons/3.png', [[(1, 2, 3, 255)] * 32 for _ in range(32)])
    (root / 'gohan.local.toml').write_text('[hidden_icons]\npng_directory = "icons"\n', encoding='utf-8')
    with pytest.raises(ValueError, match='64×64'):
        build(root)
