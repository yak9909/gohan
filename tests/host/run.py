#!/usr/bin/env python3
"""PC の C++20 コンパイラで core をビルドして試験する。"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


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
    print('PC 試験: 書式・UTF・Mailbox・Request・毎フレーム・終了順序・照合による導入の拒否を確認しました。')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
