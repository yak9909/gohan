#ifndef CHEATS_HPP
#define CHEATS_HPP

// gohan.md のチート本体。メニューの木（GuiMenuItems.cpp）と同じ項目名で結び付ける。
// 項目名はここだけに書き、木と登録の両方がこれを使う（片方だけ直して食い違わないように）。

namespace CTRPluginFramework
{
    namespace Cheats
    {
        // ---- root/Player ----
        static const char kCoordMove[]      = u8"座標移動";
        static const char kCoordMoveKey[]   = u8"座標移動 -> 移動キー";
        static const char kCoordMoveSpeed[] = u8"座標移動 -> 移動量";
        static const char kCoordMoveMode[]  = u8"座標移動 -> 移動方法";
        static const char kTouchWarp[]      = u8"タッチワープ";
        static const char kWalkThroughWalls[] = u8"壁抜け";
        static const char kNoBreakFlower[]  = u8"花散らせない";
        static const char kNoTrap[]         = u8"穴に落下しない";
        // ---- root/Player/PlayerStyles ----
        static const char kNoBedHead[]      = u8"寝癖付かない";
        // ---- root/Item/Drop ----
        static const char kDropItem[]       = u8"ドロップアイテム";
        static const char kTrampler[]       = u8"歩いた場所のアイテムを消し去る";
        static const char kDigAnywhere[]    = u8"どこでも掘れる";
        // ---- root/Item/Drop/Fun ----
        static const char kFellTree[]       = u8"木にぶつかって切り倒す";
        static const char kDig3x3[]         = u8"スコップ3x3マス掘り";
        // ---- root/Town ----
        static const char kWeather[]        = u8"天気";
        static const char kNoLookUp[]       = u8"空を見上げない";
        // ---- root/Shop ----
        static const char kShopsOpen[]      = u8"店24時間オープン";
        // ---- root/Game ----
        static const char kInstantText[]    = u8"メッセージ即表示";
        static const char kShizueSkip[]     = u8"しずえスキップ";
        static const char kUnlockFps[]      = u8"フレームレート制限解除";
        // ---- root/Game/Keyboard ----
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

        // メニューの木を組んだ直後に 1 回呼ぶ。項目名で引いて振る舞いを登録する。
        void    Wire(void);

        // PlayerMove.cpp（座標移動・タッチワープ）。Wire から呼ぶ。
        void    WirePlayerMove(void);
    }
}

#endif
