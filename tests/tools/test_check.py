"""ゲームの入力を使わず、自作の文字列と小さなファイルで境界を確かめる。"""
import importlib.util
from pathlib import Path
import sys

import pytest

SPEC = importlib.util.spec_from_file_location('gohan_check', Path(__file__).resolve().parents[2] / 'tools/check.py')
check = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = check
SPEC.loader.exec_module(check)


def rules(path, content=b'', allow=()):
    if isinstance(content, str):
        content = content.encode()
    return {p.rule for p in check.scan(path, content, allow)}


@pytest.mark.parametrize('suffix', sorted(check.FORMATS))
def test_game_formats_even_when_binary(suffix):
    assert 'game-format' in rules('assets/sample' + suffix.upper(), b'\xff')


@pytest.mark.parametrize('path', ['build/a.txt', 'out/a.txt', 'gohan.local.toml'])
def test_local_outputs(path):
    assert 'generated' in rules(path)


def test_size_boundary_and_binary():
    assert not rules('a.png', b'\xff' * 1_000_000)
    assert rules('a.png', b'\xff' * 1_000_001) == {'size'}
    assert not rules('docs/build.md')
    assert not rules('gohan.local.toml.example')


@pytest.mark.parametrize('path', [
    'C:' + '\\' + 'Users' + '\\' + 'someone' + '\\' + 'data',
    'D:' + '/Users' + '/someone/data',
    '/' + 'home' + '/someone/data',
    '/' + 'Users' + '/someone/data',
    '/' + 'root' + '/data',
])
def test_personal_paths(path):
    assert rules('README.md', '入力: ' + path) == {'personal-path'}


def test_generic_input_paths():
    assert not rules('settings.example', 'D:/game-inputs/sample\n./inputs\nhttps://example.org/homepage')


@pytest.mark.parametrize(('source', 'dest'), [
    ('core', 'game'), ('core', 'features'), ('game', 'ui'),
    ('ui', 'menu'), ('world', 'ui'), ('menu', 'world'),
    ('ui', 'features'), ('menu', 'features'), ('world', 'features'),
])
@pytest.mark.parametrize('style', ['root', 'layer', 'relative'])
def test_include_direction(source, dest, style):
    target = {'root': f'src/{dest}/api.hpp', 'layer': f'{dest}/api.hpp',
              'relative': f'../{dest}/api.hpp'}[style]
    assert rules(f'src/{source}/a.cpp', f'# include "{target}"') == {'include'}


@pytest.mark.parametrize(('source', 'dest'), [
    ('features', 'menu'), ('features', 'ui'), ('features', 'world'),
    ('features', 'game'), ('features', 'core'), ('menu', 'game'),
    ('world', 'game'), ('ui', 'core'), ('game', 'core'), ('core', 'core'),
])
def test_allowed_includes(source, dest):
    assert not rules(f'src/{source}/example/a.cpp', f'#include <{dest}/api.hpp>')


def test_feature_boundaries_and_local_headers():
    assert rules('src/features/player/a.cpp', '#include "features/items/api.hpp"') == {'include'}
    assert not rules('src/features/player/a.cpp', '#include "detail.hpp"')
    assert not rules('src/features/player/a.cpp', '#include "features/player/api.hpp"')


@pytest.mark.parametrize('literal', ['0x00100000', '0X3FFFFFFF', '0x100000UL', "0x0010'0000u"])
def test_address_bounds(literal):
    assert rules('src/ui/a.cpp', literal) == {'address'}
    assert not rules('src/game/a.cpp', literal)


@pytest.mark.parametrize('literal', ['0x000FFFFF', '0x40000000', '0xFFFFFFFF', '0x10', 'name0x100000'])
def test_non_addresses(literal):
    assert not rules('src/ui/a.cpp', literal)


@pytest.mark.parametrize('token', ['std::atomic<int>', 'std :: atomic_flag', '__atomic_load_n', 'volatile int'])
def test_concurrency(token):
    assert rules('src/game/a.hpp', token) == {'concurrency'}
    assert not rules('src/core/a.hpp', token)


def test_comments_strings_and_lines():
    text = '''// #include "features/items/a.hpp"
/* std::atomic<int> x; 0x100000; */
const char *s = "volatile 0x100000";
auto raw = R"tag(volatile 0x100000)tag";
int x = 0x100000;
volatile int y;
'''
    problems = check.scan('src/ui/a.cpp', text.encode())
    assert [(p.rule, p.line) for p in problems] == [('address', 5), ('concurrency', 6)]


def test_unknown_src_subdir_is_not_exempt():
    assert rules('src/misc/a.cpp', 'volatile int x = 0x100000;') == {'address', 'concurrency'}


def test_allow_is_exact_and_rule_scoped():
    allow = check.load_allow(b'''[[allow]]
path = "tests/sample.arc"
rule = "game-format"
reason = "handmade sample"
''')
    assert not rules('tests/sample.arc', b'', allow)
    assert rules('other/sample.arc', b'', allow) == {'game-format'}
    assert rules('tests/sample.arc', b'x' * 1_000_001, allow) == {'size'}


def test_allow_line():
    allow = [{'path': 'src/ui/a.cpp', 'rule': 'address', 'reason': 'test', 'line': 1}]
    problems = check.scan('src/ui/a.cpp', b'0x100000;\n0x100000;', allow)
    assert [p.line for p in problems] == [2]


@pytest.mark.parametrize('config', [
    b'allow = "all"', b'[[allow]]\npath="a"\nrule="size"',
    b'[[allow]]\npath="*"\nrule="size"\nreason="test"',
    b'[[allow]]\npath="a"\nrule="unknown"\nreason="test"',
    b'[[allow]]\npath="a"\nrule=[]\nreason="test"',
    b'[[allow]]\npath="a"\nrule="size"\nreason="test"\nline=0',
])
def test_bad_allow(config):
    with pytest.raises(ValueError):
        check.load_allow(config)


def no_git(*args, **kwargs):
    pytest.fail('明示的な一覧の検査から git を呼び出した')


@pytest.mark.parametrize('mode', ['--all', '--staged'])
@pytest.mark.parametrize('separator', ['\n', '\0'])
def test_explicit_list_without_git(tmp_path, monkeypatch, mode, separator):
    monkeypatch.setattr(check.subprocess, 'run', no_git)
    (tmp_path / 'read me.md').write_text('hello', encoding='utf-8')
    listing = tmp_path / 'list.txt'
    listing.write_text('read me.md' + separator, encoding='utf-8')
    assert check.main([mode, '--root', str(tmp_path), '--files-from', str(listing)]) == 0


def test_explicit_empty_list_and_failure_codes(tmp_path, monkeypatch):
    monkeypatch.setattr(check.subprocess, 'run', no_git)
    listing = tmp_path / 'list.txt'
    listing.write_bytes(b'')
    assert check.main(['--all', '--root', str(tmp_path), '--files-from', str(listing)]) == 0
    (tmp_path / 'a.bin').write_bytes(b'')
    assert check.main(['--all', '--root', str(tmp_path), 'a.bin']) == 1
    assert check.main(['--all', '--root', str(tmp_path), 'missing']) == 2
    assert check.main(['--all', '--root', str(tmp_path), '../outside']) == 2


def test_all_uses_tracked_files_only(tmp_path, monkeypatch):
    (tmp_path / 'tracked.md').write_text('ok', encoding='utf-8')
    (tmp_path / 'untracked.bin').write_bytes(b'')

    def fake_git(root, *args):
        assert args == ('ls-files', '-z')
        return b'tracked.md\0'

    monkeypatch.setattr(check, 'git_bytes', fake_git)
    assert check.main(['--all', '--root', str(tmp_path)]) == 0


def test_staged_reads_index_and_index_allow(tmp_path, monkeypatch):
    # 作業ツリーの安全な内容と無関係に、インデックスの違反を検出する。
    source = tmp_path / 'src/ui/a.cpp'
    source.parent.mkdir(parents=True)
    source.write_text('int safe;', encoding='utf-8')
    calls = []

    def fake_git(root, *args):
        calls.append(args)
        if args[0] == 'diff':
            assert '--diff-filter=ACMR' in args  # 削除済みファイルは読まない。
            return b'src/ui/a.cpp\0'
        if args[0] == 'ls-files':
            return b'tools/check_allow.toml\0'
        if args == ('show', ':tools/check_allow.toml'):
            return b'allow = []'
        assert args == ('show', ':src/ui/a.cpp')
        return b'volatile int value;'

    monkeypatch.setattr(check, 'git_bytes', fake_git)
    assert check.main(['--staged', '--root', str(tmp_path)]) == 1
    assert ('show', ':src/ui/a.cpp') in calls
    assert ('show', ':tools/check_allow.toml') in calls


def test_git_error_is_operational_error(tmp_path, monkeypatch):
    def fail(*args):
        raise ValueError('read failed')

    monkeypatch.setattr(check, 'git_bytes', fail)
    assert check.main(['--all', '--root', str(tmp_path)]) == 2
