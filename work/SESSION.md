# Simulator移植とソース整理（2026-09-17開始）

## 最新指示（利用者）
gohan-gui-simulatorをpull（済: 4083d70→0cabaed）。simulatorとgohan-menu.mdの仕様・見た目を把握し、gohan.3gxへ全て移植。GuiV2等バージョン表記のソース名をやめ、標準的な範囲でフォルダ/ファイル分け（やりすぎない）。必要ならファイル名変更も可。

## 制約
- 実機未確認 → branch work/simulator-port（work/spec-mdから作成）。mainへ入れない。
- 実機確認済み3gx SHA=201af7e9…は project_v2/artifacts/plugins/chat_kanji/gohan.3gx に保存済み。src/gohan/gohan.3gxはビルドで上書きされうる。
- 描画・しずえフックを無関係に変えない。生成ヘッダは手編集しない（生成器側を直す）。
- ビルド前/後の検証必須。実機操作はしない（依頼外）。
- project_v2側の検証器(tools/patches/verify_plugin_port_v2等)はソース名を正規表現で読むので改名時は追従が必要。

## 基準確認（2026-09-17）
- 変更前の検証: verify_plugin_port_v2 異常0 / verify_shizue_hook_registers PASS / verify_keyboard_port は**pull後のSimulatorで失敗**（数値キーボードのOKが即閉じ→新Simulatorは退場アニメ。oracle側の追従が必要）。ログ project_v2/work/logs/simport/baseline_*.log
- 未変更ソースのclean build: 863890B SHA b8e76e05…。確認済み201af7e9…と13755B差だが nm の全3093シンボル名・サイズ一致＝リンク順のみの差。
- フォント: Simulatorが描く文字は全てアトラス(美咲385+数字27)に既存。アトラス再生成不要。
- 上画面文字スロット: [X]分割/SYNC/長押しバー/実行確認ダイアログで追加が要る（GuiV2 kTopSlotCaps）。

## 進捗
- [ ] simulator(ui-model.js/app.js/README)とgohan-menu.md読了、差分表作成
- [ ] 移植実装
- [x] Phase A ソース改名/フォルダ分け（Sources|Includes/{Gui,Fonts,ChatKanji,Cheats,Debug}）。GuiV2→GuiRenderer（名前空間も）、GuiCavesV2.h→GuiCaves.h。未使用のテンプレート残骸 Helpers/cheats/Unicode.h/GuiFontNw.h を削除。Makefile の SOURCES/INCLUDES を追従。
  生成器(export_gui_v2/make_font_ui/export_keyboard_layout)の出力先を更新し再生成→差分はコメント行のみ。clean build 864325B、名前の置換を正規化すると全シンボル名・サイズが基準と一致。
  検証器パス更新: verify_plugin_port_v2 異常0、shizue --artifact PASS、keyboard は基準と同じ段で失敗（pull起因）。
  tools/chat_kanji/verify.py --source-only は「GuiKeyboard.cpp等がchat_kanji基準から不変」を要求する歴史的ゲートのため改名で失敗（意図した変更。緩めない）。
- [ ] 検証器追従・ビルド・成果物検証
- [ ] commit/push (work/simulator-port)

# 設計レビュー・仕様訂正（2026-09-14）

## 最新: Git保存（2026-09-17）

利用者がgohanを含む各リポジトリの変更commit/pushを明示承認。差分は仕様2本・進捗・未採用設計レビューと根拠で、実装変更なし。既存work/spec-mdへ保存し、mainへmergeしない。source5ファイルはレビュー時SHAと一致。gohan.mdの後続チート参考値の追記も保持するが今回検証済みにはしない。根拠work/design/publication_review_20260917.json。実装/ビルド/実機試験は行わない。送信結果はbranchのGit logとorigin/work/spec-mdで確認する。

以下は設計時点の記録。「commit/pushなし」はその時点の範囲であり、今回の送信指示を禁止するものではない。

最新の確定仕様（2026-09-14最終回答・前段のON必須/待機解釈を撤回）: トグル型アクション式は束縛の有無を問わずメニューON→適用で即実行しOFFへ復帰。hotkeyは確認ダイアログ→承諾後実行で、持続的なON/アームを前提にしない。アクション式との差はメニュー実行の適用待ちとhotkeyの確認ダイアログのみ。hotkey設定変更自体は適用待ちを経由し、設定の適用だけでアクションを実行しない。
質問は今回の回答で解消。仕様2本・レビューへ反映済み。旧ON必須/待機解釈は撤回。今回は文書整理のみ、実装/ビルド/実機/commit/pushは行わない。全体設計の採用/実装は利用者の次の指示。未完了操作なし。
最終訂正の機械検証PASS: 両仕様のSHA保存、旧条件の残存検査、ApplyOnce比較表、ソース5ファイルのSHA不変を確認。根拠: work/design/clarifications_20260914.json。

2026-09-14の最新回答: 「全項目が編集→適用」は文章上の矛盾の説明を求められた。既存の即時アクション仕様を変更しない。トグル型アクションのhotkeyは確認ダイアログ、承諾後に実行。効果ON中のhotkey追加/変更/解除では効果に何も変更を加えない。スタイルは「スタイルを変更」アクションから専用メニューを開き、そこで設定/実行。編集項目の独立配置を廃止し、寝癖トグルと解析根拠は保持。
今回の範囲: 仕様2本と未採用レビューの文書整合・訂正、メモ。反映・差分読戻し・文書参照検査完了。ソース5ファイルのSHA不変を確認、根拠work/design/clarifications_20260914.json。コード/ビルド/実機、設計全体の採用・実装、commit/pushはなし。未完了操作なし、次は利用者の採否/実装指示。

最新依頼: gohan.md / gohan-menu.md とClaudeのItemSpec案を読み、将来の拡張を考慮した改善案と利点・欠点を提示。今回は提案のみ。仕様の承認済み挙動、ソース、ビルド、実機を変更しない。採用・実装は次の利用者指示。
現在Gitは work/spec-md、開始時originと一致・clean。旧文の「現在main」は過去の状態。
読了: 両仕様、GuiMenu.hpp、GuiMenu.cppの登録/適用/入力/終了/パッチ、ShizueSkip.cppの呼出し文脈、ChatKanji.hppの非同期契約。
結論: 項目別登録・定型Patch・駆動規則は支持。ただし値binding・共有操作・成功/失敗/非同期・安定ID・ライフサイクル・実行場所を補った小さな構成を推奨。単なる登録層の変更で既存無影響とは保証しない。
完了: work/design/item_architecture_review.mdへ比較・具体的構成・仕様整理点・段階的移行案を未採用案として保存。ソースの直接applied代入経路、worker/ゲームフックの区別、表の未定/確定混在を確認。BuildTreeは57項目（上限96）、計算と対象SHAはreview_evidence.json、再現はreview_evidence.py。
未完了操作: なし。次は利用者の採否・実装指示。提案を承認済み仕様へ昇格させない。今回はビルド/IDA/実機操作、commit/pushなし。

## 過去の引継ぎ（現行状態として再開しない）

最新訂正（以下の実機未確認記述を上書き）: 利用者は現行の漢字変換・リストボックス表示・ACNL内蔵フォントによる変換結果描画を実機確認済みと明示し、現行ソースのmain pushを許可。既存統合3gx SHA=201af7e973b3175b7995d18aa7f92eeace5a08280debbd146099dcc794101455。mainへ現行統合コードを戻す。コード/成果物を今回変更せず、再ビルド/実機操作なし。今後の未確認変更は作業用branch。F394 commit fa76dfc05bcc8b362829f53ce48b1593c4099a38はGitから復元可能。

最新指示: 実機未確認の変更は作業用ブランチ限定。チャット統合はwork/chat-kanjiへ分離、mainの先端は実機確認済みF394ソースへ戻す新commit。旧初回commitは履歴のため残る。mainへ統合するには当該成果物SHAと利用者による実機結果が必要。Git保存済みソースのコピー/zipを今後増やさない。未保存の成果物や固有解析資料と区別する。

目的: 継続開発するgohanソースと仕様をyak9909/gohanのmainで管理。旧プロジェクトから同一bytesで取り込み。親acnl_disassembleはgitignoreで除外。Buildと出力バイナリはローカル保持、Gitではソース・仕様を管理。

実装状態: 通常gohanへ普通チャット入力の漢字変換候補を下画面一覧へ表示する機能を追加済み。入力書戻し/送信なし。内蔵タイトルをRAMで読み、APT/内蔵キーボード画面を使わない。辞書/元codeのSD・3gx同梱なし。スレッド結果の公開・取消・解放はthreadJoinの戻り値0を確認する。

検証: 移設前後のソースと3gx/ELFのSHA一致、独立した検証記録はproject_v2のwork/evidence/chat_kanji/・reorganization/。キーボード10943遷移/15場面、しずえ、frontend24ケース、全65536字形、5入力の候補順位、リンク済み全分岐/LDRをPCで検証済み。**統合版の実機表示は未確認**。

未完了/注意: gohan.txtは開発仕様であり現状の全項目実装完了を意味しない。F394の実機正常版と現在統合版を混同しない。今回ビルド・実機書込み・Resumeなし。旧CIは未検証のためtemplatesへ保管。

公開範囲: 現行ソース・仕様・開発文書だけ。旧バックアップ・引退版・CIテンプレート・キャッシュはローカル保持し、公開Gitから除外。旧資料まで含めたpushは自動承認レビューが拒否し、実行されていない。

次の1手: 最新依頼→仕様/既存解析の確認→変更前メモ→ソース検証→必要ならビルド→成果物検証。実機はboot/停止/元値/領域を再観測してから扱う。公開pushの結果はgit logとorigin/mainを照合する。
