"""実際の通知・メニュー・Canvas・Pane の木を PC で測る（PowerShell から実行）。"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=root, help='比較するソースの写し（既定は現在）')
    parser.add_argument('--name', default='current')
    parser.add_argument('--cxx', default=os.environ.get('HOST_CXX') or shutil.which('g++'))
    parser.add_argument('--repeats', type=int, default=20)
    parser.add_argument('scenarios', nargs='*')
    args = parser.parse_args()
    if Path(args.name).name != args.name or args.name in ('.', '..'):
        parser.error('--name は一つのフォルダ名にしてください')
    if not args.cxx or args.repeats < 1:
        parser.error('C++ コンパイラと 1 以上の繰り返し回数が必要です')
    tree = args.tree.resolve()
    out = root / 'build/bench-notice' / args.name
    out.mkdir(parents=True, exist_ok=True)
    for tool, extra in [('ui', ['--out', str(tree / 'build/gen')]),
                        ('font_atlas', ['--output', str(tree / 'build/gen/font_atlas.hpp')]),
                        ('menu', ['--spec', 'spec/menu.toml', '--out', str(out / 'gen')])]:
        subprocess.run([sys.executable, f'tools/gohan_tools/codegen/{tool}.py', *extra], cwd=tree, check=True)
    engine = [f'src/menu/{name}.cpp' for name in (
        'engine_core', 'engine_edit', 'engine_action', 'engine_input', 'engine_view', 'hotkey',
        'notice', 'value_text', 'surface', 'notices', 'panel', 'modal', 'shell', 'sample_items')]
    sources = [root / 'tests/host/bench_notice.cpp',
               *[tree / f'src/core/{name}.cpp' for name in ('frame', 'feature', 'utf', 'format')],
               *[tree / name for name in engine],
               *sorted((tree / 'src/ui/renderer').glob('*.cpp')),
               *sorted((tree / 'src/game').glob('title_background*.cpp')),
               *[tree / f'src/game/lyt/{name}.cpp' for name in ('engine', 'scene', 'nodes', 'recording_resources')],
               out / 'gen/menu_table.cpp']
    executable = out / ('bench_notice.exe' if os.name == 'nt' else 'bench_notice')
    flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-rtti', '-fno-exceptions', '-pthread']
    includes = [root / 'tests/host', tree / 'src', tree / 'build/gen', out / 'gen']
    subprocess.run([args.cxx, *flags, *(f'-I{x}' for x in includes), *map(str, sources), '-o', str(executable)],
                   cwd=tree, check=True)
    subprocess.run([str(executable), str(args.repeats), *args.scenarios], check=True)


if __name__ == '__main__':
    main()
