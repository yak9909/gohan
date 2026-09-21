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
        static const char kGridCursor[]     = u8"グリッドカーソル";
        static const char kGridCursorSize[] = u8"大きさ";
        static const char kGridCursorTile[] = u8"1マスの大きさ";
        static const char kGridCursorMove[] = u8"十字キーで動かす";
        // ---- root/ゲーム/キーボード ----
        static const char kKanji[]          = u8"漢字変換";

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

        // グリッドカーソルの大きさ（GridCursor.cpp の kFootprints と同じ並び）
        static const char *const kGridCursorSizeOptions[] = {
            u8"1x1", u8"2x1", u8"1x2", u8"2x2", u8"3x3"
        };
        static const int kGridCursorSizeOptionCount =
            (int)(sizeof(kGridCursorSizeOptions) / sizeof(kGridCursorSizeOptions[0]));

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
