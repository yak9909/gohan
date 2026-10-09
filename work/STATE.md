# 現在地 — 室内の機能による家具リスト判定（2026-10-09）

- IDA-gpt-6.1-sol-F013、作業枝work/decor-room-capabilities（基点3e9654f）。壁配置は実マップ、壁紙床は所有する保存部屋＋稼働中の張替え対象モデルと材質で判定。外観のテント判定を撤去。
- PC31条件/リンクARM19条件/必須しずえフック/リンク全分岐等PASS。3gx SHA8dce114e6f44fdab2b17b2b1836a1774b2ab64976a0f573ca3a524fe0c13748e、SD資源の変更不要。実機未確認、mainへ未反映。
- 詳細work/SESSION.md、解析側project_v2/work/evidence/t022_room_capabilities。次: 改造外観の室内で壁配置・壁紙床の実機結果を確認。

# （履歴）gohan.md の詰め（2026-09-17）

- main = 実機確認済みの Simulator 移植版（3389f71）。
- 作業ブランチ work/patchlist-spec（仕様書のみ）: PatchList 表（壁抜けは実機確認済み3語）、Trampler・アイテムが消えない・キーボード制限解除の PatchList 候補、§17 に PatchList 以外 8 件の設計。値は JPN code.bin と照合済み、効果は未解析（R2）。
- 次: 利用者と設計を詰める（天気の選択肢、3x3 の置き場、キーボード制限の OFF 値など）。
