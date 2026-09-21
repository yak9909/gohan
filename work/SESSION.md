# UnitCursor 屋外表示の実機確認（2026-09-18 開始・最新）
利用者指示: 実機確認を行う。IDAで実機にアタッチ済み・Chokistream起動済み。3gxの方がやりやすければそちらでも可。**コードを設計・実装したら実機適用前に必ず逆アセンブルして検証する**。
確認済みの現在状態（実機、読み取りのみ）: プロセスは停止中 state=-1 / PC=0x137064 / SP=0x946C70 / LR=0x137178 / BP 0件。
  実機ビルド＝artifacts/update/exefs/code.bin を 8 点（0x1B7090・0x1B71C4・0x317920・0x48D26C・0x4EB840・0x4EBC14・0x4F048C・0x569BA0、各32B）で照合し全一致。VA=offset+0x100000。
  ExHeader（artifacts/update/exheader.bin）: .text [0x00100000,0x00838018) page_end 0x00839000 / .rodata [0x00839000,0x0093EF94) / .data [0x0093F000,0x0097C768) / .bss サイズ0x175F2C → [0x0097C768,0x00AF2694)。
  **空きRX = [0x00838018,0x00839000) の 0xFE8 B。実機でも全ゼロ＝gohan 未ロード・他のケーブ無し。**
  ロード中CRO（vc_CRODATA 0x94B698 → 0x3225B6A8）: Outdoor/Village/RealVillage/Summer/EventNpc/FsInsAward/Kappei。**Indoor と Ftr は無い＝屋外（村・夏）**。V2-F019/F020 の前提が実機で裏付いた。
  フック候補 0x001B721C: Frame_DrawPass1024 の `BL BaseObj_RunDrawPhase`(0x1B7218) 直後の **NOP スロット**。0x1B71C4〜0x1B72D4 を capstone で逆アセンブルし、全 BL の前後に NOP 対がある素の並びで code.bin と完全一致することを確認済み（＝他者のパッチではない）。復元値は 0xE320F000。
方針: **コードケーブは最後の1本だけ**。それまでは BP で止めた地点からレジスタ注入でゲーム関数を直接呼ぶ（書き込み可能な領域を新たに探さずに済む）。
  段取り: (1) 0x001B721C に BP → ゲームスレッドで停止 (2) sub_4B8CF0(size,align) 0x4B8CF0 で nw::lyt ヒープから作業領域を借りる（off_96F150 の vtable+8。返却は ssys_ma_Allocator_Free 0x569B44。旧F-296で実機往復確認済み）
  (3) Heap_CreateNamed_0 0x31785C と G3dResHolder_Ctor 0x4ED860（264B） (4) G3dResHolder_RequestLoad 0x317920 を完了まで毎フレーム → G3dResHolder_Setup 0x317958
  (5) G3dResource_FindModelByName 0x4A5F20 (6) ModelInstance_Create 0x4F048C (0x834,1,3) (7) SetMatrix3x4 0x4ED794 → UpdateWorldAndSkeleton 0x4ECFEC
  (8) 最後に **ストアを持たない小さなケーブ**（リテラルのみ）を .text 末尾へ置き、NOP スロットから BL して毎フレーム Scene_SubmitNode(holder,0) 0x4ED630。
  旧プロジェクトの番地は**同一ビルド**（主IDBが旧 code.bin.i64 の複製で、上記 11 関数すべて期待どおりの名前に解決）。ただし個別に照合してから使う。
未実施: 実機への書き込みはまだ 1 バイトも行っていない。BP 設置も未実施。
次の1手: BP 0x001B721C を置き、continue→停止を確認してから段(2)。
進捗（実機、2026-09-18）:
  - BP 0x001B721C を設置 → continue → **ヒット確認**（PC=0x1B721C, SP=0x0FFFFE20, LR=0x520008）。ida_dbg.wait_for_next_event が py_eval 内で機能することも確認＝呼び出し列を1本のスクリプトで回せる。
  - レジスタは R0-R12/SP/LR/PC/PSR が読める。**PSR=0x60000010（User モード・T=0＝ARM）**。S0/D0 は IDA 側から読めない。
  - **浮動小数点引数は VFP ではなくスタック渡し**だった（0x611A00 の `STMEA SP,{R0,R1,R6}` と 0x6119F8 の STR で [SP+0]=1(a5)・[SP+4]=0(a6 の float)。VFP を触る必要は無い）。
    Heap_CreateNamed_0 の実引数: R0=ScStage+0x418(HeapAllocator), R1=0x5000, R2=dword_94CC48(親ヒープ), R3=&SafeString{vtbl_sead_SafeStringBase_char, "UnitCursorRes"}, [SP+0]=1, [SP+4]=0。
    G3dResHolder_RequestLoad: R0=ScStage+0x420(holder), R1=&SafeString{vtbl,"Ftr/Chip/UnitCursor.bcres"}, R2=[ScStage+0x41C](作ったヒープ), R3=0x80。ScStage+0x416 が「読み込み済み」旗。
  - **方針変更（改善）**: 自前で 264B の holder やヒープを確保せず、**ScStage 自身の +0x418 / +0x41C / +0x420 を使う**。これらは部屋に関係なく ScStage の ctor で初期化され、屋外では誰も触らない遊休領域。ゲームが屋内でやることをそのまま屋外で行う形になり、書き込み可能領域の新規確保が不要になる。
  - ScStage の実体アドレスを取るため BP を 0x611928(ScStage_LoadIndoorRes、屋外でも PrepareStep から呼ばれるが中で bit8 判定して抜ける) へ移した（0x1B721C の BP は規則どおり削除済み）。
  - **未解決（成否不明ではなく、明確に保留）**: continue 後 0x611928 は 6 秒以内にヒットせず、以後 MCP がタイムアウトで塞がっている。
    ＝ **ScStage_PrepareStep は屋外の通常フレームでは走らない**（場面準備のときだけ）。対象は走行中で、こちらから止める手段は無い（旧CLAUDE.mdの既知事項）。
    → 利用者に IDA で Suspend してもらう必要がある。Suspend 後の最初の1手: 0x611928 の BP を削除し、ScStage の実体は別手段（vtable 0x8F24C8 を含む vtbl_ScStage を持つオブジェクトの探索、または 0x94FDC4 のクラス表）で求める。


# 残る未解明の解析と整合性確認（2026-09-18 開始・最新）
利用者指示(/goal): 実機でしか分からないもの以外の未解明を全て解析。AGENTS.md のルールで結果・前提の誤りと矛盾を確認。「描画順」は静的に確認できるはずなので調査。作業後に push。
対象: (1) 描画順（Scene_DrawLayers のレイヤー、UnitCursor のマテリアル設定、Zファイト・半透明の静的な判断材料）(2) アニメAPI 0x4EBC14/0x4F0300/0x4EDBD0/0x4EDC98/0x4EDC74 (3) 必要ヒープ量（sub_736158 の問い合わせモード）(4) 非同期読み込みのスレッド制約 (5) 既存記録との矛盾点（特に Frame_DrawPass1040 が「下画面」か「上画面右目」か、V2-F019〜F023 の前提）。
方針: 静的のみ。独立2経路で照合し、暗算しない。
結果（完了）: V2-F024。(1) 描画順は静的に確定 — Scene_DrawLayers 0x4EB840 の i=0..3 がレイヤーで、判定は material→(+8 ResMaterial)+32。romfs 2,577 マテリアルの分布 0:2160/1:401/2:4/3:12、不透明率は層0=85%・層1=7%・層2/3=0。m_UnitCursor はレイヤー1。同層内は submit 順で並べ替え無し。(2) 描画状態はコードに無く MTOB の PICA コマンド列（sub_48D26C 末尾が mesh+0x68/+0x6C を丸ごと転送）。UnitCursor は blend_mode=1・0x0101=0x01160000＝加算合成・アルファテスト無効・ステンシル無効・0x0107=0x41＝深度LESS（byte mask=1 で深度書き込みは不変）。→ 埋まる部分は隠れる／明るく光る／影は落ちない（番号2に積まないため）まで静的に確定。(3) 矛盾を1件訂正 — Frame_DrawPass1025 0x1B7090（0x401=下画面）は Render_DrawScene0/1 を呼ばない。1040 は上画面右目。設計書 §3.1 の「両方に出る」を「上画面の左右両目に出る」へ修正。(4) アニメAPI 5本の役割と呼ぶ順（0x4EBC14→0x4F0300→0x4EDC74→0x4EDBD0→0x4EDC98）。0x4EDC74 は VFP s0 渡し。アニメ3本は全部 MaterialAnimation で "MaterialAnim::build" と整合。(5) ヒープ量 3,064 B（木の 932/1664 を再現して検算）。(6) 非同期読み込みはロック付きキューでスレッドを選ばない。ツール: tools/models3d/dump_material_state.py 新規、tools/bcres_anim_dump.py のオフセット修正（+0x10 は対象グループ名で loop ではない。group=MaterialAnimation が読めることで自己検証）。IDB: コメント9件・保存・閉じた。記録: FINDINGS・FUNCTIONS・evidence/material_state.txt・outdoor_plan.md・設計書 §3.1/§6/§8/§10・STATE。XML→commit→push まで実施。

# UnitCursor 屋外表示の未解明部分（2026-09-18 開始・最新）
利用者指示: (1) commit は毎回続ける、**push は指示があるまでしない**（記憶更新済み）。(2) 未解明部分の解析を進める。
対象: 設計書 docs/topics/unit_cursor_outdoor.md §7 の 1.フック位置 2.資源ホルダの型 3.ModelInstance_Create の引数 4.マス→ワールド換算 5.アニメAPI 6.ヒープ量。方針: 静的のみ。
結果（完了）: V2-F023。解決 4 件 — ホルダ=HeapAllocator(+0x418,8B)＋G3D資源ホルダ(+0x420,264B、ctor 0x4ED860)、ModelInstance_Create の a5=マテリアルコピーマスク/a6=フラグ/a7=SceneNode+72（実装は (0x834,1,3) をそのまま）、マス→ワールドは 32*tile+16 と Field_GetGroundHeight、フック候補は BaseObj_RunDrawPhase のリスト（F-113 推奨）か Frame_DrawPass1024 の 0x1B7218 直後。残り: アニメAPI・ヒープ量・描画スレッドから読み込みを回してよいか・実機項目。IDB 6関数命名＋4コメント・保存・閉じた。設計書 §7/§8/§9 を更新。commit のみ（push は指示待ち）。

# シーン番号の解析と UnitCursor 屋外表示の設計書（2026-09-18 開始・最新）
利用者指示: V2-F021 の不足リスト 1 番（Scene_SubmitNode のシーン番号の意味）から解析。設計書が無ければ作る。
方針: 静的のみ。Render_InitGlobal 0x4E97CC の 3 本の SceneContext/Traverser、Scene_DrawLayers 0x4EB840 の呼び出し元 2 つ、Render_DrawSceneIndexed を読み、index と画面（上左/上右/下）の対応を確定。docs/topics に設計書を作る。
結果（完了）: V2-F022。シーン番号 0=通常描画（byte_94CA2C==0）、1=別モード（同==1、カタログ等のプレビューと推定 LOW）、2=影マップ。上画面パス 1024 と 1040 の両方が 2→0→1 の順で同じ SceneContext を使うので番号は画面ではない。屋外は 0 を使う（旧 F-111/F-112 の実機実例と一致）。設計書 docs/topics/unit_cursor_outdoor.md を新規作成（手順・状態機械・実行文脈・未解明7点・検証順）。IDB 4コメント・保存・閉じた。

# UnitCursor を屋外で出すための解析（2026-09-18 開始・最新）
利用者指示: UnitCursor 表示へ向けた解析調査と、現在足りないもの・次に解析すべきものの提示。
方針: 静的のみ。(1) 資源読み込みAPIの引数 (2) 資源ホルダ→モデル検索 (3) ModelInstance_Create の実引数（CRO の実例と V2-F007 の照合）(4) シーン登録 Scene_SubmitNode と屋外シーンの取得 (5) 毎フレーム更新をどこから回すか (6) アニメAPI。未解明点を一覧化して記録する。
結果（完了）: V2-F021。資源読み込みは非同期（FileRes_RequestLoadAsync、+232 状態、完了まで毎フレーム呼ぶ）＋Setup 必須と確定。モデル取得・インスタンス生成・行列設定・Scene_SubmitNode（実機確認済みAPI、最小実例 sub_1F9BC4 が index 0）まで道筋が揃い、屋外でも BsFtrMgr が動く分だけ見込みは高い。不足は (1)シーン番号の意味 (2)毎フレームのフック位置 (3)資源ホルダの型/サイズ (4)ModelInstance_Create の 0x834/1/3 (5)アニメAPI引数 (6)マス→ワールド換算 (7)実機の描画順・Zファイト・影。IDB 5関数命名＋4コメント・保存・閉じた。記録: evidence/unit_cursor/outdoor_plan.md、FINDINGS、FUNCTIONS、models3d.md。

# オートキャンプでの家具モデル表示（2026-09-18 開始・最新）
利用者指摘: 「資源が屋内でしか読まれない」としたが、オートキャンプでは屋外の判定を持ちながら家具モデルを表示している。これを調べる。
方針: 静的のみ。オートキャンプ関連の部屋IDと g_RoomFlagTable のフラグ、ModuleAutoCamp.cro が読む資源、家具モデルの読み込み経路（ScStage_LoadIndoorRes 以外）、UnitCursor.bcres が読まれるかを確認し、V2-F019 の結論を訂正する。
結果（完了）: V2-F020。指摘は正しい。g_RoomFurnitureCapacity 0x8835BC は屋外でも非0（村38・Downtown2・オートキャンプ場10・カー内部36・家1F48）で、BsFtrMgr_CreateResources がその数だけ家具枠を作る＝家具モデルは code.bin 側で屋外でも動く。ItemIndoor.bin は bit8|bit28(キャンプ場)|bit29(カー内部)で読み、bit28/29 では ScStage_LoadCampingCarInfo が NpcCampingCarInfo/*.bin を読む。UnitCursor.bcres は bit8 のみ、Room_SelectModules も bit8 のときだけ Indoor/Ftr を積む（Ftr 参照は1か所）。V2-F019 の表現を訂正。IDB 命名3件＋コメント3件・保存・閉じた。記録: evidence §6・FINDINGS・FUNCTIONS・models3d.md。

# UnitCursor の未確認部分を詰める（2026-09-18 開始・最新）
利用者指示: (1) 今後は結果報告のたびに XML 書き出し→commit→IDA ブランチへ push まで行う（恒久）。(2) V2-F019 の未確認部分を詰める。
対象: 部屋フラグ8=屋内の断定、枠+120 の種類とアニメ3本の対応、家具の大きさ 0x690B68 とフレーム/倍率の具体値、UnitCursorLine の用途。方針: 静的のみ（主IDB＋CRO ツール）。
結果（完了）: 部屋フラグ8=屋内を g_RoomFlagTable で確認（200中118件が室内のみ、RoomID.txt と照合）。UnitCursor.bcres はモデル1つ＋アニメ3本で、UnitCursorLine はノード名、UnitCursorRotate は 3F の形選択（家具の大きさ 0x690B68 = (v+2)>>2 の 0〜3 をフレームに固定、速度0）。明滅は UnitCursor/NG の 150F アニメ。カーソル追加・削除の呼び出し元は ARM/Thumb・ポインタ走査とも 0 件で未特定（LOW のまま）。IDB: g_RoomFlagTable 命名＋3か所コメント・保存・閉じた。ツール tools/bcres_anim_dump.py 追加。記録更新後に XML 書き出し→commit→IDA へ push（今後は毎回）。

# 模様替えモードの家具の下の範囲表示（2026-09-18 開始・最新）
利用者指示: 自分の家の模様替えモードで家具を掴んで動かしているとき家具の下に出る影／パーティクルのような範囲表示を、屋外でも好きに出したい。その表示の解析。思考は日本語で。
方針: 静的のみ（idalib 主IDB）。既存記録（BsMenuRoomMgr、模様替え、Ftr、影、パーティクル）を検索→描画しているクラス・リソース（romfs）・生成関数・座標の渡し方を特定。実装は依頼されていないので解析と記録まで。IDB・FINDINGS（V2-F019）・evidence へ。
結果（完了）: V2-F019。範囲表示＝Ftr/Chip/UnitCursor.bcres の 3D モデル。ScStage_LoadIndoorRes 0x611928 が部屋フラグ8のときだけ読み、表示は ModuleIndoor.cro の AcObjUnitCursor（64枠、アニメ3本 UnitCursor/NG/Rotate、位置は ModuleFtr の家具配列依存）。屋外は資源もクラスも無いので不可、道筋は models3d.md の自前モデル表示。IDB 3 関数命名＋コメント・保存・閉じた。ツール: tools/patches/cro_rtti.py 追加、cro_call_context.py の匿名インポートをモジュール別・セグメント別に修正。記録: evidence/unit_cursor/README.md、FINDINGS、FUNCTIONS、docs/topics/models3d.md。未 commit。

# 模様替えで家具を掴んだときの床の範囲表示（2026-09-18 開始・最新）
利用者指示: 自分の家の模様替えモードで家具を掴んで動かすとき、家具の下に出る影／パーティクルのような範囲表示を、屋外でも好きに出せるようにしたい。その解析。
方針: 静的のみ（idalib 主IDB）。既存記録（FINDINGS/reference の模様替え・BsMenuRoom・パーティクル・models3d）を先に検索。RTTI（BsMenuRoomMgr 等）と文字列（layout/particle/bcres 名）から表示物の正体（モデル/パーティクル/ポリゴン）と生成・更新関数を特定し、屋外で呼ぶための条件（部屋・資源のロード・所有者）を洗う。実機操作はしない。IDB・FINDINGS(V2-F019)・evidence へ。

# 手紙の ID 種別 3・4・6 の意味（2026-09-18 開始・最新）
利用者指示: 「種別3・4・6 の意味を調査してください」（V2-F017 の LOW 残件。PersonalID ブロック +0x30 / 手紙 +0x30・+0x64 の種別バイト）。
方針: 静的のみ（idalib 主IDB）。種別バイトを書く箇所（STRB #0x30/#0x64 と MOV #3/#4/#6、Thumb 含む）と読む箇所（1/3/6・2/4 判定の呼び出し元）を列挙し、どの経路でどの値になるかを確定。IDB・FINDINGS・evidence/mail_sender へ。
結果（完了）: V2-F018。3=名前差し込み無しのプレイヤー、4=同住民（テンプレートで名前位置が負のとき LetterId_DropNameTag）、5=IDなし、6=自分自身（未来の自分への手紙）、7=自由な名前（書く箇所未発見）。romfs SYS_2D_Mail の見出しで独立確認（tools/msbt_dump.py）。判定条件は変わらず。IDB 14 関数命名・コメント・保存・閉じた。記録: evidence/mail_sender 追記＋SYS_2D_Mail.txt・sender_kind_table.txt、FINDINGS、FUNCTIONS。未 commit。

# sub_6070CC / sub_606964 と「通信相手からの手紙」判定（2026-09-18 開始・最新）
利用者指示: sub_6070CC / sub_606964 の解析。郵便ポストで受け取る手紙に「通信したプレイヤーから送られたか」を判定できるかを調査。目的: 将来、通信プレイヤーからの手紙以外を届かないようにするチートを作りたい（実装はまだ依頼されていない）。
方針: 静的のみ（idalib 主IDB）。Mail_Deliver 0x2FF184 の行き先 sub_1BB1F8 / sub_1BB484、手紙構造体（差出人欄・種別）、通信で手紙を受け取る経路（BsMailSaveMgr 等）を確認し、判定に使える欄・フック候補を記録。IDB・FINDINGS（V2-F017）・evidence へ。
結果（完了）: V2-F017。sub_6070CC/sub_606964 はセーブ入出力中を返すデバッグ判定（戻り値未使用）。手紙 640B の差出人 PersonalID は +0x34、種別 +0x64（プレイヤー=1）、町ID は PersonalID+22。ゲストの手紙はパケット74でホストの預かりへそのまま入り Mail_DeliverFromPool → Mailbox_WriteLetter 0x6F4454。判定「差出人種別プレイヤー かつ 差出人の町≠自分の町」は可能、フック候補 0x6F4454（未来の手紙は 0x6F4264 が別）。IDB 22 関数命名・コメント・保存・閉じた。記録: evidence/mail_sender/README.md・FINDINGS・FUNCTIONS・growup.md 修正。未 commit。

# sub_5C9C24 の調査（2026-09-18 開始・最新）
利用者指示: 「sub_5C9C24 の調査をしてください」。V2-F015 で Structure_StampAll(1,1) を呼ぶ日替わり外の呼び出し元として残件だったもの。
方針: 静的のみ（idalib 主IDB）。呼び出し元（sub_6E205C←sub_6E2D04）を遡り、いつ走るか・中身を確定。IDB・FINDINGS・evidence growup.md へ記録。
結果（完了）: V2-F016。sub_5C9C24 = Dream_RebuildDownloadedTown。呼び出し元は DreamNet_ApplyDownloadedTown（受信 0x71880 B を TownBase へ複写）だけで、そこへ入る状態は ModuleDream.cro からしか設定されない＝夢見の館で町を落とした直後。日替わり・成長は呼ばない。IDB 7 関数命名・コメント・保存・閉じた。記録: FINDINGS/FUNCTIONS/growup.md 追記/gohan.md。cro_call_context.py に CRO_CONTEXT を追加。未 commit。

# 日替わり処理の残り（郵便・岩・木/竹/低木の成長・花の増殖/枯れ・埋没アイテム・竹の増殖・アイテム消えない）（2026-09-18 開始・最新）
利用者指示: 残りの解析（g_SvProcTypeTable 11 種別・PlSelect case 2〜12・sub_110CFC）を進めつつ、「郵便ポストのメールの更新」「岩の増殖」「木／竹／低木などの成長」「花の増殖・枯れ」「埋没アイテムの発生」「竹の増殖」を調査。利用者の推測: Vapecord「アイテム消えない」（無効な位置のアイテムが消えない）も日替わり処理に含まれる。
方針: 静的のみ。Field_GrowUp（0x104B54）の各コールバックをアイテムID（IDS一覧）と突き合わせて特定、Vapecord ItemsDontDissappear の 2 パッチ（0x6FA8A8/0x52734C）とフック先 0x2FC950 の位置づけを確認。判明事項は IDB・FINDINGS（V2-F015〜）・evidence へ。
結果（一区切り）: V2-F015。romfs Fg/Param/Fg.bin を定義表と確定（tools/fg_param_dump.py、実測7件一致）。成長処理の岩・竹・苗の成長/枯れ・花の枯れ/交配/自然発生・化石/落とし穴の埋設・キノコ等を特定。郵便は Mail_Deliver、準備中は日替わり後に Mail_SP_First/Housing/Gardening、日替わり内で季節NPC・住民の手紙。Vapecord アイテム消えない の 3 部品はすべて日替わり内の処理を止める（パッチ関数は日替わり外からも使用）。IDB 43 関数命名・コメント・保存・閉じた。記録: FINDINGS/FUNCTIONS/evidence growup.md/README 追記/gohan.md。
残り: g_SvProcTypeTable 11 種別、PlSelect case 2〜12、sub_110CFC、LOW 項目（byte_A0EAFC・雨フラグ・岩フラグ・フック5か所の対応）。acnl_disassemble 未 commit（XML 手順が必要）。

# タッチワープのホットキー・移動量の無効化・村/マップフォルダ（2026-09-18 開始・最新）
利用者指示: (1) タッチワープにホットキーが設定されていれば、押している間だけワープ可。(2) 移動方法がグリッド単位なら移動量を無効状態に。(3) 名称変更「選択したマップアイコンのroomに移動」→「選択中アイコンに部屋移動」、「選択したプレイヤーのデータに切り替える」→「選択中プレイヤーに切り替え」。(4) 選択中アイコンに部屋移動・選択した住民への操作・選択中プレイヤーに切り替え を root/村/マップ フォルダへ。(5) 選択中プレイヤーに切り替え は通信中でなく村にいる時に発動、トグル型アクション式（未解析なので仕様として記載、実装は空のまま）。
方針: 無効化は Behavior に「無効判定」コールバックを足し、メニューが毎フレーム評価（登録した項目だけ）。移動方法の編集値で判定。gohan.md（§1/§3/§9/§17.2）と gohan-menu.md §6 を更新。検証・クリーンビルド・監査の後 work/cheat-impl へ push（実機未確認）。
結果（完了・rev6 実機未確認）: GuiMenu::RegisterDisabled（Behavior.IsDisabled、Step の前後で評価）を追加し移動量を移動方法の編集値で無効化。タッチワープはホットキー設定時に押下中だけ。村/マップ フォルダに 3 項目（改名済み、選択中プレイヤーに切り替え はトグル型アクション・未設計のまま）。gohan.md §1/§3/§9.5/§17.2、gohan-menu.md §6(f)。検証（本物の木で移動方法=グリッドで移動量 disabled を確認）・クリーンビルド・監査 PASS。成果物 rev6 sha256 eb6a9fa4…。

# 記録の commit と実機確認の反映（2026-09-17 最新）
利用者指示: 「これらの記録をコミットして」。利用者の実機確認: しずえスキップでは少なくとも寝癖・郵便ポストのメールも一緒にスキップされる。ほかの日替わり処理も同様と思われるが断定せず確度は高めに。
方針: FINDINGS V2-F013/F014・evidence・gohan.md・IDB コメントへ反映→主IDBの XML を書き出し検証→acnl_disassemble（ブランチ IDA）へ commit（push はしない）。loose IDB ファイル・pycache・work/cache は含めない。
記録済み: V2-F013 を HIGH、V2-F014 に実機傍証、evidence・gohan.md・IDB（0x20C688 / 0x114D18 コメント追記・保存）へ反映。
完了: XML 書き出し（PASS、関数 44192・シンボル 32117、新しい名前とコメントの反映を確認）→ acnl_disassemble ブランチ IDA に commit 9831e3d8（push していない）。含めなかったもの: IDB の loose ファイル（.id0/.id1/.nam/.til）、__pycache__、work/cache の試験出力。

# 準備中の残件＋掲示板/雑草/引っ越しの所在調査（2026-09-17 開始・最新）
利用者指示: 「残っているもの（sub_30C9C4 各段・g_SvProcTypeTable 11 種別・PlSelect case 2〜12）の解析を進めつつ、『掲示板の更新』『雑草が生える』『住民の引っ越し関連』を調べて。これらも準備中にある気がする」。
方針: 静的のみ（idalib 主IDB）。日替わり処理（SvProcType4_Prepare 以下）と sub_30C9C4 以下の呼び出し木を洗い、雑草のアイテムID・掲示板/引っ越しの文字列やRTTIから入口を特定し、準備中の経路に含まれるかを判定。判明事項は IDB と work/FINDINGS（V2-F014〜）、evidence へ記録。
結果（一区切り）: V2-F014。掲示板（Bbs_OnDayChangePworks/Events→Bbs_AddPost）・雑草（Field_GrowUp→Field_GrowUpSpawnWeed、0x7C〜0x7F）・住民の引っ越し（Villager_OnDayChange→Villager_DayChangeMoveInOut）はすべて準備中の日替わり処理の中。旧 sub_30C9C4 は Villager_DailyUpdateOnce（日替わり後段から先に走る）。IDB 22 関数命名・保存・閉じた。記録: FINDINGS/FUNCTIONS/evidence prepare_screen/README.md/gohan.md（しずえスキップ注記）。
残り: g_SvProcTypeTable 11 種別の使い手、PlSelect case 2〜12、sub_110CFC の抽選の正体。acnl_disassemble 未 commit（XML 手順が必要）。

# しずえ画面「準備中」未解明部分の解析（2026-09-17 開始・最新）
利用者指示: 「しずえ画面の『準備中』未解明部分の解析・調査を進めて」。利用者の推測: 「寝癖付かない」（0x0020C6D8 STRB R1,[R4,#4] を NOP）はこの準備中部分を書き換えている。
既存の到達点（旧 F-361/F-362、reference/old_project/HANDOFF_MESSAGE.md）: 要求元 ModulePlSelect.cro case 0、ワーカー this+632（sub_27EB34 が組む。callback sub_5254C0 等＝受信ボックス取り込みと推定 MEDIUM）、一括処理 sub_30C994→sub_30C9C4（boot 1 回、中身未解析: sub_10D980/110CFC/10FF58/10DBA0/10CCB8/110668）、case 2〜12 未読。
方針: 静的のみ（idalib 主IDB）。寝癖パッチの関数と sub_30C9C4 系からの到達を先に確認→一括処理の中身→ワーカーの callback の確定。判明事項は IDB（命名/コメント）と work/FINDINGS（V2-F013〜）に記録。
結果（一区切り）: V2-F013。準備中の仕事＝SvProc Thread 上の種別4 SvProcType4_Prepare→日替わり処理→Player_ApplyAbsenceBedHead（寝癖 0x20C6D8）。利用者の推測を静的に裏付け。旧「this+632 を叩く」を訂正（叩くのは this+0x168 TalkSaveTask_Step）。IDB 13 関数＋表を命名・保存・閉じた。記録: work/FINDINGS.md・FUNCTIONS.md・work/evidence/prepare_screen/README.md・gohan.md（寝癖付かない・しずえスキップ注記）。
残り: sub_30C9C4 各段の中身、g_SvProcTypeTable 11 種別の使い手、PlSelect case 2〜12、しずえスキップで日替わりが補われるか（要実機）。acnl_disassemble 側は未 commit（XML 手順が必要）。

# 店の営業時間判定 3 関数の正体調査＋記録（2026-09-17 開始・最新）
利用者指示: 「判明したことは記録するように。IDBも含めて」「調査中に見つかった三つの判定（sub_3093F8 / sub_7123E8 / sub_716860）がなんの判定なのか調査」。
方針: 静的のみ。(1) 営業時間判定の関数を Unicorn で時刻×条例ごとに実行し表にする（逆コンパイルと独立な裏付け）、(2) CRO の呼び出し箇所の文脈で用途を特定、(3) work/FINDINGS.md に V2-F012 として記録、IDB へ HIGH/MEDIUM は命名・LOW はコメント（idalib で保存）、acnl_disassemble の XML 手順は commit 時に。
結果（完了）: V2-F012 として work/FINDINGS.md・work/FUNCTIONS.md・work/evidence/shop_open/README.md（時間帯表と 3 判定）に記録。主IDBへ ShopHours_* 13 関数を改名、sub_112104/sub_1120B8/sub_69D848/0x7103A0 にコメントし保存して閉じた。
- 0x3093F8 = Re-Tail の営業時間だけ（住民の外出先 5 の可否 sub_112104 と ModuleShop の条件分岐）。0x7123E8 = エイブル判定と挙動同一（住民の外出先 21 の可否 sub_1120B8 のみ、存在理由 LOW）。0x716860 = つねきちの店の営業時間だけ（ModuleIndoor、用途 LOW）。
- 新ツール: tools/patches/shop_hours.py（Unicorn）/ cro_call_context.py / shop_open_callers.py。acnl_disassemble 側（tools・work・IDB）は未 commit。commit 時は XML 書き出し（docs/xml_exchange.md）が必要。

# 店24時間オープンのパッチ数削減の静的調査（2026-09-17 開始・最新）
利用者指示: 「店24時間オープンについて、もっと少ないパッチ数で実現できるのでは。静的に解析して調査」。現行は Vapecord ShopsAlwaysOpen の 9 語（MOV R0,#0→#1）。
方針: 実機・書き込みなし。主IDB ida/db/acnl_jpn_v15_annotated.i64（idalib）と code.bin の逆アセンブルで、9 か所の関数・呼び出し元・共通の判定関数を調べる。結果は work/evidence/shop_open/ と gohan.md に記録予定。
結果（完了）: 減らせない。判定は関数ごとに複製（共通の入口なし）、9 語は 9 関数の閉店出口で関数ごと 1 語が下限。Vapecord が触らない同種の判定（ModuleShop の sub_3093F8、ModuleIndoor の sub_716860、看板/BGM 番号の選択など）を発見。別案は Date_GetRaw の LR 絞りフック（未設計）。根拠 work/evidence/shop_open/、gohan.md の店24時間オープン節に注記。IDB は保存せず閉じた。

# gohan.md の内容でメニューを実装（2026-09-17 開始・最新）
利用者指示: 「一旦これで実装」。未設計チートはラベル末尾に（未設計）を付け、空のチートとして実装。既存のテストチート項目群は全て削除。
方針（決定・2026-09-17）: ブランチ work/cheat-impl（work/patchlist-spec fa29186 から）。
- 実装: PatchList 10 件（壁抜け/花散らせない/穴に落下しない/寝癖付かない/歩いた場所…/どこでも掘れる/空を見上げない/店24時間/メッセージ即表示/フレームレート制限解除）、天気（連動リスト 0x0062E728）、座標移動＋子設定3、タッチワープ、木（ケーブ）、3x3（ケーブ3本）、しずえスキップ（既存）、漢字変換（既存のチャット漢字候補アクションを流用）、ドロップアイテム（値だけ）。
- 空項目（（未設計））: チャットコマンド、アクション解除・気絶（設計にパケット欄が未解析と明記）、選択したプレイヤー…、PlayerResources 5（ITEM_VALUE）、スタイルを変更、ポケットアイテム、アイテムセレクター、アイテムが消えない、選択したマップアイコン…、選択した住民…、公共事業エディター、キーボード制限解除。
- 木/3x3 のケーブは BL の届く .text 末尾 0x00838400〜（空き 0x00838310-0x00838950。GuiCaves の台帳と重ねない）。OFF はフックを戻すだけでケーブ語は残す（ゲームスレッドが実行中の可能性があるため消さない）。
- 字形不足 120 字 > 空き 33 マス → UI アトラスを 256x256 に拡張、借用 0x9000→0x11000。不足字は GuiMenuItems.cpp の文字列から自動抽出して BDF から追加。
- 既定ホットキー §13 は初期値として焼く。
- 差分試験の見本の木は tools/patches/menu_sample_items.cpp へ移す（ホストだけが使う）。

結果（2026-09-17）: 実装・検証・ビルド完了。実機未確認。
- 検証: verify_plugin_port_v2 / verify_keyboard_port / verify_menu_port（見本の木の差分＋本物の木 48 項目の描画予算）/ verify_start_tap / verify_shizue_hook_registers --artifact / 成果物監査（work/evidence/cheatimpl/artifact.json）すべて PASS。PatchList 34 語は gohan.md の表と code.bin の元命令に一致。ケーブ表は ELF 内で一致。
- 追加の変更: 説明文の折り返しを字数（スロット最大 28 字）でも行う（GuiMenuDraw Wrap）。WireBehaviors は ResetState の後（ResetState が ToggleHandlers を消すため）。
- 成果物: artifacts/plugins/cheat_impl/gohan.3gx（922844 B, sha256 d774e473…）。ログ work/logs/cheatimpl/。
- 実機で見るべき点: 256x256 アトラス＋借用 0x11000（手紙を開いて落ちないか）、各 PatchList、座標移動/タッチワープ、木/3x3 ケーブ（.text 末尾 0x00838400-0x008386CC）。

追加指示（2026-09-17 第 2 弾・完了、実機未確認）: フォルダ名を日本語（プレイヤー/資産/スタイル/アイテム/ドロップ/お遊び/村/ゲーム/キーボード）、Shop 廃止で店24時間オープンをゲームへ、通知を旧形式（題=項目名・本文=〈項目名〉を有効/無効にしました、無効は赤）へ戻す、資産 5 項目を実装。
- 資産: GetSaveOffset(4)=0x002FEB60 基準で 所持金+0x6F08/貯金+0x6B8C/メダル+0x6B9C/ふるさとチケット+0x8D1C、カブ=[0x00955F8C]+0x6ADE0×12。Decrypt 0x00303530/Encrypt 0x00303404。根拠はゲームの呼び出し箇所と Vapecord 注記（-0xA0）の一致（gohan.md §4）。
- ★Windows で増分ビルドすると main.d の「multiple target patterns」で失敗する。make clean してから make。
- 成果物 rev2: artifacts/plugins/cheat_impl/gohan.3gx 924170 B sha256 c0e6dbd5…。全検証・監査 PASS。

追加指示（第 3 弾・完了、実機未確認）: プレイヤー/資産・スタイルの下に「座標移動」フォルダを作り、座標移動・移動キー・移動量・移動方法を移した（タッチワープはプレイヤー直下のまま）。読み取りに失敗した連動型（disabled）にもカーソルを置ける（編集・決定・ホットキー設定は不可。Simulator との意図した差、oracle も同じ差を持つ。verify_menu_port の disabled_cursor で検査）。成果物 rev3 sha256 29dd7431…。

追加指示（第 4 弾・完了、実機未確認）: カブは店のカブ価（利用者確認）→ `カブ価` に改名し、ゲーム/キーボードの下隣に `店` フォルダを作って店24時間オープンと一緒に入れた。座標移動の移動量は十字キーで 0.5 刻み。gohan.md §1/§4/§10/§12.5/座標移動に反映。成果物 rev4 sha256 dc0512a4…。

★実機確認（2026-09-17）: 利用者が work/cheat-impl `d706c4d`（3gx rev4 sha256 dc0512a4…）の**全動作を実機で確認し、すべて意図どおり**と回答。
- ただし **main へはまだマージしない**（他エージェントのレビューを待つため。利用者指示）。上記「実機未確認」の記述はこの確認で解消。次の一手はレビュー結果を待つこと。

追加指示（第 5 弾・完了、rev5 は実機未確認。main 未マージのまま）:
- 座標移動フォルダに `十字キーの無効化`［なし / 座標移動中］（既定なし）。座標移動中かつ移動キーが十字キーのとき、ホットキー押下中は `GuiMenu::BlockGameDpad()` → 入力遮断ケーブ A の制御値 2 で十字キー 4 ビット（0x000F0000）だけ落とす。
- ケーブ A は 27 → 32 語（`input_block_cave.build_cave` の変更 → `export_gui_v2.py` で GuiCaves.h 再生成。差分は入力ケーブのみ）。新しい検査 `tools/patches/verify_input_block_modes.py`（Unicorn で値 0/1/2/3 × タッチ 4800 件、振る舞いの変異を検出することを確認）。
- gohan-menu.md に §5.6「固定」（連動型の値を常に固定。設計のみ・未実装）を追加、§3 表と §9 の 6 に反映。
- 成果物 rev5 sha256 ec4b2fa4…。

# ポケットアイテム未解析・§15 整理（2026-09-17・最新）
利用者: ポケットアイテムを未解析に（アイテムID→名前・存在判定を資料ファイルに頼るのは冗長。ゲーム自身のデータを解析して使う）。§15 の解決済みを明記。
反映: gohan.md §17.5 を未解析＋参考に、§14 <ITEMID>/<ITEMNAME> に未解析の注記、§15 を表にしてトグル型アクション式の実装形を解決済み（main 3389f71 実機確認）、既定ホットキー・スタイル専用UI は未解決のまま。

# どこでも掘れる 9 件目を有効（2026-09-17・最新）
利用者指示で 0x5982C4 0A000017→EA000017 を表の 9 語目に追加（code.bin 元命令一致 BEQ 0x598328）。設計の置き場所は gohan.md §17（振る舞いの型は gohan-menu.md）と回答。

# gohan.md 詰め（回答反映）— 2026-09-17・最新
利用者回答: 花散らせない＋歩いて消すの併用は歩いて消すが優先（利用者の知見）。アイテムが消えない・キーボード制限解除は解析が必要なので「未解析」扱い。天気は「ゲームに任せる」、嵐・雪はゲーム内に天気として存在するなら加える。3x3掘りはどこでも掘れるに依存しない。「どこでも掘れる」9件目の意味を説明する。
結果: gohan.md 反映（併用優先、未解析2件は参考情報のみ残す、3x3 非依存、9件目の説明）。天気は code.bin 0x62E690〜0x62E72C の逆アセンブルで、日付時刻表 0x957FB4 から値を引き、条件付きで 3→5・4→6 に置換（雨→雪）。JOKER の 0〜6 の名前（晴れ雲なし/雲あり/くもり/雨小/雨/雪小/雪）と一致。嵐の独立値は無し（Vapecord の STORMY=4 は雨の強い方）→ 嵐は加えず、ゲームに任せる＋0〜6 を選択肢案に。値7は別分岐（未解析）。実機の見た目は未確認。

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

2026-09-18 続き（実機、書き込みはまだ 0 バイト）:
  - 利用者が手動 Suspend → state=-1, PC=0x137064, BP 0件 まで復帰。0x611928 の BP は削除済み。
  - ScStage の vtable は **0x008F2480**（TypeInfo 0x008C66E0、PrepareStep は slot 18）。この値を **.data 全域と .bss 全域（0x93F000〜0xAF2694）で総当たり検索したが 0 件**
    ＝ ScStage の実体もそれを指すポインタも静的領域には無い（ヒープ上）。よって「ScStage の遊休領域を借りる」案は取り下げ、
    **sub_4B8CF0(size,align) 0x4B8CF0（nw::lyt ヒープ、off_96F150 の vtable+8）で作業領域を借りる**当初案に戻す。返却は ssys_ma_Allocator_Free 0x569B44。
    文字列は romfs ではなく .rodata の実物を使える: "UnitCursorRes"=0x8818F4 / "Ftr/Chip/UnitCursor.bcres"=0x881904 / vtbl_sead_SafeStringBase_char=0x8FEC00。
  - 実機メモリの読み出し速度は約 54 KB/s（1 回の py_eval で 0x20000 を 2.4 秒）。1.7MB の走査は 3 回に分ける必要がある。
  - **現在の障害**: 手動 Suspend 以降、スクリプトから再開できない。`continue_process()` と `idc.resume_process()` がどちらも失敗を返し
    （False / -1）、`request_continue_process()`+`run_requests()` は True を返すが実際には動かない。`is_debugger_on`=True、スレッド 20 本、
    current=0x113 で接続自体は生きている。BP 0x1B721C は登録済み（qty=1）。
    → 利用者に IDA 側で F9（Continue）を 1 回押してもらう。BP が登録済みなので 1 フレーム以内に 0x1B721C でヒットして再び停止するはず。
  - 呼び出しヘルパ（py_eval の locals に定義済み。まだ 1 度も実行していない）: park() と call(fn,args,stack)。
    call は R0-R12/SP/LR/PC を退避 → SP を 0x400 下げて引数語を置き → R0-R3 と LR=RET_TRAP(0x00838FFC) と PC=fn を設定 →
    continue → RET_TRAP の BP で停止 → R0/R1 を読む → BP 削除 → 全レジスタ復元。ヒットした BP は必ず削除してから再開する規則に従っている。

2026-09-18 実機・第2段（IDA 再起動後、再アタッチで復旧。以後 continue は正常）:
  **コードは 1 語も注入していない。** レジスタ注入でゲーム関数を直接呼ぶ方式（tools/hw/unitcursor_hw.py、MODE を書き換えて py_exec_file）。
  park=0x001B721C、RET_TRAP=0x00838FFC（.text 末尾、実機でゼロ）。ida_dbg.wait_for_next_event は py_exec_file 内で機能する。
  ★py_eval は exec の globals/locals 分離で関数から外の名前が見えない（旧CLAUDE.md の既知事項）ため保存スクリプト必須。
  経過:
   1. Room_GetCurrentId() = **0（村）** … 呼び出し機構の無害な試験。CRO の証拠（Outdoor/Village）と独立に一致。
   2. sub_4B8CF0(0x200,32) で作業領域 0x32EFD0E0 を借用。SafeString 2 本を書き、HeapAllocator_Ctor と G3dResHolder_Ctor を実行（vtable 0x90053C / 0x8FCD18 を確認）。
   3. Heap_CreateNamed_0(allocr,0x5000,親0x301668EC,&SafeString,[SP]=1,[SP+4]=0) → heap 0x30FA8BC0。
      RequestLoad は **1 フレーム待って完了**（戻り値1）。資源 0x30FA8C80 の先頭が **"CGFX"** で実体確認。Setup(holder,allocr,0,1) は戻り値 0。
      ★holder+232 は 0xC0 で「0..5 の状態」ではなかった。V2-F021 の「+232 が状態」は要訂正（別の欄の可能性）。
   4. FindModelByName(&holder[1],"UnitCursor") → 0x30FA8DD0、+4 が **"CMDL"**。
   5. ModelInstance_Create(holder,model,allocr,allocr,0x834,1,3) → R0=1・node=0x30FACFF8・count=3。
   6. SetMatrix3x4 → 0x801、UpdateWorldAndSkeleton → OK、Scene_SubmitNode(holder,0) → OK。
      **1 フレーム目の描画中に 0x494C18 `LDR R2,[R0]` で R0=0 の NULL 参照で停止**（bp 0 件＝データアボート）。
      経路は ctx+0x20(material) → +0x1C(オーナー Model) → +0x1EC。**model+0x1EC が 0**。
      復旧: PC を 0x494C28（その関数自身の「束縛しない」経路）へ移して continue → park に復帰。**ゲームは落ちていない**。
   7. 原因特定: model+0x1EC は **nw::gfx::MaterialActivator / SimpleMaterialActivator**。sub_4A9A04 が内部ビルダを順に呼び、
      **どれかが負を返すとアクティベータ生成ブロックに到達しない**。ModelInstance_Create は V2-F007 の警告どおり **それでも 1 を返す**。
      真因は **ヒープ不足**: 20480 B のヒープに 16,256 B の bcres を読んだ残り約 4 KB へ 3,064 B のインスタンスを作ろうとしていた。
   8. 対策: インスタンス専用に 0x20000 のヒープを別に作成（allocr2=scratch+0x180、heap 0x30FADBD0）→ 再生成で
      **model+0x1EC = 0x30FAFCA0（vtable 0x8FBA18 = nw::gfx::MaterialActivator）が付いた**。node=0x30FAEF78、holder=0x32EFD270。
   9. MODE="show"（毎フレーム Scene_SubmitNode して 120 フレーム進める）を実行。MCP は 30 秒で切れたが**スクリプトは完走し、
      ゲームは park に正常復帰（落ちていない）**。描画されたかは利用者の目視待ち。以後 SHOW_FRAMES=40 に縮めた。
  現在の資源（解放していない）: 借用 0x32EFD0E0(0x200)、heap 0x30FA8BC0(0x5000)、heap 0x30FADBD0(0x20000)、失敗インスタンス 0x30FACFF8。

2026-09-18 実機・第3段（画面確認まで到達。**コードは 1 語も注入していない**）:
  道具: tools/hw/unitcursor_hw.py（MODE を書き換えて py_exec_file）。画面確認は reference の PATCHES/grab_screen.ps1（powershell.exe 5.1）。
  ★MCP プロキシは 30 秒で切れる。**長いループを py_exec_file に入れると途中で中断され、call() のレジスタ復元前に死ぬ**。
    実際に 1 度そうなり、描画スレッドが RET_TRAP で止まったまま復元不能になった。復旧は PC=0x1B721C / SP=0x0FFFFE20 / R4=1 /
    R5=0x0096F350（0x1B72D4 のリテラル）を手で書き戻して 1 フレーム完走を確認。**以後 SHOW_FRAMES=45 に制限**。
  ★利用者提供: **プレイヤー座標 = 0x33099E50**（float x,y,z）。実測 (2601.536, 112.0, 1747.981)。
    **vc_CAMCOORD 0x0097F6F4 はカメラではなくプレイヤー位置**（同じ値）と判明。V2 の呼び名は誤解を招くので要訂正。
  確定した実機事実:
   - Scene_DrawLayers 0x4EB840 に渡るノード一覧（scene_arg+12..+16）に **我々のノードが入っていることを確認**（34 件中の最後）。
     次フレームでは消える＝Render_FrameEnd が毎フレーム空にするという静的結論も裏付け。**登録経路は正しい**。
   - 我々のインスタンスのメッシュ: 1 個・visible バイト(+36)=1・ボーン番号(+38)=-1・マテリアル番号 0・
     **ResMaterial+32 のレイヤー = 1**（V2-F024 の静的結論と実機で一致）・コマンド +0x68 非 0 / +0x6C = 56 バイト。
   - つまり **Scene_DrawLayers のふるいは全部通っている**。
  未解決（次の一手）: **画素が出ない**。位置をプレイヤー直上にしても、倍率を 40 倍にしても画面は変化しない。
    最有力仮説: UnitCursor のマテリアルは **加算合成（0x0101=0x01160000、RGB = src*SRC_ALPHA + dst*ONE。V2-F024）**で、
    アニメ 3 本はすべて **MaterialAnimation**。アニメを結び付けていないので色が 0 のままで、加算の黒＝不可視になっている可能性。
    → 検証案: 利用者に屋内で模様替えに入ってもらい、**ゲーム自身が作った UnitCursor インスタンスのマテリアルと我々のものを差分**する（陽性対照）。
  途中で潰した仮説: holder+12 = 0x80000002 は失敗フラグではない（資源ヒープを 0x5000→0x40000 にしても同じ値）。
  ヒープ不足は**別の実害**を出していた: 最初のインスタンスは model+0x1EC（MaterialActivator）が 0 のままで、
    描画時に 0x494C18 `LDR R2,[R0]` の NULL 参照になった。ModelInstance_Create は V2-F007 の警告どおり **それでも 1 を返す**。
    インスタンス専用に 0x20000 のヒープを分けたら活性化子が付き、以後クラッシュしない。
  現在の状態: ゲームは park(0x1B721C) で停止中・BP 0 件・実機への書き込みは全てデータのみ（.text へのコード注入なし）。
    借用: scratch 0x32EFD0E0(0x200) / 資源ヒープ 0x40000 / インスタンスヒープ 0x20000（いずれも未解放）。

2026-09-18 実機・第4段（屋内・模様替えでの陽性対照）:
  ★**重要な方法上の制約**: 範囲表示は利用者が家具を掴んで**押しっぱなし**のときだけ出る。**Continue するたびに離れて消える**。
    よって「BP で止めては再開」を繰り返す採取法は使えない。最初の find_game_cursor / find2（90 ヒット×2）で m_UnitCursor が
    見つからなかったのは**カーソルが既に消えていたからで、「描画経路が違う」という結論は成立しない。撤回する。**
  対策: **1 回だけ止めて、そこから先は全部メモリ読み**にする（mode scan_scene）。Scene_DrawLayers 0x4EB840 に 1 回止まって
    R0 のシーンから node 一覧（+12..+16）を取り、各ノードの mesh 配列（node[92]/[93]）とマテリアル配列（node+356）を辿って
    ResMaterial の MTOB 名（+0x0C 相対）を読む。再開は 1 度きりなので押しっぱなしのまま通せる。
  屋内で観測できたマテリアル名（参考）: m_fade, m_stairs, m_thickness, m_carpet, m_wall, m_window, m_b, df_m, ev_m, lp_m_b,
    m_flower, df_0..3_m, m_mnqn_pesestal, m_waist, legs, cloth, mnqn_head, m_fade_wall, m_sunshine, m0, m1。

2026-09-18 実機・第4段の結末と道具の修正:
  ★**GDB の読みは未マップ番地で 60 秒ブロックする**（ida_mcp.sync が IDASyncError を投げるまで）。scene のノード一覧には
    Model 以外（FuncNode vtbl 0x8FCF10 ほか）が混ざり、Model 前提のオフセットを読むとゴミのポインタになるため、
    **参照する前に plausible() で 0x30000000..0x34000000 と 4 バイト整列を検査する**ようにした。これを入れる前は 1 本の
    ポインタでスキャン全体が固まっていた（10 分以上）。
  ★**MCP プロキシは 30 秒で切れるが IDA 側のスクリプトは走り続ける**。stdout はそこで失われるので、
    main() を try/except で包み、結果と traceback を必ず work/evidence/unit_cursor/hw_last_result.json に書くようにした。
  ★u32() が 1 語ごとに invalidate_dbgmem_contents(0,0) していたため 1 語 = 1 往復になっていた。invalidate は
    「対象を走らせた後」だけに限定（call/park/advance の末尾）。
  結果: scene 0x329ABEF0 のノード 42 件を全件走査したが **m_UnitCursor は無し**。
    ただし **この否定は根拠として弱い**: (1) 待ち時間が長く、その間に利用者が家具を離していた可能性が高い
    (2) cstring が「NUL までが全て印字可能」を要求するため、名前が空で返るマテリアルが多数あり取りこぼす穴がある。
    → **「ゲームの UnitCursor が Scene_DrawLayers を通らない」とは結論しない。**
  次の一手（押しっぱなし不要で、より実用的）: ゲーム自身の UnitCursor に固執せず、
    **正常に描かれている任意の Model のマテリアル（0x8FBD7C）と我々のマテリアルを 1 語ずつ差分する**。
    知りたいのは「なぜ我々のメッシュが画素を出さないか」なので、比較対象は UnitCursor でなくてよい。屋内外を問わず 40 件以上ある。
  現在: ゲームは park(0x1B721C) で停止・BP 0 件。実機への書き込みはデータのみ（.text へのコード注入なし）。

2026-09-18 実機・第5段（屋外へ戻って原因を絞り込み。結論まで到達）:
  部屋を跨いでも自前のオブジェクトは全て健在だった（scratch・両ヒープ・ホルダ・ノード・マテリアル名 m_UnitCursor・レイヤー1）。
  MaterialActivator::Activate 0x0049C08C を解析 → マテリアル a3 の a3[10..20] が側面を指し、コピーマスク 0x834 の側面
  （自前では a3[12]/[14]/[15]/[20]）だけインスタンス専用コピー 0x30FA9428 を指す。
  **棄却した仮説 2 件**: (1) 色が 0 で加算の黒＝不可視 → 専用コピーと ResMaterial は完全同値だった。
  (2) テクスチャ未束縛 → 両者同一で 128x128・物理 0x20FCFA00（reg 0x0085=0x041F9F40）。正しい。
  ブレンド・深度の実機バイト列は **romfs の静的解読と完全一致**（V2-F024 の PICA 解読が実機で裏付いた）。
  **決定的実験**: 専用コピーだけを不透明(0x0101=ONE/ZERO)・深度 ALWAYS にして描画 → **画面は変化しない**。
  → **ジオメトリが 1 枚もラスタライズされていない**。書き換えは元に戻した。
  手掛かり: mesh+0x68 のコマンド列が自前 56B / 正常モデル 80B。先頭 28B は同一、続く語が 0x10020000 vs 0x20060000、
  正常側には 24B の追加ブロックがある。→ 次は **sub_4957C4 0x004957C4**（sub_48D26C がコマンド転送の前に呼ぶ）を読む。
  記録: docs/topics/unit_cursor_outdoor.md §11 に実機結果と道具の注意を追記。
  現在: ゲームは park で停止・BP 0 件・.text へのコード注入なし。

2026-09-18 静的解析の見直し（利用者の指摘「ここまでくると静的解析の範疇。不完全な静的解析をしていた」を受けて）:
  指摘は正しい。**「ジオメトリが描かれるために何が要るか」を一度も静的に追わないまま設計書を書いていた。**
  動的解析を止めて記録済みの結論を洗い直し、瑕疵 3 件と弱い断定 1 件を見つけて訂正した（V2-F025）。
  (1) **誤り**: 「ホルダ +232 が読み込み状態」は 28 バイトずれ。G3dResHolder_RequestLoad が FileRes_RequestLoadAligned(a1+7語) と呼び、
      その中継は素通しなので **FileRes は holder+28 から**。正: holder+96 パス / +232 ヒープ / +256 アライン / **+260 状態** / +261 非同期旗 /
      **+236 読み込んだ資源の先頭**。実機で +232 が 0xC0 だった件と一致（独立2経路）。
  (2) **見落とし**: メッシュの GPU コマンド列は 2 本。sub_4957C4 0x4957C4 が mesh+96/+100 の**頂点属性・バッファ束縛の列**と
      mesh+48 の 48B を先に流し、sub_48D26C 末尾の mesh+0x68/+0x6C は 2 本目。V2-F024 で sub_48D26C を読んだとき前段を追わなかった。
      さらに UnitCursor.bcres には 0x0200〜0x0230 のコマンドが皆無＝**1 本目は実行時組み立て**。生成が打ち切られると
      マテリアルだけ正常でジオメトリの列が欠ける、という実機の症状と辻褄が合う。
  (3) **訂正**: ModelInstance_Create は半壊でも 1 を返し holder+4 も埋める。**model+0x1EC（MaterialActivator）まで確認が必要**。
  (4) **断定の取り下げ**: holder+12 の bit31 を「失敗フラグではない」としたのは、両方失敗の可能性を排除できておらず根拠が弱い。意味未確定に戻す。
  (5) **数字どうしの矛盾**: 20480-16256=4224 空きに 3064 のインスタンスが入らなかった。Setup も同じアロケータから取ること、
      ExpHeap のブロック管理とアラインが積むことが理由。**3,064 B を確保量の根拠に使わない**と明記。
  問題が無かったもの: sub_4F05F4 / sub_4EC83C は副作用のない型チェック付きキャスト（設計書の手順に抜けは無い）。
  Setup の第2引数はアロケータ物体・読み込みは生ヒープ（取り違えやすいので明記）。ModelInstance_Create の a3/a4 もアロケータ物体（表に追記）。
  Field_TileToWorld の 32×マス+16 は実機の描画中モデル x=2607.82≒2608 と一致。
  反映先: IDB コメント 4 件（0x569BA0 / 0x317920 / 0x4957C4 / 0x4F048C）・保存・閉じた。FINDINGS V2-F025、FUNCTIONS 2 行、
  設計書 §3 表・§3.2・§7.1・§7.2・§8・§10.3・§11.3、evidence/outdoor_plan.md。XML 書き出し・検証 PASS。
  次: 自前インスタンスの mesh+0x60/+0x64 と正常モデルの同欄の比較（静的に何を期待すべきかは上記で確定した）。

2026-09-18 残る不明点の静的解析（V2-F026）:
  追ったのは「mesh+0x60 の頂点属性コマンド列を誰が作るか」。
  - G3dResHolder_Setup → sub_4A5A74 → sub_4A693C は型タグで off_96F0F4 / off_96F0F8 へ分岐するが、
    .data の初期値は 0x004A6934 / 0x004A7254 で **どちらも return 0 のスタブ**（書き換えるコードなし）。
    → **資源側のモデル setup はこのビルドでは実質何もしない。**
  - 2 本とも作るのは **sub_4A8B44**（インスタンス生成側）。sub_4B14CC で属性列の大きさ、sub_4B1540 で描画列の大きさを求め、
    **アロケータから 1 回だけ (v28+v56) を確保**し、sub_7E5710 で前半・sub_4B12C0 で後半を埋め、
    mesh+96/+100/+104/+108/+128 をまとめて書く。sub_4B12C0 の出力語は実機で読んだ 56B と完全一致。
  - **自説の反証**: 2 本は同じ確保の前半・後半なので、mesh+0x68 が有効なら mesh+0x60 も必ず有効。
    さらに sub_4B14CC は最小 48B を返し 0 にならない。→ 「ジオメトリのコマンドが欠けている」は成立しない。
  - 付随: sub_4A8B44 は常に 0 を返す（失敗を報告できない・確保の null 検査も無い）。
    ワールド行列は **node+188(0xBC)**、局所は node+76(0x4C)、親は node+12 で null なら単位元（sub_4EF69C）。
    → **親を持たない自前ノードでもワールド行列は正当に計算される。**
  次の候補: 1 本目の中身は材質から解決される属性対応表 desc に依存し、属性ごとに desc[index+76] >= 0 で採否が決まる。
    ここが噛み合わないと「コマンド列はあるのに属性が 1 つも出ない」状態になりうる。sub_7E5710 と desc の解決経路を読む。
  反映: IDB コメント 4 件（0x4A8B44 / 0x4B14CC / 0x4A693C / 0x4EF69C）・保存・閉じた。FINDINGS V2-F026。XML 書き出し・検証 PASS。

2026-09-18 続き（V2-F027。原因の最有力候補を静的に特定）:
  sub_7E5710（属性コマンド列の組み立て）を読んだ。**頂点バッファの番地は `v13 = sub_143B54(VA)` で作られ、
  `*(mesh+52)` に保存されると同時に `buf[8] = v13 >> 3` として PICA の頂点バッファ基底になる**。属性ごとのオフセットも
  同じ基底からの差分。sub_143B54 は sub_140C3C への thunk で、**VA→GPU 番地の変換**。
  経路: VRAM 域は VA-0x07000000、登録済みの線形領域 [sub_143238(), +sub_143220()) なら基底<0x30000000 で VA+0x0C000000、
  そうでなければ VA-0x10000000。**どれにも当てはまらなければ 0 を返す（例外も警告も無い）。**
  → 自前ヒープが線形領域の外だと **基底も全オフセットも 0** になり、コマンド列は正常、メッシュ可視、マテリアル正常のまま
    GPU が物理 0 から頂点を読む。**クラッシュせず何も出ない**という実機の症状と完全に一致する。
  **検証できる 1 点: 自前インスタンスの mesh+52(0x34) が 0 かどうか。** 0 なら確定。非 0 なら棄却し、
  次は desc[usage+76] の属性番号対応を疑う（sub_7E5710 は sub_4A8B44 と違い >= 0 の除外判定を持たない）。
  注意: テクスチャが正常だったことは反証にならない。テクスチャ番地 0x20FCFA00 は VA 0x30FCFA00-0x10000000 に相当し、
  「テクスチャが置かれた番地は線形領域内」までしか言えない。
  反映: IDB コメント 2 件（0x7E5710 / 0x140C40）・保存・閉じた。FINDINGS V2-F027。XML 検証 PASS。

2026-09-18 V2-F027 の実機検証を試みて**ゲームを壊した**（私のツールの不具合。要再起動）:
  目的は自前インスタンスの mesh+52 が 0 かどうかの確認 1 点だった。まず状態を見ると、部屋移動か再開で
  **scratch もヒープもノードも全部解放・ゼロ化**されていたので作り直しから始めた。そこで不具合が 3 段に連鎖した。
  (1) mode_redo が **2 つ目のアロケータ（scratch+0x180）を構築せずに使っていた**。HeapAllocator_Ctor を通していないので
      vtable が未設定のまま ModelInstance_Create が `(*vtable+8)` を呼び、0x496AD4 でデータアボート。
      → 修正: redo でも両方 ctor する。ただし **+4 が非 0 のときは ctor しない**（後述）。
  (2) その修正で両方 ctor したところ、**既存ヒープの handle が消えて孤児化**した。HeapAllocator_Ctor は p[1]=0 にするので、
      Heap_CreateNamed_0 が本来行う「古いヒープを壊してから作る」が効かなくなる。**redo のたびに 0x40000+0x20000 漏れる**。
      → 修正: `*allocr` が 0 のときだけ ctor する。
  (3) 漏れの結果、親ヒープ 0x301668EC が 0x40000 を満たせなくなり **Heap_CreateNamed_0 が 0 を返した**のに、
      mode_redo は heap を検査せず **heap=0 のまま G3dResHolder_RequestLoad を呼んだ**。
      描画スレッドが PC=0x8000002C へ飛び、park の文脈を手で復元しても PC=0x3309C42C で再度例外。**復旧不能。**
      → 修正: heap が 0 なら即座に中止する guard を入れた。
  教訓: **ゲームの関数へ「失敗した戻り値」をそのまま渡さない。** 戻り値 1/0 を毎回検査する。
  現在: 実機は描画スレッドが死んでいる。**利用者にゲームの再起動を依頼**。再起動後は alloc → redo（修正版）→ mesh+52 の読み取り。
  V2-F027 の検証自体は未実施（成否不明ではなく、未着手）。

2026-09-18 設計→検証→実装→検査を実施（適用は未実施。利用者の指示による段取り）:
  前回ゲームを壊した反省から、場当たりの実機操作をやめて手順を文書化してから作る形に切り替えた。
  **設計**: docs/topics/unit_cursor_hw_plan.md
    目的は 1 点のみ ― 自前インスタンスの mesh+52 が 0 か（0 なら V2-F027 確定）。画面へ出す試みはしない。
    前提条件 P1..P6、手順 S1..S13 を各段の成功条件つきで表にし、後始末（作った逆順、作れなかったものは触らない）、
    安全規則（失敗した戻り値を次へ渡さない／45フレーム以内／未マップを読まない／結果は必ずファイルへ）、
    使う番地の一覧と各々の独立した根拠を明記。
  **検証**: tools/hw/verify_unitcursor_hw.py（IDA も実機も不要。code.bin と exheader だけで動く）
    関数番地はゲーム自身の呼び出し site の BL から逆算して確認（0x611A0C→Heap_CreateNamed_0 等 6 件）、
    vtable はコンストラクタが書くリテラルから、文字列は実体から、park の NOP・R5 復元リテラル・.text 末尾の
    ゼロ・セグメント境界・sub_4A8B44 が書く mesh の 5 欄・sub_143B54 の飛び先まで機械照合。**45 件すべて ok**。
    ★最初の実行で 2 件落ちたが、どちらも検証器自身の不具合（末尾の範囲取り違えとリテラル窓の狭さ）で、設計側は無傷だった。
  **実装**: tools/hw/unitcursor_probe.py（設計の S1..S13 をそのまま実装）
    Abort 例外で「成功条件を満たさなければ絶対に次へ進まない」を構造にした。ヒープ作成が 1 を返しかつ +4 が
    非0でなければ読み込みへ行かない（前回の事故点）。アロケータは vtable が未設定のときだけ ctor（孤児化防止）。
    teardown は作った逆順で、ヒープは heap の vtable+16、scratch は ssys_ma_Allocator_Free。
  **検査・逆アセンブル**: tools/hw/inspect_unitcursor_probe.py
    実装中の定数を名前で抜き出し、設計文書に載っているか・code.bin から再導出できるかを照合。
    anchor 6 件のバイト列、R5 復元値、scratch 配置の重なりと総量（416 <= 512）まで見る。**0 failed**。
    park 前後・Heap_CreateNamed_0 入口・sub_4B8CF0 入口を逆アセンブルして出力。
  **適用は未実施**。実機は前回の事故で描画スレッドが死んでおり、ゲームの再起動が要る。
  再起動後の手順: verify → probe（MODE="probe"）→ mesh+52 を読む → 終わったら MODE="teardown"。

2026-09-18 適用（設計→検証→実装→検査→適用 を完走。V2-F028）:
  再起動後、verify（45件 ok）→ 前提条件の確認（停止中・BP0・anchor一致・park NOP・RET_TRAP空・親ヒープ有効）→ probe 実行。
  **1 回で通り、クラッシュも中断も無く、teardown まで完了**（インスタンスヒープ・資源ヒープ・作業領域を解放）。
  測定: room 0 / holder+260=3 / holder+12=0x80000002 / mesh+0x60=0x30FACC94(64B) / mesh+0x68=0x30FACCD4(56B) /
        mesh+52=0x000F02C0 / node+0x1EC=0x30FE9C80。
  **V2-F027 は棄却。しかも欄の同定自体が誤りだった**: mesh+52 は番地ではなく sub_4A8B44 が書く定数 983744=0x000F02C0
  （PICA ヘッダ。mesh+48 からの 48B 埋め込み列の一部）。sub_7E5710 の *(a2+52)=v13 は**資源側のメッシュ**への書き込みで別 object。
  **頂点バッファの束縛は正しい**: 属性列を復号すると reg0x0200 ATTRIBBUFFERS_LOC=0x03000000（基底 0x18000000）、
  reg0x0203 ATTRIBBUFFER0_OFFSET=0x08FAA908 → 物理 0x20FAA908 = VA 0x30FAA908 で bcres 内。
  PICA は (LOC<<3)+OFFSET なので基底は基準点にすぎず、v13 は「番地」ではなく「差分の基準点」だった。
  **実機で裏付いた既存知見 3 件**: V2-F025(1) 状態は +260（=3 完了）／V2-F025(3) ヒープを分ければ活性化子が付く／
  V2-F026 2 本は 1 回の確保の前半・後半（0x30FACC94+64=0x30FACCD4）。
  現状: 確かめた範囲はすべて正常なのに画素が出ない。残る候補は (a) desc[usage+76] の属性番号対応
  （属性列の先頭語 0xA000000B はシェーダが 12 属性を期待していることを示す。メッシュの実数と噛み合うか未確認）
  (b) 頂点シェーダの束縛 material+32→sub_496298 (c) 変換行列がシェーダ定数として届いているか。
  道具の修正: probe の MESH_VERTEX_BASE を MESH_CMD_HEADER に改名し、実際の頂点番地は属性列から復号するようにした。
  inspect 側もその定数を code.bin のリテラルプールから照合する検査に差し替え（窓を関数末尾+0x20 まで広げた）。

2026-09-19 測定2（V2-F029）。設計→検証(51件ok)→実装→検査(0 failed)→適用 を再度完走:
  仮説「行列がシェーダへ届いていない」を測定 → **棄却**。node+561=0・node+544 有効・pool+24=0 で、
  識別用の平行移動 (1111,2222,3333) を設定すると node+0xBC のワールド行列に正しく入った。
  「行列プールが空（begin==end）」も、**いま描かれている 3 モデルが全く同じ**だったので正常と判明（対照が効いた）。
  動作中モデルとの語単位差分で node+0x48/+0x144 が 3 対 1 → 生成引数と判明したので (0,0,1) で A/B → **+0x228/+0x22C は変わらず棄却**。
  **残る差: node+0x228（相手はポインタ、我々は 0）と +0x22C（相手は 0、我々は -1）**。行列プール(+0x220)と
  ゲート(+0x231)の間にある skeleton 関連の区画で、生成時には埋まらない＝生成後に別の呼び出しが設定している。
  運用の学び: **実行ごとに probe_state.json を消してはいけない**（後片付けの控えを失う）。今回それで親ヒープが
  一時的に枯れたが、**ガードが働いて安全に中止**し、出力に残っていた番地を state に書き戻して teardown で全部回収できた。
  前回の事故（heap=0 を渡してゲーム死亡）とは対照的で、設計の狙いどおりに働いた。
  現在: 実機は健全、借用は全て解放済み。

