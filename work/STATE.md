# 現在地 — `work/coord-sync-map-editor`（2026-10-04、gpt-6-sol）

- 利用者の2台（NEW 3DS LL/OLD 3DS）は屋外通信中に同じ `Garden=0x31F49A00 Town=0x31F49A80 Player=0x33099E3C XYZ=0x33099E50 room=0 slot=0` を実測。下位`0x0C`による自己XYZ→相手XYZ同期チートを継続検討する。
- 前の`work/coord-sync-probe`はmain由来のためマップエディターが消えた。`work/map-editor`（257ea1b）から新ブランチを作成し、診断項目を移植。マップエディターのメニュー登録を保持し、3gxビルド・マップエディター検査・しずえフック検査PASS。診断にはNet/M/F/Tを追加。旧メニュー検査は基底の「薄」の欠字で停止し、本変更とは無関係。実機適用はまだない。送信機能も未追加。次の1手: 診断付き3gxを作業ブランチへpushし、両端末のNet/M/F/Tを測る。

# 以前の現在地 — gohan.md の詰め（2026-09-17）

- main = 実機確認済みの Simulator 移植版（3389f71）。
- 作業ブランチ work/patchlist-spec（仕様書のみ）: PatchList 表（壁抜けは実機確認済み3語）、Trampler・アイテムが消えない・キーボード制限解除の PatchList 候補、§17 に PatchList 以外 8 件の設計。値は JPN code.bin と照合済み、効果は未解析（R2）。
- 次: 利用者と設計を詰める（天気の選択肢、3x3 の置き場、キーボード制限の OFF 値など）。
