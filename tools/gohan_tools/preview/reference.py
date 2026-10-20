"""CA2 の手元の見本と全台本を並べる。画像は入力先・出力先とも収録しない。"""
from __future__ import annotations

import base64
import html
import json
from pathlib import Path

from .images import Image, read


REFERENCES = ('01_open', '02_folder', '03a_wait', '03b_on', '04a_reserve', '04b_ok',
              '05a_values', '05b_disabled', '06a_number', '06b_list', '06c_key', '07a_dialog',
              '07b_hotkey_confirm', '08_notices', '08b_notices_settled', '09_hold', '10_scroll')


def reference_for(label):
    scenario, capture = label.split(' / ', 1)
    if scenario == 'ca2-scroll':
        return '10_scroll'
    if scenario == 'ca2-key':
        return '06c_key' if capture in ('waiting', 'recording') else '02_folder'
    if scenario == 'ca2-notices':
        return '08b_notices_settled' if capture == 'settled' else '08_notices'
    if scenario == 'ca2-reserved':
        return '07a_dialog' if capture == 'close-confirm' else '04a_reserve'
    if scenario == 'ca2-disabled':
        return '05b_disabled' if capture == 'disabled-row' else '05a_values'
    if scenario == 'entries':
        return '06a_number' if 'number' in capture else '06b_list' if 'selection' in capture else '05a_values'
    if scenario == 'hold':
        return '07a_dialog' if 'confirm' in capture else '09_hold'
    if scenario == 'dialogs':
        return '07b_hotkey_confirm' if 'action' in capture else '07a_dialog'
    if scenario == 'device-calibration':
        return '04a_reserve'
    if scenario == 'device-capture-input':
        return '09_hold'
    return {'folder-off': '02_folder', 'pending-on': '04a_reserve', 'standby': '03a_wait',
            'active-on': '03b_on', 'reserved': '04a_reserve', 'ok': '04b_ok',
            'notice-032': '08_notices', 'notice-140': '08_notices', 'notice': '08b_notices_settled',
            'closed': '07b_hotkey_confirm', 'closed-again': '07b_hotkey_confirm'}.get(capture, '01_open')


def beside(a, b):
    if (a.width, a.height) != (400, 240) or (b.width, b.height) != (400, 240):
        raise ValueError('CA2 の見本とプレビューは 400×240 にしてください')
    pixels = bytearray()
    for y in range(240):
        pixels.extend(a.pixels[y * 1200:(y + 1) * 1200])
        pixels.extend(b.pixels[y * 1200:(y + 1) * 1200])
    return Image(800, 240, pixels)


def compare_all(frames, source: Path, output: Path):
    refs = {name: read(source / f'{name}.png') for name in REFERENCES}
    output.mkdir(parents=True, exist_ok=True)
    comparisons, records, chosen = [], [], {}
    for n, frame in enumerate(frames):
        label = frame['scenario'] + ' / ' + frame['capture']
        ref = reference_for(label)
        pair = beside(refs[ref], frame['top'])
        pair.save(output / f'{n:02d}-{frame["scenario"]}-{frame["capture"]}.png')
        comparisons.append(pair)
        delta = [max(abs(a - b) for a, b in zip(refs[ref].pixels[i:i + 3], frame['top'].pixels[i:i + 3]))
                 for i in range(0, len(frame['top'].pixels), 3)]
        records.append(dict(preview=label, reference=ref, mean_rgb_max=sum(delta) / len(delta),
                            pixels_over_24=sum(v > 24 for v in delta) / len(delta)))
        # 動きの途中より、止まった場面を 17 場面の一覧に優先する。
        if ref not in chosen or not any(word in frame['capture'] for word in ('opening', 'closing', 'early', '-048')):
            chosen[ref] = (label, pair)
    def strip(images):
        return Image(800, 240 * len(images), bytearray().join(im.pixels for im in images))
    strip(comparisons).save(output / 'all-scenarios.png')
    preferred = {'01_open': 'states / opened', '02_folder': 'states / folder-off',
                 '04a_reserve': 'ca2-reserved / reserved', '05a_values': 'ca2-disabled / values',
                 '06a_number': 'entries / number', '06b_list': 'entries / selection',
                 '06c_key': 'ca2-key / recording', '07a_dialog': 'ca2-reserved / close-confirm',
                 '09_hold': 'hold / progress-372'}
    for ref, label in preferred.items():
        for record, pair in zip(records, comparisons):
            if record['preview'] == label:
                chosen[ref] = (label, pair)
    representatives = []
    for ref in REFERENCES:
        if ref not in chosen:
            raise ValueError(f'CA2 の見本に対応する台本がありません: {ref}')
        representatives.append(chosen[ref][1])
    strip(representatives).save(output / 'ca2-17.png')
    rows = ['<!doctype html><meta charset="utf-8"><title>CA2 の比較</title>',
            '<style>body{background:#202830;color:#eee;font:14px sans-serif}img{image-rendering:pixelated;max-width:100%}</style>',
            '<p>左: CA2 の見本 / 右: 実際のメニューのプレビュー。背景・状態・時刻が異なる組も含みます。</p>']
    for record, pair in zip(records, comparisons):
        rows.append('<p>' + html.escape(record['reference'] + ' / ' + record['preview']) + '</p>')
        rows.append('<img src="data:image/png;base64,' + base64.b64encode(pair.png()).decode() + '">')
    (output / 'index.html').write_text('\n'.join(rows), encoding='utf-8')
    (output / 'metrics.json').write_text(json.dumps(dict(frames=records, representatives={k: v[0] for k, v in chosen.items()}), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
