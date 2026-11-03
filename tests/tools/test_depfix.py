"""依存ファイルのドライブ付きパスを、make が読める形に書き直すことを確かめる。"""
import os
import shutil
import subprocess
import time
from pathlib import Path

import pytest

DEPFIX = Path(__file__).resolve().parents[2] / 'third_party/libctrpf/depfix.mk'



def find_make():
    """コロンを区切りと読む MSYS の make（devkitPro 同梱）を優先する。"""
    root = os.environ.get('DEVKITPRO', 'C:/devkitPro')
    for candidate in (Path(root) / 'msys2/usr/bin/make.exe', Path('C:/devkitPro/msys2/usr/bin/make.exe')):
        if candidate.is_file():
            return str(candidate)
    return shutil.which('make')


MAKE = find_make()
pytestmark = pytest.mark.skipif(
    not MAKE or not all(shutil.which(tool) for tool in ('grep', 'sed', 'xargs')),
    reason='make と POSIX の道具が要る')
windows_only = pytest.mark.skipif(os.name != 'nt', reason='ドライブ付きのパスは Windows だけ')


def write_makefile(workdir: Path, fix: bool, body: str):
    makefile = workdir / 'test.mk'
    call = '$(call fix_deps,a.d b.d missing.d)\n' if fix else ''
    makefile.write_text(f'include {DEPFIX.as_posix()}\n{call}-include a.d b.d\n{body}')


def run_make(workdir: Path, target: str):
    return subprocess.run([MAKE, '-f', 'test.mk', target], cwd=workdir, capture_output=True, text=True)


def test_drive_letter_only_is_lowercased(tmp_path):
    (tmp_path / 'a.d').write_text(
        'a.o: \\\n A:/Work/Src/A.c \\\n C:DevKit/Include/X.h\n\nA:/Work/Src/A.c:\n\nC:DevKit/Include/X.h:\n')
    (tmp_path / 'b.d').write_text('b.o: src/B.c Src/b.h\n')
    write_makefile(tmp_path, True, 'a.o: ;\nall: a.o\n\t@echo ok\n')
    result = run_make(tmp_path, 'all')
    assert result.returncode == 0, result.stderr
    text = (tmp_path / 'a.d').read_text()
    # ドライブの文字だけを小文字にし、その後のパスの大文字・小文字は変えない。
    assert ' /a/Work/Src/A.c' in text
    assert ' /c/DevKit/Include/X.h' in text
    assert '\n/a/Work/Src/A.c:' in text and '\n/c/DevKit/Include/X.h:' in text
    assert 'A:' not in text and 'C:' not in text
    # 通常の目的語・相対パスは変えない。
    assert text.startswith('a.o: ')
    assert (tmp_path / 'b.d').read_text() == 'b.o: src/B.c Src/b.h\n'


def drive_dep_setup(tmp_path: Path, fix: bool):
    """実在のヘッダーを、ドライブ付きの絶対パスで書いた .d を用意する。"""
    header = tmp_path / 'Dep.h'
    header.write_text('x\n')
    drive_path = str(header.resolve()).replace('\\', '/')
    assert drive_path[1:3] == ':/'
    (tmp_path / 'a.d').write_text(f'a.o: {drive_path}\n\n{drive_path}:\n')
    write_makefile(tmp_path, fix, 'a.o:\n\t@echo build\n\t@touch a.o\n')
    return header


def drive_relative_setup(tmp_path: Path, fix: bool):
    """`-I` にバックスラッシュ区切りのドライブ付きパスを渡した時の、`C:dir/file` の形。"""
    (tmp_path / 'a.d').write_text('a.o: C:devkit/include/x.h\n\nC:devkit/include/x.h:\n')
    write_makefile(tmp_path, fix, 'a.o:\n\t@echo build\n')


def test_without_fix_make_stops(tmp_path):
    drive_relative_setup(tmp_path, fix=False)
    result = run_make(tmp_path, 'a.o')
    assert result.returncode != 0
    assert 'a.d:1: ***' in result.stderr


def test_with_fix_make_runs(tmp_path):
    drive_relative_setup(tmp_path, fix=True)
    result = run_make(tmp_path, 'a.o')
    assert result.returncode == 0, result.stderr
    assert 'build' in result.stdout


@windows_only
def test_rewritten_dependency_still_works(tmp_path):
    header = drive_dep_setup(tmp_path, fix=True)
    first = run_make(tmp_path, 'a.o')
    assert first.returncode == 0, first.stderr
    assert 'build' in first.stdout
    second = run_make(tmp_path, 'a.o')
    assert second.returncode == 0, second.stderr
    assert 'build' not in second.stdout
    later = time.time() + 10
    os.utime(header, (later, later))
    third = run_make(tmp_path, 'a.o')
    assert third.returncode == 0, third.stderr
    assert 'build' in third.stdout
