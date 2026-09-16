# gohan 開発

このフォルダは `yak9909/gohan` の独立Gitルート。ソースと仕様（`gohan.md` / `gohan-menu.md`）はここで編集し、このリポジトリへpushする。親の `acnl_disassemble` には登録しない。

- **実機動作未確認の変更は必ず `work/<内容>` 等の作業用ブランチへpushする。mainへ直接push/mergeしない。** PC検証・ビルド成功・別版の実機成功は実機確認の代わりにならない。
- mainへ反映する前に、その変更を含む成果物のSHA・実機で試した操作/結果・利用者の確認をworkに記録する。未確認/一部確認は作業用ブランチに残す。圧縮後も現在branchと確認状態を再確認する。
- ソースの版管理・復元はGit commit/branchを用い、作業ごとのソースzipやバックアップフォルダを増やさない。Gitに保存していない固有データは削除せず、必要な実機確認済み成果物は解析環境のartifactsへ集約する。

- 日本語版無印ACNL＋更新が対象。依頼の用途を推測しない。
- 暗算禁止。アドレス・サイズ・個数・進数変換はPython等で計算する。
- 編集前・重要な結果/訂正の直後に `work/SESSION.md`、一区切りで `work/STATE.md` も更新。圧縮後はこの2つと仕様を読み直す。目的・制約・承認範囲・完了/未完了・根拠・実行中操作・次の1手を保存する。
- **ビルド前と実機適用前に検証必須**。ソース→ビルド→リンク済み分岐/リテラル/ABI/SP/VFP/領域重複/hashの検査。停止状態・このbootの元値・復元先を確認して最小操作で試す。成否不明の書込み・Resumeを再送しない。
- 描画・しずえフックを無関係な追加で変更しない。生成ヘッダは手編集しない。元の呼出し規約・初期化・全引数を確認する。
- 現行mainは利用者が実機確認済み（2026-09-17）: Simulator 0cabaed の移植・フォルダ分け・issues #1〜#5・効果のON/OFFからの通知。3gx SHA=4fdc1cfd7805fa17a0bb02cb95927f1294ea79f1d5562e143d31825a74f0824d（project_v2/artifacts/plugins/simulator_port）。以前の漢字変換統合版は 201af7e9…（artifacts/plugins/chat_kanji）。今後の変更まで確認済みと一般化しない。辞書・元エンジンcodeをSD/3gxへ同梱しない。
- 解析用環境を併用する場合はproject_v2からこのリポジトリを `src/CTRPluginFramework-BlankTemplate-0.8.0/` にcloneする。検証の入口はその環境の `tools/chat_kanji/verify.py`。ソースだけをcloneした状態で検証済みと主張しない。
- 旧版は `_v1_retired/`、旧CIは `.github/templates/` の参照専用。現行の指示として再開しない。新規出力と途中メモはworkへ保存。
