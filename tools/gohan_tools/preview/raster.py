"""Canvas の指示を LA4 の字形と頂点色で合成する。較正値は settings.toml に集約する。"""
from __future__ import annotations

import json
from pathlib import Path

from .images import Image


class Raster:
    def __init__(self, directory: Path, settings: dict):
        self.font = json.loads((directory / 'fonts.json').read_text(encoding='utf-8'))
        self.atlas = (directory / 'atlas.la4').read_bytes()
        self.maps = [dict(zip(self.font[name][::2], self.font[name][1::2])) for name in ('main', 'numeric')]
        self.settings = settings
        self.textures = []
        if (directory / 'textures.json').exists():
            for item in json.loads((directory / 'textures.json').read_text(encoding='utf-8')):
                self.textures.append((item, (directory / item['file']).read_bytes()))

    def image(self, output, command, dx, dy):
        x, y, w, h = command['bounds']
        cx, cy, cw, ch = command['clip']
        sx, sy, sw, sh = command['source']
        texture, data = self.textures[command['texture']]
        tw, th = texture['width'], texture['height']
        for yy in range(max(y, cy, -dy), min(y + h, cy + ch, output.height - dy)):
            for xx in range(max(x, cx, -dx), min(x + w, cx + cw, output.width - dx)):
                if command['kind'] == 3:
                    tx = (sx + (xx - x) // command['scale']) % tw
                    ty = (sy + (yy - y) // command['scale']) % th
                else:
                    tx = sx + (xx - x) * sw // w
                    ty = sy + (yy - y) * sh // h
                morton = sum(((tx >> i) & 1) << (2 * i) | ((ty >> i) & 1) << (2 * i + 1) for i in range(3))
                offset = (((ty // 8) * (tw // 8) + tx // 8) * 64 + morton) * 4
                channels = data[offset:offset + 4][::-1]
                tint = command['color']
                # RGBA8 の各色と頂点色を掛ける。最終合成の較正は字と共通。
                color = ((channels[3] * (tint >> 24) // 255) << 24 |
                         (channels[0] * (tint >> 16 & 255) // 255) << 16 |
                         (channels[1] * (tint >> 8 & 255) // 255) << 8 |
                         channels[2] * (tint & 255) // 255)
                self.blend(output, xx + dx, yy + dy, color)

    def texel(self, x: int, y: int) -> tuple[int, int]:
        morton = sum(((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1) for i in range(3))
        offset = ((y // 8) * (self.font['sheet'][0] // 8) + x // 8) * 64 + morton
        value = self.atlas[offset]
        # LA4 は 1 byte 内の上位が輝度、下位が alpha。
        return (value >> 4) * 17, (value & 15) * 17

    def blend(self, image: Image, x: int, y: int, color: int, luminance: int = 255, alpha: int = 255):
        if not (0 <= x < image.width and 0 <= y < image.height):
            return
        model = self.settings
        a = min(1.0, (color >> 24) / 255 * alpha / 255 * model['alpha_gain'])
        source = [(color >> shift) & 255 for shift in (16, 8, 0)]
        if model['swap_red_blue']:
            source.reverse()
        index = (y * image.width + x) * 3
        for channel in range(3):
            src = source[channel] * luminance / 255 * model['rgb_gain'][channel]
            value = min(255, max(0, src)) * a + image.pixels[index + channel] * (1 - a)
            image.pixels[index + channel] = int(value + (0.5 if model['rounding'] == 'nearest' else 0))

    def draw(self, commands: list[dict], background: Image) -> Image:
        image = Image(background.width, background.height, background.pixels.copy())
        dx, dy = self.settings['offset']
        for command in commands:
            if command['kind'] in (2, 3):
                self.image(image, command, dx, dy)
                continue
            x, y, width, height = command['bounds']
            x += dx
            y += dy
            color = command['color']
            if command['kind'] == 0:
                for yy in range(max(0, y), min(image.height, y + height)):
                    for xx in range(max(0, x), min(image.width, x + width)):
                        self.blend(image, xx, yy, color)
                continue
            cx, cy, cw, ch = command['clip']
            # 実機側の Scene と同じく縦は行全体、横は送り幅全体が入る字だけを残す。
            if y - dy < cy or y - dy + height > cy + ch:
                continue
            scale = command['scale']
            started = False
            for code in command['text']:
                glyph = self.maps[command['font']].get(code)
                if glyph is None:
                    continue
                left, glyph_width, advance = self.font['widths'][glyph * 3:glyph * 3 + 3]
                inside = x - dx >= cx and x - dx + advance * scale <= cx + cw
                if inside:
                    started = True
                    ax = glyph % self.font['columns'] * (self.font['cell'][0] + 1) + 1
                    ay = glyph // self.font['columns'] * (self.font['cell'][1] + 1) + 1
                    for yy in range(self.font['cell'][1]):
                        for xx in range(glyph_width):
                            luminance, alpha = self.texel(ax + xx, ay + yy)
                            if not alpha:
                                continue
                            for sy in range(scale):
                                for sx in range(scale):
                                    self.blend(image, x + (left + xx) * scale + sx,
                                               y + yy * scale + sy, color, luminance, alpha)
                elif started:
                    break
                x += advance * scale
        # Rosalina の実測に合わせた最終出力の色段階。途中の合成精度の断定ではない。
        if self.settings.get('output_format', 'rgb888') == 'rgb565':
            for i, value in enumerate(image.pixels):
                shift = 2 if i % 3 == 1 else 3
                value >>= shift
                image.pixels[i] = (value << shift) | (value >> (8 - 2 * shift))
        return image
