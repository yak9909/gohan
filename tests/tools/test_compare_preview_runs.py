"""前後の比較で、小さい差・撮影漏れ・寸法違いを見落とさない。"""
import importlib.util
import json
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from gohan_tools.preview.images import Image

SPEC = importlib.util.spec_from_file_location('compare_runs', ROOT / 'tools/compare_preview_runs.py')
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


def test_all_pixels_and_capture_inventory(tmp_path):
    before, after, out = (tmp_path / n for n in ('before', 'after', 'out'))
    a = Image.solid(2, 2, (10, 20, 30))
    b = Image.solid(2, 2, (10, 20, 30))
    b.pixels[5] += 1
    a.save(before / 'scene/open-top.png')
    b.save(after / 'scene/open-top.png')
    summary = module.compare_runs(before, after, out)
    assert summary['pixels'] == 4 and summary['changed_pixels'] == 1
    assert summary['maximum_channel_difference'] == 1
    assert (out / 'index.html').is_file()
    a.save(after / 'scene/extra-top.png')
    with pytest.raises(ValueError, match='一覧'):
        module.compare_runs(before, after, out)
    with pytest.raises(ValueError, match='寸法'):
        module.difference(a, Image.solid(1, 1, (0, 0, 0)))


def test_rom_comparison_stays_outside_workspace(tmp_path, monkeypatch):
    before, after = tmp_path / 'before', tmp_path / 'after'
    before.mkdir()
    (before / 'manifest.json').write_text(json.dumps({'title_background_romfs_configured': True}))
    monkeypatch.setattr(module, 'ROOT', tmp_path / 'repo')
    with pytest.raises(ValueError, match='作業ツリー外'):
        module.compare_runs(before, after, module.ROOT / 'build/comparison')
