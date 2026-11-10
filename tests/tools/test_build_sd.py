"""SD の名前表は入力を保ち、壊れた表は配らない。"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from gohan_tools.sd import build, validate

ROOT = Path(__file__).resolve().parents[2]


def test_copy_and_manifest(tmp_path):
    (tmp_path / 'spec').mkdir()
    original = (ROOT / 'spec/hidden_items.tsv').read_bytes()
    (tmp_path / 'spec/hidden_items.tsv').write_bytes(original)
    destination = build(tmp_path)
    assert destination.read_bytes() == original
    manifest = json.loads((tmp_path / 'out/manifest.json').read_text(encoding='utf-8'))
    row = manifest['files'][0]
    assert row['input'] == 'spec/hidden_items.tsv'
    assert row['output'] == 'sdmc/luma/plugins/gohan/0004000000086200/hidden_items.tsv'
    assert row['input_sha256'] == row['output_sha256'] == hashlib.sha256(original).hexdigest()


@pytest.mark.parametrize('text', [
    '0x0001\t0\tかな\n', '1\t0\tかな\t名前\n',
    '0x8000\t0\tかな\t名前\n', '0x0001\t0\tかな\t名前\t余分\n',
    '0x0001\t0\tかな\t名前\n0X1\t0\tかな\t重複\n',
    '0x0001\tx\tかな\t名前\n', '0x0001\t0\tかな\t\n',
    'id\ticon\tname\n0x0001\t0\tかな\t名前\n',
])
def test_bad_shape(text, tmp_path):
    with pytest.raises(ValueError):
        validate(text.encode())
    (tmp_path / 'spec').mkdir()
    (tmp_path / 'spec/hidden_items.tsv').write_bytes(text.encode())
    with pytest.raises(ValueError):
        build(tmp_path)
    assert not (tmp_path / 'out').exists()


def test_cli():
    subprocess.run([sys.executable, '-m', 'gohan_tools', 'build-sd'], cwd=ROOT, check=True)
