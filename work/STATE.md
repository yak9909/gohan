# 現在地 — リスト操作と模様替えカメラ移動（2026-10-10）

- IDA-gpt-6.1-sol-F015 / T002、work/catalog-camera-controls、基点15d7aeb1。前回カメラ/壁判定版は利用者実機PASS。新追加は未確認、mainへ未反映。
- リスト背後の開くタブ/InteriorEditorを6フレームfade、Xで最後のリスト（初回家具）、B既存保持、家具/壁家具のカテゴリ音BOOK_ICON_SELECTED。HHDカメラON＋実模様替えUI中だけスライドパッドで向きに沿ったX/Z移動。通常のプレイヤー操作/Yを保持、list/menu中停止。
- 原ARM Eye対C++/linked pan63条件、旧HHD493frame/20条件、fade24条件（全pane bytes復元）、X/B/音7条件、170関数の分岐/リテラル/ABI/領域/hash、必須しずえ、新説明文の欠字0 PASS。指定build終了0。次: 作業枝push/配布。根拠は解析側work/evidence/catalog_camera_controls、設計docs/topics/catalog_camera_controls_design.md。実機への適用/Resumeなし。

## 前回 — HHD式室内カメラ（利用者実機PASS）

- IDA-gpt-6.1-sol-F014 / T001、work/hhd-room-camera（基点be72724）。調査→設計→実装。チェック項目root/ゲーム/「家のカメラをHHD式に」、家の左右連続回転/上下連続奥行き、普通室内mode0/模様替えmode25。F013の外観に依存しない壁家具/壁紙床修正も含む。
- HHD原ARM493 frame対C++全bytes一致、リンク20条件/屋外共存/ABI等PASS。2026-10-10提示hhd_room_camera1を利用者実機PASS（端末SHA/個別操作一覧は未再測定）。新F015へ確認を一般化しない。
- 詳細work/SESSION.md、解析側work/evidence/hhd_room_camera/とdocs/topics/hhd_room_camera_design.md。work/hhd-room-camera 15d7aeb1はpush済み。mainへの反映は行っていない。

# （履歴）室内の機能による家具リスト判定（2026-10-09）

- IDA-gpt-6.1-sol-F013、作業枝work/decor-room-capabilities（基点3e9654f）。壁配置は実マップ、壁紙床は所有する保存部屋＋稼働中の張替え対象モデルと材質で判定。外観のテント判定を撤去。
- PC31条件/リンクARM19条件/必須しずえフック/リンク全分岐等PASS。3gx SHA8dce114e6f44fdab2b17b2b1836a1774b2ab64976a0f573ca3a524fe0c13748e、SD資源の変更不要。実機未確認、mainへ未反映。
- 詳細work/SESSION.md、解析側project_v2/work/evidence/t022_room_capabilities。次: 改造外観の室内で壁配置・壁紙床の実機結果を確認。

# （履歴）gohan.md の詰め（2026-09-17）

- main = 実機確認済みの Simulator 移植版（3389f71）。
- 作業ブランチ work/patchlist-spec（仕様書のみ）: PatchList 表（壁抜けは実機確認済み3語）、Trampler・アイテムが消えない・キーボード制限解除の PatchList 候補、§17 に PatchList 以外 8 件の設計。値は JPN code.bin と照合済み、効果は未解析（R2）。
- 次: 利用者と設計を詰める（天気の選択肢、3x3 の置き場、キーボード制限の OFF 値など）。
