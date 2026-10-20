"""利用者の任意の RomFS 設定を、共通 C++ 読み込み器へ渡す。資源は複製しない。"""
from pathlib import Path
import os
import tomllib


def environment(root: Path, config: Path | None, output: Path) -> tuple[dict, bool]:
    data = tomllib.loads(config.read_text(encoding='utf-8')) if config and config.exists() else {}
    rom = data.get('rom', {})
    env = os.environ.copy()
    configured = False
    for key, variable in (('acnl_update_romfs', 'GOHAN_PREVIEW_ROMFS_UPDATE'),
                          ('acnl_base_romfs', 'GOHAN_PREVIEW_ROMFS_BASE')):
        env.pop(variable, None)
        value = rom.get(key)
        if value is None:
            continue
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f'rom.{key} は RomFS のルートへのパスにしてください')
        path = Path(value)
        if not path.is_absolute():
            path = config.parent / path
        path = path.resolve()
        if path.is_relative_to(root.resolve()):
            raise ValueError('ROM の展開物は gohan の作業ツリー外に置いてください')
        env[variable] = str(path)
        configured = True
    if configured and output.resolve().is_relative_to(root.resolve()):
        raise ValueError('ROM を使うプレビューの --out は作業ツリー外にしてください')
    return env, configured
