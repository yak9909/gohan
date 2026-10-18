# gohan

とびだせ どうぶつの森の日本版（無印＋更新データ）向けに、CTRPluginFramework を使った
`gohan.3gx` と SD 用ファイルを作るプロジェクトです。現在は作り直しの途中（G0）で、
プラグインのビルドと資源生成はまだ実装していません。

## 完成後の作業の流れ

1. ビルド環境を用意し、`make` で `out/gohan.3gx` を作ります。
   診断を含める場合は `make DEV=1` を使う予定です。ROM は必要ありません。
2. `gohan.local.toml.example` を `gohan.local.toml` として複製し、手元の入力先を設定します。
   SD 資源には、日本版の更新データとハッピーホームデザイナー日本版 v1.1 の ROM 展開物を使う予定です。
3. 生成器の導入後、`python -m gohan_tools check` で入力の版を確認し、
   `python -m gohan_tools build-sd` で `out/sdmc/` と `out/manifest.json` を生成します。
4. `gohan.3gx` をプラグインローダーの対象タイトル用フォルダへ置き、
   `out/sdmc/` の内容を SD のルートへ同じ階層でコピーします。
   資源は `sdmc/luma/plugins/gohan/` 以下への配置を予定しています。

上記は今後実装する手順です。ツールチェーン、対象タイトル用フォルダ、manifest の
配置と資源ごとの詳細は、ビルド・生成機能の導入時に確定して文書化します。
実行時には資源の不足や版の不一致を確認し、該当機能を止めて通知する設計です。

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

