#ifndef CHEATS_HPP
#define CHEATS_HPP

// gohan.md のチート本体。メニューの木（GuiMenuItems.cpp）と同じ項目名で結び付ける。
// 項目名はここだけに書き、木と登録の両方がこれを使う（片方だけ直して食い違わないように）。

namespace CTRPluginFramework
{
    namespace Cheats
    {
        // ---- root/プレイヤー/資産 ----
        static const char kWallet[]         = u8"所持金";
        static const char kBank[]           = u8"貯金";
        static const char kCoupons[]        = u8"ふるさとチケット";
        static const char kMedals[]         = u8"メダル";
        // ---- root/プレイヤー/座標移動 ----
        static const char kCoordMove[]      = u8"座標移動";
        // ★座標移動フォルダの中。フォルダ名も「座標移動」だが、平たい配列では子（トグル）が
        //   フォルダより前に並ぶので FindItem(kCoordMove) はトグルを返す。
        static const char kCoordMoveKey[]   = u8"移動キー";
        static const char kCoordMoveSpeed[] = u8"移動量";
        static const char kCoordMoveMode[]  = u8"移動方法";
        static const char kCoordMoveDpad[]  = u8"十字キーの無効化";
        // ---- root/プレイヤー ----
        static const char kTouchWarp[]      = u8"タッチワープ";
        static const char kWalkThroughWalls[] = u8"壁抜け";
        static const char kNoBreakFlower[]  = u8"花散らせない";
        static const char kNoTrap[]         = u8"穴に落下しない";
        // ---- root/プレイヤー/スタイル ----
        static const char kNoBedHead[]      = u8"寝癖付かない";
        // ---- root/アイテム ----
        static const char kPocketItem[]     = u8"ポケットアイテム";
        // ---- root/アイテム/ドロップ ----
        static const char kDropItem[]       = u8"ドロップアイテム";
        static const char kTrampler[]       = u8"歩いた場所のアイテムを消し去る";
        static const char kDigAnywhere[]    = u8"どこでも掘れる";
        // ---- root/アイテム/ドロップ/お遊び ----
        static const char kFellTree[]       = u8"木にぶつかって切り倒す";
        static const char kDig3x3[]         = u8"スコップ3x3マス掘り";
        // ---- root/村 ----
        static const char kWeather[]        = u8"天気";
        static const char kNoLookUp[]       = u8"空を見上げない";
        // ---- root/ゲーム/店 ----
        static const char kShopsOpen[]      = u8"店24時間オープン";
        static const char kTurnips[]        = u8"カブ価";          // 店のカブの値段（プレイヤーの資産ではない）
        // ---- root/ゲーム ----
        static const char kInstantText[]    = u8"メッセージ即表示";
        static const char kShizueSkip[]     = u8"しずえスキップ";
        static const char kUnlockFps[]      = u8"フレームレート制限解除";
        // ---- root/テスト ----
        static const char kGridCursor[]      = u8"グリッドカーソル";
        static const char kGridCursorCols[]  = u8"横のマス数";
        static const char kGridCursorRows[]  = u8"縦のマス数";
        static const char kGridCursorTile[]  = u8"1マスの間隔";
        static const char kGridCursorScale[] = u8"カーソルの拡大率";
        static const char kGridCursorSnap[]  = u8"マス目に合わせる";
        static const char kGridCursorDiag[]  = u8"縞模様を 45 度傾ける";
        static const char kGridCursorMove[]  = u8"十字キーで動かす";
        static const char kGridCursorStat[]  = u8"状態を見る";

        // ---- root/テスト／モデルビューア ----
        static const char kMvBuild[]  = u8"一覧を作る";
        static const char kMvScroll[] = u8"一覧の位置";
        static const char kMvPick[]   = u8"モデル";
        static const char kMvShow[]   = u8"モデルを出す";
        static const char kMvIndex[]  = u8"ファイル内の番号";
        static const char kMvScale[]  = u8"モデルの大きさ";
        static const char kMvX[]      = u8"X ずらし";
        static const char kMvY[]      = u8"Y ずらし";
        static const char kMvZ[]      = u8"Z ずらし";
        static const char kMvStat[]   = u8"モデルの状態";

        // ---- root/テスト／プレイヤー複製 ----
        static const char kPcShow[]  = u8"複製を出す";
        static const char kPcHair[]  = u8"複製の髪型";
        static const char kPcColor[] = u8"複製の髪色";
        static const char kPcStat[]  = u8"複製の状態";
        static const char kPcScreen[] = u8"画面に固定";
        static const char kPcYaw[]    = u8"複製の向き";
        static const char kPcPitch[]  = u8"複製の傾き";
        static const char kPcX[]      = u8"画面の X";
        static const char kPcY[]      = u8"画面の Y";
        static const char kPcZoom[]   = u8"複製の大きさ";
        static const char kPcLight[]  = u8"複製の明るさ";
        static const char kPcLitX[]   = u8"光の向き 横";
        static const char kPcLitY[]   = u8"光の向き 縦";
        static const char kPcLitZ[]   = u8"光の向き 手前";
        static const char kPcLitAmb[] = u8"環境光";
        static const char kPcLitFragAmb[] = u8"光の環境色";
        static const char kPcLitDiff[] = u8"光の拡散";
        static const char kPcLitSpec[] = u8"光の反射";
        static const char kPcLitHemi[] = u8"半球光";

        // ---- root/テスト／公共事業 ----
        static const char kPwPick[]    = u8"公共事業";
        static const char kPwPlace[]   = u8"足元に置く";
        static const char kPwNearest[] = u8"近くの公共事業を選ぶ";
        static const char kPwRemove[]  = u8"選んだ公共事業を消す";
        static const char kPwMove[]    = u8"選んだ公共事業を足元へ";
        static const char kPwRebuild[] = u8"当たり判定を作り直す";
        static const char kHlOn[]      = u8"選んだ建物を光らせる";
        static const char kHlTint[]    = u8"選択の色の濃さ";
        static const char kHlAlpha[]   = u8"選択の不透明度";
        static const char kHlWave[]    = u8"選択の揺れ幅";
        static const char kHlSpeed[]   = u8"選択の揺れの速さ";
        static const char kBeOn[]      = u8"公共事業エディター";
        static const char kBpAlpha[]   = u8"プレビューの不透明度";
        static const char kBpWave[]    = u8"プレビューの揺れ幅";
        static const char kBpSpeed[]   = u8"プレビューの揺れの速さ";
        // 実際の名前は WirePublicWorks がゲームの表から入れる。ITEM_LIST は空を許さない。
        static const char *const kPwEmptyOptions[] = { u8"（読み込み中）" };

        // 一覧を作るまでの仮の中身。ITEM_LIST は空の options を許さない。
        static const char *const kMvEmptyOptions[] = { u8"（一覧を作ってください）" };
        // ---- root/ゲーム/キーボード ----
        static const char kKanji[]          = u8"漢字変換";
        static const char kComposition[]    = u8"コンポジション";

        // 天気の選択肢（gohan.md §17.8。0 番がゲームに任せる、以降が値 0..6）
        static const char *const kWeatherOptions[] = {
            u8"ゲームに任せる", u8"晴れ", u8"晴れ（雲あり）", u8"曇り", u8"小雨", u8"雨", u8"小雪", u8"雪"
        };
        static const int kWeatherOptionCount = (int)(sizeof(kWeatherOptions) / sizeof(kWeatherOptions[0]));
        // 座標移動の子設定の選択肢（並びが PlayerMove.cpp の MoveKey / MoveMode）
        static const char *const kMoveKeyOptions[] = { u8"十字キー", u8"スライドパッド", u8"C スティック" };
        static const int kMoveKeyOptionCount = (int)(sizeof(kMoveKeyOptions) / sizeof(kMoveKeyOptions[0]));
        static const char *const kMoveModeOptions[] = { u8"通常", u8"グリッド単位" };
        static const int kMoveModeOptionCount = (int)(sizeof(kMoveModeOptions) / sizeof(kMoveModeOptions[0]));
        static const char *const kMoveDpadOptions[] = { u8"なし", u8"座標移動中" };
        static const int kMoveDpadOptionCount = (int)(sizeof(kMoveDpadOptions) / sizeof(kMoveDpadOptions[0]));

        // メニューの木を組んだ直後に 1 回呼ぶ。項目名で引いて振る舞いを登録する。
        void    Wire(void);

        // PlayerMove.cpp（座標移動・タッチワープ）。Wire から呼ぶ。
        void    WirePlayerMove(void);
        // PlayerResources.cpp（所持金・貯金・ふるさとチケット・メダル、店のカブ価）。Wire から呼ぶ。
        void    WirePlayerResources(void);

        // ---- 毎フレームの配り手（Cheats.cpp）----
        // GuiMenu::SetToggleHandlers は大域に 1 組しか持てないので、登録は Cheats.cpp が
        // 1 回だけ行い、各チートの Tick / Disable をここから回す。自分の項目でなければ偽を返す。
        bool    PlayerMoveTick(int index, u16 held);
        bool    PlayerMoveDisable(int index);
    }
}

#endif
