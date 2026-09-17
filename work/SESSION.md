# gohan.md 詰め（続き）— 2026-09-17・最新
利用者:
1. 壁抜けは `[壁抜け 3patches]`（0064F03C E3A07000 / 0064F19C E1A00000 / 0064F1B4 E1A00000）が**実機で動作確認済み**なので採用（8語版は別候補へ）。
2. 「歩いた場所のアイテムを消し去る」（Trampler と呼ばれることがある）「アイテムが消えない」「キーボード制限解除」も PatchList で実装できるはず。キーボード制限解除は Vapecord のキーボード制限を取り払う系チートを全部パッチする。
3. 次に PatchList 以外を詰める: 座標移動関連・タッチワープ・アクション解除・気絶・ポケットアイテム・木にぶつかって切り倒す（旧資料）・スコップ3x3マス掘り（旧資料）・天気 の設計を gohan.md に記載。
ブランチ work/patchlist-spec（仕様書のみ）。IDA は使わず資料＋code.bin 照合で進める（必要なら利用者に確認）。
結果（2026-09-17）:
- 壁抜け: 3語（実機確認済み）を採用、8語を別候補へ。
- 歩いた場所のアイテムを消し去る: AnimalBytes Trampler の JPN 6語 NOP（元命令と全一致、FOXXY の変換可能3語と一致）。Vapecord Walkseeder 1語は別候補。花散らせないと同じ関数（同時ONは未解析）。
- アイテムが消えない: Vapecord の2語＋フックの代わりに NEVER_WILT_CALLER_1..5（Thumb の BLX 0x2FC950 そのもの）を NOP 2個 0xBF00BF00 にする案。OFF は code.bin の4バイト。フックと等価かは未解析。
- キーボード制限解除: .bss 1バイト×3（KEYENTER 0xAD0253=1 / KEYAT 0xAD05C0=1 / MORE_NUMBERS 0xAD0158=2）。OFF 未確定、key_limit の動的データと CustomKeyboard は固定番地にならない、キーボードを開くたびの再初期化は要解析。
- どこでも掘れる: 旧 patches.md の9件目 0x5982C4（副作用あり）を注記。
- §17 設計: 共通（GetPlayer 0x5C27D8 / 番号 0x305F6C / WorldCoords 0x5BFCE4 / 座標 +0x14..+0x1C / 状態 +0x1A9 / GetAnimInst 0x6561F0 / Actor_SetState 0x64C688）、座標移動、タッチワープ（Vapecord の地図変換表）、アクション解除（状態6）、気絶（状態0x9D）、ポケットアイテム（sub_2FB900()+27600、ReloadIcons 未変換）、木の伐採（F-42 フック1語）、3x3（F-34 フック4か所、置き場の依存）、天気（0x62E728 MOV R0,#n）。変換番地は design_addresses.py で code.bin 照合。
- 根拠: project_v2/tools/patches/patchlist_candidates2.py・design_addresses.py、work/evidence/patchlist/*.json。

# gohan.md の詰め — PatchList 表（2026-09-17 開始・最新）
利用者指示: PatchList（複数アドレスの貼る/剥がす）だけで実現できそうなチートについて、ON/OFF のアドレス・値表を gohan.md に記載。アドレスは文書から探して最適なものを選ぶ。reference/old_project の CTRPF ソース群も確認する。
制約: 対象は JPN 無印＋更新（code.bin = project_v2/artifacts/update/exefs/code.bin、VA=off+0x100000）。USA 等の番地を流用しない（変換表で JPN に直し、OFF 値は code.bin の実バイトで照合）。暗算しない。実機・ビルド・IDB 変更なし。
結果（2026-09-17）:
- 候補の収集・照合: project_v2/tools/patches/patchlist_candidates.py（gohan.md・REFERENCES/CHEATS・Vapecord USA→JPN（Addresses.hpp の JPN 列）・JOKER/ROTATION/Adios/AnimalBytes/FOXXY）。全候補で code.bin の元命令と資料の OFF 値が一致、USA→JPN 変換も JPN 実装と一致（壁抜け・穴・速度）。結果 work/evidence/patchlist/candidates.json、比較 patchlist_context.py。
- gohan.md に「0.5 PatchList 表について」と各項目の表を追加: 壁抜け8語（Vapecord/JOKER/ROTATION/Adios、旧3patchesは別候補）、花散らせない 0x596890（BL→MOV R0,#29 で既存 BEQ から抜ける。Vapecord 0x59689C は別候補）、穴に落下しない2語、寝癖付かない1語、どこでも掘れる8語、空を見上げない 0x64C594（CHEATS camera 4語は別候補）、店24時間オープン9語、メッセージ即表示 0x5F8278（sub_5F8238 の呼出し元は1件。CHEATS/Vapecord 案は別候補）、フレームレート制限解除 0x54C6E8（F-356。Vapecord はセーブ中に外す注記）。
- PatchList だけでは不足: アイテムが消えない（フック併用）、キーボード制限解除（毎フレーム書込み）、歩いた場所のアイテムを消し去る、タッチワープ等。
- 効果は未解析（R2）。IDA は途中で使わず、code.bin の逆アセンブルと BL 走査のみ。

# main 反映（2026-09-17・最新）
- 利用者が work/simulator-port 先端 5062f72 の成果物を**実機で確認**し、main へのマージを指示。
- 成果物: gohan.3gx 875060B SHA 4fdc1cfd7805fa17a0bb02cb95927f1294ea79f1d5562e143d31825a74f0824d / gohan.elf SHA 185224c49de8c85c44aea439a708858872507f97d00c2e9cfd1287b5fcc794ed。
  project_v2/artifacts/plugins/simulator_port/（manifest.json）へ保存。以前の確認済み 201af7e9… は artifacts/plugins/chat_kanji/ に残す。
- 内容: Simulator 0cabaed の移植、ソースのフォルダ分け、実機フィードバック修正（入力待ち中のタッチ、issues #1〜#5）、効果の ON/OFF からの通知。
- 操作: main を work/simulator-port へ fast-forward して push。今後の未確認変更は再び作業用ブランチ。

# issue 解決確認と通知の整理（2026-09-17・最新）
利用者: issues #1〜#5 は全て解決（実機確認）。タグ（ラベル）を付けてから解決済み（close）にする。
通知: しずえスキップやテストチート等、メニューで項目を有効/無効にした時の通知をやめる。通知は基本的に関数内定義（効果）のON/OFFから発生させる。効果のON/OFFはホットキーの設定状態でも変わる（gohan-menu.md §4.3: 束縛ありのON適用はアームのみ、OFF適用は必ず解除、ホットキーで反転）ので考慮する。
方針: 効果の状態（IsActive）をフレームごとに観測し、変化した時だけ CHEAT ENABLED/DISABLED を出す（変化の原因＝適用・ホットキー・関数側を問わない）。項目の適用自体では通知しない。
結果:
- issues #1〜#5: 既存ラベルから #1〜#3 に bug、#4・#5 に enhancement を付け、「4fd21d4 で修正・実機確認済み」とコメントして close。
- 実装: GuiMenuModel の PollEffects（Step の最後、walkItems 順で IsActive を観測し変化時だけ通知）。SetCheckboxEffect と CommitItem の通知を削除（効果なし項目の適用通知も廃止）。RegisterToggleEffect/Unregister で PrimeEffect（登録しただけで通知しない）。menu_oracle.js も同じ規則（フレーム終わりに効果の変化を通知）。gohan-menu.md §6(a)・§8.4 更新。
- 検証: verify_menu_port / plugin_port_v2 / keyboard_port / start_tap / shizue --artifact / リンク済みELF監査 すべて PASS。シナリオ2で通知は「束縛なしON適用・ホットキー反転x2・OFF適用・束縛ありON適用後のホットキー」でのみ発生、アームのみでは無し。シナリオ3（効果なし項目）は通知0。PollEffects を外すと差分試験が検出。
- 成果物: gohan.3gx 875060B SHA 4fdc1cfd7805fa17a0bb02cb95927f1294ea79f1d5562e143d31825a74f0824d。実機未確認。

# 実機確認後の修正（2026-09-17 開始・最新）

利用者: work/simulator-port（060fa1d, 3gx 7ad9fc3d…）が**実機で動作**。修正依頼:
1. ホットキー入力待ちの間、下画面を操作不能にする。
2. トグル式チート（PatchList/ToggleEffect）の通知を、メニュー項目のON/OFF適用ではなく関数内定義（効果）のON/OFFの変化で出す。
3. yak9909/gohan の open issues を解決: #1 退場アニメ中も操作を受け付ける / #2 ホットキーで開いた文字・数値入力を閉じても暗幕が戻らない / #3 文字キーボード退場時の暗幕フェードがずれる / #4 濁点・半濁点キーを「゛」「゜」表記に / #5 STARTは単押し（押して離す）で初めて押下扱い（START+十字上等のホットキーでゲームにSTARTを渡さない）。
制約: 引き続き作業用ブランチ（work/simulator-port）。実機未確認の変更はmainへ入れない。
利用者の回答/訂正:
- 1 は「ゲーム側の下画面が反応する」。推定原因: 入力待ちを無効/取消タッチで閉じた瞬間にタッチ遮断を外し、指が残っているとゲームへ新規タッチが届く。対策: タッチ遮断は下画面UIが存在する間（退場中も）＋離すまで維持。暗幕はUIの出現量に連動（#2/#3も同根: 暗幕を独自タイマーで描き再描画が止まる／キーボード退場と別タイミング）。
- #5 START/SELECT の経路は F-355（reference/old_project/RE/FINDINGS.md）で特定済み。**IDAの追加解析は不要**と指摘された（idalibで開いたが未保存で閉じた）。既存のケーブB（0x003534E0、読み替え前 bit2=SELECT/bit3=START）を生成する tools/patches/input_block_cave.py を書き換え、GuiCaves.h 再生成＋OwnGui の制御ブロック初期化で実装する。
  設計: 制御ブロック 0x009B7010 +3=START単押しモード(導入時1) +4=判定中 +5=押下合成の残り。ケーブ内でフレーム単位に判定（STARTを押した時に他ボタン無し→離した時に1フレーム hold+trig、次フレーム release を合成。押している間に他ボタンが入れば取り消し）。STARTの生ビットは常に削る。ケーブBは語数が増えるので置き場を 0x00838200（原本で0、未使用）へ移す。
- #4 「゛」(U+309B)「゜」(U+309C) は美咲BDFにあるがアトラスに無い。simulatorリポジトリは触らず、make_font_ui.py が BDF から追加字形を読む形で足す案。
実装・検証（2026-09-17、ビルドとPC検証まで。実機未確認）:
- 入力待ち等: GuiRenderer の SetBottomLock を廃止し SetTouchBlock と DrawBottomDim(色,出現量) に分離。GuiMenu が毎フレーム SyncInputLock（下画面UIがある間＋指が離れるまでタッチ遮断）。暗幕は BottomDim（Simulator の backdrop×amount）。
- 通知: SetCheckboxEffect で効果が変わった時に CHEAT ENABLED/DISABLED。効果未登録の項目は従来どおり適用で通知。
- #1: HandleKey で g_visible && g_openTarget!=0 のときだけメニュー操作（退場中はホットキーも閉じた扱い）。
- #4: make_font_ui.py が simulator の BDF から ゛゜ を追加（美咲387字）。GuiKeyboard の表記を変更。
- #5: input_block_cave.build_cave_b に START 単押し（68語、0x00838200）。export_gui_v2 で再生成、OwnGui が +3=1/+4=0/+5=0 を初期化・取り外しで 0。objdump で命令確認、tools/patches/verify_start_tap.py（Unicorn ARM11 で 7 シナリオ＋乱数8000フレームを参照モデルと照合、判定を壊すと検出）。
- 検証: verify_menu_port（シナリオ3388＋乱数6系列一致、#1 の手順追加、修正を外すと検出。キーボード表示中のタッチはキーボード検証側で照合するため送らない）、verify_plugin_port_v2 異常0（ケーブBの位置と BIC #8 の検査を追加）、verify_keyboard_port PASS、shizue --artifact PASS、リンク済みELF監査 PASS（命令9579/分岐1688/リテラル767）。暗幕がキーボードの出現量に比例し閉じ切ると消えることを捕獲描画で確認。
- 成果物: gohan.3gx 874787B SHA 72b3c9d38465b2925a11853737c82683d48221dbe7c27f69e3d8f588e9584c8b。
- 注意: タッチの原因は推定（実機で未観測）。START はケーブBの呼び出し条件 byte_9778D8==0 の間だけ効く（SELECT 遮断と同じ条件）。

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
- [x] Phase B 移植実装（2026-09-17 完了。ビルド・PC検証まで。実機未確認）
  方針: Simulator(0cabaed)のCheatMenuModel.handle/update/commitItem等を1対1で写す。入力はフレーム毎の水準→離し/押し/連打(JSのControlRepeater: nextAt+=60)のイベント列へ変換し handle(key) と同順。handle→update の順（JSのframe順）。
  分割: Sources/Gui/GuiMenuInternal.hpp(定数/状態) GuiMenuModel.cpp(状態遷移) GuiMenuDraw.cpp(描画) GuiMenuItems.cpp(項目木/見本) GuiMenu.cpp(スレッド/CTRPF入力/公開API)。Model/Draw/ItemsはCTRPF非依存→ホストでJSと差分試験(menu_host.cpp/menu_oracle.js)。
  追加: 連動型数値/リスト(開いた時に読む・失敗で選択不可)、トグル型アクション(適用で実行→OFF、OK 800ms、ホットキーは確認ダイアログ、閉じても残る)、X/L 600ms長押しの全適用/全戻し確認、L短押し=選択項目戻し、スライダー下画面UI、数値/入力待ちの退場アニメ、上画面リストはメニューを閉じても残る、checkbox効果: ON適用で束縛なし→効果ON/束縛あり→アームのみ/OFF適用→必ず効果OFF、適用時CHEAT ENABLED/DISABLED通知、[X]のXは#ff6b6b、S+水色/SYNC、説明6行、通知7件、ホットキー入力はA/B/Xも記録（無効/取消はタッチ）。
  Simulatorに合わせて変える既存差: 通知題（SELECTED/ACTION）、ホットキー反転時のエンジン通知を廃止（効果関数側で出す）、SetEffectMenuJudgment廃止（§4.3が既定）。
  予算: 上スロット69→93本(最悪125136B<=131072B)、上矩形74→86。フォントは追加不要。スペースとXの送りが同じ4pxなので[X]は"[ ]"+赤X重ね描き（画素同一）。
- [x] Phase A ソース改名/フォルダ分け（Sources|Includes/{Gui,Fonts,ChatKanji,Cheats,Debug}）。GuiV2→GuiRenderer（名前空間も）、GuiCavesV2.h→GuiCaves.h。未使用のテンプレート残骸 Helpers/cheats/Unicode.h/GuiFontNw.h を削除。Makefile の SOURCES/INCLUDES を追従。
  生成器(export_gui_v2/make_font_ui/export_keyboard_layout)の出力先を更新し再生成→差分はコメント行のみ。clean build 864325B、名前の置換を正規化すると全シンボル名・サイズが基準と一致。
  検証器パス更新: verify_plugin_port_v2 異常0、shizue --artifact PASS、keyboard は基準と同じ段で失敗（pull起因）。
  tools/chat_kanji/verify.py --source-only は「GuiKeyboard.cpp等がchat_kanji基準から不変」を要求する歴史的ゲートのため改名で失敗（意図した変更。緩めない）。
- [x] 検証器追従・ビルド・成果物検証
  - 新規 tools/patches/verify_menu_port.py（menu_host.cpp＋menu_oracle.js）: C++実ソースをホストでビルドしSimulatorのCheatMenuModelと全状態を毎コマンド比較。シナリオ3309＋乱数6系列約38000コマンド一致、必須21状態到達、意図的バグ5種を全検出（work/cache/menu_port_qa/mutation.py）。描画捕獲で上下の文字スロット・矩形・字形も検査。
  - verify_plugin_port_v2 異常0（寸法時間28/色41/文字列133、上スロット最大85<=93、記録 上95%/下84%）、verify_keyboard_port PASS（数値キーボードの退場アニメにoracle追従）、verify_shizue_hook_registers --artifact PASS。
  - リンク済みELF監査（tools/chat_kanji/artifact.py audit を work/evidence/simport へ出力先変更して実行）PASS: 命令9567/直接分岐1685/PCリテラル759。prebuild.json は検証通過時の全ソースSHA。
  - clean build gohan.3gx 874750B SHA 7ad9fc3d1fccabdf7c61c02b4e32a01fcfa105dc10c3ff975ee2ddef2b402c89 / elf 8740e3f5…。実機適用はしていない。
  - 仕様反映: gohan-menu.md §5.5注記・§6(API)・§7.1注記・§8(実装項目)・§9-1、README のフォルダ説明。
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
