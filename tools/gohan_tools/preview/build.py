"""実製品のソースを PC の C++20 コンパイラでビルドする。"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


def build(root: Path, directory: Path, screen: str, compiler: str | None, dev: bool) -> tuple[Path, dict]:
    compiler = compiler or os.environ.get('HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
    if not compiler:
        raise ValueError('PC 用の g++ または clang++ を PATH に追加してください')
    gen = root / 'build/gen'
    menu_gen = directory / 'gen'
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/font_atlas.py', '--output', str(gen / 'font_atlas.hpp')],
                   cwd=root, check=True)
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/ui.py', '--out', str(gen)], cwd=root, check=True)
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/menu.py', '--spec', 'spec/menu.toml',
                    '--out', str(menu_gen)], cwd=root, check=True)
    feature_path = root / 'src/features/menu_screen/menu_screen.cpp'
    feature = feature_path
    adaptation = None
    if screen != 'top':
        raise ValueError('CA2 のメニューは上画面だけに描きます')
    engine = [root / 'src/menu' / (name + '.cpp') for name in (
        'engine_core', 'engine_edit', 'engine_action', 'engine_input', 'engine_view', 'hotkey',
        'notice', 'value_text', 'surface', 'notices', 'panel', 'modal', 'shell', 'sample_items')]
    sources = [root / 'tests/host/menu_preview.cpp', root / 'src/game/lyt/scene.cpp',
               feature, root / 'src/ui/input/buttons.cpp',
               root / 'tests/host/input_hook_stub.cpp',
               *[root / 'src/core' / (name + '.cpp') for name in ('frame', 'feature', 'utf', 'format', 'input_block')],
               *engine, *sorted((root / 'src/ui/renderer').glob('*.cpp')),
               *sorted((root / 'src/game').glob('title_background*.cpp')), menu_gen / 'menu_table.cpp']
    executable = directory / ('menu-preview.exe' if os.name == 'nt' else 'menu-preview')
    flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-rtti', '-fno-exceptions', '-pthread']
    if dev:
        flags.append('-DGOHAN_DEV=1')
    # Scene は実際のゲームの型を使う。CTRPF だけを入力・時計の代役へ向ける。
    includes = [root / 'src', root / 'tests/host/fakes', gen, menu_gen]
    command = [compiler, *flags, *(f'-I{path}' for path in includes), *map(str, sources), '-o', str(executable)]
    subprocess.run(command, cwd=root, check=True)
    # ソース・ヘッダー・定義・字形表も含む。手動の較正時に使った版を追える。
    inputs = [*sorted((root / 'src').rglob('*.cpp')), *sorted((root / 'src').rglob('*.hpp')),
              root / 'tests/host/menu_preview.cpp', root / 'tests/host/fakes/CTRPluginFramework.hpp',
              root / 'spec/menu.toml', root / 'spec/ui.toml', gen / 'font_atlas.hpp', gen / 'ui_atlas.hpp',
              gen / 'ui_parts.hpp', gen / 'ui_values.hpp', menu_gen / 'menu_table.cpp', menu_gen / 'menu_ids.hpp']
    hashes = {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    version = subprocess.run([compiler, '--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    return executable, {'compiler': version, 'flags': flags, 'screen_adaptation': adaptation, 'inputs_sha256': hashes}
