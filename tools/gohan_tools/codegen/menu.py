#!/usr/bin/env python3
"""spec/menu.toml からメニューの C++ の表と docs/menu.md を作る（Python 3.11 以上）。

make が呼ぶ。ROM は要らない。出力:
  <out>/menu_ids.hpp   enum class MenuId と、項目ごとの Spec（結び付けの検査に使う）
  <out>/menu_table.cpp 項目の表 kMenuItems（深さ優先の順。MenuId の値が添え字）
  <docs>               メニューの一覧（手で書かない）

spec の形:
  [[item]]                     最上位の項目。フォルダの子は [[item.item]] のように入れ子で書く。
  id         一意の ID（小文字・数字・_ を . でつなぐ）。階層とは無関係な名前。
  label desc control behavior source hotkey format default range step options
  lockable group enabled_if persist dev menu confirm input
誤り（未知の鍵・重複 ID・振る舞いと見た目の矛盾など）は SpecError で、終了コード 1 にする。
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import sys
import tomllib

BUTTONS = ['ZL', 'L', 'R', 'ZR', 'Up', 'Down', 'Left', 'Right', 'A', 'B', 'X', 'Y', 'Select', 'Start']
BUTTON_BIT = {name.lower(): index for index, name in enumerate(BUTTONS)}

KEYS = {'id', 'label', 'desc', 'control', 'behavior', 'source', 'hotkey', 'format', 'default', 'range', 'step',
        'options', 'lockable', 'group', 'enabled_if', 'persist', 'dev', 'menu', 'confirm', 'input', 'item'}
FOLDER_KEYS = {'id', 'label', 'desc', 'control', 'enabled_if', 'dev', 'item'}
CONTROLS = ['folder', 'toggle', 'select', 'multiselect', 'number', 'slider', 'action', 'key']
VALUE_CONTROLS = {'select', 'multiselect', 'number', 'slider', 'key'}
BEHAVIORS = ['state', 'hold', 'trigger', 'mode', 'value']
SOURCES = ['setting', 'game']
FORMATS = ['decimal', 'hex', 'decimal1']
MENUS = ['immediate', 'on_apply']
CONFIRMS = ['none', 'hotkey']
INPUTS = ['none', 'number', 'hex']
ID_PATTERN = re.compile(r'^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)*$')
COND_PATTERN = re.compile(r'^\s*([a-z][a-z0-9_.]*)\s*(==|!=|<=|>=|<|>)\s*(.+?)\s*$')
COND_OPS = {'==': 'Eq', '!=': 'Ne', '<': 'Lt', '<=': 'Le', '>': 'Gt', '>=': 'Ge'}
S32_MAX = 2**31 - 1
NO_ITEM = 0xFFFF
MAX_ITEMS = 192  # src/core/limits.hpp の MenuItems と同じ
MAX_OPTIONS = 31

LOOKS = {'folder': 'フォルダ', 'toggle': 'トグル', 'select': '選択', 'multiselect': '複数選択', 'number': '数値',
         'slider': 'スライダー', 'action': '実行', 'key': 'キー'}
BEHAVIOR_NAMES = {'': '-', 'state': '状態', 'hold': '押下中', 'trigger': '実行', 'mode': '画面', 'value': '値'}
SOURCE_NAMES = {'': '-', 'setting': '設定', 'game': 'ゲーム'}


class SpecError(Exception):
    pass


@dataclass
class Item:
    id: str
    label: str
    desc: str
    control: str
    behavior: str = ''
    source: str = ''
    hotkey_text: str = ''
    hotkey: int = 0
    format: str = ''
    default: int = 0
    minimum: int = 0
    maximum: int = 0
    step: int = 0
    options: list[str] = field(default_factory=list)
    lockable: bool = False
    group: str = ''
    enabled_if: str = ''
    cond: tuple[str, str, int] | None = None  # (参照する項目の ID, 演算子名, 右辺)
    persist: bool = False
    dev: bool = False
    menu: str = ''
    confirm: str = ''
    input: str = ''
    index: int = 0
    parent: int = NO_ITEM
    first_child: int = NO_ITEM
    next: int = NO_ITEM
    group_number: int = 0
    children: list['Item'] = field(default_factory=list)
    depth: int = 0


def parse_hotkey(text: str, where: str) -> int:
    mask = 0
    parts = [part.strip() for part in text.split('+')]
    for part in parts:
        bit = BUTTON_BIT.get(part.lower())
        if bit is None:
            raise SpecError(f'{where}: ホットキー "{text}" に未知のボタン "{part}" があります')
        if mask & (1 << bit):
            raise SpecError(f'{where}: ホットキー "{text}" に同じボタンが重なっています')
        mask |= 1 << bit
    return mask


def pascal(item_id: str) -> str:
    return ''.join(word[:1].upper() + word[1:] for part in item_id.split('.') for word in part.split('_'))


def want_type(entry: dict, key: str, kind: type, where: str, default=None):
    if key not in entry:
        return default
    value = entry[key]
    if kind is int:
        if isinstance(value, bool) or not isinstance(value, int):
            raise SpecError(f'{where}: {key} は整数にしてください')
    elif kind is float:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise SpecError(f'{where}: {key} は数値にしてください')
    elif not isinstance(value, kind) or (kind is str and not value.strip() and key != 'desc'):
        names = {str: '文字列', bool: '真偽値', list: '配列'}
        raise SpecError(f'{where}: {key} は{names.get(kind, kind.__name__)}にしてください')
    return value


def want_choice(entry: dict, key: str, choices: list[str], where: str, default: str = '') -> str:
    value = want_type(entry, key, str, where, default)
    if value != default and value not in choices:
        raise SpecError(f'{where}: {key} は {" / ".join(choices)} のどれかにしてください（"{value}"）')
    return value


def scale(value, fmt: str, key: str, where: str) -> int:
    """decimal1 は 10 倍した整数で持つ。ほかは整数だけ。"""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SpecError(f'{where}: {key} は数値にしてください')
    if fmt == 'decimal1':
        scaled = round(value * 10)
        if abs(scaled - value * 10) > 1e-6:
            raise SpecError(f'{where}: {key} は小数 1 桁までにしてください（{value}）')
    else:
        if isinstance(value, float) and value != int(value):
            raise SpecError(f'{where}: {key} は整数にしてください（{value}）')
        scaled = int(value)
    if abs(scaled) > S32_MAX:
        raise SpecError(f'{where}: {key} が大きすぎます')
    return scaled


def parse_item(entry: dict, depth: int, where: str) -> Item:
    if not isinstance(entry, dict):
        raise SpecError(f'{where}: 項目は表にしてください')
    unknown = sorted(set(entry) - KEYS)
    if unknown:
        raise SpecError(f'{where}: 未知の鍵があります: {", ".join(unknown)}')
    item_id = want_type(entry, 'id', str, where)
    if not item_id or not ID_PATTERN.match(item_id):
        raise SpecError(f'{where}: id が不正です（小文字・数字・_ を . でつなぐ）: {item_id!r}')
    where = f'{item_id}'
    label = want_type(entry, 'label', str, where)
    control = want_choice(entry, 'control', CONTROLS, where)
    if not label:
        raise SpecError(f'{where}: label が必要です')
    if not control:
        raise SpecError(f'{where}: control が必要です')
    item = Item(id=item_id, label=label, desc=want_type(entry, 'desc', str, where, ''), control=control, depth=depth)
    if control == 'folder':
        extra = sorted(set(entry) - FOLDER_KEYS)
        if extra:
            raise SpecError(f'{where}: フォルダに使えない鍵があります: {", ".join(extra)}')
        children = want_type(entry, 'item', list, where)
        if not children:
            raise SpecError(f'{where}: フォルダには子の項目（item）が要ります')
        item.dev = want_type(entry, 'dev', bool, where, False)
        item.enabled_if = want_type(entry, 'enabled_if', str, where, '')
        item.children = [parse_item(child, depth + 1, f'{where}.item[{n}]') for n, child in enumerate(children)]
        return item
    if 'item' in entry:
        raise SpecError(f'{where}: 子の項目（item）を持てるのはフォルダだけです')

    behavior = want_choice(entry, 'behavior', BEHAVIORS, where)
    if control in VALUE_CONTROLS:
        if behavior not in ('', 'value'):
            raise SpecError(f'{where}: control={control} の振る舞いは value だけです（{behavior}）')
        behavior = 'value'
    elif control == 'toggle':
        if behavior not in ('state', 'hold', 'trigger'):
            raise SpecError(f'{where}: トグルの振る舞いは state / hold / trigger のどれかです（"{behavior}"）')
    elif control == 'action':
        if behavior not in ('trigger', 'mode'):
            raise SpecError(f'{where}: 実行（action）の振る舞いは trigger / mode のどちらかです（"{behavior}"）')
    item.behavior = behavior

    source = want_choice(entry, 'source', SOURCES, where)
    if behavior == 'value':
        source = source or 'setting'
        if control == 'key' and source != 'setting':
            raise SpecError(f'{where}: キーの値の出どころは setting だけです')
    elif source:
        raise SpecError(f'{where}: source を持てるのは値（value）の項目だけです')
    item.source = source

    fmt = want_choice(entry, 'format', FORMATS, where)
    if control == 'number':
        fmt = fmt or 'decimal'
    elif control == 'slider':
        if fmt == 'hex':
            raise SpecError(f'{where}: スライダーに hex は使えません')
        fmt = fmt or 'decimal'
    elif fmt:
        raise SpecError(f'{where}: format を持てるのは数値とスライダーだけです')
    item.format = fmt

    options = want_type(entry, 'options', list, where)
    if control in ('select', 'multiselect'):
        if not options or not all(isinstance(o, str) and o for o in options):
            raise SpecError(f'{where}: options に選択肢の名前（空でない文字列）を並べてください')
        if len(set(options)) != len(options):
            raise SpecError(f'{where}: options に同じ名前があります')
        if len(options) > MAX_OPTIONS:
            raise SpecError(f'{where}: 選択肢は {MAX_OPTIONS} 個までです')
        item.options = list(options)
    elif options is not None:
        raise SpecError(f'{where}: options を持てるのは選択と複数選択だけです')

    default = entry.get('default')
    if control in ('number', 'slider'):
        bounds = want_type(entry, 'range', list, where)
        if bounds is None or len(bounds) != 2:
            raise SpecError(f'{where}: range = [最小, 最大] が必要です')
        item.minimum = scale(bounds[0], fmt, 'range', where)
        item.maximum = scale(bounds[1], fmt, 'range', where)
        if item.minimum > item.maximum:
            raise SpecError(f'{where}: range の最小が最大より大きいです')
        step = entry.get('step', 0.1 if fmt == 'decimal1' else 1)
        item.step = scale(step, fmt, 'step', where)
        if item.step <= 0:
            raise SpecError(f'{where}: step は正の数にしてください')
        item.default = item.minimum if default is None else scale(default, fmt, 'default', where)
        if not item.minimum <= item.default <= item.maximum:
            raise SpecError(f'{where}: default が range の外です')
    else:
        for key in ('range', 'step'):
            if key in entry:
                raise SpecError(f'{where}: {key} を持てるのは数値とスライダーだけです')
        if control == 'toggle':
            if default is not None and not isinstance(default, bool):
                raise SpecError(f'{where}: トグルの default は true / false にしてください')
            item.default = 1 if default else 0
        elif control == 'select':
            if default is None:
                item.default = 0
            elif isinstance(default, str) and default in item.options:
                item.default = item.options.index(default)
            elif isinstance(default, int) and not isinstance(default, bool) and 0 <= default < len(item.options):
                item.default = default
            else:
                raise SpecError(f'{where}: default は選択肢の名前か番号にしてください')
        elif control == 'multiselect':
            names = default if default is not None else []
            if not isinstance(names, list) or not all(n in item.options for n in names):
                raise SpecError(f'{where}: default は選択肢の名前の配列にしてください')
            item.default = sum(1 << item.options.index(n) for n in set(names))
        elif control == 'key':
            text = default if default is not None else ''
            if not isinstance(text, str):
                raise SpecError(f'{where}: キーの default はホットキーの文字列にしてください')
            item.default = parse_hotkey(text, where) if text else 0
        elif default is not None:
            raise SpecError(f'{where}: この項目に default は使えません')

    hotkey = want_type(entry, 'hotkey', str, where, '')
    if hotkey:
        if control == 'key':
            raise SpecError(f'{where}: キーの項目は値がホットキーです（hotkey は使えません）')
        item.hotkey_text = hotkey
        item.hotkey = parse_hotkey(hotkey, where)

    item.lockable = want_type(entry, 'lockable', bool, where, False)
    if item.lockable and not (behavior == 'value' and source == 'game'):
        raise SpecError(f'{where}: lockable はゲームから読む値（source = "game"）の項目だけです')
    item.group = want_type(entry, 'group', str, where, '')
    if item.group and behavior not in ('state', 'mode'):
        raise SpecError(f'{where}: group を持てるのは状態（state）と画面（mode）だけです')
    item.dev = want_type(entry, 'dev', bool, where, False)
    item.enabled_if = want_type(entry, 'enabled_if', str, where, '')

    persist = want_type(entry, 'persist', bool, where)
    if behavior == 'mode' and persist:
        raise SpecError(f'{where}: 画面（mode）は次の起動で戻せません（persist = true は使えません）')
    item.persist = (behavior != 'mode' and source != 'game') if persist is None else persist

    menu = want_choice(entry, 'menu', MENUS, where)
    if menu and not (control == 'action' and behavior == 'trigger'):
        raise SpecError(f'{where}: menu を持てるのは実行（action + trigger）だけです')
    confirm = want_choice(entry, 'confirm', CONFIRMS, where)
    if confirm and behavior != 'trigger':
        raise SpecError(f'{where}: confirm を持てるのは実行（trigger）だけです')
    input_kind = want_choice(entry, 'input', INPUTS, where)
    if input_kind and behavior != 'trigger':
        raise SpecError(f'{where}: input を持てるのは実行（trigger）だけです')
    if behavior == 'trigger':
        item.menu = menu or ('immediate' if control == 'action' else '')
        item.confirm = confirm or 'none'
        item.input = input_kind or 'none'
        if item.menu == 'on_apply' and item.input != 'none':
            raise SpecError(f'{where}: 適用で実行（on_apply）の項目は入力を受け取れません')
        if item.confirm == 'hotkey' and not hotkey:
            raise SpecError(f'{where}: confirm = "hotkey" にはホットキー（hotkey）が要ります')
    return item


def flatten(items: list[Item], parent: int, out: list[Item]) -> None:
    for item in items:
        item.index = len(out)
        item.parent = parent
        out.append(item)
        if item.children:
            item.first_child = len(out)
            flatten(item.children, item.index, out)
        for n, child in enumerate(item.children):
            child.next = item.children[n + 1].index if n + 1 < len(item.children) else NO_ITEM
    for n, item in enumerate(items):
        item.next = items[n + 1].index if n + 1 < len(items) else NO_ITEM


def resolve_cond(item: Item, by_id: dict[str, Item]) -> None:
    if not item.enabled_if:
        return
    match = COND_PATTERN.match(item.enabled_if)
    if not match:
        raise SpecError(f'{item.id}: enabled_if の書き方が不正です（"<ID> <比較> <値>"）: {item.enabled_if!r}')
    ref_id, op, rhs = match.groups()
    ref = by_id.get(ref_id)
    if ref is None:
        raise SpecError(f'{item.id}: enabled_if が知らない項目 {ref_id} を参照しています')
    if ref is item:
        raise SpecError(f'{item.id}: enabled_if が自分自身を参照しています')
    if ref.control not in ('toggle', 'select', 'multiselect', 'number', 'slider'):
        raise SpecError(f'{item.id}: enabled_if で参照できない種類の項目です: {ref_id}')
    quoted = re.fullmatch(r"'([^']*)'|\"([^\"]*)\"", rhs)
    if quoted:
        name = quoted.group(1) if quoted.group(1) is not None else quoted.group(2)
        if ref.control != 'select' or name not in ref.options:
            raise SpecError(f'{item.id}: enabled_if の "{name}" は {ref_id} の選択肢にありません')
        value = ref.options.index(name)
        if op not in ('==', '!='):
            raise SpecError(f'{item.id}: 選択肢の名前との比較は == と != だけです')
    elif rhs in ('true', 'false'):
        if ref.control != 'toggle':
            raise SpecError(f'{item.id}: true / false と比べられるのはトグルだけです')
        value = 1 if rhs == 'true' else 0
    else:
        if ref.control not in ('toggle', 'select', 'multiselect', 'number', 'slider'):
            raise SpecError(f'{item.id}: enabled_if の右辺が不正です')
        try:
            number = float(rhs) if '.' in rhs else int(rhs, 0)
        except ValueError:
            raise SpecError(f'{item.id}: enabled_if の右辺が不正です: {rhs!r}') from None
        value = scale(number, ref.format if ref.control in ('number', 'slider') else '', 'enabled_if', item.id)
    item.cond = (ref_id, COND_OPS[op], value)


def validate(items: list[Item]) -> None:
    if not items:
        raise SpecError('項目がありません')
    if len(items) > MAX_ITEMS:
        raise SpecError(f'項目が多すぎます（{len(items)} > {MAX_ITEMS}）')
    by_id: dict[str, Item] = {}
    names: dict[str, Item] = {}
    for item in items:
        if item.id in by_id:
            raise SpecError(f'{item.id}: ID が重複しています')
        by_id[item.id] = item
        name = pascal(item.id)
        if name in names:
            raise SpecError(f'{item.id}: C++ の名前 {name} が {names[name].id} と重なります')
        names[name] = item
    groups: dict[str, Item] = {}
    hotkeys: dict[int, Item] = {}
    for item in items:
        resolve_cond(item, by_id)
        if item.group:
            first = groups.setdefault(item.group, item)
            if first.behavior != item.behavior:
                raise SpecError(f'{item.id}: group "{item.group}" に state と mode を混ぜられません')
            item.group_number = list(groups).index(item.group) + 1
        if item.hotkey:
            other = hotkeys.setdefault(item.hotkey, item)
            if other is not item:
                raise SpecError(f'{item.id}: ホットキー {item.hotkey_text} が {other.id} と重なっています')


def load(path: Path) -> list[Item]:
    try:
        data = tomllib.loads(path.read_text(encoding='utf-8'))
    except tomllib.TOMLDecodeError as error:
        raise SpecError(f'{path}: TOML として読めません: {error}') from None
    unknown = sorted(set(data) - {'item'})
    if unknown:
        raise SpecError(f'最上位に未知の鍵があります: {", ".join(unknown)}')
    top = data.get('item')
    if not isinstance(top, list) or not top:
        raise SpecError('[[item]] が必要です')
    roots = [parse_item(entry, 0, f'item[{n}]') for n, entry in enumerate(top)]
    items: list[Item] = []
    flatten(roots, NO_ITEM, items)
    validate(items)
    return items


def cstr(text: str) -> str:
    out = ['"']
    for char in text:
        if char in '\\"':
            out.append('\\' + char)
        elif char == '\n':
            out.append('\\n')
        elif char == '\t':
            out.append('\\t')
        elif ord(char) < 0x20 or ord(char) == 0x7F:
            out.append('\\%03o' % ord(char))
        else:
            out.append(char)
    out.append('"')
    return ''.join(out)


def enum_name(text: str) -> str:
    return ''.join(word[:1].upper() + word[1:] for word in text.split('_'))


HEADER_NOTE = '// 自動生成: tools/gohan_tools/codegen/menu.py（入力 spec/menu.toml）。手で編集しない。\n'


def render_ids(items: list[Item]) -> str:
    out = [HEADER_NOTE, '#pragma once\n#include "menu/types.hpp"\n#include <cstddef>\n#include <cstdint>\n\n',
           'namespace gohan::menu {\n', 'enum class MenuId : std::uint16_t {\n']
    for item in items:
        out.append(f'    {pascal(item.id)} = {item.index},\n')
    out.append('};\n')
    out.append(f'inline constexpr std::size_t kMenuItemCount = {len(items)};\n')
    out.append('extern const ItemDef kMenuItems[kMenuItemCount];\n\n')
    for item in items:
        out.append(f'template <> struct Spec<MenuId::{pascal(item.id)}> {{\n')
        out.append(f'    static constexpr Control control = Control::{enum_name(item.control)};\n')
        out.append(f'    static constexpr Behavior behavior = Behavior::{enum_name(item.behavior or "none")};\n')
        out.append(f'    static constexpr Source source = Source::{enum_name(item.source or "none")};\n')
        out.append(f'    static constexpr MenuKind menu = MenuKind::{enum_name(item.menu or "immediate")};\n')
        out.append(f'    static constexpr Confirm confirm = Confirm::{enum_name(item.confirm or "none")};\n')
        out.append(f'    static constexpr InputKind input = InputKind::{enum_name(item.input or "none")};\n')
        out.append('};\n')
    out.append('} // namespace gohan::menu\n')
    return ''.join(out)


def render_table(items: list[Item]) -> str:
    by_id = {item.id: item for item in items}
    out = [HEADER_NOTE, '#include "menu_ids.hpp"\n\nnamespace gohan::menu {\nnamespace {\n']
    for item in items:
        if item.options:
            out.append(f'constexpr const char *const kOptions{item.index}[] = {{'
                       + ', '.join(cstr(o) for o in item.options) + '};\n')
    out.append('} // namespace\n\nconst ItemDef kMenuItems[kMenuItemCount] = {\n')
    for item in items:
        cond = 'Cond{}'
        if item.cond:
            ref, op, rhs = item.cond
            cond = f'Cond{{{by_id[ref].index}, CondOp::{op}, {rhs}}}'
        lo, hi = item.minimum, item.maximum
        step = item.step
        if item.control == 'toggle':
            lo, hi, step = 0, 1, 1
        elif item.control == 'select':
            lo, hi, step = 0, len(item.options) - 1, 1
        elif item.control == 'multiselect':
            lo, hi, step = 0, (1 << len(item.options)) - 1, 1
        out.append('    {\n')
        out.append(f'        .key = {cstr(item.id)}, .label = {cstr(item.label)}, .desc = {cstr(item.desc)},\n')
        out.append(f'        .control = Control::{enum_name(item.control)}, '
                   f'.behavior = Behavior::{enum_name(item.behavior or "none")}, '
                   f'.source = Source::{enum_name(item.source or "none")}, '
                   f'.format = Format::{enum_name(item.format or "decimal")},\n')
        out.append(f'        .menu = MenuKind::{enum_name(item.menu or "immediate")}, '
                   f'.confirm = Confirm::{enum_name(item.confirm or "none")}, '
                   f'.input = InputKind::{enum_name(item.input or "none")},\n')
        out.append(f'        .lockable = {str(item.lockable).lower()}, .persist = {str(item.persist).lower()}, '
                   f'.dev = {str(item.dev).lower()}, .option_count = {len(item.options)},\n')
        out.append(f'        .parent = {item.parent}, .first_child = {item.first_child}, .next = {item.next}, '
                   f'.group = {item.group_number}, .hotkey = {item.hotkey},\n')
        out.append(f'        .def = {item.default}, .min = {lo}, .max = {hi}, .step = {step},\n')
        options = f'kOptions{item.index}' if item.options else 'nullptr'
        out.append(f'        .options = {options}, .cond = {cond},\n')
        out.append('    },\n')
    out.append('};\n} // namespace gohan::menu\n')
    return ''.join(out)


def cell(text: str) -> str:
    return text.replace('|', '\\|').replace('\n', ' ') if text else '-'


def default_text(item: Item) -> str:
    if item.control == 'toggle':
        return 'ON' if item.default else 'OFF'
    if item.control == 'select':
        return item.options[item.default]
    if item.control == 'multiselect':
        return ', '.join(o for n, o in enumerate(item.options) if item.default >> n & 1) or '-'
    if item.control in ('number', 'slider'):
        if item.format == 'decimal1':
            return f'{item.default / 10:.1f}'
        return f'0x{item.default:X}' if item.format == 'hex' else str(item.default)
    if item.control == 'key':
        return mask_text(item.default) or '-'
    return '-'


def mask_text(mask: int) -> str:
    return '+'.join(name for bit, name in enumerate(BUTTONS) if mask >> bit & 1)


def render_docs(items: list[Item]) -> str:
    out = ['# メニューの一覧\n\n',
           '`spec/menu.toml` から `tools/gohan_tools/codegen/menu.py` が作った一覧です。手で編集しないでください。\n\n',
           '- 振る舞い: 状態＝効果が続く / 押下中＝押している間だけ動く / 実行＝1 回実行 / 画面＝開いて閉じる / 値＝値を反映する\n',
           '- 値の出どころ: 設定＝gohan の変数 / ゲーム＝開いた時にゲームから 1 回読む\n',
           '- 開発用は開発版（`make DEV=1`）だけに出ます。\n\n',
           '| 項目 | ID | 見た目 | 振る舞い | 出どころ | 既定 | ホットキー | 備考 | 説明 |\n',
           '|---|---|---|---|---|---|---|---|---|\n']
    for item in items:
        notes = []
        if item.menu == 'on_apply':
            notes.append('適用で実行')
        if item.confirm == 'hotkey':
            notes.append('ホットキーで確認')
        if item.input in ('number', 'hex'):
            notes.append('入力: ' + ('16進' if item.input == 'hex' else '数値'))
        if item.group:
            notes.append('排他: ' + item.group)
        if item.lockable:
            notes.append('値の固定可')
        if item.enabled_if:
            notes.append('有効条件: ' + item.enabled_if)
        if item.control not in ('folder',) and not item.persist:
            notes.append('保持しない')
        if item.dev:
            notes.append('開発用')
        if item.control in ('number', 'slider'):
            low, high = item.minimum, item.maximum
            if item.format == 'decimal1':
                notes.append(f'範囲 {low / 10:.1f}..{high / 10:.1f}')
            elif item.format == 'hex':
                notes.append(f'範囲 0x{low:X}..0x{high:X}')
            else:
                notes.append(f'範囲 {low}..{high}')
        name = '　' * item.depth + item.label
        out.append('| ' + ' | '.join([
            cell(name), f'`{item.id}`', LOOKS[item.control], BEHAVIOR_NAMES[item.behavior],
            SOURCE_NAMES[item.source], cell(default_text(item) if item.control != 'folder' else ''),
            cell(mask_text(item.hotkey)), cell('、'.join(notes)), cell(item.desc)]) + ' |\n')
    return ''.join(out)


def write_if_changed(path: Path, text: str) -> None:
    data = text.encode('utf-8')
    if path.exists() and path.read_bytes() == data:
        return  # make が不要な再ビルドをしないよう、同じ内容なら触らない
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def outputs(items: list[Item]) -> dict[str, str]:
    return {'menu_ids.hpp': render_ids(items), 'menu_table.cpp': render_table(items), 'menu.md': render_docs(items)}


def generate(spec: Path, out_dir: Path, docs: Path | None) -> None:
    result = outputs(load(spec))
    write_if_changed(out_dir / 'menu_ids.hpp', result['menu_ids.hpp'])
    write_if_changed(out_dir / 'menu_table.cpp', result['menu_table.cpp'])
    if docs is not None:
        write_if_changed(docs, result['menu.md'])


def check(spec: Path, docs: Path) -> bool:
    """docs が spec から作った内容と同じかを返す。"""
    expected = outputs(load(spec))['menu.md'].encode('utf-8')
    return docs.exists() and docs.read_bytes() == expected


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--spec', type=Path, default=Path('spec/menu.toml'))
    parser.add_argument('--out', type=Path, default=Path('build/gen'))
    parser.add_argument('--docs', type=Path, default=None, help='docs/menu.md の出力先（省略時は書かない）')
    parser.add_argument('--check', action='store_true', help='--docs の内容が spec と一致するかだけを調べる')
    args = parser.parse_args(argv)
    try:
        if args.check:
            if args.docs is None:
                parser.error('--check には --docs が要ります')
            if not check(args.spec, args.docs):
                print(f'{args.docs}: {args.spec} から作った内容と違います。make で作り直してください。', file=sys.stderr)
                return 1
            return 0
        generate(args.spec, args.out, args.docs)
    except SpecError as error:
        print(f'menu.toml の誤り: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
