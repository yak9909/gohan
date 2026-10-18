#!/usr/bin/env python3
"""ELF の区分別容量と、前回成功した測定からの増減を表示する。"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

SECTIONS = ('.text', '.rodata', '.data', '.bss')


def measure(elf: Path, size_tool: str) -> dict[str, int]:
    result = subprocess.run([size_tool, '-A', str(elf)], check=True, capture_output=True, text=True)
    sizes = dict.fromkeys(SECTIONS, 0)
    for line in result.stdout.splitlines():
        columns = line.split()
        if len(columns) >= 2 and columns[0] in sizes:
            sizes[columns[0]] += int(columns[1])
    if not any(sizes.values()):
        raise ValueError('ELF の対象区分が見つかりません')
    return sizes


def report(elf: Path, size_tool: str, last: Path) -> None:
    sizes = measure(elf, size_tool)
    previous = json.loads(last.read_text(encoding='utf-8')) if last.exists() else None
    if previous is not None and (set(previous) != set(SECTIONS)
                                 or any(type(v) is not int or v < 0 for v in previous.values())):
        raise ValueError('前回の測定値の形式が不正です')
    print('区分          bytes       前回との差')
    for section, size in sizes.items():
        delta = f'{size - previous[section]:+d}' if previous is not None else '初回'
        print(f'{section:8s} {size:10d} {delta:>14s}')
    last.parent.mkdir(parents=True, exist_ok=True)
    temporary = last.with_suffix('.tmp')
    temporary.write_text(json.dumps(sizes, indent=2) + '\n', encoding='utf-8')
    temporary.replace(last)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--size-tool', default='arm-none-eabi-size')
    parser.add_argument('--last', type=Path, default=Path('build/size_last.json'))
    args = parser.parse_args()
    try:
        report(args.elf, args.size_tool, args.last)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'容量測定: {error}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
