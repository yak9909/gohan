"""gohan の手元の資源を生成する。"""
import argparse
from pathlib import Path
import sys
from .sd import build


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['build-sd'])
    args = parser.parse_args()
    try:
        if args.command == 'build-sd':
            print(build(Path.cwd()))
    except (OSError, UnicodeError, ValueError) as error:
        print(f'SD 資源の生成に失敗しました: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
