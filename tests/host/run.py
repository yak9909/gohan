#!/usr/bin/env python3
"""PC の C++20 コンパイラで core と menu をビルドして試験する。"""
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
    exe = '.exe' if os.name == 'nt' else ''
    flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-rtti', '-fno-exceptions', '-pthread']

    def run(sources, output, includes=('src',)):
        out = build / (output + exe)
        subprocess.run([compiler, *flags, *(f'-I{i}' for i in includes), *sources, '-o', str(out)], cwd=root, check=True)
        subprocess.run([str(out)], cwd=root, check=True)

    run(['tests/host/test_core.cpp', 'src/core/format.cpp', 'src/core/utf.cpp',
         'src/core/frame.cpp', 'src/core/feature.cpp'], 'test_core')
    run(['tests/host/test_frame_hook.cpp', 'src/game/frame_hook.cpp', 'src/core/frame.cpp'],
        'test_frame_hook', ('tests/host/fakes', 'src'))

    # 描画: 字形の表を生成してから試験する。
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/font_atlas.py'], cwd=root, check=True)
    renderer = [str(path.relative_to(root)) for path in sorted((root / 'src/ui/renderer').glob('*.cpp'))]
    run(['tests/host/test_renderer.cpp', 'src/core/frame.cpp', 'src/core/utf.cpp', *renderer],
        'test_renderer', ('src', 'build/gen'))
    run(['tests/host/test_lyt_renderer.cpp', 'src/game/lyt/renderer.cpp'],
        'test_lyt_renderer', ('tests/host/lyt_fakes', 'src'))

    # メニュー: 試験用の spec と、収録している見本の spec から、それぞれ表を生成して試験する。
    codegen = root / 'tools/gohan_tools/codegen/menu.py'
    engine = ['src/menu/engine_core.cpp', 'src/menu/engine_edit.cpp', 'src/menu/engine_action.cpp',
              'src/menu/engine_input.cpp', 'src/menu/engine_view.cpp', 'src/menu/hotkey.cpp',
              'src/menu/notice.cpp', 'src/core/format.cpp']
    for name, spec, test, extra in (
            ('test', 'tests/host/fixtures/menu_test.toml', 'tests/host/test_menu.cpp', []),
            ('sample', 'spec/menu.toml', 'tests/host/test_menu_sample.cpp', ['src/menu/sample_items.cpp'])):
        gen = build / f'gen-{name}'
        subprocess.run([sys.executable, str(codegen), '--spec', spec, '--out', str(gen)], cwd=root, check=True)
        run([test, *engine, *extra, str(gen.relative_to(root) / 'menu_table.cpp')], f'test_menu_{name}',
            ('src', str(gen.relative_to(root))))
    print('PC 試験: 書式・UTF・Mailbox・Request・毎フレーム・終了順序・照合による導入の拒否・'
          '描画指示の範囲・順序・文字枠・メニューのエンジンの規則と見本の結び付けを確認しました。')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
