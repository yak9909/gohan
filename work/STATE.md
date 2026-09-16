# 現在地 — 仕様と設計レビューの保存（2026-09-17）

利用者の指示で既存変更をwork/spec-mdへcommit/pushする。仕様2本・未採用設計案・根拠・作業記録が対象。mainへmergeしない。コード変更・ビルド・実機試験なし。source5ファイルはレビュー時SHA不変（work/design/publication_review_20260917.json）。送信結果はGit logとorigin/work/spec-mdを照合する。

確定仕様: トグル型アクションはhotkeyの有無によらずメニューON→適用で即実行/OFF復帰。hotkeyは確認後実行でON/アーム不要。hotkey設定だけの適用で発動しない。効果ON中のhotkey追加/変更/解除は効果を変えない。スタイルは一つのアクションから専用メニューへ。

設計案はwork/design/item_architecture_review.md。採用/実装は未指示であり、次は利用者の採否/実装指示。gohan.mdの追加チート参考値も今回の実機検証済みとは扱わない。詳細と以前の記録はSESSION.md。

正常版の漢字候補・一覧・内蔵フォント描画は利用者確認済み。今後の実機未確認変更は必ず作業用branchで保存する。
