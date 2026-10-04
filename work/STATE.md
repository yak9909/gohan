# 現在地 — `work/coord-sync-action-font`（2026-10-04、gpt-6-sol）

- 利用者は旧送信版で、ホスト/ゲスト両方向の座標同期とマップエディター動作を実機確認。旧報告の「どちらかが歩行/アクション中」を訂正し、**送信者本人が歩行/アクション中だけ送信がキャンセルされる**、相手の状態には依存しない。利用者が試した3gxのSHAは不明。新しいマスク省略フォールバックとメニュー字形同期は本作業枝に隔離し、実機未確認。接続・転送枠の拒否は維持する。
- フォント生成後656美咲字形、欠字0。menu/port/map-editor/Shizue検査PASS、3gxビルドPASS。新3gx SHA-256 `B68FFD48F25B8D0A74C0DDD05615FD88FD50794DD6EE8CAEDD71D9C4D57825B5`。次の1手: 成果物を親artifactsへSHA付き保存して作業枝へcommit/push、利用者に送信者が動作中の`Gate`/`Busy`と実際の座標結果を試してもらう。

# 以前の現在地 — `work/coord-sync-send`（2026-10-04、gpt-6-sol）

- マップエディター復旧とNet/M/F/T読取り診断は `work/coord-sync-map-editor` commit `a990946`、origin push済み。3gxは親 `artifacts/plugins/coord_sync_map_editor/`、SHA-256 `99A0D0A3BF5F81BA6915EAEAC819D891BF2C59C40ED692C33D541E49455E2A24`。この版は送信なし。
- 現在の作業枝に検証用 `root/テスト/相手へ座標を1回送る` を追加し、commit `b500514` をoriginへpush済み。今回の2台のTown/XYZ、屋外・接続・相手1人・T=1を検査し、下位種別12の16B本文を汎用送信APIへ1回渡す。ビルド・マップエディター・しずえフック検査PASS、送信関数呼出しの9引数ABI・literal・branchを逆アセンブルで照合済み。3gx SHA-256 `284E4CC0752A80658E458EA0A94E4929B5F693C47C06FC001F36184544AB203F`。実機にはまだ適用していない。受信完了副作用とメニュースレッドからのscratch競合は未検証。Net/M/F/Tの実測前に送信の動作を断定しない。次の1手: 両端末のNet/M/F/T取得。

# 以前の現在地 — `work/coord-sync-map-editor`（2026-10-04、gpt-6-sol）

- 利用者の2台（NEW 3DS LL/OLD 3DS）は屋外通信中に同じ `Garden=0x31F49A00 Town=0x31F49A80 Player=0x33099E3C XYZ=0x33099E50 room=0 slot=0` を実測。下位`0x0C`による自己XYZ→相手XYZ同期チートを継続検討する。
- 前の`work/coord-sync-probe`はmain由来のためマップエディターが消えた。`work/map-editor`（257ea1b）から新ブランチを作成し、診断項目を移植。マップエディターのメニュー登録を保持し、3gxビルド・マップエディター検査・しずえフック検査PASS。診断にはNet/M/F/Tを追加。旧メニュー検査は基底の「薄」の欠字で停止し、本変更とは無関係。実機適用はまだない。送信機能も未追加。次の1手: 診断付き3gxを作業ブランチへpushし、両端末のNet/M/F/Tを測る。

# 以前の現在地 — gohan.md の詰め（2026-09-17）

- main = 実機確認済みの Simulator 移植版（3389f71）。
- 作業ブランチ work/patchlist-spec（仕様書のみ）: PatchList 表（壁抜けは実機確認済み3語）、Trampler・アイテムが消えない・キーボード制限解除の PatchList 候補、§17 に PatchList 以外 8 件の設計。値は JPN code.bin と照合済み、効果は未解析（R2）。
- 次: 利用者と設計を詰める（天気の選択肢、3x3 の置き場、キーボード制限の OFF 値など）。
