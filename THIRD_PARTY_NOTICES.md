# 第三者の物について

自作部分の MIT は以下の第三者のソースには適用しません。
上流の著作権表示とライセンス文を各ソースの近くに保存しています。
変更内容は [PATCHES.md](third_party/libctrpf/PATCHES.md) を参照してください。

| 名称・用途 | 配置先・出所と版 | ライセンスと原文の場所 |
| --- | --- | --- |
| CTRPluginFramework（CTRPF）・プラグインの実行とメニュー | `third_party/libctrpf/`。[The Pixellizer Group の上流](https://gitlab.com/thepixellizeross/ctrpluginframework)、0.8.0 / `a502818c` | The Pixellizer Group の独自ライセンス。[LICENSE.txt](third_party/libctrpf/LICENSE.txt) に全文を収録 |
| libcwav・効果音の再生 | `third_party/libctrpf/libcwav/`。[上流](https://github.com/mariohackandglitch/libcwav)、`4fc8b11b` | zlib。[README.md の License 節](third_party/libctrpf/libcwav/README.md#license) に全文を収録 |
| libncsnd・CSND 音声サービスの利用 | `third_party/libctrpf/libcwav/libncsnd/`。[上流](https://github.com/mariohackandglitch/libncsnd)、`667cf998` | wxWindows Library Licence（例外条項を含む）。[LICENSE.md](third_party/libctrpf/libcwav/libncsnd/LICENSE.md) の同名節と、同ファイル内の LGPL2 License 節に原文を収録 |
| libncsnd 内の libctru 由来の CSND 実装 | 上記 libncsnd に含む。[libctru 上流](https://github.com/devkitPro/libctru) | zlib。[同 LICENSE.md の zLib License 節](third_party/libctrpf/libcwav/libncsnd/LICENSE.md#zlib-license) に全文を収録 |
| 美咲ゴシック第2・UI の書体 | [作者の配布ページ](https://littlelimit.net/misaki.htm) の [BDF 2021-05-05 版](https://littlelimit.net/arc/misaki/misaki_bdf_2021-05-05.zip)。`third_party/fonts/misaki/` に元 BDF を無変更で収録 | M+ FONTS 型の利用条件。配布元の説明書全文を [LICENSE.txt](third_party/fonts/misaki/LICENSE.txt) に収録 |
| PixelMplus・UI の数字と英字の一部 | [作者のソース](https://github.com/itouhiro/PixelMplus/tree/d89b95f04eeb95bf98bb09cb9234a2f99d186df5)、2013-06-02 公開系統。`src/bdf.d/mplus_j10r-iso-W5.bdf` を `third_party/fonts/pixelmplus/` に無変更で収録 | M+ FONT LICENSE。[LICENSE_E](third_party/fonts/pixelmplus/LICENSE_E) と [LICENSE_J](third_party/fonts/pixelmplus/LICENSE_J) に原文を収録。著作権表示は [README.md](third_party/fonts/pixelmplus/README.md) に保存 |

CTRPF は設定保存とビルド手順を変更しています。libcwav・libncsnd はビルド手順を変更し、
実装と公開ヘッダーは採用版のままです。CTRPF のソースに含まれる個別の著作権・利用条件の表示も保持します。

書体はビルド時に `tools/gohan_tools/codegen/font_atlas.py` で変換します。
美咲は 667 字、PixelMplus は 27 字を採用します。採用する文字と並びは同生成器の
`*_characters.txt` が定義します。美咲は基線を上から 7px とし、PixelMplus は
11px 高の元セルの上 2 行と下 1 行の空白を除き、共通の 7×8px セルに収めます。
1px の間隔を付け、塗り潰しセルを加えた LA4 のアトラスを `build/gen/` に生成します。

追加の第三者ライブラリは `third_party/<名前>/`、書体は `third_party/fonts/<名前>/`、
生成用ツールのソースは `tools/<名前>/` に置き、出所・版・ライセンス文・変更点を追記してください。
実行形式やビルド出力は収録せず、依存ライブラリも個別に記載します。
