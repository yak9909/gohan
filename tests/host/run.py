#!/usr/bin/env python3
"""PC の C++20 コンパイラで core をビルドして試験する。"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('HOST_CXX'))
    args = parser.parse_args()
    compiler = args.cxx or shutil.which('g++') or shutil.which('clang++')
    if not compiler:
        parser.exit(1, 'PC 用の g++ または clang++ が必要です。\n')
    root = Path(__file__).resolve().parents[2]
    build = root / 'build/host'
    build.mkdir(parents=True, exist_ok=True)
    output = build / ('test_core.exe' if os.name == 'nt' else 'test_core')
    sources = ['tests/host/test_core.cpp', 'src/core/format.cpp', 'src/core/utf.cpp',
               'src/core/frame.cpp', 'src/core/feature.cpp']
    command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-fno-rtti', '-fno-exceptions', '-pthread', '-Isrc', *sources, '-o', str(output)]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(output)], cwd=root, check=True)
    hook_output = build / ('test_frame_hook.exe' if os.name == 'nt' else 'test_frame_hook')
    hook_sources = ['tests/host/test_frame_hook.cpp', 'src/game/frame_hook.cpp', 'src/core/frame.cpp']
    hook_command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-fno-rtti', '-fno-exceptions', '-pthread', '-Itests/host/fakes', '-Isrc',
                    *hook_sources, '-o', str(hook_output)]
    subprocess.run(hook_command, cwd=root, check=True)
    subprocess.run([str(hook_output)], cwd=root, check=True)
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/font_atlas.py'],
                   cwd=root, check=True)
    renderer_output = build / ('test_renderer.exe' if os.name == 'nt' else 'test_renderer')
    renderer_sources = ['tests/host/test_renderer.cpp', 'src/core/frame.cpp', 'src/core/utf.cpp']
    renderer_sources += [str(path.relative_to(root)) for path in sorted((root / 'src/ui/renderer').glob('*.cpp'))]
    renderer_command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fno-rtti', '-fno-exceptions', '-pthread', '-Isrc', '-Ibuild/gen',
                        *renderer_sources, '-o', str(renderer_output)]
    subprocess.run(renderer_command, cwd=root, check=True)
    subprocess.run([str(renderer_output)], cwd=root, check=True)
    lyt_output = build / ('test_lyt_renderer.exe' if os.name == 'nt' else 'test_lyt_renderer')
    lyt_command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-fno-rtti', '-fno-exceptions', '-Itests/host/lyt_fakes', '-Isrc',
                   'tests/host/test_lyt_renderer.cpp', 'src/game/lyt/renderer.cpp', '-o', str(lyt_output)]
    subprocess.run(lyt_command, cwd=root, check=True)
    subprocess.run([str(lyt_output)], cwd=root, check=True)
    print('PC 試験: core・フックの照合・描画指示の範囲・順序・文字枠・スレッド間の要求を確認しました。')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
