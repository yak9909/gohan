# gohan

とびだせ どうぶつの森・日本語版無印向けのCTRPFプラグイン。開発用ソースと仕様書はこのリポジトリで管理する。

- 仕様: [gohan.txt](gohan.txt)。記載された全機能が実装済みという意味ではない。
- 開発規則: [AGENTS.md](AGENTS.md)。再開時は `work/STATE.md` と `work/SESSION.md` を読む。
- 現行ソース: `Sources/`・`Includes/`。生成ヘッダは手編集しない。
- **実機未確認の変更は作業用ブランチへpush。mainは実機確認済みの版だけを反映する。** 新しい変更はmainから `work/<内容>` を作成する。
- 漢字変換・リストボックス表示・ACNL内蔵フォントによる変換結果描画は利用者が実機確認済み（2026-09-13）。現行統合ソースのmain反映を承認済み。

解析用の `yak9909/acnl_disassemble` とは別Git。解析環境では `project_v2/src/CTRPluginFramework-BlankTemplate-0.8.0/` にcloneして使う。同じソースを解析リポジトリへ登録しない。

ビルドには既存devkitPro/MSYS2・CTRPFが必要。**ビルド前に**解析環境の `tools/chat_kanji/verify.py` 等で変更範囲を検査し、**実機適用前に**成果物の分岐・リテラル・ABI・領域重複・復元値を検証する。手順・証拠は解析環境の `docs/topics/chat_kanji.md`・`docs/topics/gui.md` にある。今回の移設では再ビルド・実機操作は行っていない。

`Build/`・3gx・ELFはローカル生成物としてGitから除外する。元テンプレートREADMEは `README.template.md`。旧CI・引退版・バックアップはローカルに保持し、公開Gitには含めない。旧CIを自動ビルドとして有効にしない。
