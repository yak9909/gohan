# CTRPluginFramework 同梱版の変更

## 出所と版

| 対象 | 上流 | 採用版 |
| --- | --- | --- |
| `include/`・`source/`・`Makefile` | https://gitlab.com/thepixellizeross/ctrpluginframework の `Library/` | タグ 0.8.0、リビジョン `a502818c`（2024-10-21） |
| `LICENSE.txt` | 上記のプロジェクト直下 | 同じ版 |
| `libcwav/` | https://github.com/mariohackandglitch/libcwav | `4fc8b11b`（2023-06-22） |
| `libcwav/libncsnd/` | https://github.com/mariohackandglitch/libncsnd | libcwav が指定する `667cf998` |

ソースと元のライセンス・README を収録する。libcwav のサンプル、配布済みの書庫、
オブジェクト、依存ファイルなどの生成物は含めない。

## 設定保存

- `Preferences.cpp` で設定ファイル名を `CTRPFData.bin` から `GohanCTRPFData.bin` に変更する。
  従来のファイルを移行する処理は設けない。
- CTRPF の見出し、有効なチート、お気に入り、ホットキーの形式を維持し、追加の設定欄を設ける。
  見出しの `reserved[0]` は識別値 `'GOHN'`（`0x4E484F47`）、`[1]` は欄の版（1）、
  `[2]` は欄のオフセット、`[3]` は欄のバイト数とする。
- `include/CTRPluginFramework/System/GohanData.hpp` に公開 API `CTRPluginFramework::GohanData` を追加し、
  `System.hpp` から公開する。`SetHandlers` で読み書き関数を登録し、`Load` で追加欄を読み込み関数へ渡す。
  ファイルや有効な欄がなければサイズ 0 を渡す。`Save` は CTRPF の欄と追加欄をまとめて保存する。
- CTRPF メニューを閉じる通常の保存にも、登録された書き出し関数を使う。
  書き出し関数が未登録の場合は既存の有効な追加欄を読み出して保持し、設定が消えることを防ぐ。
- 複数のメニュースレッドからの読み書きとハンドラー登録は、共通の再帰 Mutex で排他する。
  呼び出しを入れ子にしても同じロックを使う。
- 保存処理のコメントは、動作の説明に整理する。

## ビルド

- 版を 0.8.0（識別文字列は `0.8.0-gohan`）と `a502818c` に固定する。
  ビルド中の版の問い合わせ、依存ソースのネットワーク取得・更新は行わない。
- CTRPF と音声ライブラリの Makefile は、ソースを参照したままプロジェクトの
  `build/libctrpf/` でコンパイルする。中間生成物、依存ファイル、書庫をソース側へ作らない。
- 既定の `all` と `release` は最適化版 `libctrpf.a` を作る。`debug` は `libctrpfd.a` を作る。
  CTRPF は C++20、例外・RTTI 無効、LTO 無効とし、リリースの最適化と警告設定を維持する。
  libcwav は `CWAV_DISABLE_DSP` を定義し、CSND 用にビルドする。
- Windows 版 ar に渡す書庫結合の制御文字列は相対パスで記述し、MSYS のパス変換に依存しない。
- 結合書庫には CTRPF・libcwav・libncsnd の全オブジェクトを含める。
  結合前の書庫も `build/` に保持する。ビルド日時を持つ `PluginMenuTools.o` は
  ビルド後に取り除き、次のビルド時に作り直す。
- `clean` は対応する `build/` 配下だけを削除する。依存ソースや開発環境は削除しない。
  CTRPF の `install`・`install-upstream`、音声ライブラリの `install`・`uninstall` は無効にする。
  配布用アーカイブをソース側へ作る `dist` 系の手順は設けない。

音声ライブラリの実装・公開ヘッダーと元のライセンス文は変更していない。
