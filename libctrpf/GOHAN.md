# gohan に同梱した libctrpf

gohan はこのディレクトリの libctrpf を改造して使う（`../Makefile` の `CTRPFLIB := $(TOPDIR)/libctrpf`）。
devkitPro の `$(DEVKITPRO)/libctrpf` は使わない。

## 出所

| 部分 | 出所 | 版 |
|---|---|---|
| `include/` `source/` `Makefile` | gitlab.com/thepixellizeross/ctrpluginframework の `Library/` | tag **0.8.0** = `a502818c`（2024-10-21） |
| `LICENSE.txt` | 同リポジトリの直下 | 同上 |
| `libcwav/` | github.com/mariohackandglitch/libcwav（`example_libcwav` は除く） | `4fc8b11b`（2023-06-22） |
| `libcwav/libncsnd/` | libcwav のサブモジュール libncsnd | `667cf998` |

- devkitPro の libctrpf（`82a974d9` = 0.8.0 の次の commit。差はテスト用プラグインの plgInfo 1 行だけ）とは
  **公開ヘッダが改行以外一致**。中のオブジェクトは 175 個で同じ、data / bss も同じ。text の差は
  コンパイラの版（devkitPro 版は devkitARM r65 / GCC 14.2.0、この PC は r61 / GCC 13.1.0）。

## 改造点（ここに全部書く）

1. `Makefile`: 版を `git describe` から取らず 0.8.0 に固定（このディレクトリは gohan の Git の中なので）。
   ビルドのたびに libcwav を clone / `git pull` する処理を外した（gohan のリポジトリに pull が走るため）。
   `clean` が `libcwav` を丸ごと消していたのを、生成物だけ消すように変えた。`install` は止まるようにした
   （devkitPro の libctrpf を上書きするため。元の処理は `install-upstream`）。

## ビルド

gohan の `make` が先に `make -C libctrpf lib/libctrpf.a` を呼ぶ。生成物（`lib/` `release/` `libcwav/**/build` `lib`）は Git に入れない。
`make clean-all` で libctrpf の生成物も消す（ソースは消さない）。
