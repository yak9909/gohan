#ifndef GUIMENU_HPP
#define GUIMENU_HPP

#include <CTRPluginFramework.hpp>

namespace CTRPluginFramework
{
    // 自前チートメニュー（CTRPF-GUI-Simulator の MENU を最小構成で移植）。
    // 描画は GuiV2（基準仕様 v2）。開いている間もゲームは止まらない。
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

        // 通知を積む（段 7）。上画面右下へ出す。別スレッドから呼べる。
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

        // 関数側連携。トグル実行を利用者が持つ場合に使う。
        //   SetHotkeyHandler で登録すると、有効中のチェック項目のホットキー
        //   発動時に handler(項目番号) を呼ぶ。無効中の押下・メニュー開閉中は
        //   呼ばない。メニューは有効無効を切り替えない。
        //   handler はメニュースレッドから呼ばれる。短く・止めないこと。
        //   項目番号は BuildTree から変わらない。状態の読み書きは単語単位。
        typedef void (*HotkeyHandler)(int index);
        void    SetHotkeyHandler(HotkeyHandler handler);

        // トグル実行エンジン。効果の実体（アドレス・有効値・既定値）は
        // 関数側が持つ。登録するのは振る舞いでありデータではない。
        //   壁抜けの例： IsActive＝壁抜け値が書かれているか読む、
        //               SetActive＝有効値／既定値を書く。
        //   ★既定値は必ず明示値にする。初回書込値の自動保持はしない（禁止）。
        //   メニューで ON（アーム）＋閉鎖中のホットキー押下で：
        //     IsActive() が真なら SetActive(false)＋無効通知（赤）、
        //     偽なら SetActive(true)＋有効通知を出す。題はチート名。
        //   メニューを OFF にしても効果には触らない（残る）。
        //   メニュー開閉中の押下は発火自体が走らない。
        //   SetActive 内でメモリを書いたら FlushMemory を呼ぶこと。
        //   特殊な効果は HotkeyHandler / OnTick で自作する（逃げ道）。
        //   登録は Unregister まで残る（メニューの開閉・再有効化をまたぐ）。
        //   いずれもメニュースレッドから呼ばれる。短く・止めないこと。
        struct ToggleEffectFuncs
        {
            bool (*IsActive)(int index);
            void (*SetActive)(int index, bool active);
        };
        bool    RegisterToggleEffect(int index, const ToggleEffectFuncs *funcs);
        void    UnregisterToggleEffect(int index);
        // 効果が書かれているか（未登録は false）。
        bool    ToggleEffectActive(int index);
        // メニューON/OFF でホットキー判定と同じ反転評価を送るか（既定 OFF）。
        //   ON（適用値 0→1）も OFF（1→0）も、ホットキー押下時と同じ
        //   ToggleEvaluate（読取検証→反転→通知）が走る。束縛の有無は問わない。
        //   ホットキーを持たない項目をメニューだけで駆動するときに使う。
        //   既定 OFF のままならメニュー操作はアームだけで効果に触らない。
        //   送らないようにもできる（false に戻す）。
        void    SetEffectMenuJudgment(int index, bool send);
        // 書換後のキャッシュ処理（データ＋命令）。自作効果からも使える。
        void    FlushMemory(u32 address, u32 size);

        // 複数パッチの定型処理。効果記述は関数側が持つ。
        //   壁抜けの例： static const TogglePatch kWall[] = {
        //                    { アドレス, 4, 壁抜け値, 既定値 }, ...
        //                };
        //                IsActive → PatchListIsActive(kWall, 2);
        //                SetActive → PatchListSetActive(kWall, 2, active);
        //   全て有効値なら真、1 つでも違えば偽。既定値は必ず明示値。
        //   不正な要素（番地 0・大きさ不正）は IsActive＝偽・SetActive＝飛ばす。
        struct TogglePatch
        {
            u32     address;    // 実行時 VA（整列していること）
            u8      size;       // 1/2/4
            u32     onValue;    // 有効値
            u32     offValue;   // 既定値（明示値。自動保持はしない）
        };
        bool    PatchListIsActive(const TogglePatch *patches, int count);
        void    PatchListSetActive(const TogglePatch *patches, int count,
                                   bool active);

        // メニューが表示されているか。下画面パッド等を直接読む効果は、
        // 開いている間は動かさないこと（OnTick の held は開閉中 0 だが、
        // 直接読みは素通しになるため）。別スレッドから呼べる。
        bool    IsVisible(void);
        int     ItemCount(void);
        const char *ItemLabel(int index);
        int     ItemValue(int index);
        int     ItemApplied(int index);
        // 値と適用値をまとめて反映する（通知なし。黄色表示も付かない）。
        void    SetItemApplied(int index, int value);
        u16     ItemHotkey(int index);
        u16     ItemAppliedHotkey(int index);
        // 束縛の表示（"L+UP" / "なし"。buf へ書く）。
        const char *FormatItemHotkey(int index, char *buf, unsigned int cap);

        // トグル実行の関数側連携。チェック項目の有効状態で駆動する。
        //   OnEnable: 無効→有効に変わったフレームに 1 回だけ呼ばれる。
        //   OnTick: 有効な間は毎フレーム（約16ms）呼ばれる。
        //     held は HotkeyBit マスクの水準。メニューを開いている間は 0。
        //     ON/OFF の振る舞い（ホットキー判定・誤発防止）は利用者がここに書く。
        //     有効なチートだけ呼ばれるので、不要なチートを無効にすれば
        //     他のチートの暴発を防げる。
        //   OnDisable: 有効→無効に変わったフレームに 1 回だけ呼ばれる。
        //   いずれもメニュースレッドから呼ばれる。短く・止めないこと。
        //   状態変更は次フレームに反映される。未登録（nullptr）は呼ばれない。
        //
        // 押し続け型の例（座標移動。方式はリスト項目の値で選ぶ）：
        //   static void MoveTick(int index, u16 held) {
        //       u16 hk = GuiMenu::ItemAppliedHotkey(index);
        //       if (hk == 0 || (held & hk) != hk) return;  // 未押下
        //       int method = GuiMenu::ItemValue(kMethodItem);  // 0:十字/1:C/2:パッド
        //       int dx = 0, dy = 0;
        //       if (method == 0) {
        //           if (held & (1u << HB_LEFT)) dx -= 1;
        //           if (held & (1u << HB_RIGHT)) dx += 1;
        //           if (held & (1u << HB_UP)) dy += 1;
        //           if (held & (1u << HB_DOWN)) dy -= 1;
        //       } else if (method == 1) {
        //           if (GuiMenu::IsVisible()) return;  // 直接読みは自前で閉塞
        //           u32 k = hidKeysHeld();  // KEY_CSTICK_*（libctru の方向 bit）
        //           if (k & KEY_CSTICK_LEFT) dx -= 1;
        //           if (k & KEY_CSTICK_RIGHT) dx += 1;
        //           if (k & KEY_CSTICK_UP) dy += 1;
        //           if (k & KEY_CSTICK_DOWN) dy -= 1;
        //       } else {
        //           if (GuiMenu::IsVisible()) return;
        //           circlePosition pos; hidCircleRead(&pos);  // libctru
        //           dx = pos.dx / 48; dy = -(pos.dy / 48);  // 遊びを切る
        //       }
        //       if (dx != 0 || dy != 0) { 座標へ加算する; }
        //   }
        //   ※held は開閉中 0 なので十字キー方式は自動で止まる。
        //    直接読み方式だけ IsVisible() で閉塞すること。
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
