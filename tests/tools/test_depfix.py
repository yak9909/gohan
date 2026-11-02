"""依存ファイルのドライブ付きパスを、make が読める形に書き直すことを確かめる。"""
import shutil
import subprocess
from pathlib import Path

import pytest

DEPFIX = Path(__file__).resolve().parents[2] / 'third_party/libctrpf/depfix.mk'

pytestmark = pytest.mark.skipif(
    not all(shutil.which(tool) for tool in ('make', 'grep', 'sed', 'xargs')),
    reason='make と POSIX の道具が要る')


def run_make(workdir: Path):
    makefile = workdir / 'test.mk'
    makefile.write_text(
        f'include {DEPFIX.as_posix()}\n'
        '$(call fix_deps,a.d b.d missing.d)\n'
        '-include a.d b.d\n'
        'a.o: ;\n'
        'all: a.o\n'
        '\t@echo ok\n')
    return subprocess.run(['make', '-f', 'test.mk', 'all'], cwd=workdir,
                          capture_output=True, text=True)


def test_drive_paths_are_rewritten(tmp_path):
    (tmp_path / 'a.d').write_text('a.o: \\\n A:/work/src/a.c \\\n C:devkit/include/x.h\n\nA:/work/src/a.c:\n\nC:devkit/include/x.h:\n')
    (tmp_path / 'b.d').write_text('b.o: src/b.c src/b.h\n')
    result = run_make(tmp_path)
    assert result.returncode == 0, result.stderr
    text = (tmp_path / 'a.d').read_text()
    assert ' /a/work/src/a.c' in text
    assert ' /c/devkit/include/x.h' in text
    assert 'A:' not in text and 'C:' not in text
    # 通常の目的語・相対パスは変えない。
    assert (tmp_path / 'b.d').read_text() == 'b.o: src/b.c src/b.h\n'
    assert text.startswith('a.o: ')


def test_rewritten_dependency_still_works(tmp_path):
    """書き直した依存の更新が、目的語の作り直しに効く。"""
    (tmp_path / 'dep.h').write_text('x\n')
    (tmp_path / 'a.d').write_text('a.o: dep.h\n')
    makefile = tmp_path / 'test.mk'
    makefile.write_text(
        f'include {DEPFIX.as_posix()}\n'
        '$(call fix_deps,a.d)\n'
        '-include a.d\n'
        'a.o:\n\t@echo build\n\t@touch a.o\n')
    first = subprocess.run(['make', '-f', 'test.mk', 'a.o'], cwd=tmp_path, capture_output=True, text=True)
    assert 'build' in first.stdout
    again = subprocess.run(['make', '-f', 'test.mk', 'a.o'], cwd=tmp_path, capture_output=True, text=True)
    assert 'build' not in again.stdout
