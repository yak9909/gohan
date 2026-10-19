"""時刻付きの入力列を、実際のフレーム呼び出しの列へ展開する。"""
from __future__ import annotations

import re


# CTRPF の Key。ui/input/buttons.cpp を通すため、menu のビット値ではない。
KEYS = dict(A=1, B=2, Select=4, Start=8, Right=16, Left=32, Up=64, Down=128,
            R=256, L=512, X=1024, Y=2048, ZL=16384, ZR=32768)


def name(value: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}', value):
        raise ValueError('名前は英数字・ハイフン・下線の 1〜80 文字にしてください')
    return value


def milliseconds(value) -> int:
    if type(value) is not int or not 0 <= value <= 600_000:
        raise ValueError('時刻は 0〜600000 の整数ミリ秒にしてください')
    return value


def protocol(scenario: dict, frame_ms: int) -> str:
    if type(frame_ms) is not int or not 1 <= frame_ms <= 100:
        raise ValueError('frame_ms は 1〜100 の整数にしてください')
    name(scenario['name'])
    events = scenario.get('events', [])
    captures = scenario['captures']
    if not captures:
        raise ValueError('出力するフレームを 1 つ以上指定してください')
    times, event_at, capture_at = set(), {}, {}
    capture_names = set()
    previous = -1
    for event in events:
        at = milliseconds(event['at'])
        if at <= previous:
            raise ValueError('入力時刻は重複せず昇順にしてください')
        previous = at
        unknown = set(event) - {'at', 'buttons', 'touch', 'toggle'}
        if unknown:
            raise ValueError(f'未対応の入力: {sorted(unknown)}')
        buttons = event.get('buttons', [])
        if not isinstance(buttons, list) or any(key not in KEYS for key in buttons):
            raise ValueError('ボタン名が不正です')
        touch = event.get('touch')
        if touch is not None and (not isinstance(touch, list) or len(touch) != 2 or
                                 any(type(v) is not int for v in touch) or
                                 not (0 <= touch[0] < 320 and 0 <= touch[1] < 240)):
            raise ValueError('タッチ座標は下画面の整数座標で指定してください')
        if type(event.get('toggle', False)) is not bool:
            raise ValueError('toggle は真偽値にしてください')
        event_at[at] = event
        times.add(at)
    for i, capture in enumerate(captures):
        at = milliseconds(capture['at'])
        capture_name = name(capture['name'])
        if at in capture_at or capture_name in capture_names:
            raise ValueError('出力の時刻・名前は台本内で重複できません')
        capture_names.add(capture_name)
        capture_at[at] = i
        times.add(at)
    end = max(capture_at)
    if events and max(event_at) > end:
        raise ValueError('最後の出力より後に入力があります')
    times.update(range(0, end + 1, frame_ms))
    buttons, touch = [], None
    lines = []
    for at in sorted(times):
        event = event_at.get(at, {})
        buttons = event.get('buttons', buttons)
        touch = event.get('touch', touch)
        keys = sum(KEYS[k] for k in set(buttons))
        x, y = touch or (0, 0)
        lines.append(f'{at} {keys} {int(touch is not None)} {x} {y} '
                     f'{int(event.get("toggle", False))} {capture_at.get(at, -1)}')
    return '\n'.join(lines) + '\n'
