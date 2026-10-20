#pragma once
#include "menu/engine.hpp"
#include "menu/modal.hpp"
#include "menu/notices.hpp"
#include "menu/panel.hpp"

// 上画面のメニュー。エンジンにボタンを渡し、一覧・説明・確認・入力の枠・通知を描く。
// 描画先は Surface、時刻はミリ秒で、どちらも引数で受ける（CTRPF にもゲームにも依存しない）。
// 呼び出しは毎フレーム 1 つのスレッドから（Step → Draw の順）。
namespace gohan::menu {
struct ShellConfig {
    int screen_width = 400;
};

class Shell {
  public:
    Shell(Engine &engine, ShellConfig config);

    // 開閉。ダイアログや入力の枠が出ている間は何もしない。
    void Toggle(u32 now);
    // 毎フレーム。toggle は外から来た開閉の要求。Select（Up を押していない時）でも開閉する。
    void Step(u32 now, const Input &input, bool toggle);
    // 何か描くものがある（メニュー・動き・ダイアログ・通知）。偽になったら空の画面を 1 回描けば足りる。
    bool NeedsDraw(u32 now) const;
    void Draw(Surface &surface, u32 now);

    Engine &engine() { return engine_; }
    const Panel &panel() const { return panel_; }
    const NoticeQueue &notices() const { return notices_; }
    bool Modal() const { return engine_.Dialog() != DialogKind::None || entry_.Active(); }

  private:
    static void OnNotice(void *self, const Notice &notice);

    Engine &engine_;
    ShellConfig config_;
    Panel panel_;
    Entry entry_;
    NoticeQueue notices_;
    motion::Tween modal_;
    u32 now_ = 0;
    u16 prev_held_ = 0;
    bool was_modal_ = false;
};
} // namespace gohan::menu
