#ifndef GUIMENU_HPP
#define GUIMENU_HPP

#include <CTRPluginFramework.hpp>

namespace CTRPluginFramework
{
    // 自前チートメニュー（gohan-gui-simulator の MENU を移植）。
    // 描画は GuiRenderer。開いている間もゲームは止まらない。
    // 項目の挙動の仕様は gohan-menu.md。
    namespace GuiMenu
    {
        bool    Initialize(void);
        void    Shutdown(void);
        void    Redraw(void);

        // PluginMenu::OnOpening に挿す。
        //   Select 単独        -> 自前メニューの開閉（false を返して CTRPF を開かせない）
        //   Select + 十字上    -> 従来の CTRPF メニュー（true を返す）
        bool    OnOpening(void);

        bool    IsOpen(void);
        void    Open(void);
        void    Close(void);

        // 通知を積む。上画面右下へ出す。別スレッドから呼べる。
        void    Notify(const char *title, const char *message);
        // 赤い通知を積む（無効時など）。別スレッドから呼べる。
        void    NotifyRed(const char *title, const char *message);

        // ホットキーマスクの bit（表示順＝Simulator の HOTKEY_BUTTON_ORDER）。
        enum HotkeyBit
        {
            HB_ZL = 0, HB_L, HB_R, HB_ZR,
            HB_UP, HB_DOWN, HB_LEFT, HB_RIGHT,
            HB_A, HB_B, HB_X, HB_Y, HB_SELECT, HB_START,
            HB_COUNT = 14
        };

        // ================================================================
        // 関数側の登録。いずれもメニュースレッドから呼ばれる。短く・止めないこと。
        // 登録は Unregister / Shutdown まで残る。項目番号は FindItem で引く。
        // ================================================================
        int     FindItem(const char *label);        // 見つからなければ -1

        // (a) チェック項目の効果（gohan-menu.md §4.3 の規則で駆動する）
        //   項目 ON を適用: ホットキー束縛なし -> SetActive(true) / 束縛あり -> アームのみ
        //   項目 OFF を適用: 効果が残っていれば必ず SetActive(false)
        //   ホットキー押下（項目 ON のときだけ）: IsActive() を反転
        //   ホットキー設定だけの適用では効果に触れない。
        //   ★既定値は必ず明示値にする。初回書込値の自動保持はしない。
        //   SetActive 内でメモリを書いたら FlushMemory を呼ぶこと。
        struct ToggleEffectFuncs
        {
            bool (*IsActive)(int index);
            void (*SetActive)(int index, bool active);
        };
        bool    RegisterToggleEffect(int index, const ToggleEffectFuncs *funcs);
        void    UnregisterToggleEffect(int index);
        bool    ToggleEffectActive(int index);       // 未登録は false

        // (b) 連動型（数値／リスト）。メニューを開いた瞬間に Read を 1 回呼ぶ。
        //   false を返すとその項目は編集不可になる（カーソルは置ける）。適用で値が変わったら Write を呼ぶ。
        //   書込み後に再構築が要るものは Write の中で面倒を見る。
        typedef bool (*LinkedRead)(int index, s32 *value);
        typedef void (*LinkedWrite)(int index, s32 value);
        bool    RegisterLinked(int index, LinkedRead read, LinkedWrite write);

        // (c) 実行。アクション項目は決定／ホットキーで、トグル型アクションは
        //   ON を適用したとき／ホットキーの確認を承諾したときに 1 回呼ぶ。
        typedef void (*ExecuteFunc)(int index);
        bool    RegisterExecute(int index, ExecuteFunc execute);

        // (d) 数値・スライダー・リストの値を適用したとき（値が変わったときだけ）。
        //   「変数へ代入」型は登録せず ItemApplied() を読むだけでもよい。
        typedef void (*ApplyFunc)(int index, s32 value);
        bool    RegisterApply(int index, ApplyFunc apply);

        // (f) 無効状態。登録した項目は毎フレーム（メニュースレッド）この関数で disabled を決める。
        //   無効な項目はカーソルを置けるが、値の変更・決定・ホットキー設定はできない。
        typedef bool (*DisabledFunc)(int index);
        bool    RegisterDisabled(int index, DisabledFunc isDisabled);

        // 書換後のキャッシュ処理（データ＋命令）。
        void    FlushMemory(u32 address, u32 size);

        // 複数パッチの定型処理。効果記述は関数側が持つ。
        //   static const TogglePatch kWall[] = { { アドレス, 4, 有効値, 既定値 }, ... };
        //   IsActive → PatchListIsActive(kWall, n) / SetActive → PatchListSetActive(kWall, n, active)
        //   全て有効値なら真。不正な要素（番地 0・大きさ不正）は IsActive＝偽・SetActive＝飛ばす。
        struct TogglePatch
        {
            u32     address;    // 実行時 VA（整列していること）
            u8      size;       // 1/2/4
            u32     onValue;    // 有効値
            u32     offValue;   // 既定値（明示値。自動保持はしない）
        };
        bool    PatchListIsActive(const TogglePatch *patches, int count);
        void    PatchListSetActive(const TogglePatch *patches, int count, bool active);

        // メニューや入力 UI が表示されているか。下画面パッド等を直接読む効果は、
        // 表示中は動かさないこと（OnTick の held は表示中 0 だが直接読みは素通し）。
        bool    IsVisible(void);
        // このフレームだけゲーム側の十字キーを無効にする。OnTick から毎フレーム呼び続けている間だけ効く
        // （呼ばなくなった次のフレームで戻る）。CTRPF 側の入力（held）は影響を受けない。
        void    BlockGameDpad(void);
        // このフレームだけゲーム側のタッチを無効にする。OnTick から毎フレーム呼び続けている間だけ効く。
        void    BlockGameTouch(void);
        // このフレームだけゲーム側の入力をスライドパッドも含めて全部無効にする（建物エディター）。
        // メニューの表示中も優先する。OnTick から毎フレーム呼び続けている間だけ効く。
        void    BlockGameAll(void);
        int     ItemCount(void);
        const char *ItemLabel(int index);
        int     ItemValue(int index);
        int     ItemApplied(int index);
        // 値と適用値をまとめて反映する（通知なし。黄色表示も付かない）。非推奨。
        void    SetItemApplied(int index, int value);
        // リスト項目の選択肢を実行時に入れ替える。options の寿命は呼び側が持つ。
        // count は 255 まで（項目の optionCount が u8）。
        void    SetItemOptions(int index, const char *const *options, int count);
        u16     ItemHotkey(int index);
        u16     ItemAppliedHotkey(int index);
        // 束縛の表示（"L+UP" / "なし"。buf へ書く）。
        const char *FormatItemHotkey(int index, char *buf, unsigned int cap);

        // (e) 毎フレームの駆動（チェック項目の適用値で駆動する）。
        //   OnEnable: 無効→有効に変わったフレームに 1 回 / OnDisable: 有効→無効で 1 回
        //   OnTick: 有効な間は毎フレーム。held は HotkeyBit マスクの水準で、
        //     メニュー・入力 UI・確認ダイアログの表示中は 0。
        //   押下中実行型（座標移動）の例:
        //     static void MoveTick(int index, u16 held) {
        //         u16 hk = GuiMenu::ItemAppliedHotkey(index);
        //         if (hk == 0 || (held & hk) != hk) return;   // 未押下
        //         ...座標へ加算する
        //     }
        struct ToggleHandlers
        {
            void (*OnEnable)(int index);
            void (*OnTick)(int index, u16 heldMask);
            void (*OnDisable)(int index);
        };
        void    SetToggleHandlers(const ToggleHandlers *handlers);
    }
}

#endif
