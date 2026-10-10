# 現在地 — チップ長押し窓とアニメ修正（2026-10-10）

- IDA-gpt-6.1-sol-F017/T004、work/chip-longpress-fix（基点21172d6）。F016実機で長押し窓が出ず縞アニメが震えるため修正。CRO static recordをheap判定からread permission判定へ、Select counterはゼロ再開始から上限5へ変更。原ARM付き旧版再現/新版7条件/ABI/SP/VFP/build PASS。新版は未実機、mainへ未反映。詳細と次の保存/配布はSESSION/解析側chip_longpress_fix証拠。

## F016の履歴

- IDA-gpt-6.1-sol-F016/T003、work/interior-presets（基点48b734a）。利用者のF015実機結果はチップだけ隠れ本体UIが残るため訂正。list中カメラ/3倍速度/長押し複製/内装保存・読込・保存枠との交換を追加中。新F016は未実機、mainへ入れない。
- UI dirty再記録、native選択窓、8枠保存/読み込み/交換、サイズ違い確認/clip、frame即反映/復元、SD取引を実装。build3/仕様更新、配置file/カメラ/native cache/内装と窓/backend11条件/Storage21失敗/GUI確認3条件PASS。catalog OFFでも複製を使える共有backendの実linked検査PASS。新字形663/全53source欠字0、225関数の全分岐/リテラル/ABI/領域/hash/しずえPASS。
- 保存/配布: code9d606ddb05c41d3da2b22384925db9335d466163をorigin work/interior-presetsへpush済み、remote独立照合一致。解析側artifacts/plugins/interior_presets1、3gx SHAbf0fc36378c78ce6e2fa04c7cf7e5d5bf6e56abc537503d710ef185610348222、ELF SHA648b70d9d9023c6f431d320866cba4612a887e328caa0cdd25e685298ec87094。配布/hash/保存DB/XML読戻しPASS。新SD素材なし、実機操作なし・未確認/main未反映。次: 利用者実機確認。根拠project_v2/work/evidence/interior_presets/report.md。

## 前回F015の履歴

- IDA-gpt-6.1-sol-F015 / T002、work/catalog-camera-controls、基点15d7aeb1。前回カメラ/壁判定版は利用者実機PASS。新追加は未確認、mainへ未反映。
- リスト背後の開くタブ/InteriorEditorを6フレームfade、Xで最後のリスト（初回家具）、B既存保持、家具/壁家具のカテゴリ音BOOK_ICON_SELECTED。HHDカメラON＋実模様替えUI中だけスライドパッドで向きに沿ったX/Z移動。通常のプレイヤー操作/Yを保持、list/menu中停止。
- 原ARM Eye対C++/linked pan63条件、旧HHD493frame/20条件、fade24条件、X/B/音7条件、FrameStep fade往復/actualEditor8条件、170関数の分岐/リテラル/ABI/領域/hash、必須しずえ/新字形 PASS。指定build終了0。
- コードcommit9792469cbca7d1fde4e74e6f0ad1d51b829a112aは作業枝へpush済み。配布は解析側artifacts/plugins/catalog_camera_controls1、3gx SHA e87dbe26bc32066fa917a3913a72512557c8aa36bb8518718b705ab1cd4764d5。メモ追補はコードを変更しない。次: 利用者が今回の追加分を実機確認。根拠は解析側work/evidence/catalog_camera_controls、設計docs/topics/catalog_camera_controls_design.md。実機への適用/Resumeなし。

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
