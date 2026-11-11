"""表で指定された PNG を縮小し、SD 用のアイコンを生成する。"""
import hashlib
import math
from pathlib import Path
import struct
from . import darc, etc1a4


def bclim(pixels: bytes) -> bytes:
    if len(pixels) != 1024:
        raise ValueError('ETC1A4 のデータは 1,024 B 必要です')
    return (pixels + struct.pack('<4sHHIII', b'CLIM', 0xFEFF, 0x14, 0x02020000, 1064, 1)
            + struct.pack('<4sIHHII', b'imag', 0x10, 32, 32, 11, 1024))


def psnr(source, decoded):
    errors = [(a[c] - b[c]) ** 2 for row_a, row_b in zip(source, decoded)
              for a, b in zip(row_a, row_b) if a[3] for c in range(3)]
    return 99.0 if not errors or not sum(errors) else 10 * math.log10(255 ** 2 * len(errors) / sum(errors))


def build(table: bytes, folder: Path, destination: Path) -> dict:
    wanted = sorted({int(line.split('\t')[1]) for line in table.decode('utf-8-sig').splitlines()
                     if line.lower().startswith('0x') and int(line.split('\t')[1])})
    if not folder.is_dir():
        raise ValueError('アイコンの PNG フォルダがありません')
    files, missing, scores, inputs = {}, [], [], []
    for number in wanted:
        path = folder / f'{number}.png'
        if not path.is_file():
            missing.append(number)
            continue
        width, height, image = etc1a4.read_png(path)
        if (width, height) != (64, 64):
            raise ValueError(f'{number}.png は 64×64 RGBA である必要があります')
        small = etc1a4.downscale2(image)
        encoded = etc1a4.encode(small, 32, 32)
        scores.append((psnr(small, etc1a4.decode(encoded, 32, 32)), number))
        files[f'timg/{number}.bclim'] = bclim(encoded)
        inputs.append({'input': f'hidden_icons.png_directory/{number}.png',
                       'input_sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    blob = darc.pack(files)
    if len(blob) > 512 * 1024:
        raise ValueError('アイコンの arc が 512 KiB を超えています')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(blob)
    if darc.read(destination.read_bytes()) != files:
        raise ValueError('アイコンの書き出しと読み直しが一致しません')
    worst = min(scores) if scores else None
    print(f'絵 {len(files)} 枚（表の番号 {len(wanted)} 種、PNG が無い番号 {missing}）、'
          f'{len(blob):,} B、読み直し一致')
    if worst:
        print(f'見える画素の PSNR 最悪 {worst[0]:.2f} dB（番号 {worst[1]}）')
    return {'input_version': 1, 'inputs': inputs, 'count': len(files), 'bytes': len(blob),
            'wanted': len(wanted), 'missing': missing, 'worst_psnr': worst[0] if worst else None,
            'worst_number': worst[1] if worst else None,
            'output_sha256': hashlib.sha256(blob).hexdigest()}
