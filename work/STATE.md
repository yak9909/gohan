# 現在地 — Simulator 移植とソース整理（2026-09-17）

利用者指示: gohan-gui-simulator（pull 済み 0cabaed）と gohan-menu.md の仕様・見た目を gohan.3gx へ全移植し、GuiV2 等の版表記ファイル名をやめて標準的な範囲でフォルダ分け。

- ブランチ `work/simulator-port`（work/spec-md から）。**実機未確認なので main へ入れない。**
- 完了: フォルダ分け（Gui/Fonts/ChatKanji/Cheats/Debug）、GuiV2→GuiRenderer、GuiMenu を Internal/Model/Draw/Items/本体へ分割し Simulator の全機能を移植、検証器追従、clean build、PC 検証一式 PASS（詳細 SESSION）。
- 成果物: gohan.3gx 874750B SHA 7ad9fc3d…（src/gohan/ のビルド出力。Git 管理外）。実機で確認済みの 3gx は project_v2/artifacts/plugins/chat_kanji/（201af7e9…）。
- Simulator に合わせて変えた既存挙動: 通知の題（SELECTED/ACTION/CHEAT ENABLED・DISABLED）、ホットキー反転時のエンジン通知なし、ホットキー入力で A/B/X も記録（無効/取消はタッチ）。
- 次: 利用者の実機確認。確認結果と成果物 SHA を記録してから main へ反映を判断する。
