// ============================================================================
// GuiMenuItems — メニュー項目の木（gohan.md §1〜§12）
// ============================================================================
//
// 並び・項目名・説明・種別は gohan.md が正本。R1 により各フォルダはサブフォルダを先に置く。
// 振る舞いは Cheats（Sources/Cheats）が項目名で引いて登録する。
// ★設計の無いチートは項目名の末尾に「（未設計）」を付け、振る舞いを登録しない（空の項目）。
// 差分試験で Simulator と突き合わせる見本の木は tools/patches/menu_sample_items.cpp にある。

#include "GuiMenuInternal.hpp"
#include "Cheats.hpp"

#include <cstring>

namespace CTRPluginFramework
{
    namespace GuiMenu
    {
        namespace detail
        {
            namespace
            {
                using namespace Cheats;

                int     AddItem(u8 type, const char *label, const char *desc)
                {
                    Item &it = g_items[g_itemCount];

                    std::memset(&it, 0, sizeof(it));
                    it.type = type;
                    it.label = label;
                    it.desc = desc;
                    return g_itemCount++;
                }

                void    SetValue(int i, u8 fmt, s32 value, s32 minimum, s32 maximum, s32 step)
                {
                    g_items[i].fmt = fmt;
                    g_items[i].value = value;
                    g_items[i].applied = value;
                    g_items[i].minimum = minimum;
                    g_items[i].maximum = maximum;
                    g_items[i].step = step;
                }

                void    SetOptions(int i, const char *const *options, int count)
                {
                    g_items[i].options = options;
                    g_items[i].optionCount = (u8)count;
                }

                void    SetFolder(int i, int first, int count)
                {
                    g_items[i].childFirst = (u8)first;
                    g_items[i].childCount = (u8)count;
                }

                // 既定ホットキー（gohan.md §13）。初期値として持つ。
                void    SetHotkey(int i, u16 mask)
                {
                    g_items[i].hotkey = mask;
                    g_items[i].appliedHotkey = mask;
                }

                u16     Bit(int b)
                {
                    return (u16)(1u << b);
                }

                const char kUndecided[] = u8"説明は未定です。";
            }

            void    BuildTree(void)
            {
                int i;

                g_itemCount = 0;
                // g_longOpts は見本の汎用リスト用。この木では使わないが空にしておく
                for (i = 0; i < kLongList; i++)
                    g_longOpts[i] = "";

                // ---- root/プレイヤー/資産（§4）----
                const int resFirst = g_itemCount;
                i = AddItem(ITEM_LINKED_VALUE, kWallet, u8"所持金を変更します。");
                SetValue(i, FMT_DEC, 0, 0, 99999, 1);
                i = AddItem(ITEM_LINKED_VALUE, kBank, u8"ATMの貯金を変更します。");
                SetValue(i, FMT_DEC, 0, 0, 999999999, 1);
                i = AddItem(ITEM_LINKED_VALUE, kCoupons, u8"ふるさとチケットを変更します。");
                SetValue(i, FMT_DEC, 0, 0, 9999, 1);
                i = AddItem(ITEM_LINKED_VALUE, kMedals, u8"オン島のメダルを変更します。");
                SetValue(i, FMT_DEC, 0, 0, 9999, 1);
                const int resCount = g_itemCount - resFirst;

                // ---- root/プレイヤー/スタイル（§5）----
                const int styleFirst = g_itemCount;
                AddItem(ITEM_ACTION, u8"スタイルを変更（未設計）", kUndecided);
                AddItem(ITEM_CHECKBOX, kNoBedHead, u8"日にちを空けてログインしても寝癖が付かなくなります。");
                const int styleCount = g_itemCount - styleFirst;

                // ---- root/プレイヤー/座標移動（§3）----
                const int moveFirst = g_itemCount;
                i = AddItem(ITEM_CHECKBOX, kCoordMove, u8"移動キーで高速移動できます。");
                SetHotkey(i, Bit(HB_A));
                i = AddItem(ITEM_LIST, kCoordMoveKey, u8"座標移動に使うキーを指定できます。");
                SetOptions(i, kMoveKeyOptions, kMoveKeyOptionCount);
                // ★float は 1/10 単位（既定 5.0 / 範囲 0.1..99.9 / 十字キーで 0.5 ずつ）
                i = AddItem(ITEM_VALUE, kCoordMoveSpeed, u8"座標移動の移動量を設定できます。");
                SetValue(i, FMT_FLOAT, 50, 1, 999, 5);
                i = AddItem(ITEM_LIST, kCoordMoveMode,
                            u8"座標移動の移動方法を変更できます。グリッド単位の時、スライドパッドによる向きは8方向に限定されます。");
                SetOptions(i, kMoveModeOptions, kMoveModeOptionCount);
                i = AddItem(ITEM_LIST, kCoordMoveDpad,
                            u8"座標移動中にすると、移動キーが十字キーの時、ホットキーを押している間はゲームの十字キーを無効にします。");
                SetOptions(i, kMoveDpadOptions, kMoveDpadOptionCount);
                const int moveCount = g_itemCount - moveFirst;

                // ---- root/プレイヤー（§3）----
                const int playerFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"資産", u8"所持金などの数値を変更します。");
                SetFolder(i, resFirst, resCount);
                i = AddItem(ITEM_FOLDER, u8"スタイル", u8"プレイヤーの見た目に関するチートです。");
                SetFolder(i, styleFirst, styleCount);
                i = AddItem(ITEM_FOLDER, u8"座標移動", u8"座標移動とその設定です。");
                SetFolder(i, moveFirst, moveCount);
                AddItem(ITEM_CHECKBOX, kTouchWarp, u8"地図をタッチした場所にワープします。");
                i = AddItem(ITEM_CHECKBOX, kWalkThroughWalls,
                            u8"どこでも歩けるようになります。あなたの歩みを阻むものは何一つとして存在しません。");
                SetHotkey(i, Bit(HB_L) | Bit(HB_UP));
                i = AddItem(ITEM_CHECKBOX, u8"アクション解除（未設計）",
                            u8"強制的に立ち状態に変更します。身動きが取れなくなった時用です。");
                SetHotkey(i, Bit(HB_L) | Bit(HB_DOWN));
                AddItem(ITEM_TOGGLE_ACTION, u8"気絶（未設計）", u8"気絶します。身動きが取れなくなった時用です。");
                AddItem(ITEM_CHECKBOX, kNoBreakFlower,
                        u8"もう友達の村の移動に気を遣う必要はなくなりました。花の上を走っても散らなくなります。");
                AddItem(ITEM_CHECKBOX, kNoTrap, u8"穴にハマらなくなります。残念でしたね。");
                const int playerCount = g_itemCount - playerFirst;

                // ---- root/アイテム/ドロップ/お遊び（§8）----
                const int funFirst = g_itemCount;
                AddItem(ITEM_CHECKBOX, kFellTree, u8"木や竹にぶつかると切り倒されます。");
                AddItem(ITEM_CHECKBOX, kDig3x3, u8"[穴に落下しない] [どこでも掘れる] と併せて使うと面白いかもしれません。");
                const int funCount = g_itemCount - funFirst;

                // ---- root/アイテム/ドロップ（§7）----
                const int dropFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"お遊び", u8"遊び向けのチートです。");
                SetFolder(i, funFirst, funCount);
                AddItem(ITEM_CHECKBOX, u8"アイテムセレクター（未設計）", kUndecided);
                i = AddItem(ITEM_VALUE, kDropItem, u8"ドロップ系チートが参照するアイテムIDを指定します。");
                SetValue(i, FMT_HEX, 0, 0, 0xFFFF, 1);
                i = AddItem(ITEM_CHECKBOX, kTrampler, u8"あなたが歩いてきた場所に何かが残る事は決してありません。");
                SetHotkey(i, Bit(HB_R) | Bit(HB_UP));
                AddItem(ITEM_CHECKBOX, kDigAnywhere,
                        u8"木/岩やアイテムから、川/崖越し/建造物まで、ありとあらゆる状況で掘れるようになります。");
                const int dropCount = g_itemCount - dropFirst;

                // ---- root/アイテム（§6）----
                const int itemFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"ドロップ", u8"アイテムを置く・消す・掘るチートです。");
                SetFolder(i, dropFirst, dropCount);
                AddItem(ITEM_ACTION, u8"ポケットアイテム（未設計）",
                        u8"入力したIDのアイテムを持ち物の空いているスロットに入れます。");
                const int itemCount = g_itemCount - itemFirst;

                // ---- root/村/マップ（§9.5）----
                const int mapFirst = g_itemCount;
                AddItem(ITEM_ACTION, u8"選択中アイコンに部屋移動（未設計）",
                        u8"地図上で選択したアイコンのroomへワープします。");
                AddItem(ITEM_ACTION, u8"選択した住民への操作（未設計）",
                        u8"地図上で選択した家主の住民に対して様々な操作をします。");
                AddItem(ITEM_TOGGLE_ACTION, u8"選択中プレイヤーに切り替え（未設計）",
                        u8"もうわざわざタイトル画面へ戻る必要はありません。地図上で選択したプレイヤーのデータへ切り替えます。");
                const int mapCount = g_itemCount - mapFirst;

                // ---- root/村/公共事業エディターの設定（§9）----
                const int editorSetFirst = g_itemCount;
                i = AddItem(ITEM_VALUE, kHlTint, u8"移動で選んだ建物に重ねる青の濃さです。0 で元の色、255 で青一色。削除の赤も同じ濃さです。");
                SetValue(i, FMT_DEC, 176, 0, 255, 8);
                i = AddItem(ITEM_VALUE, kHlAlpha, u8"選んだ建物の、揺れの中心の不透明度です。255 で不透明。");
                SetValue(i, FMT_DEC, 208, 0, 255, 8);
                i = AddItem(ITEM_VALUE, kHlWave, u8"選んだ建物の不透明度が sin 波で上下する幅です。0 で揺れません。");
                SetValue(i, FMT_DEC, 64, 0, 255, 8);
                i = AddItem(ITEM_VALUE, kHlSpeed, u8"選んだ建物の揺れの速さです。1 フレームに周期の 1/1000 ずつ進みます。");
                SetValue(i, FMT_DEC, 11, 1, 100, 1);
                i = AddItem(ITEM_VALUE, kBpAlpha, u8"設置プレビューの、揺れの中心の不透明度です。");
                SetValue(i, FMT_DEC, 160, 0, 255, 8);
                i = AddItem(ITEM_VALUE, kBpWave, u8"設置プレビューの不透明度が sin 波で上下する幅です。");
                SetValue(i, FMT_DEC, 64, 0, 255, 8);
                i = AddItem(ITEM_VALUE, kBpSpeed, u8"設置プレビューの揺れの速さです。1 フレームに周期の 1/1000 ずつ進みます。");
                SetValue(i, FMT_DEC, 11, 1, 100, 1);
                const int editorSetCount = g_itemCount - editorSetFirst;

                // ---- root/村（§9）----
                const int townFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"マップ", u8"地図で選んだものに対する操作です。");
                SetFolder(i, mapFirst, mapCount);
                i = AddItem(ITEM_FOLDER, u8"公共事業エディターの設定", u8"公共事業エディターの選択と設置プレビューの見た目です。");
                SetFolder(i, editorSetFirst, editorSetCount);
                AddItem(ITEM_CHECKBOX, u8"アイテムが消えない（未設計）", u8"無効な位置にあるアイテムが翌日に消えないようにします。");
                i = AddItem(ITEM_LINKED_LIST, kWeather, u8"天気を変更します。");
                SetOptions(i, kWeatherOptions, kWeatherOptionCount);
                AddItem(ITEM_CHECKBOX, kBeOn,
                        u8"好きな場所に建造物を建てましょう。カメラだけを動かして置く・動かす・消すができ、プレイヤーは動けません。"
                        u8"スライドパッドでカーソル、L/R でモード、十字左右で建物、X でカーソルの建物をコピー、"
                        u8"A で実行、移動の選択は B で解除。");
                AddItem(ITEM_CHECKBOX, kNoLookUp, u8"あなたは勝手に空を見上げて呆けることはありません。");
                const int townCount = g_itemCount - townFirst;

                // ---- root/ゲーム/キーボード（§12）----
                const int keyboardFirst = g_itemCount;
                AddItem(ITEM_CHECKBOX, u8"キーボード制限解除（未設計）",
                        u8"文字数制限、改行、無効化されたキーの有効化など、キーボードに関する制限を撤去します。");
                // ★現行の実装はアクション（チャットの入力から候補を取って下画面に出す）
                i = AddItem(ITEM_ACTION, kKanji, u8"チャットの入力から漢字候補を取得し下画面に表示します。");
                g_items[i].action = ACT_CHAT_KANJI;
                const int keyboardCount = g_itemCount - keyboardFirst;

                // ---- root/ゲーム/店（§12.5）----
                const int shopFirst = g_itemCount;
                AddItem(ITEM_CHECKBOX, kShopsOpen, u8"全てのお店は常にあなたのために回り続けます。");
                i = AddItem(ITEM_LINKED_VALUE, kTurnips, u8"カブ価を変更します。");
                SetValue(i, FMT_DEC, 0, 0, 99999, 1);
                const int shopCount = g_itemCount - shopFirst;

                // ---- root/ゲーム（§11）----
                const int gameFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"キーボード", u8"キーボードとチャットに関するチートです。");
                SetFolder(i, keyboardFirst, keyboardCount);
                i = AddItem(ITEM_FOLDER, u8"店", u8"お店に関するチートです。");
                SetFolder(i, shopFirst, shopCount);
                AddItem(ITEM_CHECKBOX, kInstantText,
                        u8"メッセージの字送りを即時に完了させます。しずえやかっぺいの面倒な話を聞く必要はもうありません。");
                AddItem(ITEM_CHECKBOX, kShizueSkip, u8"タイトル遷移後のしずえの会話をスキップします。ドパガキなあなたへ。");
                i = AddItem(ITEM_CHECKBOX, kUnlockFps,
                            u8"フレームレート制限を取っ払うことで、事実上ゲームの進行速度を上げます。");
                SetHotkey(i, Bit(HB_B) | Bit(HB_UP));
                const int gameCount = g_itemCount - gameFirst;

                // ---- root/テスト/UnitCursor（§18）----
                // 解析で確かめたモデル描画を実機で試すための場所。確かめ終わったものから本来のフォルダへ移す。
                const int unitCursorFirst = g_itemCount;
                AddItem(ITEM_CHECKBOX, kGridCursor,
                        u8"村の地面にグリッドカーソルを出します。プレイヤーの足元が基点です。");
                i = AddItem(ITEM_VALUE, kGridCursorCols,
                            u8"横に並べるマスの数です。1 マスにつき 1 体作ります。");
                SetValue(i, FMT_DEC, 1, 1, 8, 1);
                i = AddItem(ITEM_VALUE, kGridCursorRows,
                            u8"縦に並べるマスの数です。横×縦が 64 を超えない範囲で選べます。");
                SetValue(i, FMT_DEC, 1, 1, 8, 1);
                i = AddItem(ITEM_VALUE, kGridCursorTile,
                            u8"マスの間隔です。十字キー 1 回で動く量でもあります。村の 1 マスは 32 です。");
                SetValue(i, FMT_DEC, 32, 1, 400, 1);
                i = AddItem(ITEM_VALUE, kGridCursorScale,
                            u8"カーソル自身の拡大率です。間隔とは別に決められます。百分率。");
                SetValue(i, FMT_DEC, 100, 5, 1000, 5);
                AddItem(ITEM_CHECKBOX, kGridCursorSnap,
                        u8"基点を間隔のマス目へ丸めます。切るとプレイヤーの足元そのままになります。");
                AddItem(ITEM_CHECKBOX, kGridCursorDiag,
                        u8"縞模様を斜め 45 度にします。家の模様替えで出る向きと同じものです。大きさは変わりません。");
                AddItem(ITEM_CHECKBOX, kGridCursorMove,
                        u8"有効な間、十字キーでカーソルを 1 マスずつ動かします。プレイヤーは歩きません。");
                AddItem(ITEM_ACTION, kGridCursorStat,
                        u8"グリッドカーソルがいまどこで止まっているかを通知で出します。");
                const int unitCursorCount = g_itemCount - unitCursorFirst;

                // ---- root/テスト/モデル（§18）----
                const int modelFirst = g_itemCount;
                AddItem(ITEM_ACTION, kMvBuild,
                        u8"ゲームの RomFS を歩いて .bcres の一覧を作ります。最初に 1 回だけ。");
                i = AddItem(ITEM_VALUE, kMvScroll,
                            u8"一覧のどこを見るかです。項目は 200 件ずつしか持てないので窓をずらします。");
                SetValue(i, FMT_DEC, 0, 0, 20000, 200);
                i = AddItem(ITEM_LIST, kMvPick,
                            u8"出す .bcres です。選ぶとそのファイルに合わせて資源ヒープを取り直します。");
                SetOptions(i, kMvEmptyOptions, 1);
                AddItem(ITEM_CHECKBOX, kMvShow,
                        u8"選んだモデルをプレイヤーの足元へ 1 体出します。");
                i = AddItem(ITEM_VALUE, kMvIndex,
                            u8"1 つの .bcres にモデルが複数入っているときの番号です。");
                SetValue(i, FMT_DEC, 0, 0, 63, 1);
                i = AddItem(ITEM_VALUE, kMvScale,
                            u8"モデルの拡大率です。百分率。");
                SetValue(i, FMT_DEC, 100, 1, 2000, 5);
                i = AddItem(ITEM_VALUE, kMvX, u8"プレイヤーからの X ずらしです。world 単位。");
                SetValue(i, FMT_DEC, 0, -500, 500, 1);
                i = AddItem(ITEM_VALUE, kMvY, u8"プレイヤーからの Y ずらしです。上が正です。");
                SetValue(i, FMT_DEC, 0, -500, 500, 1);
                i = AddItem(ITEM_VALUE, kMvZ, u8"プレイヤーからの Z ずらしです。");
                SetValue(i, FMT_DEC, 0, -500, 500, 1);
                AddItem(ITEM_ACTION, kMvStat,
                        u8"モデルビューアがどこで止まっているかを通知で出します。");
                const int modelCount = g_itemCount - modelFirst;

                // ---- root/テスト/プレイヤー複製（試験）----
                const int cloneFirst = g_itemCount;
                AddItem(ITEM_CHECKBOX, kPcShow,
                        u8"自分と同じ見た目の複製を 1 体、プレイヤーの右隣に出します。服・帽子・アクセはそのままです。");
                i = AddItem(ITEM_VALUE, kPcHair,
                            u8"複製だけの髪型です。-1 で本物のまま。0〜16 は男の子、17〜33 は女の子の髪型です。");
                SetValue(i, FMT_DEC, -1, -1, 33, 1);
                i = AddItem(ITEM_VALUE, kPcColor,
                            u8"複製だけの髪色です。-1 で本物のまま。0〜15。");
                SetValue(i, FMT_DEC, -1, -1, 15, 1);
                AddItem(ITEM_CHECKBOX, kPcScreen,
                        u8"複製を世界ではなく上画面の決まった位置に、専用のカメラで描きます。");
                i = AddItem(ITEM_VALUE, kPcYaw, u8"画面に固定したときの複製の向きです。度。");
                SetValue(i, FMT_DEC, 0, -180, 180, 15);
                i = AddItem(ITEM_VALUE, kPcPitch, u8"画面に固定したときの複製の傾き（前後）です。度。正で頭が手前へ。");
                SetValue(i, FMT_DEC, 10, -90, 90, 5);
                i = AddItem(ITEM_VALUE, kPcX, u8"複製を出す位置（上画面の横、0〜400）です。");
                SetValue(i, FMT_DEC, 330, 0, 400, 5);
                i = AddItem(ITEM_VALUE, kPcY, u8"複製を出す位置（上画面の縦、0〜240）です。");
                SetValue(i, FMT_DEC, 150, 0, 240, 5);
                i = AddItem(ITEM_VALUE, kPcZoom, u8"画面に固定したときの複製の大きさです。百分率。");
                SetValue(i, FMT_DEC, 60, 10, 300, 5);
                AddItem(ITEM_ACTION, kPcStat,
                        u8"複製がいまどこで止まっているかを通知で出します。");
                const int cloneCount = g_itemCount - cloneFirst;

                // ---- root/テスト（§18）----
                const int testFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"UnitCursor", u8"ゲームの UnitCursor（模様替えのマス）を村の地面に出す試験です。");
                SetFolder(i, unitCursorFirst, unitCursorCount);
                i = AddItem(ITEM_FOLDER, u8"モデル", u8"RomFS の .bcres を選んで出すモデルビューアです。");
                SetFolder(i, modelFirst, modelCount);
                i = AddItem(ITEM_FOLDER, u8"プレイヤー複製", u8"自分のプレイヤーの複製を出し、髪型と髪色だけを変える試験です。");
                SetFolder(i, cloneFirst, cloneCount);
                const int testCount = g_itemCount - testFirst;

                // ---- root（§2）----
                g_rootFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"プレイヤー", u8"プレイヤーに関するチートです。");
                SetFolder(i, playerFirst, playerCount);
                i = AddItem(ITEM_FOLDER, u8"アイテム", u8"アイテムに関するチートです。");
                SetFolder(i, itemFirst, itemCount);
                i = AddItem(ITEM_FOLDER, u8"村", u8"村に関するチートです。");
                SetFolder(i, townFirst, townCount);
                i = AddItem(ITEM_FOLDER, u8"ゲーム", u8"ゲーム全体に関するチートです。");
                SetFolder(i, gameFirst, gameCount);
                i = AddItem(ITEM_FOLDER, u8"テスト", u8"解析の試験用です。確かめ終わったら本来の場所へ移します。");
                SetFolder(i, testFirst, testCount);
                i = AddItem(ITEM_CHECKBOX, u8"チャットコマンド（未設計）",
                            u8"チャットにコマンドを打つ事で様々なチートを素早く実行できます。コマンドリストは list で確認できます。");
                SetHotkey(i, Bit(HB_R) | Bit(HB_B));
                g_rootCount = g_itemCount - g_rootFirst;
            }

            void    WireBehaviors(void)
            {
                std::memset(g_behavior, 0, sizeof(g_behavior));
                Cheats::Wire();
            }
        }
    }
}
