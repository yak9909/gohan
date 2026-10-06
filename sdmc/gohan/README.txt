gohan のリソース（SD カードの sdmc:/gohan/ にこのフォルダの中身をそのまま置く）

gohan/
  common/                          言語に依らないもの
    hidden_item_icons.arc          没アイテムの自前のアイコン（DARC: timg/<番号>.bclim、32x32 ETC1A4）
  0004000000086200/                とびだせ どうぶつの森（日本版）。ほかの版はその版のタイトル ID（16 桁）の名前で同じ並びに置く
    hidden_items.tsv               没アイテム表（UTF-8 タブ区切り。中の説明どおりに直してよい）
    hhd_charcreate.arc             HHD式スタイル変更のレイアウト

arc はゲーム由来の素材を含むので Git には入れていない。解析 repo の次のスクリプトで作る:
  python tools/items/export_hidden_icons.py   -> common/hidden_item_icons.arc
  python tools/items/export_hidden_items.py   -> 0004000000086200/hidden_items.tsv
  python tools/hhd/build_hhd_arc.py           -> 0004000000086200/hhd_charcreate.arc
