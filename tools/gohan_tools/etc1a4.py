"""32×32 アイコン用の ETC1A4 と RGBA8 PNG の読み書き。"""
from pathlib import Path
import struct
import zlib

TABLE = [(2, 8), (5, 17), (9, 29), (13, 42), (18, 60), (24, 80), (33, 106), (47, 183)]
# 選択値 0..3 = +a, +b, -a, -b（MSB,LSB = 00 +a / 01 +b / 10 -a / 11 -b）
MODS = lambda t: (TABLE[t][0], TABLE[t][1], -TABLE[t][0], -TABLE[t][1])


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else v


def ext4(v):
    return (v << 4) | v


def ext5(v):
    return (v << 3) | (v >> 2)


def decode_block(etc, alpha):
    hi = etc >> 32
    lo = etc & 0xFFFFFFFF
    flip = hi & 1
    diff = (hi >> 1) & 1
    t1 = (hi >> 5) & 7
    t2 = (hi >> 2) & 7
    if diff:
        r1, g1, b1 = (hi >> 27) & 31, (hi >> 19) & 31, (hi >> 11) & 31

        def sx(v):
            return v - 8 if v & 4 else v
        r2, g2, b2 = r1 + sx((hi >> 24) & 7), g1 + sx((hi >> 16) & 7), b1 + sx((hi >> 8) & 7)
        c1 = (ext5(r1), ext5(g1), ext5(b1))
        c2 = (ext5(r2 & 31), ext5(g2 & 31), ext5(b2 & 31))
    else:
        c1 = (ext4((hi >> 28) & 15), ext4((hi >> 20) & 15), ext4((hi >> 12) & 15))
        c2 = (ext4((hi >> 24) & 15), ext4((hi >> 16) & 15), ext4((hi >> 8) & 15))
    px = [[None] * 4 for _ in range(4)]
    for x in range(4):
        for y in range(4):
            i = x * 4 + y
            sel = ((lo >> i) & 1) | (((lo >> (16 + i)) & 1) << 1)
            second = (y >= 2) if flip else (x >= 2)
            base, t = (c2, t2) if second else (c1, t1)
            m = (TABLE[t][0], TABLE[t][1], -TABLE[t][0], -TABLE[t][1])[sel]
            a = ext4((alpha >> (4 * i)) & 15)
            px[y][x] = (clamp(base[0] + m), clamp(base[1] + m), clamp(base[2] + m), a)
    return px


def block_order(w, h):
    """(ブロックの x, y) をデータの並び順で返す"""
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for bx, by in ((0, 0), (4, 0), (0, 4), (4, 4)):
                yield tx + bx, ty + by


def decode(data, w, h):
    img = [[(0, 0, 0, 0)] * w for _ in range(h)]
    o = 0
    for bx, by in block_order(w, h):
        alpha, etc = struct.unpack_from('<QQ', data, o)
        o += 16
        px = decode_block(etc, alpha)
        for y in range(4):
            for x in range(4):
                img[by + y][bx + x] = px[y][x]
    return img


def encode_block(px):
    """px[y][x] = (r,g,b,a)。個別モード（4 ビットの基本色 2 つ）で、flip の両方を試して誤差の小さい方"""
    best = None
    alpha = 0
    for x in range(4):
        for y in range(4):
            alpha |= (px[y][x][3] >> 4) << (4 * (x * 4 + y))
    for flip in (0, 1):
        halves = []
        for half in (0, 1):
            pts = [(x, y) for x in range(4) for y in range(4) if ((y >= 2) if flip else (x >= 2)) == bool(half)]
            # 見えない画素（アルファ 0）は色を合わせなくてよい
            vis = [p for p in pts if px[p[1]][p[0]][3] > 0] or pts
            avg = [sum(px[y][x][c] for x, y in vis) / len(vis) for c in range(3)]
            bestHalf = None
            for t in range(8):
                mods = MODS(t)
                q = [min(15, max(0, int(round(avg[c] / 17.0)))) for c in range(3)]
                base = [ext4(v) for v in q]
                err = 0
                sels = {}
                for x, y in pts:
                    p = px[y][x]
                    w8 = 1 if p[3] > 0 else 0
                    e_best, s_best = None, 0
                    for s, m in enumerate(mods):
                        e = sum((clamp(base[c] + m) - p[c]) ** 2 for c in range(3))
                        if e_best is None or e < e_best:
                            e_best, s_best = e, s
                    err += e_best * w8
                    sels[(x, y)] = s_best
                if bestHalf is None or err < bestHalf[0]:
                    bestHalf = (err, q, t, sels)
            halves.append(bestHalf)
        total = halves[0][0] + halves[1][0]
        if best is None or total < best[0]:
            best = (total, flip, halves)
    _, flip, halves = best
    (_, q1, t1, s1), (_, q2, t2, s2) = halves
    hi = (q1[0] << 28) | (q2[0] << 24) | (q1[1] << 20) | (q2[1] << 16) | (q1[2] << 12) | (q2[2] << 8) | (t1 << 5) | (t2 << 2) | flip
    lo = 0
    sel = dict(s1)
    sel.update(s2)
    for (x, y), s in sel.items():
        i = x * 4 + y
        lo |= (s & 1) << i
        lo |= ((s >> 1) & 1) << (16 + i)
    return alpha, (hi << 32) | lo


def encode(img, w, h):
    out = bytearray()
    for bx, by in block_order(w, h):
        px = [[img[by + y][bx + x] for x in range(4)] for y in range(4)]
        a, e = encode_block(px)
        out += struct.pack('<QQ', a, e)
    return bytes(out)


# ---- PNG ----
def read_png(path):
    b = Path(path).read_bytes()
    if b[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('PNG の見出しが不正です')
    w = h = 0
    ended = False
    i = 8
    idat = b''
    while i < len(b):
        if len(b) - i < 12:
            raise ValueError('PNG のチャンクが途中で終わっています')
        n, t = struct.unpack('>I4s', b[i:i + 8])
        if n > len(b) - i - 12:
            raise ValueError('PNG のチャンクが範囲外です')
        c = b[i + 8:i + 8 + n]
        if zlib.crc32(t + c) & 0xFFFFFFFF != struct.unpack_from('>I', b, i + 8 + n)[0]:
            raise ValueError('PNG の CRC が一致しません')
        if t == b'IHDR':
            if i != 8 or n != 13:
                raise ValueError('PNG の画像見出しが不正です')
            w, h, bd, ct, comp, filt, interlace = struct.unpack('>IIBBBBB', c)
            if bd != 8 or ct != 6 or comp or filt or interlace or not 0 < w <= 64 or not 0 < h <= 64:
                raise ValueError('64×64 以下の非インターレース RGBA8 PNG が必要です')
        if t == b'IDAT':
            idat += c
        i += 12 + n
        if t == b'IEND':
            ended = True
            break
    if not w or not h or not ended:
        raise ValueError('PNG の画像か終端がありません')
    decoder = zlib.decompressobj()
    expected = h * (w * 4 + 1)
    try:
        raw = decoder.decompress(idat, expected + 1)
    except zlib.error as error:
        raise ValueError('PNG の画素を復号できません') from error
    if len(raw) != expected or not decoder.eof or decoder.unused_data:
        raise ValueError('PNG の画素長が不正です')
    st = w * 4
    prev = bytearray(st)
    img = []
    p = 0
    for _ in range(h):
        f = raw[p]
        if f > 4:
            raise ValueError('PNG のフィルターが不正です')
        p += 1
        line = bytearray(raw[p:p + st])
        p += st
        for x in range(st):
            a = line[x - 4] if x >= 4 else 0
            up = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + up) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + up) >> 1)) & 255
            elif f == 4:
                pp = a + up - c
                pa, pb, pc = abs(pp - a), abs(pp - up), abs(pp - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else up if pb <= pc else c)) & 255
        img.append([tuple(line[x * 4:x * 4 + 4]) for x in range(w)])
        prev = line
    return w, h, img


def write_png(path, img, scale=1):
    h = len(img) * scale
    w = len(img[0]) * scale
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(img[y // scale][x // scale])

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def downscale2(img):
    """2x2 の平均（色はアルファで重み付け）"""
    h, w = len(img) // 2, len(img[0]) // 2
    out = []
    for y in range(h):
        row = []
        for x in range(w):
            ps = [img[2 * y + dy][2 * x + dx] for dy in (0, 1) for dx in (0, 1)]
            asum = sum(p[3] for p in ps)
            if asum == 0:
                row.append((0, 0, 0, 0))
            else:
                row.append(tuple(int(round(sum(p[c] * p[3] for p in ps) / asum)) for c in range(3)) + (int(round(asum / 4)),))
        out.append(row)
    return out

