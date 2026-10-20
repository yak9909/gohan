"""位置を一定の比較領域で探索し、RGB または輝度の差を数値化する。"""
from __future__ import annotations

from .images import Image


def compare(preview: Image, capture: Image, radius: int = 4, region=None, luma: bool = False) -> tuple[dict, Image, Image]:
    if (preview.width, preview.height) != (capture.width, capture.height):
        raise ValueError('比較する画像の寸法が違います')
    if type(radius) is not int or not 0 <= radius <= 16:
        raise ValueError('位置探索は 0〜16 px にしてください')
    width, height = preview.width, preview.height
    x0, y0, x1, y1 = region or (0, 0, width, height)
    if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
        raise ValueError('比較領域が画像の外か空です')
    # どの候補にも同じ画素を使う。画面外へ逃がして平均差を小さくする候補を防ぐ。
    x0, y0, x1, y1 = max(x0, radius), max(y0, radius), min(x1, width - radius), min(y1, height - radius)
    if x0 >= x1 or y0 >= y1:
        raise ValueError('位置探索の余白を除くと比較領域が空です')
    pixels = (x1 - x0) * (y1 - y0)
    a, b = preview.pixels, capture.pixels

    def differences(dx, dy):
        for y in range(y0, y1):
            for x in range(x0, x1):
                i, j = (y * width + x) * 3, ((y + dy) * width + x + dx) * 3
                dr, dg, db = b[j] - a[i], b[j + 1] - a[i + 1], b[j + 2] - a[i + 2]
                luminance = .299 * dr + .587 * dg + .114 * db
                yield abs(luminance) if luma else max(abs(dr), abs(dg), abs(db)), luminance

    scores = []
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            total = sum(d for d, _ in differences(dx, dy))
            scores.append((total / pixels, dx * dx + dy * dy, dx, dy))
    mean, _, dx, dy = min(scores)
    values = list(differences(dx, dy))
    report = {
        'mode': 'luma' if luma else 'rgb_max', 'region': [x0, y0, x1, y1], 'pixels': pixels,
        'search_radius': radius, 'shift_capture': [dx, dy], 'mean': mean,
        'mean_unshifted': next(s[0] for s in scores if s[2:] == (0, 0)),
        'max': max(d for d, _ in values),
        'over_8_percent': sum(d > 8 for d, _ in values) * 100 / pixels,
        'over_24_percent': sum(d > 24 for d, _ in values) * 100 / pixels,
        'signed_luma_mean_capture_minus_preview': sum(v for _, v in values) / pixels,
    }
    diff = Image.solid(width, height, (0, 0, 0))
    side = Image.solid(width * 3, height, (0, 0, 0))
    for y in range(height):
        for x in range(width):
            i, j = (y * width + x) * 3, (y * width * 3 + x) * 3
            cap_x, cap_y = x + dx, y + dy
            c = (cap_y * width + cap_x) * 3
            if 0 <= cap_x < width and 0 <= cap_y < height:
                side.pixels[j:j + 3] = b[c:c + 3]
            side.pixels[j + width * 3:j + width * 3 + 3] = a[i:i + 3]
    for (y, x), (d, _) in zip(((y, x) for y in range(y0, y1) for x in range(x0, x1)), values):
        value = min(255, int(d * 4 + .5))
        i = (y * width + x) * 3
        diff.pixels[i:i + 3] = bytes([value] * 3)
    for y in range(height):
        i, j = y * width * 3, (y * width * 3 + width * 2) * 3
        side.pixels[j:j + width * 3] = diff.pixels[i:i + width * 3]
    return report, diff, side
