// ============================================================================
// GuiMenuItems — メニュー項目の木（ui-model.js の createMenuTree の写し）
// ============================================================================
//
// 項目を足す・並べ替えるときはここだけを直す。並びと文言は Simulator が正本。
// ここにある「見本」の振る舞い（無敵モード・壁抜け・連動型・トグル型アクション）は
// **ゲームメモリに触らない**。プラグイン内の変数を読み書きするだけ。

#include "GuiMenuInternal.hpp"

#include <cstdio>
#include <cstring>

namespace CTRPluginFramework
{
    namespace GuiMenu
    {
        namespace detail
        {
            namespace
            {
                char        g_names[70][kNameBytes];
                int         g_nameCount = 0;
                const char *kWeather[] = { u8"晴れ", u8"雨", u8"雪" };

                const char *MakeName(const char *prefix, int index)
                {
                    char *p = g_names[g_nameCount++];

                    std::snprintf(p, kNameBytes, "%s%02d", prefix, index + 1);
                    return p;
                }

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

                // ---- 見本の振る舞い（ゲームメモリには触らない）----
                int     s_effectIndex[2] = { -1, -1 };
                bool    s_effect[2] = { false, false };
                int     s_linkedIndex[2] = { -1, -1 };
                s32     s_linked[2] = { 1250, 0 };
                bool    s_linkedAvailable = true;
                int     s_executeCount = 0;

                int     Slot(const int *table, int index)
                {
                    return index == table[0] ? 0 : index == table[1] ? 1 : -1;
                }

                bool    SampleIsActive(int index)
                {
                    const int s = Slot(s_effectIndex, index);

                    return s >= 0 && s_effect[s];
                }

                void    SampleSetActive(int index, bool active)
                {
                    const int s = Slot(s_effectIndex, index);

                    if (s >= 0)
                        s_effect[s] = active;
                }

                bool    SampleLinkedRead(int index, s32 *value)
                {
                    const int s = Slot(s_linkedIndex, index);

                    if (s < 0 || !s_linkedAvailable)
                        return false;
                    *value = s_linked[s];
                    return true;
                }

                void    SampleLinkedWrite(int index, s32 value)
                {
                    const int s = Slot(s_linkedIndex, index);

                    if (s >= 0)
                        s_linked[s] = value;
                }

                void    SampleExecute(int index)
                {
                    (void)index;
                    s_executeCount++;
                }
            }

            void    BuildTree(void)
            {
                int i;

                g_itemCount = 0;
                g_nameCount = 0;
                for (i = 0; i < kLongList; i++)
                    g_longOpts[i] = MakeName(u8"リスト項目", i);

                // ---- 子を先に並べる（平たい配列なので連続させる）----
                const int playerFirst = g_itemCount;
                AddItem(ITEM_CHECKBOX, u8"無敵モード", u8"ダメージを受けなくなります。");
                AddItem(ITEM_CHECKBOX, u8"壁抜け", u8"当たり判定を無効にします。");
                i = AddItem(ITEM_ACTION, u8"名前を変更", u8"下画面に五十音順キーボードを開きます。");
                g_items[i].action = ACT_TEXT;
                const int playerCount = g_itemCount - playerFirst;

                const int numFirst = g_itemCount;
                i = AddItem(ITEM_VALUE, u8"所持ベル", u8"10進数で入力します。左右で100ずつ変更します。");
                SetValue(i, FMT_DEC, 1000, 0, 99999, 100);
                i = AddItem(ITEM_VALUE, u8"アイテムID", u8"16進数で入力します。左右で1ずつ変更します。");
                SetValue(i, FMT_HEX, 0x2001, 0, 0xFFFF, 1);
                // ★float は 1/10 単位（1.0 / 0.1..5.0 / 0.1 刻み）
                i = AddItem(ITEM_VALUE, u8"移動速度", u8"float値です。左右で0.1ずつ変更します。");
                SetValue(i, FMT_FLOAT, 10, 1, 50, 1);
                i = AddItem(ITEM_SLIDER, u8"通知時間", u8"下画面の横向きスライダーで変更します。");
                SetValue(i, FMT_FLOAT, 30, 10, 80, 5);
                const int numCount = g_itemCount - numFirst;

                const int uiFirst = g_itemCount;
                i = AddItem(ITEM_ACTION, u8"上画面リスト", u8"上画面へ汎用リストボックスを開きます。");
                g_items[i].action = ACT_TOP_LIST;
                i = AddItem(ITEM_ACTION, u8"下画面リスト", u8"下画面へ汎用リストボックスを開きます。");
                g_items[i].action = ACT_BOTTOM_LIST;
                i = AddItem(ITEM_ACTION, u8"文字キーボード", u8"五十音順とQWERTYを切り替えられます。");
                g_items[i].action = ACT_TEXT;
                i = AddItem(ITEM_ACTION, u8"小型文字キーボード", u8"小さい文字キーボードを開きます。");
                g_items[i].action = ACT_COMPACT_TEXT;
                i = AddItem(ITEM_LINKED_VALUE, u8"連動型数値",
                            u8"メニューを開いた時にゲームの値を取得し、適用時に設定する連動型です。");
                SetValue(i, FMT_DEC, 1250, 0, 99999, 50);
                i = AddItem(ITEM_LINKED_LIST, u8"連動型リスト",
                            u8"メニューを開いた時にゲームの値を取得し、適用時に設定する連動型です。");
                SetOptions(i, kWeather, 3);
                AddItem(ITEM_TOGGLE_ACTION, u8"トグル型アクション",
                        u8"ONにして適用すると一度実行してOFFへ戻ります。ホットキーでは確認します。");
                const int uiCount = g_itemCount - uiFirst;

                const int scrollFirst = g_itemCount;
                for (int k = 0; k < 24; k++)
                {
                    const int n = AddItem(ITEM_CHECKBOX, MakeName(u8"スクロール項目", k),
                                          u8"多数の項目を移動した時のスクロールを確認します。");

                    g_items[n].value = (k % 3 == 0) ? 1 : 0;
                    g_items[n].applied = g_items[n].value;
                }
                const int scrollCount = g_itemCount - scrollFirst;

                // ---- 根 ----
                g_rootFirst = g_itemCount;
                i = AddItem(ITEM_FOLDER, u8"プレイヤー", u8"プレイヤー関連のチートを開きます。");
                SetFolder(i, playerFirst, playerCount);
                AddItem(ITEM_CHECKBOX, u8"歩行速度アップ", u8"移動速度の変更を有効にします。");
                i = AddItem(ITEM_ACTION, u8"セーブ実行", u8"UIを変更せずセーブ関数だけを呼び出します。");
                g_items[i].action = ACT_SAVE;
                i = AddItem(ITEM_LIST, u8"天候", u8"項目の位置にリストを展開して天候を選択します。");
                SetOptions(i, kWeather, 3);
                i = AddItem(ITEM_FOLDER, u8"数値設定", u8"数値入力とスライダーのサンプルです。");
                SetFolder(i, numFirst, numCount);
                i = AddItem(ITEM_FOLDER, u8"UIテスト", u8"自前UI関数の表示サンプルです。");
                SetFolder(i, uiFirst, uiCount);
                i = AddItem(ITEM_LIST, u8"大量リスト", u8"多数のリスト項目をインライン表示してスクロールを確認します。");
                SetOptions(i, g_longOpts, kLongList);
                i = AddItem(ITEM_FOLDER, u8"スクロールテスト", u8"多数のチート項目を表示してスクロールを確認します。");
                SetFolder(i, scrollFirst, scrollCount);
                AddItem(ITEM_CHECKBOX, u8"しずえスキップ",
                        u8"しずえの会話を飛ばして村へ出ます。起動時の一括処理を先に実行します。");
                i = AddItem(ITEM_ACTION, u8"チャット漢字候補", u8"チャットの入力から漢字候補を取得し下画面に表示します。");
                g_items[i].action = ACT_CHAT_KANJI;
                for (int k = 0; k < 12; k++)
                    AddItem(ITEM_CHECKBOX, MakeName(u8"追加チート", k), u8"ルートメニューのスクロール確認用項目です。");
                g_rootCount = g_itemCount - g_rootFirst;
            }

            void    WireSamples(void)
            {
                std::memset(g_behavior, 0, sizeof(g_behavior));
                s_effectIndex[0] = s_effectIndex[1] = -1;
                s_linkedIndex[0] = s_linkedIndex[1] = -1;
                s_effect[0] = s_effect[1] = false;
                s_linked[0] = 1250;
                s_linked[1] = 0;
                s_linkedAvailable = true;
                s_executeCount = 0;
                for (int i = 0; i < g_itemCount; i++)
                {
                    const char *label = g_items[i].label;

                    if (std::strcmp(label, u8"無敵モード") == 0 || std::strcmp(label, u8"壁抜け") == 0)
                    {
                        s_effectIndex[std::strcmp(label, u8"壁抜け") == 0 ? 1 : 0] = i;
                        g_behavior[i].IsActive = SampleIsActive;
                        g_behavior[i].SetActive = SampleSetActive;
                    }
                    else if (std::strcmp(label, u8"連動型数値") == 0 || std::strcmp(label, u8"連動型リスト") == 0)
                    {
                        s_linkedIndex[std::strcmp(label, u8"連動型リスト") == 0 ? 1 : 0] = i;
                        g_behavior[i].LinkedRead = SampleLinkedRead;
                        g_behavior[i].LinkedWrite = SampleLinkedWrite;
                    }
                    else if (std::strcmp(label, u8"トグル型アクション") == 0)
                        g_behavior[i].Execute = SampleExecute;
                }
            }

            // ---- 試験用の観測口（ホストの差分試験が読む）----
            bool    SampleEffectState(int which)        { return s_effect[which]; }
            s32     SampleLinkedValue(int which)        { return s_linked[which]; }
            void    SetSampleLinkedAvailable(bool ok)   { s_linkedAvailable = ok; }
            int     SampleExecuteCount(void)            { return s_executeCount; }
        }
    }
}
