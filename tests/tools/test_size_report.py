"""自作の size 出力で、区分の選択と前回値の更新を確かめる。"""
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

SPEC = importlib.util.spec_from_file_location('size_report', Path(__file__).resolve().parents[2] / 'tools/size_report.py')
size_report = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(size_report)


def test_measure(monkeypatch):
    def run(command, **kwargs):
        assert command == ['fake-size', '-A', 'sample.elf']
        assert kwargs['check']
        return SimpleNamespace(stdout='sample.elf :\nsection size addr\n.text 100 0\n'
                               '.rodata 24 100\n.data 8 124\n.bss 16 132\n.debug_info 999 0\n')
    monkeypatch.setattr(size_report.subprocess, 'run', run)
    assert size_report.measure(Path('sample.elf'), 'fake-size') == {
        '.text': 100, '.rodata': 24, '.data': 8, '.bss': 16}


def test_first_and_delta(tmp_path, monkeypatch, capsys):
    sizes = {'.text': 100, '.rodata': 24, '.data': 8, '.bss': 16}
    monkeypatch.setattr(size_report, 'measure', lambda *args: sizes)
    last = tmp_path / 'build/size_last.json'
    size_report.report(Path('sample.elf'), 'unused', last)
    assert capsys.readouterr().out.count('初回') == 4
    assert json.loads(last.read_text()) == sizes
    sizes = {'.text': 110, '.rodata': 20, '.data': 8, '.bss': 15}
    size_report.report(Path('sample.elf'), 'unused', last)
    text = capsys.readouterr().out
    assert all(delta in text for delta in ('+10', '-4', '+0', '-1'))
    assert json.loads(last.read_text()) == sizes
    assert not last.with_suffix('.tmp').exists()


def test_invalid_previous_is_not_overwritten(tmp_path, monkeypatch):
    monkeypatch.setattr(size_report, 'measure', lambda *args: dict.fromkeys(size_report.SECTIONS, 1))
    last = tmp_path / 'size_last.json'
    last.write_text('{".text": true}')
    with pytest.raises(ValueError):
        size_report.report(Path('sample.elf'), 'unused', last)
    assert last.read_text() == '{".text": true}'
