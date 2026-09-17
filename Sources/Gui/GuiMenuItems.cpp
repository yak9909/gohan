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
                i = AddItem(ITEM_LINKED_VALUE, kTurnips, u8"カブを変更します。");
                SetValue(i, FMT_DEC, 0, 0, 99999, 1);
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
                // ★float は 1/10 単位（2.0 / 0.1..99.9 / 0.1 刻み）
                i = AddItem(ITEM_VALUE, kCoordMoveSpeed, u8"座標移動の移動量を設定できます。");
                SetValue(i, FMT_FLOAT, 20, 1, 999, 1);
                i = AddItem(ITEM_LIST, kCoordMoveMode,
                            u8"座標移動の移動方法を変更できます。グリッド単位の時、スライドパッドによる向きは8方向に限定されます。");
                SetOptions(i, kMoveModeOptions, kMoveModeOptionCount);
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
                AddItem(ITEM_CHECKBOX, u8"選択したプレイヤーのデータに切り替える（未設計）",
                        u8"もうわざわざタイトル画面へ戻る必要はありません。地図上で選択したプレイヤーのデータへ切り替えます。");
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

                // ---- root/村（§9）----
                const int townFirst = g_itemCount;
                AddItem(ITEM_CHECKBOX, u8"アイテムが消えない（未設計）", u8"無効な位置にあるアイテムが翌日に消えないようにします。");
                i = AddItem(ITEM_LINKED_LIST, kWeather, u8"天気を変更します。");
                SetOptions(i, kWeatherOptions, kWeatherOptionCount);
                AddItem(ITEM_ACTION, u8"選択したマップアイコンのroomに移動（未設計）",
                        u8"地図上で選択したアイコンのroomへワープします。");
                AddItem(ITEM_ACTION, u8"選択した住民への操作（未設計）",
                        u8"地図上で選択した家主の住民に対して様々な操作をします。");
                AddItem(ITEM_ACTION, u8"公共事業エディター（未設計）", u8"好きな場所に建造物を建てましょう。");
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

                // ---- root/ゲーム（§11）----
                const int gameFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"キーボード", u8"キーボードとチャットに関するチートです。");
                SetFolder(i, keyboardFirst, keyboardCount);
                AddItem(ITEM_CHECKBOX, kInstantText,
                        u8"メッセージの字送りを即時に完了させます。しずえやかっぺいの面倒な話を聞く必要はもうありません。");
                AddItem(ITEM_CHECKBOX, kShopsOpen, u8"全てのお店は常にあなたのために回り続けます。");
                AddItem(ITEM_CHECKBOX, kShizueSkip, u8"タイトル遷移後のしずえの会話をスキップします。ドパガキなあなたへ。");
                i = AddItem(ITEM_CHECKBOX, kUnlockFps,
                            u8"フレームレート制限を取っ払うことで、事実上ゲームの進行速度を上げます。");
                SetHotkey(i, Bit(HB_B) | Bit(HB_UP));
                const int gameCount = g_itemCount - gameFirst;

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
