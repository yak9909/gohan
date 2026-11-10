# gohan

とびだせ どうぶつの森の日本版（無印＋更新データ）向けに、CTRPluginFramework を使った
`gohan.3gx` と SD 用ファイルを作るプロジェクトです。プラグインとメニューの土台、
旧版のチートの移植を段階的に進めています。

## ビルドと SD の配置

1. [開発手順](docs/development.md) の環境を用意し、`make` で `out/gohan.3gx` を作ります。
   診断を含める場合は `make DEV=1` を使います。ROM は必要ありません。
2. プロジェクト直下で `python -m gohan_tools build-sd` を実行します。
   現在は `spec/hidden_items.tsv` の形を検査し、
   `out/sdmc/luma/plugins/gohan/0004000000086200/hidden_items.tsv` へそのまま写します。
   `out/manifest.json` に入力と出力の SHA-256 を残します。この生成にも ROM は不要です。
3. `gohan.3gx` をプラグインローダーの対象タイトル用フォルダへ置き、
   `out/sdmc/` の内容を SD のルートへ同じ階層でコピーします。

通常のアイテム名は実行時にゲームの RomFS から読みます。没アイテムの自前の名前は SD の表を使い、
表が無い時はゲームの名前のままです。読み込み中もゲームを待たせません。
「自前表示」のアイコンは次の段で対応します。今回は名前までです。
ROM から生成する他の SD 資源は今後の範囲です。手元の入力先の例は `gohan.local.toml.example` を参照してください。

## 現在動かせる検査

Python 3.11 以上を使います。検査本体は標準ライブラリだけで動きます。

```sh
python tools/check.py --all
python -m pytest tests/tools
```

pytest は試験用に別途インストールしてください。コミット前は
`python tools/check.py --staged` を使います。ワーカーは git・gh を実行せず、
明示的なファイル一覧を使って検査します（[開発手順](docs/development.md)）。

自作部分は [MIT](LICENSE) です。第三者の物を追加する際の記載方法は
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。

