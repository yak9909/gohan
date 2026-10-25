#!/usr/bin/env python3
"""PC の C++20 コンパイラで core・menu・画面の接続をビルドして試験する。"""
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
    # core の描画予算も設計表から取るため、最初のコンパイルより前に生成する。
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/ui.py'], cwd=root, check=True)

    def run(sources, output, includes=('src',), defines=()):
        out = build / (output + exe)
        subprocess.run([compiler, *flags, *defines, *(f'-I{i}' for i in includes), '-Ibuild/gen', *sources, '-o', str(out)], cwd=root, check=True)
        subprocess.run([str(out)], cwd=root, check=True)

    run(['tests/host/test_core.cpp', 'src/core/format.cpp', 'src/core/utf.cpp',
         'src/core/frame.cpp', 'src/core/feature.cpp'], 'test_core')
    for dev in (False, True):
        run(['tests/host/test_status.cpp', 'src/features/status/status.cpp', 'src/core/format.cpp'],
            'test_status_dev' if dev else 'test_status', ('tests/host/status_fakes', 'src'),
            ('-DGOHAN_DEV=1',) if dev else ())
    run(['tests/host/test_input_block.cpp', 'src/core/input_block.cpp'], 'test_input_block')
    run(['tests/host/test_input_filter.cpp', 'src/game/input_filter.cpp'], 'test_input_filter')
    run(['tests/host/test_frame_hook.cpp', 'src/game/frame_hook.cpp', 'src/core/frame.cpp'],
        'test_frame_hook', ('tests/host/fakes', 'src'))
    run(['tests/host/test_input_hook.cpp', 'src/game/input_hook.cpp', 'src/game/input_filter.cpp',
         'src/core/input_block.cpp', 'src/core/format.cpp'], 'test_input_hook',
        ('tests/host/fakes', 'src'), ('-DGOHAN_DEV=1',))

    # 描画: 字形の表を生成してから試験する。
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/font_atlas.py'], cwd=root, check=True)
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/ui.py'], cwd=root, check=True)
    renderer = [str(path.relative_to(root)) for path in sorted((root / 'src/ui/renderer').glob('*.cpp'))]
    background = ['src/game/title_background.cpp', 'src/game/title_background_io.cpp', 'src/game/title_background_self.cpp']
    renderer += background
    run(['tests/host/test_title_background.cpp', *background], 'test_title_background')
    run(['tests/host/test_selection_compositor.cpp', *background], 'test_selection_compositor')
    run(['tests/host/test_title_background.cpp', *background], 'test_title_background_self',
        ('tests/host/romfs_fakes', 'src'), ('-D__3DS__',))
    run(['tests/host/test_renderer.cpp', 'src/core/frame.cpp', 'src/core/feature.cpp', 'src/core/utf.cpp', *renderer],
        'test_renderer', ('src', 'build/gen'))
    # ゲームの描画の層: 資源・Pane の木・ノード・導入と取り外し。ゲームの API は代役で受ける。
    lyt = ['src/game/lyt/engine.cpp', 'src/game/lyt/scene.cpp', 'src/game/lyt/nodes.cpp',
           'src/game/lyt/recording_resources.cpp']
    run(['tests/host/test_lyt_renderer.cpp', 'src/game/lyt/renderer.cpp', 'src/game/lyt/verification.cpp', *lyt],
        'test_lyt_renderer', ('tests/host/lyt_fakes', 'tests/host', 'src'))
    run(['tests/host/test_lyt_resources.cpp', 'src/game/lyt/recording_resources.cpp'],
        'test_lyt_resources', ('tests/host', 'src'))
    run(['tests/host/test_lyt_engine.cpp', *lyt], 'test_lyt_engine', ('tests/host', 'src'))
    run(['tests/host/test_lyt_selection.cpp', *lyt], 'test_lyt_selection', ('tests/host', 'src'))

    # メニュー: 試験用の spec と、収録している見本の spec から、それぞれ表を生成して試験する。
    codegen = root / 'tools/gohan_tools/codegen/menu.py'
    engine = ['src/menu/engine_core.cpp', 'src/menu/engine_edit.cpp', 'src/menu/engine_action.cpp',
              'src/menu/engine_input.cpp', 'src/menu/engine_view.cpp', 'src/menu/hotkey.cpp',
              'src/menu/notice.cpp', 'src/menu/value_text.cpp', 'src/core/format.cpp']
    run(['tests/host/test_menu_motion.cpp'], 'test_menu_motion')
    # 画面（帯・説明・ダイアログ・入力の枠・通知）。描画先は代役で受ける。
    screen = ['src/menu/surface.cpp', 'src/menu/notices.cpp', 'src/menu/panel.cpp', 'src/menu/modal.cpp',
              'src/menu/shell.cpp']
    for name, spec, tests in (
            ('test', 'tests/host/fixtures/menu_test.toml',
             (('tests/host/test_menu.cpp', []), ('tests/host/test_menu_entry.cpp', screen))),
            ('sample', 'spec/menu.toml',
             (('tests/host/test_menu_sample.cpp', ['src/menu/sample_items.cpp']),
              ('tests/host/test_menu_shell.cpp', ['src/menu/sample_items.cpp', *screen])))):
        gen = build / f'gen-{name}'
        subprocess.run([sys.executable, str(codegen), '--spec', spec, '--out', str(gen)], cwd=root, check=True)
        for test, extra in tests:
            run([test, *engine, *extra, str(gen.relative_to(root) / 'menu_table.cpp')],
                Path(test).stem, ('src', 'tests/host', str(gen.relative_to(root))))

    # ゲーム中のメニュー: 毎フレームの処理から描画指示まで。入力・時計・ゲーム側の描画は代役。
    run(['tests/host/test_menu_screen.cpp', 'src/features/menu_screen/menu_screen.cpp', 'src/ui/input/buttons.cpp',
         'tests/host/input_hook_stub.cpp', 'src/core/input_block.cpp',
         'src/core/frame.cpp', 'src/core/utf.cpp', *engine, *screen, 'src/menu/sample_items.cpp', *renderer,
         'build/host/gen-sample/menu_table.cpp'],
        'test_menu_screen', ('tests/host/fakes', 'src', 'build/gen', 'build/host/gen-sample'))
    # 描く文字がすべて字形の表にあるか（無い文字は実機で何も出ない）。
    run(['tests/host/test_menu_glyphs.cpp', 'src/core/frame.cpp', 'src/core/utf.cpp', *engine, *screen,
         'src/menu/sample_items.cpp', *renderer, 'build/host/gen-sample/menu_table.cpp'],
        'test_menu_glyphs', ('src', 'tests/host', 'build/gen', 'build/host/gen-sample'))
    # プレビューも実際の feature・入力・Canvas を通し、全台本の描画内容を照合する。
    subprocess.run([sys.executable, 'tools/preview.py', '--cxx', compiler,
                    '--no-rom', '--out', str(build / 'preview')], cwd=root, check=True)
    print('PC 試験: 書式・UTF・Mailbox・Request・毎フレーム・終了順序（描画の排出はフックを止める前）・'
          '照合と前提の不足による導入の拒否・資源の失敗時巻き戻し・反復の確保と解放・束縛中の解放拒否・'
          'Pane の木と Layout ノードの組み立て・登録の取り下げと排出・画面ごとの確定・'
          '描画指示の範囲・順序・文字枠・メニューのエンジンの規則と見本の結び付け・'
          '動きの部品・タッチのスクロール・メニューの画面（開閉・行・タッチ・ダイアログ・入力の枠・通知）・'
          '描く文字の字形と描画の予算・ゲーム中の入力から描画指示までを確認しました。')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
