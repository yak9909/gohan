"""メニューの生成器の試験。ゲームの入力を使わず、自作の小さな spec で確かめる。"""
import importlib.util
import os
from pathlib import Path
import re
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
    assert menu.BUTTON_NAMES == {name.lower() for name in menu.BUTTONS}  # display = "buttons" の名前は BUTTONS から作る
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


def test_shared_default_hotkeys_are_explicit_pairs(tmp_path):
    body = item(['hotkey = "L+A"']) + item(['hotkey = "A+L"']).replace('"a"', '"b"')
    assert len(load(tmp_path, 'shared_hotkeys = [["a", "b"]]\n' + body)) == 2
    for declaration in ('[["a", "missing"]]', '[["a", "a"]]', '[["a", "b"], ["b", "a"]]', '"a"'):
        assert 'shared_hotkeys' in error(tmp_path, f'shared_hotkeys = {declaration}\n' + body)
    mismatch = body.replace('A+L', 'R+B')
    assert 'shared_hotkeys' in error(tmp_path, 'shared_hotkeys = [["a", "b"]]\n' + mismatch)
    third = item(['hotkey = "L+A"']).replace('"a"', '"c"')
    assert 'ホットキー' in error(tmp_path, 'shared_hotkeys = [["a", "b"]]\n' + body + third)


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


SETTINGS_GOOD = '''
[[item]]
id = "root"
label = "根"
control = "folder"
  [[item.item]]
  id = "root.flag"
  label = "旗"
  control = "toggle"
  behavior = "state"

[[settings]]
id = "settings"
label = "設定"
control = "folder"
  [[settings.item]]
  id = "settings.fixed"
  label = "固定中の項目"
  control = "folder"
  list = "fixed"
  [[settings.item]]
  id = "settings.keep"
  label = "保持設定"
  control = "folder"
    [[settings.item.item]]
    id = "settings.keep.toggles"
    label = "トグルを全て保持"
    control = "toggle"
    behavior = "value"
    apply = "immediate"
    default = false
    [[settings.item.item]]
    id = "settings.keep.erase"
    label = "保存を全て消す"
    control = "action"
    behavior = "trigger"
    confirm = "menu"
  [[settings.item]]
  id = "settings.press"
  label = "押し切る"
  control = "multiselect"
  options = ["A", "B", "Start"]
  default = ["Start"]
  display = "buttons"
  apply = "immediate"
'''


def test_settings_root_and_new_keys(tmp_path):
    path = tmp_path / 'menu.toml'
    path.write_text(SETTINGS_GOOD, encoding='utf-8')
    items = menu.load(path)
    assert [i.id for i in items] == ['root', 'root.flag', 'settings', 'settings.fixed', 'settings.keep',
                                     'settings.keep.toggles', 'settings.keep.erase', 'settings.press']
    root, flag, settings, fixed, keep, toggles, erase, press = items
    # 設定画面の根は一覧の列とつながない（一覧の最後の項目の次は無し）。
    assert root.next == menu.NO_ITEM and settings.settings and not root.settings and settings.parent == menu.NO_ITEM
    assert fixed.list_kind == 'fixed' and fixed.children == [] and fixed.settings
    assert toggles.behavior == 'value' and toggles.source == 'setting' and toggles.apply == 'immediate'
    assert toggles.persist is True and toggles.default == 0
    assert erase.confirm == 'menu' and erase.persist is False and erase.menu == 'immediate'
    assert press.display == 'buttons' and press.default == 1 << 2 and press.apply == 'immediate'
    header = menu.render_ids(items)
    assert 'inline constexpr MenuId kSettingsRoot = MenuId::Settings;' in header
    table = menu.render_table(items)
    assert '.list = ListKind::Fixed' in table and '.display = DisplayKind::Buttons' in table
    assert '.apply = ApplyKind::Immediate' in table and '.confirm = Confirm::Menu' in table
    docs = menu.render_docs(items)
    assert '持ち越さない' in docs and '保持しない' not in docs and 'メニューで確認' in docs


def test_settings_root_is_optional(tmp_path):
    items = load(tmp_path, GOOD)
    assert 'kSettingsRoot' not in menu.render_ids(items)


PERSIST = '''
[[item]]
id = "p"
label = "P"
control = "folder"
  [[item.item]]
  id = "p.state"
  label = "状態"
  control = "toggle"
  behavior = "state"
  [[item.item]]
  id = "p.hold"
  label = "押下中"
  control = "toggle"
  behavior = "hold"
  [[item.item]]
  id = "p.setting"
  label = "設定"
  control = "number"
  range = [0, 9]
  source = "setting"
  [[item.item]]
  id = "p.game"
  label = "ゲーム"
  control = "number"
  range = [0, 9]
  source = "game"
  [[item.item]]
  id = "p.run"
  label = "実行"
  control = "action"
  behavior = "trigger"
  [[item.item]]
  id = "p.mode"
  label = "画面"
  control = "action"
  behavior = "mode"
  [[item.item]]
  id = "p.toggle_run"
  label = "実行のトグル"
  control = "toggle"
  behavior = "trigger"
'''


def test_persist_defaults_follow_the_table(tmp_path):
    # 6.2: 状態・押下中・設定の値は既定で真。実行・実行のトグル・画面・ゲームの値は既定で偽。
    items = load(tmp_path, PERSIST)
    persist = {i.id: i.persist for i in items if i.control != 'folder'}
    assert persist == {'p.state': True, 'p.hold': True, 'p.setting': True, 'p.game': False,
                       'p.run': False, 'p.mode': False, 'p.toggle_run': False}


@pytest.mark.parametrize(('text', 'word'), [
    (PERSIST.replace('behavior = "trigger"\n  [[item.item]]\n  id = "p.mode"', 'behavior = "trigger"\npersist = true\n  [[item.item]]\n  id = "p.mode"'), 'persist'),
    (PERSIST.replace('source = "game"', 'source = "game"\n  persist = true'), 'persist'),
    (PERSIST.replace('behavior = "mode"', 'behavior = "mode"\n  persist = true'), 'persist'),
])
def test_persist_true_is_refused_where_it_cannot_be_kept(tmp_path, text, word):
    assert word in error(tmp_path, text)


SETTINGS_ERRORS = [
    # 表示: 複数選択の選択肢はボタンの名前だけ。複数選択以外には使えない。
    (item(['options = ["A", "Foo"]', 'display = "buttons"'], 'multiselect', ''), 'ボタンの名前'),
    (item(['options = ["A"]', 'display = "buttons"'], 'select', ''), '複数選択'),
    (item(['options = ["A"]', 'display = "list"'], 'multiselect', ''), 'display'),
    # 固定中の項目（仮のフォルダ）: 子の項目は書けない。フォルダにだけ使える。
    ('[[item]]\nid = "a"\nlabel = "A"\ncontrol = "folder"\nlist = "fixed"\n[[item.item]]\nid = "b"\nlabel = "B"\n'
     'control = "toggle"\nbehavior = "state"\n', '子の項目'),
    (item(['list = "fixed"']), 'list'),
    # 適用: 設定の値（value + setting）だけ。
    (item(['apply = "immediate"']), 'apply'),
    (item(['apply = "later"'], 'number', ''), 'apply'),
    # トグルの値は設定の値だけ。ゲームの値は書けない。
    (item(['source = "game"'], 'toggle', 'value'), 'setting'),
    # 実行のトグル・実行（trigger）は次の起動に残せない。
    (item(['persist = true'], 'toggle', 'trigger'), 'persist'),
    (item(['persist = true'], 'action', 'trigger'), 'persist'),
    # 設定画面の根はフォルダ 1 つだけ。
    ('[[item]]\nid = "a"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n'
     '[[settings]]\nid = "s"\nlabel = "S"\ncontrol = "toggle"\nbehavior = "state"\n', 'フォルダ'),
    ('[[item]]\nid = "a"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n'
     '[[settings]]\nid = "s"\nlabel = "S"\ncontrol = "folder"\n[[settings.item]]\nid = "s.t"\nlabel = "T"\n'
     'control = "toggle"\nbehavior = "state"\n[[settings]]\nid = "s2"\nlabel = "S2"\ncontrol = "folder"\n'
     '[[settings.item]]\nid = "s2.t"\nlabel = "T2"\ncontrol = "toggle"\nbehavior = "state"\n', '1 つだけ'),
    # 確認（メニュー）は実行（trigger）だけ。
    (item(['confirm = "menu"']), 'confirm'),
]


@pytest.mark.parametrize(('text', 'word'), SETTINGS_ERRORS)
def test_settings_spec_errors(tmp_path, text, word):
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


# ---- 次の起動に残す物の大きさ（src/menu/persist.hpp の形式。全項目の鍵の長さと欄の最大の合計が上限に収まる） ----

def test_persist_limit_constants_match_the_headers():
    """codegen の写しの定数が、C++ の定義と同じ。違えば静的検査の意味が無くなる。"""
    persist_h = (ROOT / 'src/menu/persist.hpp').read_text(encoding='utf-8')
    limits_h = (ROOT / 'src/core/limits.hpp').read_text(encoding='utf-8')
    assert re.search(r'kHeaderBytes\s*=\s*12\b', persist_h) and menu.PERSIST_HEADER == 12
    assert re.search(r'kMaxIdBytes\s*=\s*64\b', persist_h) and menu.PERSIST_ID_MAX == 64
    assert re.search(r'kFieldBytesMax\s*=\s*8\b', persist_h) and menu.PERSIST_FIELD_MAX == 8
    assert re.search(r'PersistBytes\s*=\s*16384\b', limits_h) and menu.PERSIST_BYTES == 16384


def test_persist_size_counts_each_id_and_the_fields(tmp_path):
    items = load(tmp_path, GOOD)
    # 12 + 項目ごとに（鍵の長さ 1 ・鍵・欄の旗 1・欄の最大 8）。GOOD の鍵は root・root.flag・root.mode・root.amount・go。
    expected = 12 + sum(1 + len(i.id) + 1 + 8 for i in items)
    assert menu.persist_size(items) == expected == 97


def test_id_longer_than_64_bytes_is_refused(tmp_path):
    text = f'[[item]]\nid = "{"a" * 65}"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n'
    assert '64 字' in error(tmp_path, text)
    ok = f'[[item]]\nid = "{"a" * 64}"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n'
    assert load(tmp_path, ok)[0].id == 'a' * 64


def test_persist_size_over_the_limit_is_refused(tmp_path, monkeypatch):
    monkeypatch.setattr(menu, 'PERSIST_BYTES', 40)
    text = ''.join(f'[[item]]\nid = "k{n}"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n' for n in range(3))
    message = error(tmp_path, text)
    assert '上限' in message and '40' in message


def test_worst_case_of_all_items_fits(tmp_path):
    """項目の上限いっぱいで、鍵が最も長い場合でも上限に収まる（12 + 192 × 74 = 14220）。"""
    ids = ['a' * 60 + f'{n:04d}' for n in range(menu.MAX_ITEMS)]
    text = ''.join(f'[[item]]\nid = "{i}"\nlabel = "A"\ncontrol = "toggle"\nbehavior = "state"\n' for i in ids)
    items = load(tmp_path, text)
    assert len(items) == menu.MAX_ITEMS and menu.persist_size(items) == 12 + 192 * 74 <= menu.PERSIST_BYTES


# ---- 固定できる物（docs/design/menu-item-actions.md の 3.1・11 節の決定 G）と、仮のフォルダ（お気に入り・固定中の項目） ----

GAME_VALUE = '''
[[item]]
id = "g"
label = "ゲーム"
control = "number"
range = [0, 9]
source = "game"
'''


def test_game_values_are_lockable_by_default(tmp_path):
    assert load(tmp_path, GAME_VALUE)[0].lockable is True
    off = GAME_VALUE + 'lockable = false\n'
    assert load(tmp_path, off)[0].lockable is False
    assert menu.can_fix(load(tmp_path, GAME_VALUE)[0]) and not menu.can_fix(load(tmp_path, off)[0])


def test_setting_values_are_not_lockable_by_default(tmp_path):
    items = load(tmp_path, GOOD)
    amount = items[3]
    assert amount.source == 'setting' and amount.lockable is False
    assert menu.can_fix(amount)  # 設定の値は次の起動に残せるので固定できる（既定の persist は真）


def test_fixable_items_follow_the_engine_rules(tmp_path):
    # 固定できる: 状態・押下中（persist）、設定の数値・選択・複数選択、ゲームの値（lockable）。
    # 固定できない: 実行・画面・キー・設定画面の行・persist = false の物。
    text = '''
[[item]]
id = "t"
label = "T"
control = "folder"
  [[item.item]]
  id = "t.state"
  label = "状態"
  control = "toggle"
  behavior = "state"
  [[item.item]]
  id = "t.hold"
  label = "押下中"
  control = "toggle"
  behavior = "hold"
  [[item.item]]
  id = "t.quiet"
  label = "持ち越さない"
  control = "toggle"
  behavior = "state"
  persist = false
  [[item.item]]
  id = "t.select"
  label = "選択"
  control = "select"
  options = ["a", "b"]
  [[item.item]]
  id = "t.multi"
  label = "複数"
  control = "multiselect"
  options = ["a"]
  [[item.item]]
  id = "t.slider"
  label = "スライダー"
  control = "slider"
  range = [0, 9]
  [[item.item]]
  id = "t.quiet_value"
  label = "持ち越さない値"
  control = "number"
  range = [0, 9]
  persist = false
  [[item.item]]
  id = "t.key"
  label = "キー"
  control = "key"
  [[item.item]]
  id = "t.run"
  label = "実行"
  control = "action"
  behavior = "trigger"
  [[item.item]]
  id = "t.screen"
  label = "画面"
  control = "action"
  behavior = "mode"
  [[item.item]]
  id = "t.value"
  label = "値のトグル"
  control = "toggle"
  behavior = "value"
  [[item.item]]
  id = "t.quiet_flag"
  label = "持ち越さない値のトグル"
  control = "toggle"
  behavior = "value"
  persist = false
'''
    items = load(tmp_path, text)
    fixable = {i.id: menu.can_fix(i) for i in items if i.control != 'folder'}
    assert fixable == {'t.state': True, 't.hold': True, 't.quiet': False, 't.select': True, 't.multi': True,
                       't.slider': True, 't.quiet_value': False, 't.key': False, 't.run': False, 't.screen': False,
                       't.value': True, 't.quiet_flag': False}


def test_settings_rows_are_never_fixable(tmp_path):
    path = tmp_path / 'menu.toml'
    path.write_text(SETTINGS_GOOD, encoding='utf-8')
    items = menu.load(path)
    rows = {i.id: menu.can_fix(i) for i in items if i.settings and i.control != 'folder'}
    assert rows == {'settings.keep.toggles': False, 'settings.keep.erase': False, 'settings.press': False}


def test_docs_mark_the_fixable_items(tmp_path):
    items = load(tmp_path, GAME_VALUE)
    assert '固定できる' in menu.render_docs(items)
    assert '固定できる' not in menu.render_docs(load(tmp_path, GAME_VALUE + 'lockable = false\n'))


FAVORITES_SETTINGS = SETTINGS_GOOD.replace('  [[settings.item]]\n  id = "settings.keep"',
                                           '  [[settings.item]]\n  id = "settings.favorites"\n  label = "お気に入り"\n'
                                           '  control = "folder"\n  list = "favorites"\n  [[settings.item]]\n  id = "settings.keep"')


def test_favorites_list_is_allowed_only_in_settings(tmp_path):
    path = tmp_path / 'menu.toml'
    path.write_text(FAVORITES_SETTINGS, encoding='utf-8')
    items = menu.load(path)
    favorites = next(i for i in items if i.id == 'settings.favorites')
    assert favorites.list_kind == 'favorites' and favorites.settings and favorites.children == []
    table = menu.render_table(items)
    assert '.list = ListKind::Favorites' in table
    assert 'お気に入り' in menu.render_docs(items) and '中身はお気に入り' in menu.render_docs(items)
    # 一覧の中の仮のフォルダは書けない（設定画面の中だけ）。
    for kind in ('favorites', 'fixed'):
        outside = f'[[item]]\nid = "f"\nlabel = "F"\ncontrol = "folder"\nlist = "{kind}"\n'
        assert 'list は設定画面の中のフォルダだけ' in error(tmp_path, outside)
    assert 'list' in error(tmp_path, '[[item]]\nid = "f"\nlabel = "F"\ncontrol = "folder"\nlist = "unknown"\n')


def test_favorites_list_cannot_have_children(tmp_path):
    text = FAVORITES_SETTINGS.replace('  list = "favorites"\n', '  list = "favorites"\n  [[settings.item.item]]\n  id = "settings.favorites.x"\n  label = "X"\n  control = "toggle"\n  behavior = "state"\n', 1)
    assert '子の項目' in error(tmp_path, text)


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
