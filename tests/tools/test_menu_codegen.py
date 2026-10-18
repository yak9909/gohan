"""メニューの生成器の試験。ゲームの入力を使わず、自作の小さな spec で確かめる。"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import textwrap

import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('gohan_menu_codegen', ROOT / 'tools/gohan_tools/codegen/menu.py')
menu = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = menu
SPEC.loader.exec_module(menu)


def load(tmp_path, text):
    path = tmp_path / 'menu.toml'
    path.write_text(textwrap.dedent(text), encoding='utf-8')
    return menu.load(path)


def error(tmp_path, text):
    with pytest.raises(menu.SpecError) as raised:
        load(tmp_path, text)
    return str(raised.value)


GOOD = '''
    [[item]]
    id = "root"
    label = "根"
    control = "folder"
      [[item.item]]
      id = "root.flag"
      label = "旗"
      control = "toggle"
      behavior = "state"
      hotkey = "L+Up"
      [[item.item]]
      id = "root.mode"
      label = "方法"
      control = "select"
      options = ["自由", "マス"]
      [[item.item]]
      id = "root.amount"
      label = "量"
      control = "number"
      format = "decimal1"
      default = 5.0
      range = [0.1, 99.9]
      step = 0.5
      enabled_if = "root.mode != 'マス'"
    [[item]]
    id = "go"
    label = "実行"
    control = "action"
    behavior = "trigger"
'''


def test_tree_order_and_links(tmp_path):
    items = load(tmp_path, GOOD)
    assert [i.id for i in items] == ['root', 'root.flag', 'root.mode', 'root.amount', 'go']
    root, flag, mode, amount, go = items
    assert root.first_child == flag.index and root.next == go.index and root.parent == menu.NO_ITEM
    assert flag.next == mode.index and mode.next == amount.index and amount.next == menu.NO_ITEM
    assert flag.parent == root.index and go.next == menu.NO_ITEM


def test_values_are_scaled_and_resolved(tmp_path):
    amount = load(tmp_path, GOOD)[3]
    assert (amount.default, amount.minimum, amount.maximum, amount.step) == (50, 1, 999, 5)
    assert amount.cond == ('root.mode', 'Ne', 1)
    flag = load(tmp_path, GOOD)[1]
    assert flag.hotkey == (1 << 1) | (1 << 4) and flag.persist


def test_button_bits_are_fixed():
    assert menu.BUTTONS == ['ZL', 'L', 'R', 'ZR', 'Up', 'Down', 'Left', 'Right', 'A', 'B', 'X', 'Y', 'Select', 'Start']
    assert menu.parse_hotkey('start+zl', 'x') == (1 << 13) | 1


def test_pascal_case():
    assert menu.pascal('player.walk_through_walls') == 'PlayerWalkThroughWalls'
    assert menu.pascal('a.b_c') == 'ABC'


def test_outputs(tmp_path):
    items = load(tmp_path, GOOD)
    ids = menu.render_ids(items)
    assert 'RootAmount = 3,' in ids and 'inline constexpr std::size_t kMenuItemCount = 5;' in ids
    assert 'template <> struct Spec<MenuId::RootFlag>' in ids
    assert 'static constexpr Behavior behavior = Behavior::State;' in ids
    table = menu.render_table(items)
    assert '.hotkey = 18' in table and '.cond = Cond{3, CondOp::Ne, 1}'.replace('3', '2') in table
    assert 'kOptions2[] = {"自由", "マス"}' in table
    assert '.menu = MenuKind::Immediate' in table and '.parent = 65535' in table
    docs = menu.render_docs(items)
    assert docs.endswith('\n') and '\r' not in docs and '`root.flag`' in docs and 'L+Up' in docs


def test_generate_does_not_touch_unchanged_files(tmp_path):
    spec = tmp_path / 'menu.toml'
    spec.write_text(textwrap.dedent(GOOD), encoding='utf-8')
    out, docs = tmp_path / 'gen', tmp_path / 'menu.md'
    menu.generate(spec, out, docs)
    stamps = {p: p.stat().st_mtime_ns for p in (out / 'menu_ids.hpp', out / 'menu_table.cpp', docs)}
    old = 10**18  # 内容が同じなら、更新時刻を新しくしない（make が再ビルドしない）
    os.utime(out / 'menu_ids.hpp', ns=(old, old))
    stamps[out / 'menu_ids.hpp'] = old
    menu.generate(spec, out, docs)
    assert {p: p.stat().st_mtime_ns for p in stamps} == stamps
    assert menu.check(spec, docs)
    docs.write_text('古い内容\n', encoding='utf-8')
    assert not menu.check(spec, docs)


def test_main_exit_codes(tmp_path, capsys):
    spec = tmp_path / 'menu.toml'
    spec.write_text(textwrap.dedent(GOOD), encoding='utf-8')
    out, docs = tmp_path / 'gen', tmp_path / 'menu.md'
    assert menu.main(['--spec', str(spec), '--out', str(out), '--docs', str(docs)]) == 0
    assert menu.main(['--spec', str(spec), '--docs', str(docs), '--check']) == 0
    docs.write_text('x\n', encoding='utf-8')
    assert menu.main(['--spec', str(spec), '--docs', str(docs), '--check']) == 1
    spec.write_text('[[item]]\nid = "a"\nlabel = "a"\ncontrol = "toggle"\nbehavior = "state"\nbogus = 1\n', encoding='utf-8')
    assert menu.main(['--spec', str(spec), '--out', str(out)]) == 1
    assert 'bogus' in capsys.readouterr().err


def test_repository_docs_match_spec():
    assert menu.check(ROOT / 'spec/menu.toml', ROOT / 'docs/menu.md')


def test_repository_spec_has_five_behaviors_and_two_sources():
    items = menu.load(ROOT / 'spec/menu.toml')
    assert {i.behavior for i in items if i.control != 'folder'} == {'state', 'hold', 'trigger', 'mode', 'value'}
    assert {i.source for i in items if i.behavior == 'value'} == {'setting', 'game'}


def item(extra, control='toggle', behavior='state'):
    lines = ['[[item]]', 'id = "a"', 'label = "A"', f'control = "{control}"']
    if behavior:
        lines.append(f'behavior = "{behavior}"')
    return '\n'.join(lines + extra) + '\n'


@pytest.mark.parametrize(('text', 'word'), [
    ('[[item]]\nid = "a"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\nbogus = 1\n', 'bogus'),
    ('bogus = 1\n[[item]]\nid = "a"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n', 'bogus'),
    ('not toml [', 'TOML'),
    ('', '[[item]]'),
    ('[[item]]\nid = "a"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n'
     '[[item]]\nid = "a"\nlabel = "B"\ncontrol = "toggle"\nbehavior = "state"\n', '重複'),
    ('[[item]]\nid = "a.b_c"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n'
     '[[item]]\nid = "a_b.c"\nlabel = "B"\ncontrol = "toggle"\nbehavior = "state"\n', '重なります'),
    ('[[item]]\nid = "A"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n', 'id が不正'),
    ('[[item]]\nid = "a"\nlabel = "A"\ncontrol = "dial"\n', 'control'),
    ('[[item]]\nid = "a"\ncontrol = "toggle"\nbehavior = "state"\n', 'label'),
    (item([], behavior=''), 'state / hold / trigger'),
    (item([], 'select', 'state'), 'value だけ'),
    (item([], 'action', 'state'), 'trigger / mode'),
    (item(['source = "game"']), 'source'),
    (item(['lockable = true']), 'lockable'),
    (item(['group = "g"'], behavior='hold'), 'group'),
    (item(['menu = "on_apply"']), 'menu'),
    (item(['menu = "later"'], 'action', 'trigger'), 'menu'),
    (item(['confirm = "hotkey"', 'hotkey = "A"']), 'confirm'),
    (item(['input = "hex"']), 'input'),
    (item(['confirm = "hotkey"'], 'action', 'trigger'), 'ホットキー'),
    (item(['menu = "on_apply"', 'input = "hex"'], 'action', 'trigger'), '入力を受け取れません'),
    (item(['persist = true'], 'action', 'mode'), 'persist'),
    (item(['hotkey = "L+Foo"']), '未知のボタン'),
    (item(['hotkey = "L+L"']), '重なって'),
    (item(['hotkey = "L+A"']) + item(['hotkey = "A+L"']).replace('"a"', '"b"'), 'ホットキー'),
    (item(['format = "hex"']), 'format'),
    (item(['options = ["x"]']), 'options'),
    (item(['range = [0, 1]']), 'range'),
    (item(['default = 1']), 'default'),
    (item([], 'select', ''), 'options'),
    (item(['options = ["x", "x"]'], 'select', ''), '同じ名前'),
    (item(['options = ["x"]', 'default = "y"'], 'select', ''), 'default'),
    (item([], 'number', ''), 'range'),
    (item(['range = [5, 1]'], 'number', ''), '最小'),
    (item(['range = [0, 9]', 'default = 10'], 'number', ''), 'range の外'),
    (item(['range = [0, 9]', 'step = 0'], 'number', ''), 'step'),
    (item(['range = [0.12, 9]', 'format = "decimal1"'], 'number', ''), '小数 1 桁'),
    (item(['range = [0.5, 9]'], 'number', ''), '整数'),
    (item(['range = [0, 9]', 'format = "hex"'], 'slider', ''), 'hex'),
    (item(['source = "game"', 'hotkey = "A"'], 'key', ''), 'setting'),
    (item(['hotkey = "A"'], 'key', ''), 'hotkey'),
    (item(['enabled_if = "zzz == 1"']), '知らない項目'),
    (item(['enabled_if = "a == 1"']), '自分自身'),
    (item(['enabled_if = "???"']), 'enabled_if'),
    (item([], 'folder', ''), '子の項目'),
    (item(['behavior = "state"'], 'folder', '') + '', 'フォルダ'),
    ('[[item]]\nid = "a"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n[[item.item]]\nid = "b"\nlabel = "B"\n'
     'control = "toggle"\nbehavior = "state"\n', '子の項目'),
    ('[[item]]\nid = "a"\nlabel = "A"\ncontrol = "action"\nbehavior = "mode"\ngroup = "g"\n'
     '[[item]]\nid = "b"\nlabel = "B"\ncontrol = "toggle"\nbehavior = "state"\ngroup = "g"\n', '混ぜられません'),
    (item(['options = ["' + 'x' * 1 + '"]'] + [], 'select', '').replace('["x"]', str(['o%d' % n for n in range(40)]).replace("'", '"')), '個まで'),
])
def test_spec_errors(tmp_path, text, word):
    assert word in error(tmp_path, text)


def test_enabled_if_rules(tmp_path):
    base = GOOD.replace('enabled_if = "root.mode != \'マス\'"', 'enabled_if = "%s"')
    for expression in ('root.mode == 0', 'root.flag == true', 'root.flag != false'):
        assert load(tmp_path, base % expression) is not None
    assert 'どれか' not in error(tmp_path, base % 'root.mode == \'無い\'')
    assert '選択肢' in error(tmp_path, base % 'root.mode == \'無い\'')
    assert '== と != だけ' in error(tmp_path, base % 'root.mode < \'マス\'')
    assert 'true / false' in error(tmp_path, base % 'root.mode == true')
    assert '参照できない' in error(tmp_path, base % 'root == 1')


def test_too_many_items(tmp_path):
    text = ''.join(f'[[item]]\nid = "a{n}"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n'
                   for n in range(menu.MAX_ITEMS + 1))
    assert '多すぎます' in error(tmp_path, text)


# ---- 結び付けは、spec と合わないとコンパイルで止まる（ホストの C++ コンパイラがある時だけ） ----
BIND_SPEC = '''
[[item]]
id = "a"
label = "状態"
control = "toggle"
behavior = "state"
[[item]]
id = "b"
label = "押下中"
control = "toggle"
behavior = "hold"
[[item]]
id = "c"
label = "実行"
control = "action"
behavior = "trigger"
[[item]]
id = "d"
label = "入力付き"
control = "action"
behavior = "trigger"
input = "hex"
[[item]]
id = "e"
label = "ゲームの値"
control = "number"
range = [0, 9]
source = "game"
[[item]]
id = "f"
label = "設定の値"
control = "number"
range = [0, 9]
[[item]]
id = "g"
label = "画面"
control = "action"
behavior = "mode"
[[item]]
id = "h"
label = "フォルダ"
control = "folder"
  [[item.item]]
  id = "i"
  label = "子"
  control = "toggle"
  behavior = "state"
'''
PRELUDE = '''
#include "menu/binding.hpp"
#include "menu_ids.hpp"
using namespace gohan::menu;
Result Set(bool) { return Result::Ok; }
void Step(bool) {}
Result Run() { return Result::Ok; }
Result RunWith(u32) { return Result::Ok; }
Result Open() { return Result::Ok; }
bool Read(s32 *) { return true; }
Result Write(s32) { return Result::Ok; }
'''
GOOD_BINDINGS = '''
constexpr Bound kBindings[] = {
    GOHAN_ITEM(MenuId::A, Patch{.set = Set}),
    GOHAN_ITEM(MenuId::B, Hold{.step = Step}),
    GOHAN_ITEM(MenuId::C, Trigger{.run = Run}),
    GOHAN_ITEM(MenuId::D, Trigger{.run_with = RunWith}),
    GOHAN_ITEM(MenuId::E, GameValue{.read = Read, .write = Write}),
    GOHAN_ITEM(MenuId::F, SettingValue{}),
    GOHAN_ITEM(MenuId::G, Mode{.open = Open}),
};
static_assert(kBindings[3].item == 3);
'''


def host_compiler():
    return os.environ.get('HOST_CXX') or shutil.which('g++') or shutil.which('clang++')


def try_compile(tmp_path, body):
    source = tmp_path / 'bind_check.cpp'
    source.write_text(PRELUDE + body, encoding='utf-8')
    result = subprocess.run([host_compiler(), '-std=c++20', '-fsyntax-only', '-fno-rtti', '-fno-exceptions',
                             f'-I{ROOT / "src"}', f'-I{tmp_path / "gen"}', str(source)],
                            capture_output=True, text=True, encoding='utf-8', errors='replace')
    return result.returncode, result.stderr


@pytest.fixture
def bind_env(tmp_path):
    if not host_compiler():
        pytest.skip('PC 用の C++ コンパイラがありません')
    spec = tmp_path / 'bind.toml'
    spec.write_text(BIND_SPEC, encoding='utf-8')
    menu.generate(spec, tmp_path / 'gen', None)
    code, text = try_compile(tmp_path, GOOD_BINDINGS)
    if code != 0:
        pytest.skip('PC 用の C++ コンパイラを使えません: ' + text[-200:])
    return tmp_path


@pytest.mark.parametrize(('binding', 'word'), [
    ('GOHAN_ITEM(MenuId::A, Hold{.step = Step})', 'Hold は振る舞いが hold'),
    ('GOHAN_ITEM(MenuId::B, Patch{.set = Set})', 'Patch は振る舞いが state'),
    ('GOHAN_ITEM(MenuId::A, Trigger{.run = Run})', 'Trigger は振る舞いが trigger'),
    ('GOHAN_ITEM(MenuId::A, Mode{.open = Open})', 'Mode は振る舞いが mode'),
    ('GOHAN_ITEM(MenuId::F, GameValue{.read = Read, .write = Write})', 'GameValue は source が game'),
    ('GOHAN_ITEM(MenuId::E, SettingValue{})', 'SettingValue は source が setting'),
    ('GOHAN_ITEM(MenuId::A, SettingValue{})', 'SettingValue'),
    ('GOHAN_ITEM(MenuId::C, Trigger{.run_with = RunWith})', 'run_and_run_with_must_be_exactly_one'),
    ('GOHAN_ITEM(MenuId::D, Trigger{.run = Run})', 'run_and_run_with_must_be_exactly_one'),
    ('GOHAN_ITEM(MenuId::C, Trigger{.run = Run, .run_with = RunWith})', 'run_and_run_with_must_be_exactly_one'),
    ('GOHAN_ITEM(MenuId::C, Trigger{})', 'run_and_run_with_must_be_exactly_one'),
    ('GOHAN_ITEM(MenuId::A, Patch{.set = nullptr})', 'required_function_is_null'),
    ('GOHAN_ITEM(MenuId::E, GameValue{.read = Read})', 'required_function_is_null'),
    ('GOHAN_ITEM(MenuId::H, Patch{.set = Set})', 'Patch は振る舞いが state'),
    ('GOHAN_ITEM(MenuId::A, 3)', '結び付けの型'),
])
def test_mismatched_binding_does_not_compile(bind_env, binding, word):
    code, text = try_compile(bind_env, f'constexpr Bound kBad[] = {{ {binding} }};\n')
    assert code != 0 and word in text


def test_unknown_menu_id_does_not_compile(bind_env):
    code, _ = try_compile(bind_env, 'constexpr Bound kBad[] = { GOHAN_ITEM(MenuId::NoSuchItem, Patch{.set = Set}) };\n')
    assert code != 0
