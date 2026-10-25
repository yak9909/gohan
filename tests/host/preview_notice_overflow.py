#!/usr/bin/env python3
"""7件の後に5件を一括追加する通知を、実製品のCanvasからPNG・HTMLへ描く。"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))

from gohan_tools.preview.build import build
from gohan_tools.preview.gallery import gallery
from gohan_tools.preview.images import Image
from gohan_tools.preview.raster import Raster
from gohan_tools.preview.romfs import environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True, help='作業ツリー外の出力先')
    parser.add_argument('--executable', type=Path, help='直前のtools/preview.pyで作った実行ファイルを再利用')
    parser.add_argument('--cxx')
    parser.add_argument('--no-rom', action='store_true')
    args = parser.parse_args()
    directory = args.out.resolve()
    if directory.is_relative_to(ROOT):
        parser.error('出力先は作業ツリー外にしてください')
    settings = tomllib.loads((ROOT / 'tools/preview/settings.toml').read_text(encoding='utf-8'))
    env, configured = environment(ROOT, None if args.no_rom else ROOT / 'gohan.local.toml', directory)
    directory.mkdir(parents=True, exist_ok=True)
    if args.executable:
        executable = args.executable.resolve()
        manifest = {'executable': str(args.executable)}
    else:
        target = ROOT / 'build/preview-notice-overflow'
        target.mkdir(parents=True, exist_ok=True)
        executable, manifest = build(ROOT, target, 'top', args.cxx, False)
    subprocess.run([str(executable), str(directory), '--notice-overflow'], cwd=ROOT, env=env, check=True)
    frames = []
    for path in sorted(directory.glob('frame-*.json'), key=lambda p: int(p.stem.split('-')[1])):
        drawing = json.loads(path.read_text(encoding='utf-8'))
        elapsed = drawing['at'] - 1400
        label = '追加直前の7件' if elapsed < 0 else f'7件の後に5件 / {elapsed} ms'
        raster = Raster(directory / f'resources-{path.stem.split("-")[1]}', settings['raster'])
        frame = {'label': label, 'at': drawing['at']}
        for i, screen in enumerate(('top', 'bottom')):
            frame[screen] = raster.draw(drawing['screens'][i], Image.solid(
                400 if i == 0 else 320, 240, tuple(settings['background'])))
            frame[screen].save(directory / f'{path.stem}-{screen}.png')
        frames.append(frame)
    (directory / 'index.html').write_text(gallery(frames, 'top'), encoding='utf-8')
    manifest.update(settings=settings, title_background_romfs_configured=configured,
                    description='7件の後に5件を一括追加。40 ms間隔で退場・積み替え・入りを同時に開始。')
    (directory / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'通知の連続するあふれ: {len(frames)} フレーム、{directory / "index.html"}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
