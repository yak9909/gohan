#!/usr/bin/env python3
"""収録するファイルと C++ の層の境界を検査する（Python 3.11 以上）。"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import posixpath
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tomllib

FORMATS = set('.arc .bclim .bflim .bcfnt .bclyt .bflyt .bclan .bflan .umsbt .cro .bin .cia .3ds .cxi'.split())
SOURCE = {'.h', '.hpp', '.hh', '.hxx', '.c', '.cc', '.cpp', '.cxx', '.inl', '.ipp'}
LAYERS = {
    'core': set(), 'game': {'core'},
    'ui': {'game', 'core'}, 'world': {'game', 'core'},
    'menu': {'game', 'core'},
    'features': {'ui', 'world', 'menu', 'game', 'core'},
}
RULES = {'game-format', 'generated', 'size', 'personal-path', 'include', 'address', 'concurrency'}
PERSONAL = re.compile(
    r'(?i)(?:\b[A-Z]:[\\/]+(?:Users|Documents and Settings)[\\/]+[^\s\\/]+|'
    r'(?<![\w:])/(?:home|Users)/[^\s/]+|/' r'root(?:/|\b))'
)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)
HEX = re.compile(r"(?<![\w])0[xX]([0-9a-fA-F](?:[0-9a-fA-F']*[0-9a-fA-F])?)(?:[uUlL]*)(?!\w)")
CONCURRENCY = re.compile(r'\bstd\s*::\s*atomic\w*\b|\b__atomic\w*\b|\bvolatile\b')
# コメントと文字列を空白にして、行番号を保ったままコードを調べる。
TOKENS = re.compile(
    r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<delim>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delim)"|'
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])\'', re.DOTALL
)


@dataclass(frozen=True)
class Problem:
    path: str
    line: int
    rule: str
    message: str

    def __str__(self) -> str:
        return f'{self.path}:{self.line}: [{self.rule}] {self.message}'


def normalized(name: str) -> str:
    name = name.replace('\\', '/')
    path = PurePosixPath(name)
    if not name or path.is_absolute() or re.match(r'^[A-Za-z]:', name) or '..' in path.parts:
        raise ValueError('ファイル一覧はリポジトリからの相対パスにしてください')
    return path.as_posix()


def load_allow(data: bytes) -> list[dict]:
    config = tomllib.loads(data.decode('utf-8'))
    if set(config) - {'allow'} or not isinstance(config.get('allow', []), list):
        raise ValueError('許可リストは [[allow]] の配列で指定してください')
    entries = config.get('allow', [])
    for entry in entries:
        if (not isinstance(entry, dict) or set(entry) - {'path', 'rule', 'reason', 'line'}
                or not isinstance(entry.get('rule'), str) or entry['rule'] not in RULES
                or not isinstance(entry.get('reason'), str) or not entry['reason'].strip()
                or not isinstance(entry.get('path'), str)):
            raise ValueError('許可には path・rule・空でない reason が必要です')
        entry['path'] = normalized(entry['path'])
        if any(c in entry['path'] for c in '*?['):
            raise ValueError('許可するパスはファイル名まで指定してください')
        if 'line' in entry and (type(entry['line']) is not int or entry['line'] < 1):
            raise ValueError('line は正の整数にしてください')
    return entries


def layer(path: str) -> str | None:
    parts = PurePosixPath(path).parts
    return parts[1] if len(parts) > 2 and parts[0] == 'src' and parts[1] in LAYERS else None


def target_path(source: str, include: str) -> str:
    include = include.replace('\\', '/')
    if include.startswith('src/'):
        return posixpath.normpath(include)
    if include.split('/')[0] in LAYERS:
        return posixpath.normpath('src/' + include)
    return posixpath.normpath(posixpath.join(posixpath.dirname(source), include))


def scan(path: str, data: bytes, allow: list[dict] = ()) -> list[Problem]:
    path = normalized(path)
    problems = []

    def add(rule: str, message: str, line: int = 1) -> None:
        if not any(e['path'] == path and e['rule'] == rule
                   and ('line' not in e or e['line'] == line) for e in allow):
            problems.append(Problem(path, line, rule, message))

    p = PurePosixPath(path)
    if p.suffix.lower() in FORMATS:
        add('game-format', 'ゲーム由来の形式は収録できません')
    if p.parts[0].lower() in {'build', 'out'} or path.lower() == 'gohan.local.toml':
        add('generated', 'ローカル設定・生成物は収録できません')
    if len(data) > 1_000_000:
        add('size', 'ファイルが 1 MB（1,000,000 bytes）を超えています')
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError:
        return problems
    if '\0' in text:
        return problems
    for n, line in enumerate(text.splitlines(), 1):
        if PERSONAL.search(line):
            add('personal-path', '個人の環境を指す絶対パスがあります', n)
    if not path.startswith('src/') or p.suffix.lower() not in SOURCE:
        return problems
    code = TOKENS.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), text)
    source_layer = layer(path)
    # include の文字列を残し、コメント内に書いた見本は対象にしない。
    for match in INCLUDE.finditer(text):
        hash_offset = text.find('#', match.start(), match.end())
        if code[hash_offset:hash_offset + 1] != '#':
            continue
        target = target_path(path, match[1])
        dest_layer = layer(target)
        if source_layer and dest_layer:
            allowed = dest_layer == source_layer or dest_layer in LAYERS[source_layer]
            if source_layer == dest_layer == 'features':
                allowed = p.parts[2] == PurePosixPath(target).parts[2]
            if not allowed:
                add('include', f'層の依存方向に反します: {match[1]}', text.count('\n', 0, hash_offset) + 1)
    if source_layer != 'game':
        for match in HEX.finditer(code):
            if 0x00100000 <= int(match[1].replace("'", ''), 16) <= 0x3FFFFFFF:
                add('address', '番地らしい数値は src/game に集めてください', code.count('\n', 0, match.start()) + 1)
    if source_layer != 'core':
        for match in CONCURRENCY.finditer(code):
            add('concurrency', 'スレッド間の受け渡しは core の部品を使ってください', code.count('\n', 0, match.start()) + 1)
    return problems


def git_bytes(root: Path, *args: str) -> bytes:
    result = subprocess.run(['git', *args], cwd=root, capture_output=True, check=False)
    if result.returncode:
        raise ValueError(result.stderr.decode('utf-8', 'replace').strip())
    return result.stdout


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--all', action='store_true', help='追跡中の全ファイル')
    mode.add_argument('--staged', action='store_true', help='コミット予定の内容（インデックス）')
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--files-from', type=Path, help='UTF-8 の相対パス一覧（改行または NUL 区切り）')
    parser.add_argument('files', nargs='*', help='明示的な相対パス。指定時は git を呼ばず手元の内容を検査')
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        explicit = args.files_from is not None or bool(args.files)
        if explicit:
            names = list(args.files)
            if args.files_from is not None:
                listing = args.files_from.read_text(encoding='utf-8-sig')
                names += listing.split('\0') if '\0' in listing else listing.splitlines()
        elif args.staged:
            names = git_bytes(root, 'diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z').decode('utf-8').split('\0')
        else:
            names = git_bytes(root, 'ls-files', '-z').decode('utf-8').split('\0')
        names = sorted({normalized(n) for n in names if n})

        def read(name: str) -> bytes:
            if args.staged and not explicit:
                return git_bytes(root, 'show', ':' + name)
            file = (root / name).resolve()
            if not file.is_relative_to(root):
                raise ValueError('リポジトリの外を指すファイルは読めません')
            return file.read_bytes()

        allow_name = 'tools/check_allow.toml'
        if args.staged and not explicit:
            tracked = git_bytes(root, 'ls-files', '-z', '--', allow_name).decode('utf-8').split('\0')
            allow = load_allow(read(allow_name)) if allow_name in tracked else []
        else:
            allow = load_allow(read(allow_name)) if (root / allow_name).exists() else []
        problems = [problem for name in names for problem in scan(name, read(name), allow)]
    except (OSError, ValueError) as error:
        print(f'check: エラー: {error}', file=sys.stderr)
        return 2
    for problem in problems:
        print(problem)
    print(f'check: {len(names)} ファイル、問題 {len(problems)} 件')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
