#!/usr/bin/env python3
"""本物のメニューを PC で動かし、時刻付き台本から上下の PNG と単独 HTML を作る。"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tomllib

from gohan_tools.preview.build import build
from gohan_tools.preview.gallery import gallery
from gohan_tools.preview.images import Image, read, screen_image
from gohan_tools.preview.raster import Raster
from gohan_tools.preview.script import name, protocol


ROOT = Path(__file__).resolve().parents[1]


def output_directory(path: Path) -> Path:
    path = path.resolve()
    if not path.is_relative_to(ROOT / 'build') or path == ROOT / 'build':
        raise ValueError('生成先は作業ツリーの build/ 以下のフォルダにしてください')
    path.mkdir(parents=True, exist_ok=True)
    return path


def validate_settings(settings: dict) -> None:
    if settings['screen'] not in ('top', 'bottom'):
        raise ValueError('screen は top または bottom にしてください')
    background = settings['background']
    if len(background) != 3 or any(type(x) is not int or not 0 <= x <= 255 for x in background):
        raise ValueError('background は RGB の 0〜255 の整数 3 個にしてください')
    model = settings['raster']
    if model.get('output_format', 'rgb888') not in ('rgb888', 'rgb565'):
        raise ValueError('output_format は rgb888 または rgb565 にしてください')
    if (len(model['offset']) != 2 or any(type(v) is not int for v in model['offset']) or
            model['rounding'] not in ('nearest', 'floor') or type(model['swap_red_blue']) is not bool or
            len(model['rgb_gain']) != 3 or
            any(not isinstance(v, (int, float)) or not 0 <= v <= 4 for v in [model['alpha_gain'], *model['rgb_gain']])):
        raise ValueError('raster の較正値が不正です')


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--script', type=Path, default=ROOT / 'tools/preview/scenarios.json')
    parser.add_argument('--settings', type=Path, default=ROOT / 'tools/preview/settings.toml')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/preview')
    parser.add_argument('--cxx')
    parser.add_argument('--dev', action='store_true')
    parser.add_argument('--parts', action='store_true', help='CA2 の全 UI 部品・整数倍・分割・繰り返しの一覧を出す')
    parser.add_argument('--scenario', action='append', help='指定した名前だけ実行（複数指定可）')
    parser.add_argument('--background-top', type=Path)
    parser.add_argument('--background-bottom', type=Path)
    args = parser.parse_args(argv)
    try:
        settings = tomllib.loads(args.settings.read_text(encoding='utf-8'))
        validate_settings(settings)
        script = json.loads(args.script.read_text(encoding='utf-8'))
        scenarios = script['scenarios']
        if len({name(s['name']) for s in scenarios}) != len(scenarios):
            raise ValueError('台本の名前が重複しています')
        if args.scenario:
            unknown = set(args.scenario) - {s['name'] for s in scenarios}
            if unknown:
                raise ValueError(f'台本がありません: {sorted(unknown)}')
            scenarios = [s for s in scenarios if s['name'] in args.scenario]
        if not scenarios:
            raise ValueError('台本がありません')
        protocols = [protocol(s, settings['frame_ms']) for s in scenarios]
        directory = output_directory(args.out)
        executable, manifest = build(ROOT, directory, settings['screen'], args.cxx, args.dev)
        if args.parts:
            target = output_directory(directory / 'parts')
            subprocess.run([str(executable), str(target), '--parts'], cwd=ROOT, check=True)
            raster = Raster(target, settings['raster'])
            frames = []
            for path in sorted(target.glob('frame-*.json'), key=lambda p: int(p.stem.split('-')[1])):
                drawing = json.loads(path.read_text(encoding='utf-8'))
                frame = {'label': 'CA2 部品 / ' + path.stem, 'at': 0}
                for i, screen in enumerate(('top', 'bottom')):
                    frame[screen] = raster.draw(drawing['screens'][i], Image.solid(
                        400 if i == 0 else 320, 240, tuple(settings['background'])))
                    frame[screen].save(target / f'{path.stem}-{screen}.png')
                frames.append(frame)
            (directory / 'index.html').write_text(gallery(frames, 'top'), encoding='utf-8')
            manifest['parts'] = True
            manifest['settings'] = settings
            (directory / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print(f'CA2 部品: {len(frames)} 枚、{directory / "index.html"}')
            return 0
        backgrounds, background_hashes = {}, {}
        for screen, path in (('top', args.background_top), ('bottom', args.background_bottom)):
            backgrounds[screen] = (screen_image(read(path), screen) if path else
                                   Image.solid(400 if screen == 'top' else 320, 240, tuple(settings['background'])))
            if path:
                background_hashes[screen] = hashlib.sha256(path.read_bytes()).hexdigest()
        frames = []
        for scenario, inputs in zip(scenarios, protocols):
            target = output_directory(directory / scenario['name'])
            (target / 'inputs.txt').write_text(inputs, encoding='utf-8')
            subprocess.run([str(executable), str(target)], input=inputs, text=True, check=True, cwd=ROOT)
            raster = Raster(target, settings['raster'])
            for i, capture in enumerate(scenario['captures']):
                drawing = json.loads((target / f'frame-{i}.json').read_text(encoding='utf-8'))
                inactive = 1 if settings['screen'] == 'top' else 0
                if drawing['screens'][inactive]:
                    raise ValueError('指定していない画面にメニューの描画指示があります')
                if capture.get('empty', False) and any(drawing['screens']):
                    raise ValueError(f'{scenario["name"]}/{capture["name"]}: 閉じた画面に描画指示があります')
                texts = [''.join(map(chr, command['text'])) for screen in drawing['screens']
                         for command in screen if command['kind'] == 1]
                for expected in capture.get('contains', []):
                    if not any(expected in text for text in texts):
                        raise ValueError(f'{scenario["name"]}/{capture["name"]}: 必要な文字がありません: {expected}')
                frame = {'label': scenario.get('label', scenario['name']) + ' / ' + capture['name'], 'at': drawing['at']}
                for s, screen in enumerate(('top', 'bottom')):
                    frame[screen] = raster.draw(drawing['screens'][s], backgrounds[screen])
                    frame[screen].save(target / f'{capture["name"]}-{screen}.png')
                frames.append(frame)
        (directory / 'index.html').write_text(gallery(frames, settings['screen']), encoding='utf-8')
        manifest.update(settings=settings, scenarios=scenarios, background_sha256=background_hashes,
                        script_sha256=hashlib.sha256(args.script.read_bytes()).hexdigest(), dev=args.dev)
        (directory / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(f'プレビュー: {len(frames)} フレーム、上下の PNG と {directory / "index.html"}')
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f'preview: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
