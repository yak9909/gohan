# 依存ファイル（.d）に残るドライブ付きの絶対パスを、MSYS 形式に書き直す。
#
# Windows 版のコンパイラは `A:/dir/file` の形（`-I` に `C:\dir` を渡した時は
# `C:dir/file`）でパスを書く。MSYS の make はこのコロンをターゲットの区切りと読み、
# 「multiple target patterns」で止まる。`-include` の前に呼び、`A:/` と `C:` を
# `/a/` と `/c/` にして、依存を生かしたまま読ませる。
#   $(call fix_deps,<.d のファイル一覧>)
DEPFIX_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
define fix_deps
$(if $(wildcard $(1)),$(shell grep -lE '(^|[[:space:]])[A-Za-z]:/?[^[:space:]]' $(wildcard $(1)) 2>/dev/null | xargs -r sed -i -E -f $(DEPFIX_DIR)depfix.sed))
endef
