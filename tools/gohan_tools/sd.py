"""自前のアイテム表を検査し、SD 用の階層とハッシュの控えを作る。"""
from pathlib import Path
import hashlib
import json
import re
import tomllib
from . import hidden_icons


def validate(data: bytes) -> None:
    seen = set()
    for line_no, line in enumerate(data.decode('utf-8-sig').splitlines(), 1):
        if not line or line.startswith('#'):
            continue
        columns = line.split('\t')
        if len(columns) != 4:
            raise ValueError(f'{line_no} 行目: 列は4つ必要です')
        ident, icon, kana, name = columns
        if ident == 'id':
            if columns != ['id', 'icon', 'kana', 'name']:
                raise ValueError(f'{line_no} 行目: 見出しは id・icon・kana・name です')
            continue
        if not re.fullmatch(r'0[xX][0-9a-fA-F]+', ident) or int(ident, 16) > 0x7FFF:
            raise ValueError(f'{line_no} 行目: ID は 0x0000〜0x7FFF の16進数です')
        value = int(ident, 16)
        if value in seen:
            raise ValueError(f'{line_no} 行目: ID が重複しています')
        if not re.fullmatch(r'[0-9]+', icon) or int(icon) > 0xFFFF or not name or '\0' in line:
            raise ValueError(f'{line_no} 行目: 画像番号か名前が不正です')
        seen.add(value)
    if not seen:
        raise ValueError('アイテムの行がありません')


def build(root: Path, output: Path | None = None) -> Path:
    root = root.resolve()
    output = output or root / 'out'
    source = root / 'spec/hidden_items.tsv'
    data = source.read_bytes()
    validate(data)  # 検査が通るまでは出力に触れない。
    destination = output / 'sdmc/luma/plugins/gohan/0004000000086200/hidden_items.tsv'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    manifest = {'version': 1, 'files': [{
        'input': source.relative_to(root).as_posix(), 'input_sha256': digest,
        'output': destination.relative_to(output).as_posix(),
        'output_sha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
    }]}
    local = root / 'gohan.local.toml'
    config = tomllib.loads(local.read_text(encoding='utf-8-sig')) if local.exists() else {}
    folder = config.get('hidden_icons', {}).get('png_directory')
    arc = output / 'sdmc/luma/plugins/gohan/common/hidden_item_icons.arc'
    if folder:
        row = hidden_icons.build(data, root / folder, arc)
        row['output'] = arc.relative_to(output).as_posix()
        manifest['files'].append(row)
    else:
        arc.unlink(missing_ok=True)
        print('PNG フォルダが未指定のため、アイコンの arc は作りません。')
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return destination
