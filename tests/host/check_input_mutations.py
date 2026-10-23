#!/usr/bin/env python3
"""入力の規則を一条件ずつ壊し、PC 試験がその誤りを検出するか確認する。"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


MUTATIONS = (
    ('old_state', '旧状態を揃えない', 'if (kHideReleaseOnBlock) {', 'if (false) {'),
    ('latch', '終了後の掛かりを外す', 'const Mask blocked = block | (s.latch & raw);',
     'const Mask blocked = block;'),
    ('mixed', '混ざりの判定を外す', 'if (Count(buttons) > 1) s.pending = 0;', ''),
    ('blocked_defer', '遮断中も押し切りの状態を残す',
     'const Mask defer = in.demand.defer & group::Buttons & ~blocked;',
     'const Mask defer = in.demand.defer & group::Buttons;'),
    ('touch_bit', 'タッチの保持のビットを落とさない',
     'out.held = (in.held & ~HeldBits(blocked | defer)) | HeldBits(s.window | s.passing);',
     'out.held = (in.held & ~HeldBits((blocked | defer) & ~part::Touch)) | HeldBits(s.window | s.passing);'),
    ('analog', 'アナログ値をゼロにしない',
     'if (blocked & part::SlidePad) out.slide_x = out.slide_y = 0;', ''),
    ('analog_press', 'アナログ値が残るだけでスライドパッドを押していると見なす',
     'const Mask raw = Parts(in.held, in.pointer_on);',
     'const Mask raw = Parts(in.held, in.pointer_on) |\n'
     '        ((in.slide_x != 0 || in.slide_y != 0) ? part::SlidePad : 0);'),
    ('select', 'SELECT中の他ボタンを通す', 'block |= group::Buttons;', 'block |= 0;'),
    ('ctrpf', 'CTRPFの明示的な再開を無視する',
     'const bool suspended = in.ctrpf_resumed ||', 'const bool suspended ='),
    ('gap', '100msの境界を誤る',
     'in.now_ms - s.previous_ms >= limits::InputSuspendGapMs',
     'in.now_ms - s.previous_ms > limits::InputSuspendGapMs'),
    ('window', '300msの窓を終えない',
     'if (s.window && in.now_ms - s.window_ms >= limits::InputDeferWindowMs) s.window = 0;', ''),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('HOST_CXX'))
    parser.add_argument('--evidence', required=True, type=Path, help='変異版のソースと記録の保存先（作業ツリーの外）')
    args = parser.parse_args()
    compiler = args.cxx or shutil.which('g++') or shutil.which('clang++')
    if not compiler:
        parser.error('PC 用の C++20 コンパイラが必要です')
    root = Path(__file__).resolve().parents[2]
    evidence = args.evidence.resolve()
    if evidence.is_relative_to(root):
        parser.error('変異版のソースは作業ツリーの外に置いてください')
    evidence.mkdir(parents=True, exist_ok=True)
    build = root / 'build/host/input-mutations'
    build.mkdir(parents=True, exist_ok=True)
    source = (root / 'src/game/input_filter.cpp').read_text(encoding='utf-8')
    flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-rtti', '-fno-exceptions',
             '-Isrc', '-Ibuild/gen', 'tests/host/test_input_filter.cpp']
    subprocess.run([sys.executable, 'tools/gohan_tools/codegen/ui.py'], cwd=root, check=True)

    def check(name: str, text: str, expected_failure: bool) -> dict:
        mutated = evidence / f'{name}.cpp'
        mutated.write_text(text, encoding='utf-8')
        binary = build / (name + ('.exe' if os.name == 'nt' else ''))
        compiled = subprocess.run([compiler, *flags, str(mutated), '-o', str(binary)],
                                  cwd=root, capture_output=True)
        (evidence / f'{name}-build.log').write_bytes(compiled.stdout + compiled.stderr)
        if compiled.returncode:
            raise RuntimeError(f'{name}: コンパイル失敗を検出成功とは数えません')
        result = subprocess.run([str(binary)], cwd=root, capture_output=True)
        (evidence / f'{name}-test.log').write_bytes(result.stdout + result.stderr)
        # 試験の assert が失敗したことまで確認する。起動失敗や任意の異常終了では成功にしない。
        asserted = b'Assertion' in result.stderr and b'failed' in result.stderr
        if expected_failure and (result.returncode == 0 or not asserted):
            raise RuntimeError(f'{name}: 変異を assert で検出できませんでした')
        if not expected_failure and result.returncode:
            raise RuntimeError('未変更の版の試験に失敗しました')
        print(f'{name}: ' + ('assert で検出' if expected_failure else '成功'), flush=True)
        return {'name': name, 'compile_exit': compiled.returncode,
                'test_exit': result.returncode, 'assertion_failed': asserted}

    results = [check('baseline', source, False)]
    for name, description, before, after in MUTATIONS:
        if source.count(before) != 1:
            raise RuntimeError(f'{name}: 変異する条件を一意に見つけられません')
        result = check(name, source.replace(before, after), True)
        result['description'] = description
        results.append(result)
    (evidence / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'変異検査: {len(MUTATIONS)} / {len(MUTATIONS)} 条件を検出')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
