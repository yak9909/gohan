#!/usr/bin/env python3
"""同じ台本の変更前後を全画素で比べ、横並びと差分のHTMLを作る。"""
from __future__ import annotations

import argparse
import base64
import html
import json
from pathlib import Path

from gohan_tools.preview.images import Image, read


ROOT = Path(__file__).resolve().parents[1]


def difference(before: Image, after: Image) -> tuple[dict, Image]:
    if (before.width, before.height) != (after.width, after.height):
        raise ValueError('前後の画像の寸法が一致しません')
    delta = bytearray(abs(a - b) for a, b in zip(before.pixels, after.pixels))
    changed = sum(any(delta[i:i + 3]) for i in range(0, len(delta), 3))
    return {'pixels': before.width * before.height, 'changed_pixels': changed,
            'maximum_channel_difference': max(delta, default=0),
            'mean_channel_difference': sum(delta) / len(delta)}, Image(before.width, before.height, delta)


def compare_runs(before: Path, after: Path, out: Path) -> dict:
    for source in (before, after):
        manifest = source / 'manifest.json'
        if (manifest.exists() and json.loads(manifest.read_text(encoding='utf-8')).get(
                'title_background_romfs_configured') and out.resolve().is_relative_to(ROOT)):
            raise ValueError('ROMを使う比較の出力先は作業ツリー外にしてください')
    old = {p.relative_to(before) for p in before.glob('*/*-top.png')} | {
        p.relative_to(before) for p in before.glob('*/*-bottom.png')}
    new = {p.relative_to(after) for p in after.glob('*/*-top.png')} | {
        p.relative_to(after) for p in after.glob('*/*-bottom.png')}
    if not old or old != new:
        raise ValueError('前後の台本・撮影画像の一覧が一致しません')
    out.mkdir(parents=True, exist_ok=True)
    reports, rows = {}, []
    for relative in sorted(old):
        a, b = read(before / relative), read(after / relative)
        report, delta = difference(a, b)
        reports[relative.as_posix()] = report
        delta.save(out / relative.parent / (relative.stem + '-difference.png'))
        pictures = ''.join('<img src="data:image/png;base64,' + base64.b64encode(i.png()).decode('ascii') + '">'
                           for i in (a, b, delta))
        rows.append('<article><h2>' + html.escape(relative.as_posix()) + '</h2><p>異なる画素 ' +
                    str(report['changed_pixels']) + ' / 最大成分差 ' + str(report['maximum_channel_difference']) +
                    '</p><div>' + pictures + '</div></article>')
    summary = {'images': len(reports), 'pixels': sum(r['pixels'] for r in reports.values()),
               'changed_pixels': sum(r['changed_pixels'] for r in reports.values()),
               'maximum_channel_difference': max(r['maximum_channel_difference'] for r in reports.values()),
               'frames': reports}
    (out / 'metrics.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (out / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>変更前後の比較</title>'
        '<style>body{background:#222;color:#eee;font:14px sans-serif}article{margin:24px 0}'
        'div{display:flex;gap:12px}img{image-rendering:pixelated;width:400px;height:240px;object-fit:contain}</style>'
        '<h1>変更前 / 変更後 / 差分（等倍）</h1><p>画像 ' + str(summary['images']) +
        ' / 異なる画素 ' + str(summary['changed_pixels']) + '</p>' + ''.join(rows), encoding='utf-8')
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    target = args.out.resolve()
    if target.is_relative_to(ROOT) and (not target.is_relative_to(ROOT / 'build') or target == ROOT / 'build'):
        parser.error('出力先はbuild/の下か作業ツリー外にしてください')
    for source in (args.before.resolve(), args.after.resolve()):
        if target == source or source.is_relative_to(target) or target.is_relative_to(source):
            parser.error('出力先は比較元と別のフォルダにしてください')
    result = compare_runs(args.before, args.after, target)
    print(json.dumps({k: v for k, v in result.items() if k != 'frames'}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
