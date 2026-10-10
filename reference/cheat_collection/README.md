# 参照チートのCTRPFソース集（作成中）

入力: project_v2/reference/old_project/REFERENCES/CHEATS と CTRPF PROJECTS。
元の全件抽出/重複整理: IDA-gpt-6.1-sol-F018 / gpt-6.1-sol。
追加のネイティブ関数収録: gpt-6。

現在はAR由来2367関数とC++由来107関数を収録し、
原候補3185件に対応。未処理1125グループを
pending_native_and_templates.jsonに理由付きで残す。全件収録は継続中。

## 分類と確認待ち

- ItemsInventory: アイテム・持ち物・収納・金額。
- PlayerMovement: プレイヤー・移動・動作。
- WorldTown: 村・島・地形・住民。
- InteriorCamera: 室内・家具・内装・カメラ。
- InterfaceCommunication: 表示・会話・通信。
- System: その他のシステム処理。
- Review / NativeReview: 名称・挙動・版を選び切れない候補。

## 読み方

各関数のコメントに名称、出典、未確定事項と元の登録条件を記載する。
provenance_ar.json / provenance_native.jsonで原本のhashと別名を追跡できる。
状態・補助処理は関数内へ置く。C++由来の今回の収録分は依存閉包が空の
callback本体、または依存する固定幅整数の型別名も関数内へ移した本体で、
原処理のtokenが一致し、ARM C++11構文検査を通っている。
main.cpp、メニュー登録、Makefileは含めず、gohanのビルド対象外に置く。

ホットキーを参照する関数は、コメントの元登録条件に従うMenuEntryが必要。
ライフサイクル、取消や復元は原関数に従う。コードの構文確認は実機動作や
地域/更新版の一致を証明しない。原アドレスは出典の値を保持している。
同名の違う処理や名称を選び切れないものはReviewへ収録する。

第三者端末のクラッシュ・操作強制・データ改変・チャットなりすまし専用は
restricted_metadata.jsonへ名称と出典のみを収録する。C++側の対象を
確定できない同種の候補も実行コードへの変換を保留して理由を残す。

## 来歴とライセンス

ARの命令仕様はCTRPluginFrameworkのARCode/Loader/Handlerを参照した。
LICENSE.CTRPluginFramework.txtに全文を保持する。原チートの作成者名や
出典注記をソースコメントとprovenanceへ残す。C++プロジェクトごとの
ライセンス所在はnative_attribution.jsonに記録し、見つからない場合に
利用条件を推定しない。
