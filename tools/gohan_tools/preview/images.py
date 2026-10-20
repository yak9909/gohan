"""標準ライブラリだけで扱う RGB PNG と無圧縮の 24/32 bit BMP。"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
import zlib


@dataclass
class Image:
    width: int
    height: int
    pixels: bytearray

    @classmethod
    def solid(cls, width: int, height: int, color: tuple[int, int, int]) -> Image:
        return cls(width, height, bytearray(color) * (width * height))

    def crop(self, x: int, y: int, width: int, height: int) -> Image:
        if not (0 <= x < x + width <= self.width and 0 <= y < y + height <= self.height):
            raise ValueError('切り出す範囲が画像の外です')
        data = bytearray()
        for row in range(y, y + height):
            start = (row * self.width + x) * 3
            data.extend(self.pixels[start:start + width * 3])
        return Image(width, height, data)

    def png(self) -> bytes:
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
        rows = b''.join(b'\0' + self.pixels[y * self.width * 3:(y + 1) * self.width * 3]
                        for y in range(self.height))
        return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', self.width, self.height, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(self.png())


def read_png(data: bytes) -> Image:
    pos, compressed, header = 8, bytearray(), None
    ended = False
    while pos + 12 <= len(data):
        size = struct.unpack_from('>I', data, pos)[0]
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + size]
        if pos + size + 12 > len(data) or zlib.crc32(kind + body) != struct.unpack_from('>I', data, pos + 8 + size)[0]:
            raise ValueError('PNG の長さまたは CRC が不正です')
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT':
            compressed.extend(body)
        elif kind == b'IEND':
            ended = True
            break
        elif kind[0] & 32 == 0:
            raise ValueError('未対応の PNG チャンクです')
        pos += size + 12
    if not header or not ended:
        raise ValueError('PNG のヘッダーまたは終端がありません')
    width, height, depth, color, compression, filtering, interlace = header
    if depth != 8 or color not in (2, 6) or compression or filtering or interlace:
        raise ValueError('PNG は非インターレースの 8 bit RGB/RGBA が必要です')
    validate_size(width, height)
    channels = 3 if color == 2 else 4
    stride = width * channels
    raw = zlib.decompress(compressed)
    if len(raw) != (stride + 1) * height:
        raise ValueError('PNG の画素数が一致しません')
    previous, pixels = bytearray(stride), bytearray()
    for y in range(height):
        start = y * (stride + 1)
        mode, row = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if mode > 4:
            raise ValueError('PNG のフィルターが不正です')
        for i in range(stride):
            a, b = row[i - channels] if i >= channels else 0, previous[i]
            c = previous[i - channels] if i >= channels else 0
            p = a + b - c
            paeth = min((a, b, c), key=lambda v: abs(p - v))
            row[i] = (row[i] + (0, a, b, (a + b) // 2, paeth)[mode]) & 255
        for x in range(width):
            i = x * channels
            if channels == 4 and row[i + 3] != 255:
                raise ValueError('背景・比較用の PNG は不透明な画像にしてください')
            pixels.extend(row[i:i + 3])
        previous = row
    return Image(width, height, pixels)


def validate_size(width: int, height: int) -> None:
    if not (0 < width <= 4096 and 0 < height <= 4096):
        raise ValueError('画像の寸法は 1〜4096 にしてください')


def read_bmp(data: bytes) -> Image:
    if len(data) < 54:
        raise ValueError('BMP のヘッダーが不足しています')
    offset, dib = struct.unpack_from('<II', data, 10)
    width, signed_height, planes, bits, compression = struct.unpack_from('<iiHHI', data, 18)
    height = abs(signed_height)
    validate_size(width, height)
    if dib < 40 or offset < 14 + dib or planes != 1 or bits not in (24, 32) or compression != 0:
        raise ValueError('BMP は無圧縮の 24/32 bit RGB が必要です')
    stride = ((width * bits + 31) // 32) * 4
    if len(data) < offset + stride * height:
        raise ValueError('BMP の画素が不足しています')
    pixels = bytearray()
    for y in range(height):
        start = offset + (height - 1 - y if signed_height > 0 else y) * stride
        for x in range(width):
            i = start + x * (bits // 8)
            pixels.extend((data[i + 2], data[i + 1], data[i]))
    return Image(width, height, pixels)


def read(path: Path) -> Image:
    data = path.read_bytes()
    if data.startswith(b'BM'):
        return read_bmp(data)
    if data.startswith(b'\x89PNG\r\n\x1a\n'):
        return read_png(data)
    raise ValueError('PNG または BMP が必要です')


def screen_image(image: Image, screen: str) -> Image:
    """単独画面、または上400・下中央320の400×480画像から取り出す。"""
    width = 400 if screen == 'top' else 320
    if (image.width, image.height) == (width, 240):
        return image
    if (image.width, image.height) == (400, 480):
        return image.crop(0 if screen == 'top' else 40, 0 if screen == 'top' else 240, width, 240)
    raise ValueError(f'{screen} は {width}×240 または 400×480 の画像が必要です')
