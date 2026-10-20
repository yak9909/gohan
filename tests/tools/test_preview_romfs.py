"""任意のローカル設定と、ROM の資源を作業ツリーに置かない規則。"""
from pathlib import Path
import sys
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from gohan_tools.preview.romfs import environment


def test_missing_config_clears_inherited_paths(tmp_path, monkeypatch):
    monkeypatch.setenv('GOHAN_PREVIEW_ROMFS_UPDATE', 'inherited')
    env, configured = environment(tmp_path / 'repo', tmp_path / 'absent.toml', tmp_path / 'repo/build/preview')
    assert not configured and 'GOHAN_PREVIEW_ROMFS_UPDATE' not in env
    assert not environment(tmp_path / 'repo', None, tmp_path / 'repo/build/preview')[1]


def test_relative_optional_roots_and_external_output(tmp_path):
    root = tmp_path / 'repo'
    root.mkdir()
    config = root / 'gohan.local.toml'
    config.write_text('[rom]\nacnl_update_romfs="../update"\nacnl_base_romfs="../base"\n', encoding='utf-8')
    env, configured = environment(root, config, tmp_path / 'evidence')
    assert configured and env['GOHAN_PREVIEW_ROMFS_UPDATE'] == str(tmp_path / 'update')
    assert env['GOHAN_PREVIEW_ROMFS_BASE'] == str(tmp_path / 'base')
    with pytest.raises(ValueError, match='--out'):
        environment(root, config, root / 'build/preview')


def test_rejects_rom_inside_workspace(tmp_path):
    config = tmp_path / 'gohan.local.toml'
    config.write_text('[rom]\nacnl_base_romfs="romfs"\n', encoding='utf-8')
    with pytest.raises(ValueError, match='展開物'):
        environment(tmp_path, config, tmp_path.parent / 'evidence')
