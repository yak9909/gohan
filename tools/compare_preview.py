#!/usr/bin/env python3
"""同じ状態のプレビューと実機の BMP/PNG を比べ、差分 PNG と数値を保存する。"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

from gohan_tools.preview.comparison import compare
from gohan_tools.preview.images import read, screen_image


ROOT = Path(__file__).resolve().parents[1]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('preview', type=Path)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--screen', choices=('top', 'bottom'), default='bottom')
    parser.add_argument('--search', type=int, default=4, help='ずらしの探索範囲 ±px（0 で無効）')
    parser.add_argument('--region', type=int, nargs=4, metavar=('X0', 'Y0', 'X1', 'Y1'))
    parser.add_argument('--luma', action='store_true', help='輝度差で位置探索と集計をする')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/preview/compare')
    args = parser.parse_args(argv)
    try:
        out = args.out.resolve()
        if not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build':
            raise ValueError('生成先は作業ツリーの build/ 以下にしてください')
        preview = screen_image(read(args.preview), args.screen)
        capture = screen_image(read(args.capture), args.screen)
        report, diff, side = compare(preview, capture, args.search, args.region, args.luma)
        report['screen'] = args.screen
        report['inputs_sha256'] = {label: hashlib.sha256(path.read_bytes()).hexdigest()
                                  for label, path in (('preview', args.preview), ('capture', args.capture))}
        out.mkdir(parents=True, exist_ok=True)
        diff.save(out / 'difference.png')
        side.save(out / 'comparison.png')
        text = json.dumps(report, ensure_ascii=False, indent=2)
        (out / 'metrics.json').write_text(text + '\n', encoding='utf-8')
        print(text)
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f'compare-preview: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
