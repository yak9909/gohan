# PC でメニューの見た目を確かめる

実機へ入れる前に、実際のメニューを決めた入力・時刻で動かして確認します。
`src/menu/`、`src/features/menu_screen/`、`src/ui/renderer/` を PC の C++20 コンパイラで
ビルドします。入力と時計、ゲームに描画指示を渡す境界には PC 試験の代役を使います。
メニューの処理・座標・文字の幅を別実装にはしていません。

## 実行

必要な物は Python 3.11 以上と PC 用の g++ または clang++ です。Python の追加パッケージと
ブラウザー用のライブラリは使いません。プロジェクト直下から実行してください。

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
uv run --python 3.12 python tools/preview.py
```

`build/preview/index.html` をブラウザーで開きます。上400×240、下320×240 の PNG を
等倍と2倍で縦に並べ、各時刻を一覧にします。表示倍率は100%にしてください。
HTML は PNG・CSS を埋め込んだ1ファイルで、これだけを別の PC へ移しても見られます。
生成物は全て `build/` 以下に置き、収録しません。

`--scenario entries` のように台本を絞れます。複数指定もできます。
`--dev` は開発版の項目を表示します。`--cxx` または `HOST_CXX` でコンパイラを選べます。
`--out build/preview-test` で別の出力先にできます。

## 台本とフレーム

既定の台本は `tools/preview/scenarios.json` です。開閉の途中、フォルダ、OFF／待機／ON、
適用前の黄色いON、予約／OK、数値の枠、選択の入力、通知、長押しの進み棒、閉じる前・
全適用・ホットキー実行の確認を含みます。各台本は別プロセスで初期状態から動きます。

別の台本は `--script tools/preview/example.json` で読み込めます。

```json
{
  "scenarios": [{
    "name": "example",
    "label": "開く動き",
    "events": [
      {"at": 32, "buttons": ["Select"]},
      {"at": 64, "buttons": []}
    ],
    "captures": [
      {"at": 0, "name": "closed"},
      {"at": 160, "name": "halfway"},
      {"at": 400, "name": "opened", "contains": ["GOHAN"]}
    ]
  }]
}
```

時刻 `at` は開始からの整数ミリ秒です。0から600000まで指定できます。
入力の時刻は昇順で重複させず、出力の時刻・名前も台本内で重複させません。
既定では16msごとに実際のフレーム処理を呼び、入力・出力の時刻がその間にあれば
その時刻にも呼びます。撮影時刻の入力を処理し、Canvas が確定した指示を保存します。
時刻は待ち時間ではなく仮想の時計なので、再実行しても同じ画像になります。

`buttons` は押しているボタン全体です。次に変更するまで保持されます。離すには `[]` を
指定してください。名前は `A B X Y L R ZL ZR Up Down Left Right Select Start` です。
`touch: [40, 60]` で下画面に触れ、`touch: null` で離します。省略時は前の状態を保持します。
`toggle: true` はそのフレームだけ外部の開閉要求を送ります。
`contains` はその時刻の描画指示に必要な文字があるか検査します。
`empty: true` は閉じた状態の描画指示が空かを検査します。

PNG は `<台本>/<出力名>-top.png` と `-bottom.png` です。`frame-N.json` に実際の描画指示、
`inputs.txt` に展開後の全フレーム、`fonts.json` と `atlas.la4` にビルドした字形資源を残します。
`manifest.json` には設定・台本・コンパイラの版・入力ソース等の SHA-256 を保存します。
字形は make と同じ生成器で `build/gen/font_atlas.hpp` を生成し、実際の `Fonts()` から取得します。
OS やブラウザーのフォントは描画には使いません。

## 上画面の予行と色の設定

`tools/preview/settings.toml` の `screen` が、メニューを描く画面の唯一の設定です。
`bottom` から `top` に変えると、400×240で実際の Shell と Canvas を動かします。
設定を手元に複製して `--settings build/preview-top.toml` で読むこともできます。
上画面ではタッチ入力を受けません。

実製品のソースには変更を加えません。上画面の予行時だけ、機能のソース中の
`kScreen` 定数を Top にした PC 用ソースを `build/` に生成してコンパイルします。
置換が1か所に限定できなければ停止します。変更した行は manifest の
`screen_adaptation` に残します。メニューの配置・動作はそのままで、実機での移設作業は含みません。

色・合成の較正値は同じ設定ファイルの `[raster]` にまとめています。

| 設定 | 既定値と意味 |
| --- | --- |
| `background` | `[32,40,48]`。ゲーム画面の代わりの不透明な地色 |
| `offset` | `[0,0]`。合成する全指示の整数座標の補正 |
| `alpha_gain` | `1.0`。頂点色の透明度に掛ける係数 |
| `rgb_gain` | `[1,1,1]`。各RGBの係数 |
| `swap_red_blue` | `false`。色順の診断用。通常は変えない |
| `rounding` | `nearest`。8 bitへの丸め。`floor` にもできる |

描画指示は `0xAARRGGBB` と解釈します。実機の頂点色への赤青の入れ替えはメモリ上の
並びを変える処理なので、プレビューの表示色は入れ替えません。
LA4 の8×8タイルをほどき、上位4bitの輝度と下位4bitの透明度を各17倍して使います。
RGBを輝度で、透明度をアトラスの透明度で乗算し、指示の順で
`出力 = 前景 × α + 背景 × (1−α)` を合成します。整数倍の字形は最近傍で描きます。
文字は実機側の Scene と同じく、縦は文字列全体、横は送り幅全体が切り抜き範囲へ
入る字だけを描きます。矩形の切り抜きは実際の Canvas が済ませた指示を使います。

未較正なのはマテリアルを通した最終色、GPU の端の画素の規則、テクスチャ補間、
合成途中の精度・丸め、色空間です。現在のアトラスは0/255だけなので、
中間のLA4値の実機確認も未実施です。
画面写真で測る前に、これらを確定済みの実機再現とは扱わないでください。
設定値は診断・較正のための補正で、ソースから分かる描画規則は書き換えません。

## 実機の画面写真との比較

Rosalina が保存した上・下画面の BMP を手元の入力として使います。
単独の400×240／320×240のほか、上が400幅・下が中央320幅の400×480画像にも対応します。
BMP は無圧縮24/32bit、PNG は不透明・8bit RGB/RGBA・非インターレースに対応します。
BMP の行の上下と BGR の順はヘッダーに従って処理します。ゲーム由来の画像を収録しません。

```powershell
uv run --python 3.12 python tools/compare_preview.py build/preview/states/opened-bottom.png build/captures/bottom.bmp --screen bottom --out build/comparison
uv run --python 3.12 python tools/compare_preview.py build/preview/states/opened-top.png build/captures/top.bmp --screen top --region 0 0 160 240 --luma --out build/comparison-top
```

同じ入力・同じアニメーション時刻・同じ背景で比べてください。透ける背景が違えば、メニューの
実装が同じでも差が出ます。メニューを閉じた状態の画面写真を
`--background-top build/captures/closed-top.bmp --background-bottom build/captures/closed-bottom.bmp`
でプレビューに与えれば、その背景に合成できます。動く背景や撮影時刻のずれは別途残ります。

既定のずらし探索は±4pxです。`--search 0` で位置を固定できます。
比較領域は `--region x0 y0 x1 y1`（右・下は含まない）で絞れます。
全候補に同じ画素数を使うため、画像の端から探索幅ぶんを除きます。実際の範囲を数値に残します。
`shift_capture: [dx,dy]` は「プレビューの `(x,y)` に実機の `(x+dx,y+dy)` を対応させる」の意味です。

出力は `difference.png`（差を4倍した白黒）、`comparison.png`（補正した実機・プレビュー・差分の順）、
`metrics.json` です。平均差・最大差・8／24を超える画素の割合・補正前の平均差を出します。
RGB モードは各画素のRGBの差の最大、`--luma` は `0.299R+0.587G+0.114B` の差を使います。
`signed_luma_mean_capture_minus_preview` が正なら実機が明るい傾向です。
位置→背景→色順→透明度→丸めの順で原因を分けて確認すると、誤った補正を重ねずに済みます。
実機の写真を使う較正は、利用者の同じ状態の画像が揃ってから行います。
