// メニューのエンジンの規則の試験。表は tests/host/fixtures/menu_test.toml から生成した物を使う。
// 旧版の仕様の §2〜§5 の各行と、設計書 §16.7 の案（OFF／待機／ON、予約、単発実行のホットキー無し）を確かめる。
#include "menu/engine.hpp"
#include "menu_ids.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace gohan::menu;

namespace {
struct Rec {
    int wall_set = 0;
    bool wall_on = false;
    int plain_set = 0;
    bool plain_state = false;
    Result plain_result = Result::Ok;
    int move_calls = 0;
    bool move_last = false;
    int move_true_frames = 0;
    int once_runs = 0;
    int act_runs = 0;
    int faint_runs = 0;
    int pocket_runs = 0;
    u32 pocket_input = 0;
    Result pocket_result = Result::Ok;
    int ed_open[2] = {};
    int ed_close[2] = {};
    int ed_steps = 0;
    bool ed_self_close = false;
    int rad_on[2] = {};
    int rad_off[2] = {};
    int num_calls = 0;
    s32 num_applied = 0;
    s32 wallet = 1000;
    bool wallet_ok = true;
    int wallet_writes = 0;
    Result wallet_result = Result::Ok;
    s32 weather = 1;
    int weather_writes = 0;
} g;

Result WallSet(bool on) {
    ++g.wall_set;
    g.wall_on = on;
    return Result::Ok;
}
Result PlainSet(bool on) {
    ++g.plain_set;
    if (g.plain_result != Result::Ok) return g.plain_result;
    g.plain_state = on;
    return Result::Ok;
}
bool PlainActive() { return g.plain_state; }
void MoveStep(bool holding) {
    ++g.move_calls;
    g.move_last = holding;
    if (holding) ++g.move_true_frames;
}
Result OnceRun() { ++g.once_runs; return Result::Ok; }
Result ActRun() { ++g.act_runs; return Result::Ok; }
Result FaintRun() { ++g.faint_runs; return Result::Ok; }
Result PocketRun(u32 input) {
    ++g.pocket_runs;
    g.pocket_input = input;
    return g.pocket_result;
}
Result EdAOpen() { ++g.ed_open[0]; return Result::Ok; }
Result EdBOpen() { ++g.ed_open[1]; return Result::Ok; }
bool EdStep() {
    ++g.ed_steps;
    return !g.ed_self_close;
}
void EdAClose() { ++g.ed_close[0]; }
void EdBClose() { ++g.ed_close[1]; }
Result RadASet(bool on) { ++(on ? g.rad_on : g.rad_off)[0]; return Result::Ok; }
Result RadBSet(bool on) { ++(on ? g.rad_on : g.rad_off)[1]; return Result::Ok; }
void NumApplied(s32 value) {
    ++g.num_calls;
    g.num_applied = value;
}
bool WalletRead(s32 *value) {
    if (!g.wallet_ok) return false;
    *value = g.wallet;
    return true;
}
Result WalletWrite(s32 value) {
    if (g.wallet_result != Result::Ok) return g.wallet_result;
    g.wallet = value;
    ++g.wallet_writes;
    return Result::Ok;
}
bool WeatherRead(s32 *value) {
    *value = g.weather;
    return true;
}
Result WeatherWrite(s32 value) {
    g.weather = value;
    ++g.weather_writes;
    return Result::Ok;
}

const Bound kBinds[] = {
    GOHAN_ITEM(MenuId::Wall, Patch{.set = WallSet}),
    GOHAN_ITEM(MenuId::Plain, Patch{.set = PlainSet, .is_active = PlainActive}),
    GOHAN_ITEM(MenuId::Move, Hold{.step = MoveStep}),
    GOHAN_ITEM(MenuId::Once, Trigger{.run = OnceRun}),
    GOHAN_ITEM(MenuId::Act, Trigger{.run = ActRun}),
    GOHAN_ITEM(MenuId::Faint, Trigger{.run = FaintRun}),
    GOHAN_ITEM(MenuId::Pocket, Trigger{.run_with = PocketRun}),
    GOHAN_ITEM(MenuId::EdA, Mode{.open = EdAOpen, .step = EdStep, .close = EdAClose}),
    GOHAN_ITEM(MenuId::EdB, Mode{.open = EdBOpen, .step = EdStep, .close = EdBClose}),
    GOHAN_ITEM(MenuId::RadA, Patch{.set = RadASet}),
    GOHAN_ITEM(MenuId::RadB, Patch{.set = RadBSet}),
    GOHAN_ITEM(MenuId::Num, SettingValue{.applied = NumApplied}),
    GOHAN_ITEM(MenuId::Wallet, GameValue{.read = WalletRead, .write = WalletWrite}),
    GOHAN_ITEM(MenuId::Weather, GameValue{.read = WeatherRead, .write = WeatherWrite}),
};

struct Note {
    std::string title;
    std::string message;
    bool red;
};

void Collect(void *user, const Notice &notice);

struct Harness {
    Engine engine{kMenuItems, kMenuItemCount};
    std::vector<Note> notes;
    u32 now = 1000;
    u16 held = 0;

    explicit Harness(bool bind = true) {
        g = Rec{};
        engine.SetNoticeSink(NoticeSink{Collect, this});
        if (bind) {
            bool ok = engine.BindAll(kBinds, sizeof(kBinds) / sizeof(kBinds[0]));
            assert(ok);
            (void)ok;
        }
    }
    void Frame(u32 dt = 16) {
        now += dt;
        engine.Frame(now, held);
    }
    void Push(u16 mask) {
        held = mask;
        Frame();
    }
    void Release(u32 dt = 16) {
        held = 0;
        Frame(dt);
    }
    void Tap(u16 mask) {
        Push(mask);
        Release();
    }
};

void Collect(void *user, const Notice &notice) {
    static_cast<Harness *>(user)->notes.push_back({notice.title, notice.message, notice.red});
}

u16 Idx(MenuId id) { return static_cast<u16>(id); }
bool Is(const char *a, const char *b) { return std::strcmp(a, b) == 0; }

// ルートから目的の項目まで、キー操作で選ぶ（フォルダは A で入る）。
void Goto(Harness &h, MenuId id) {
    while (h.engine.Depth() > 0) h.engine.Press(key::B, h.now);
    u16 chain[timing::kMaxDepth];
    int n = 0;
    for (u16 i = Idx(id); i != kNoItem; i = kMenuItems[i].parent) chain[n++] = i;
    for (int k = n - 1; k >= 0; --k) {
        int guard = 0;
        while (h.engine.Selected() != chain[k]) {
            h.engine.Press(key::Down, h.now);
            assert(++guard < 64);
        }
        if (k > 0) h.engine.Press(key::A, h.now);
    }
}

std::unique_ptr<Harness> Make() { return std::make_unique<Harness>(); }

// 項目を ON にして適用する（メニューを開いたまま操作して閉じる）。
void TurnOn(Harness &h, MenuId id) {
    h.engine.Open(h.now);
    assert(h.engine.SetValue(Idx(id), 1));
    assert(h.engine.Commit(Idx(id), h.now));
    assert(h.engine.RequestClose(h.now));
}

// ---- §2 状態遷移の規則 / §4.3 パッチ型 ----
void TestStateHotkeyRules() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wall = Idx(MenuId::Wall);
    e.Open(h->now);
    Goto(*h, MenuId::Wall);
    // 編集: value が変わり適用待ちになる。効果は起きない。
    e.Press(key::A, h->now);
    assert(e.State(wall).value == 1 && e.State(wall).applied == 0);
    assert(e.Dirty(wall) && g.wall_set == 0);
    assert(Is(e.View(wall).value, "ON") && e.View(wall).pending);
    // 差し戻し(L): 効果は起きない。
    e.Press(key::L, h->now);
    assert(e.State(wall).value == 0 && !e.Dirty(wall) && g.wall_set == 0);
    // 適用(X): ホットキーの束縛ありなので、アーム（待機）だけ。効果は変わらない。
    e.Press(key::A, h->now);
    e.Press(key::X, h->now);
    assert(e.State(wall).applied == 1 && g.wall_set == 0 && !e.EffectOn(wall));
    assert(Is(e.View(wall).value, "待機") && !e.View(wall).pending);
    assert(h->notes.empty());
    // メニューを開いている間は、ホットキーの発火自体が走らない。
    h->Push(key::L | key::Up);
    assert(g.wall_set == 0);
    h->Release();
    assert(e.RequestClose(h->now));
    // 項目が ON のとき: ホットキーで効果だけ反転する。項目の applied は変わらない。
    h->Push(key::L | key::Up);
    assert(g.wall_on && g.wall_set == 1 && e.State(wall).applied == 1);
    assert(h->notes.size() == 1 && h->notes[0].message == "壁抜けを有効にしました" && !h->notes[0].red);
    assert(Is(e.View(wall).value, "ON"));
    h->Release();
    h->Push(key::L | key::Up);
    assert(!g.wall_on && g.wall_set == 2 && e.State(wall).applied == 1);
    assert(h->notes.size() == 2 && h->notes[1].message == "壁抜けを無効にしました" && h->notes[1].red);
    h->Release();
    // 押しっぱなしでは繰り返さない（押した瞬間だけ）。
    h->Push(key::L | key::Up);
    h->Frame();
    h->Frame();
    assert(g.wall_set == 3);
    h->Release();
}

void TestStateOffAndHotkeyOnlyChange() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wall = Idx(MenuId::Wall);
    // 項目が OFF のときのホットキーは何もしない。
    h->Push(key::L | key::Up);
    assert(g.wall_set == 0 && !e.EffectOn(wall));
    h->Release();
    // ホットキーで貼った状態でも、項目を OFF にして適用すれば必ず剥がす。
    TurnOn(*h, MenuId::Wall);
    h->Push(key::L | key::Up);
    h->Release();
    assert(g.wall_on);
    e.Open(h->now);
    Goto(*h, MenuId::Wall);
    e.Press(key::A, h->now);
    assert(g.wall_on); // 編集しただけでは何も起きない
    e.Press(key::X, h->now);
    assert(!g.wall_on && e.State(wall).applied == 0);
    assert(h->notes.back().message == "壁抜けを無効にしました");
    assert(Is(e.View(wall).value, "OFF"));
    assert(e.RequestClose(h->now));
    h->Push(key::L | key::Up);
    assert(!g.wall_on); // OFF なので何も起きない
    h->Release();
}

void TestHotkeyOnlyChangeKeepsEffect() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wall = Idx(MenuId::Wall);
    TurnOn(*h, MenuId::Wall);
    h->Push(key::L | key::Up);
    h->Release();
    assert(g.wall_on && g.wall_set == 1);
    e.Open(h->now);
    // ホットキーを解除して適用しても、効果は変わらない（束縛解除を理由に効果を ON へ揃えない）。
    assert(e.SetHotkey(wall, 0));
    assert(e.Dirty(wall));
    assert(e.Commit(wall, h->now));
    assert(g.wall_on && g.wall_set == 1 && e.State(wall).applied_hotkey == 0);
    // 追加・変更も同じ。
    assert(e.SetHotkey(wall, key::L | key::Y));
    assert(e.Commit(wall, h->now));
    assert(g.wall_on && g.wall_set == 1 && e.State(wall).applied_hotkey == (key::L | key::Y));
    // 効果が OFF のときも貼らない。
    assert(e.RequestClose(h->now));
    h->Push(key::L | key::Y);
    assert(!g.wall_on && g.wall_set == 2);
    h->Release();
    e.Open(h->now);
    assert(e.SetHotkey(wall, 0));
    assert(e.Commit(wall, h->now));
    assert(!g.wall_on && g.wall_set == 2);
}

void TestStateWithoutHotkey() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 plain = Idx(MenuId::Plain);
    e.Open(h->now);
    Goto(*h, MenuId::Plain);
    e.Press(key::A, h->now);
    assert(g.plain_set == 0);
    e.Press(key::X, h->now);
    // 束縛なしの ON 適用は、その場で効果を入れる。
    assert(g.plain_set == 1 && g.plain_state && e.EffectOn(plain));
    assert(Is(e.View(plain).value, "ON"));
    assert(h->notes.size() == 1 && h->notes[0].message == "花を有効にしました");
    e.Press(key::A, h->now);
    e.Press(key::X, h->now);
    assert(!g.plain_state && Is(e.View(plain).value, "OFF"));
    // 効果が外から変わっても、本当に変わった時だけ通知する。
    h->notes.clear();
    g.plain_state = true;
    h->Frame();
    assert(h->notes.size() == 1 && h->notes[0].message == "花を有効にしました");
    h->Frame();
    assert(h->notes.size() == 1);
}

// ---- §4.1 押下中実行型 ----
void TestHold() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 move = Idx(MenuId::Move);
    // 項目 OFF: 何もしない。
    h->Push(key::A);
    assert(g.move_calls == 0);
    h->Release();
    TurnOn(*h, MenuId::Move);
    assert(Is(e.View(move).value, "待機"));
    h->Frame();
    assert(g.move_calls > 0 && !g.move_last);
    h->Push(key::A);
    assert(g.move_last && g.move_true_frames == 1);
    assert(Is(e.View(move).value, "動作中"));
    h->Frame();
    assert(g.move_true_frames == 2);
    // 押下で何も反転しない。
    assert(e.State(move).applied == 1);
    h->Release();
    assert(!g.move_last && e.State(move).applied == 1);
    // メニューを開いている間は自動的に止まる。
    h->Push(key::A);
    assert(g.move_last);
    e.Open(h->now);
    h->Frame();
    assert(!g.move_last);
    h->Release();
    // OFF を適用したら、動作中でも止めて、以後は呼ばない。
    assert(e.SetValue(move, 0));
    assert(e.Commit(move, h->now));
    assert(e.RequestClose(h->now));
    const int calls = g.move_calls;
    h->Push(key::A);
    h->Frame();
    assert(g.move_calls == calls && !g.move_last);
    h->Release();
}

// ---- §4.2 単発実行型 / §16.7 案3 ----
void TestOnce() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 once = Idx(MenuId::Once);
    h->Push(key::L | key::Down);
    assert(g.once_runs == 0); // OFF なら押しても何もしない
    h->Release();
    e.Open(h->now);
    assert(e.SetValue(once, 1));
    assert(e.Commit(once, h->now));
    assert(g.once_runs == 0); // 適用しただけでは実行しない
    assert(e.RequestClose(h->now));
    h->Push(key::L | key::Down);
    assert(g.once_runs == 1);
    h->Release();
    h->Push(key::L | key::Down);
    assert(g.once_runs == 2 && e.State(once).applied == 1); // 押すたびに同じ処理を 1 回。項目は変わらない
    h->Release();
    // ホットキーが無いときは、ON にしても何もしない（説明でホットキーの設定を促す）。
    e.Open(h->now);
    assert(Is(e.View(once).value, "待機"));
    assert(e.SetHotkey(once, 0));
    assert(e.Commit(once, h->now));
    assert(g.once_runs == 2);
    assert(Is(e.View(once).value, "ON"));
    assert(e.RequestClose(h->now));
    h->Push(key::L | key::Down);
    assert(g.once_runs == 2);
    h->Release();
}

// ---- §5.1 アクション式 / §16.7 案7（実行はボタンの見た目） ----
void TestAction() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 act = Idx(MenuId::Act);
    e.Open(h->now);
    Goto(*h, MenuId::Act);
    assert(e.View(act).button && Is(e.View(act).value, "実行"));
    const u32 at = h->now;
    e.Press(key::A, at); // 選んだ時にその場で実行（適用を経由しない）
    assert(g.act_runs == 1 && !e.Dirty(act) && e.State(act).applied == 0);
    assert(h->notes.size() == 1 && h->notes[0].title == "ACTION" && h->notes[0].message == "スタイルを実行しました");
    assert(Is(e.View(act).value, "OK"));
    h->Frame(799);
    assert(h->now == at + 799 && Is(e.View(act).value, "OK"));
    h->Frame(1);
    assert(Is(e.View(act).value, "実行")); // 800ms で元に戻る
    assert(e.RequestClose(h->now));
    // ホットキーは無条件に 1 回発火する（項目の ON/OFF は無い）。
    h->Push(key::R | key::Y);
    assert(g.act_runs == 2);
    h->Frame();
    assert(g.act_runs == 2);
    h->Release();
    // メニューを開いている間は発火しない。
    e.Open(h->now);
    h->Push(key::R | key::Y);
    assert(g.act_runs == 2);
}

// ---- §5.5 トグル型アクション式 / §16.7 案2（予約） ----
void TestFaint() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 faint = Idx(MenuId::Faint);
    e.Open(h->now);
    Goto(*h, MenuId::Faint);
    assert(e.View(faint).button && Is(e.View(faint).value, "実行"));
    e.Press(key::A, h->now); // 予約
    assert(e.State(faint).value == 1 && e.Dirty(faint) && g.faint_runs == 0);
    assert(Is(e.View(faint).value, "予約") && e.View(faint).pending);
    e.Press(key::A, h->now); // OFF に戻して適用すれば、実行せずに取り消せる
    assert(!e.Dirty(faint));
    assert(!e.Commit(faint, h->now) && g.faint_runs == 0);
    e.Press(key::A, h->now);
    e.Press(key::L, h->now);
    assert(g.faint_runs == 0 && Is(e.View(faint).value, "実行"));
    e.Press(key::A, h->now);
    e.Press(key::X, h->now); // 適用が引き金。束縛の有無を問わず即実行し、OFF へ戻る
    assert(g.faint_runs == 1 && e.State(faint).value == 0 && e.State(faint).applied == 0);
    assert(Is(e.View(faint).value, "OK") && !e.Dirty(faint));
    assert(h->notes.back().title == "ACTION" && h->notes.back().message == "気絶を実行しました");
    h->Frame(900);
    assert(Is(e.View(faint).value, "実行"));
    assert(e.RequestClose(h->now));
    // ホットキーでは確認ダイアログを出し、承諾後に 1 回実行する。ON／アームは要らない。
    h->Push(key::R | key::B);
    assert(e.Dialog() == DialogKind::Action && g.faint_runs == 1);
    e.Confirm(false, h->now);
    assert(e.Dialog() == DialogKind::None && g.faint_runs == 1);
    h->Release();
    h->Push(key::R | key::B);
    e.Confirm(true, h->now);
    assert(e.Dialog() == DialogKind::None && g.faint_runs == 2);
    h->Release();
    // 束縛設定だけの適用では実行しない。
    e.Open(h->now);
    assert(e.SetHotkey(faint, key::L | key::Y));
    assert(e.Commit(faint, h->now));
    assert(g.faint_runs == 2 && e.Dialog() == DialogKind::None);
}

void TestTriggerInput() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 pocket = Idx(MenuId::Pocket);
    h->Push(key::R | key::X);
    assert(e.Ui().kind == UiKind::TriggerInput && e.Ui().item == pocket && g.pocket_runs == 0);
    e.SubmitUi(0x1234, h->now);
    assert(g.pocket_runs == 1 && g.pocket_input == 0x1234 && e.Ui().kind == UiKind::None);
    assert(h->notes.back().title == "ACTION");
    h->Release();
    // メニューから選んでも同じ入力を求める。取り消せば実行しない。
    e.Open(h->now);
    Goto(*h, MenuId::Pocket);
    e.Press(key::A, h->now);
    assert(e.Ui().kind == UiKind::TriggerInput);
    // 入力 UI が持っている間は、メニューの操作を受け付けない。
    e.Press(key::Down, h->now);
    assert(e.Selected() == pocket);
    e.CancelUi();
    assert(e.Ui().kind == UiKind::None && g.pocket_runs == 1);
    // 失敗は理由を返し、文言は共通の形（題は項目名、赤）。
    g.pocket_result = Result::Busy;
    e.Press(key::A, h->now);
    e.SubmitUi(7, h->now);
    assert(g.pocket_runs == 2);
    assert(h->notes.back().title == "ポケット" && h->notes.back().red);
    assert(h->notes.back().message == FailureText(Result::Busy));
}

// ---- 画面（mode）と排他（group） ----
void TestModeAndGroup() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 a = Idx(MenuId::EdA);
    const u16 b = Idx(MenuId::EdB);
    e.Open(h->now);
    Goto(*h, MenuId::EdA);
    assert(Is(e.View(a).value, "開く") && e.View(a).button);
    e.Press(key::A, h->now);
    assert(g.ed_open[0] == 1 && e.State(a).mode_active && Is(e.View(a).value, "起動中"));
    h->Frame();
    assert(g.ed_steps == 1);
    e.Press(key::A, h->now); // 起動中のアクション再実行で閉じる
    assert(g.ed_close[0] == 1 && !e.State(a).mode_active && Is(e.View(a).value, "開く"));
    // 後に起動した方が先行を止める。
    e.Press(key::A, h->now);
    Goto(*h, MenuId::EdB);
    e.Press(key::A, h->now);
    assert(g.ed_close[0] == 2 && g.ed_open[1] == 1 && !e.State(a).mode_active && e.State(b).mode_active);
    // 機能が自分で閉じたら、close は呼ばずに状態だけ戻す。
    g.ed_self_close = true;
    h->Frame();
    assert(!e.State(b).mode_active && g.ed_close[1] == 0);
    g.ed_self_close = false;
    // ホットキーでも開閉できる（メニューを閉じている時）。
    assert(e.RequestClose(h->now));
    h->Push(key::ZL | key::A);
    assert(e.State(a).mode_active);
    h->Release();
    h->Push(key::ZL | key::A);
    assert(!e.State(a).mode_active);
}

void TestStateGroup() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 a = Idx(MenuId::RadA);
    const u16 b = Idx(MenuId::RadB);
    e.Open(h->now);
    assert(e.SetValue(a, 1) && e.Commit(a, h->now));
    assert(g.rad_on[0] == 1 && e.EffectOn(a));
    assert(e.SetValue(b, 1) && e.Commit(b, h->now));
    assert(g.rad_on[1] == 1 && g.rad_off[0] == 1);
    assert(!e.EffectOn(a) && e.State(a).applied == 0 && e.State(a).value == 0 && e.EffectOn(b));
    assert(h->notes.size() == 3 && h->notes[1].message == "排他Aを無効にしました");
}

// ---- §5.2 数値入力式 / §5.3 リストボックス式 ----
void TestSettingValue() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 num = Idx(MenuId::Num);
    e.Open(h->now);
    Goto(*h, MenuId::Num);
    assert(Is(e.View(num).value, "10") && e.State(num).available);
    e.Press(key::A, h->now);
    assert(e.Ui().kind == UiKind::Number && e.Ui().item == num);
    e.SubmitUi(55, h->now);
    assert(e.State(num).value == 55 && e.State(num).applied == 10 && e.Dirty(num) && g.num_calls == 0);
    e.Press(key::X, h->now); // 適用で代入する
    assert(e.Applied(num) == 55 && g.num_calls == 1 && g.num_applied == 55);
    e.Press(key::A, h->now);
    e.SubmitUi(500, h->now); // 範囲に収める
    assert(e.State(num).value == 100);
    e.Press(key::L, h->now);
    assert(e.State(num).value == 55 && g.num_calls == 1);
    e.Press(key::Right, h->now);
    assert(e.State(num).value == 60);
    for (int i = 0; i < 20; ++i) e.Press(key::Left, h->now);
    assert(e.State(num).value == 0);
    // ホットキーが束縛されていれば、数値入力を直接開く。
    assert(e.SetValue(num, 60) && e.Commit(num, h->now));
    assert(e.RequestClose(h->now));
    h->Push(key::ZR | key::A);
    assert(e.Ui().kind == UiKind::Number && e.Ui().item == num);
    e.CancelUi();
    h->Release();
    // 選択（リスト）は端で止まる。
    e.Open(h->now);
    Goto(*h, MenuId::How);
    e.Press(key::A, h->now);
    assert(e.Ui().kind == UiKind::List);
    e.CancelUi();
    e.Press(key::Left, h->now);
    assert(e.State(Idx(MenuId::How)).value == 0);
    e.Press(key::Right, h->now);
    e.Press(key::Right, h->now);
    assert(e.State(Idx(MenuId::How)).value == 1 && Is(e.View(Idx(MenuId::How)).value, "Grid"));
}

void TestValueFormats() {
    auto h = Make();
    Engine &e = h->engine;
    e.Open(h->now);
    assert(Is(e.View(Idx(MenuId::Amount)).value, "5.0"));
    assert(e.SetValue(Idx(MenuId::Amount), 1000)); // 範囲に収める
    assert(Is(e.View(Idx(MenuId::Amount)).value, "99.9"));
    assert(e.SetValue(Idx(MenuId::Amount), 1));
    assert(Is(e.View(Idx(MenuId::Amount)).value, "0.1"));
    assert(Is(e.View(Idx(MenuId::Hexv)).value, "0xFF"));
    assert(Is(e.View(Idx(MenuId::Multi)).value, "1/3"));
    assert(e.SetValue(Idx(MenuId::Multi), 0xFF)); // 選択肢のビットだけ残す
    assert(e.State(Idx(MenuId::Multi)).value == 7 && Is(e.View(Idx(MenuId::Multi)).value, "3/3"));
    assert(Is(e.View(Idx(MenuId::Keyc)).value, "ZL+B"));
    assert(e.View(Idx(MenuId::Folder)).value[0] == '\0');
}

// ---- 有効の条件（enabled_if） ----
void TestEnabledIf() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 how = Idx(MenuId::How);
    const u16 amount = Idx(MenuId::Amount);
    e.Open(h->now);
    assert(!e.Disabled(amount) && e.SetValue(amount, 100));
    assert(e.SetValue(how, 1)); // 編集した時点で追従する（適用を待たない）
    assert(e.Disabled(amount) && e.View(amount).disabled);
    assert(!e.SetValue(amount, 200));
    Goto(*h, MenuId::Amount);
    e.Press(key::A, h->now);
    assert(e.Ui().kind == UiKind::None); // カーソルは置けるが編集できない
    assert(e.DirtyCount() == 1);         // 灰色の項目は数えない
    assert(e.CommitAll(h->now));
    assert(e.State(amount).applied == 50 && e.State(how).applied == 1); // 灰色の項目は一括でも適用しない
    assert(!e.SetHotkey(amount, key::L | key::Y));
}

// ---- §5.4 連動型 ----
void TestGameValue() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wallet = Idx(MenuId::Wallet);
    e.Open(h->now);
    // 開いたその時に 1 回読み、編集値と適用値の両方に入る（適用待ちにならない）。
    assert(e.State(wallet).value == 1000 && e.State(wallet).applied == 1000 && !e.Dirty(wallet));
    g.wallet = 2000;
    h->Frame();
    assert(e.State(wallet).value == 1000); // 開いている間は同期しない
    assert(e.SetValue(wallet, 3000) && e.Dirty(wallet));
    assert(g.wallet_writes == 0);
    assert(e.Commit(wallet, h->now)); // 適用した時に書く
    assert(g.wallet == 3000 && g.wallet_writes == 1 && e.Applied(wallet) == 3000);
    // 書き込みの失敗は理由を通知し、適用待ちのまま残す。
    g.wallet_result = Result::Unsupported;
    assert(e.SetValue(wallet, 5));
    assert(!e.Commit(wallet, h->now));
    assert(e.Applied(wallet) == 3000 && e.Dirty(wallet));
    assert(h->notes.back().title == "所持金" && h->notes.back().red);
    assert(h->notes.back().message == FailureText(Result::Unsupported));
    g.wallet_result = Result::Ok;
    assert(e.RequestClose(h->now) == false); // 適用待ちが残っているので確認が出る
    e.Confirm(true, h->now);
    assert(!e.Visible() && g.wallet == 5);
    // ホットキーで数値入力を開く時は、現在値を読み直す。
    g.wallet = 777;
    h->Push(key::ZR | key::B);
    assert(e.Ui().kind == UiKind::Number && e.State(wallet).value == 777);
    e.CancelUi();
    h->Release();
}

void TestGameValueUnreadable() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wallet = Idx(MenuId::Wallet);
    g.wallet_ok = false;
    e.Open(h->now);
    // 読み取りに失敗したら編集できなくする。カーソルは置ける。
    assert(e.Disabled(wallet) && e.View(wallet).disabled && Is(e.View(wallet).value, "---"));
    assert(!e.SetValue(wallet, 5));
    assert(!e.SetHotkey(wallet, key::L | key::Y));
    assert(!e.Commit(wallet, h->now));
    Goto(*h, MenuId::Wallet);
    assert(e.Selected() == wallet);
    e.Press(key::A, h->now);
    assert(e.Ui().kind == UiKind::None);
    assert(!e.SetFixed(wallet, true, h->now));
    assert(e.RequestClose(h->now));
    h->Push(key::ZR | key::B);
    assert(e.Ui().kind == UiKind::None); // ホットキーでも開かない
    h->Release();
    // 読めるようになったら、開き直した時に編集できる。
    g.wallet_ok = true;
    e.Open(h->now);
    assert(!e.Disabled(wallet) && e.State(wallet).value == 1000);
}

// ---- §5.6 値の固定 ----
void TestFixed() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wallet = Idx(MenuId::Wallet);
    e.Open(h->now);
    assert(!e.SetFixed(Idx(MenuId::Weather), true, h->now)); // lockable でない項目は固定できない
    assert(e.SetFixed(wallet, true, h->now)); // 編集値を適用値にして 1 回書く
    assert(g.wallet_writes == 1 && e.State(wallet).fixed && e.View(wallet).locked);
    h->Frame();
    assert(g.wallet_writes == 1); // 同じ値を毎フレーム書かない
    g.wallet = 50;                // ゲームが値を変えたら戻す
    h->Frame();
    assert(g.wallet == 1000 && g.wallet_writes == 2);
    g.wallet_ok = false; // 読めないフレームは何も書かない。固定は解除しない
    g.wallet = 0;
    h->Frame();
    assert(g.wallet_writes == 2 && e.State(wallet).fixed);
    g.wallet_ok = true;
    h->Frame();
    assert(g.wallet == 1000 && g.wallet_writes == 3);
    // 開いた時の読み取りは、固定した値を上書きしない。
    g.wallet = 5;
    assert(e.RequestClose(h->now));
    e.Open(h->now);
    assert(e.State(wallet).value == 1000 && e.State(wallet).applied == 1000);
    // 固定中に値を編集して適用すると、新しい適用値で固定し続ける。
    assert(e.SetValue(wallet, 2000) && e.Commit(wallet, h->now));
    g.wallet = 1;
    h->Frame();
    assert(g.wallet == 2000);
    assert(e.SetFixed(wallet, false, h->now));
    g.wallet = 1;
    h->Frame();
    assert(g.wallet == 1 && !e.View(wallet).locked);
}

// ---- 閉じる時の一括の確認 ----
void TestCloseDialog() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 num = Idx(MenuId::Num);
    e.Open(h->now);
    assert(e.RequestClose(h->now)); // 適用待ちが無ければそのまま閉じる
    e.Open(h->now);
    assert(e.SetValue(num, 55));
    assert(!e.RequestClose(h->now) && e.Dialog() == DialogKind::Close && e.Visible());
    e.Press(key::Right, h->now);
    assert(!e.DialogYes());
    e.Press(key::A, h->now); // いいえ: 閉じずに戻る。適用待ちは残る
    assert(e.Dialog() == DialogKind::None && e.Visible() && e.Dirty(num) && e.Applied(num) == 10);
    e.Press(key::B, h->now); // ルートで B: 閉じる要求
    assert(e.Dialog() == DialogKind::Close);
    e.Press(key::B, h->now); // ダイアログの B は「いいえ」（キャンセルを選んだ状態で閉じる）
    assert(e.Visible() && e.Dirty(num) && !e.DialogYes());
    assert(!e.RequestClose(h->now));
    e.Press(key::Left, h->now);
    e.Press(key::A, h->now); // はい: 全項目を適用して閉じる
    assert(!e.Visible() && e.Applied(num) == 55 && g.num_calls == 1);
}

// ---- X／L の長押し ----
void TestLongPress() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 num = Idx(MenuId::Num);
    const u16 how = Idx(MenuId::How);
    e.Open(h->now);
    e.SetValue(num, 55);
    e.SetValue(how, 1);
    Goto(*h, MenuId::Num);
    // 2/5（240ms）未満で離すと、選択中の項目だけの適用。
    h->Push(key::X);
    u32 permille = 0;
    bool muted = false;
    u16 held_key = 0;
    assert(e.HoldProgress(h->now, &permille, &muted, &held_key) && muted && held_key == key::X);
    h->Release(100);
    assert(e.Applied(num) == 55 && e.Dirty(how) && h->notes.empty());
    // 2/5 以上・満了前で離すと、何もせず取り消し。
    h->Push(key::X);
    h->Frame(100);
    h->Frame(200);
    assert(e.HoldProgress(h->now, &permille, &muted, &held_key) && !muted && permille > 400 && permille < 600);
    h->Release(0);
    assert(e.Dirty(how) && h->notes.size() == 1 && h->notes[0].message == "全適用をキャンセルしました");
    assert(e.Dialog() == DialogKind::None);
    // 満了で全項目の確認。
    h->Push(key::X);
    h->Frame(600);
    assert(e.Dialog() == DialogKind::ApplyAll);
    h->Release();
    e.Confirm(true, h->now);
    assert(!e.Dirty(how) && e.Applied(how) == 1);
    // L: 短い押しは単体の差し戻し、満了で全項目の差し戻し。
    e.SetValue(num, 20);
    e.SetValue(how, 0);
    h->Push(key::L);
    h->Release(100);
    assert(e.State(num).value == 55 && e.State(how).value == 0);
    h->Push(key::L);
    h->Frame(600);
    assert(e.Dialog() == DialogKind::RevertAll);
    h->Release();
    e.Confirm(true, h->now);
    assert(e.State(how).value == 1 && !e.Dirty(how));
    h->Push(key::L);
    h->Frame(300);
    h->Release(0);
    assert(h->notes.back().message == "全戻しをキャンセルしました");
}

// ---- 移動・選択・リピート ----
void TestNavigation() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wall = Idx(MenuId::Wall);
    const u16 multi = Idx(MenuId::Multi);
    e.Open(h->now);
    assert(e.Selected() == wall && e.Depth() == 0 && e.Folder() == kNoItem);
    e.Press(key::Up, h->now); // 押した瞬間は端で折り返す
    assert(e.Selected() == multi);
    e.Press(key::Up, h->now, true); // リピートは端で止まる
    e.Press(key::Down, h->now, true);
    assert(e.Selected() == multi);
    e.Press(key::Down, h->now);
    assert(e.Selected() == wall);
    // フォルダに入る・戻る。
    Goto(*h, MenuId::Folder);
    e.Press(key::A, h->now);
    assert(e.Depth() == 1 && e.Folder() == Idx(MenuId::Folder) && e.Selected() == Idx(MenuId::Num));
    e.Press(key::B, h->now);
    assert(e.Depth() == 0 && e.Selected() == Idx(MenuId::Folder));
    // 開発用の項目は、開発版のときだけ見える。
    const u16 devx = Idx(MenuId::Devx);
    assert(e.Hidden(devx));
    u16 count = 0;
    for (u16 i = e.FirstVisible(Idx(MenuId::Folder)); i != kNoItem; i = e.NextVisible(i)) {
        assert(i != devx);
        ++count;
    }
    assert(count == 7);
    e.SetDevVisible(true);
    count = 0;
    for (u16 i = e.FirstVisible(Idx(MenuId::Folder)); i != kNoItem; i = e.NextVisible(i)) ++count;
    assert(count == 8 && !e.Hidden(devx));
    // B をルートで押すと閉じる。
    while (e.Depth() > 0) e.Press(key::B, h->now);
    e.Press(key::B, h->now);
    assert(!e.Visible());
}

void TestRepeat() {
    auto h = Make();
    Engine &e = h->engine;
    e.Open(h->now);
    h->Push(key::Down);
    assert(e.Selected() == Idx(MenuId::Plain));
    h->Frame(150); // 200ms 経つまでは動かない
    assert(e.Selected() == Idx(MenuId::Plain));
    h->Frame(60);
    assert(e.Selected() == Idx(MenuId::Move));
    h->Frame(40); // 60ms 間隔（210ms の次は 260ms）
    assert(e.Selected() == Idx(MenuId::Move));
    h->Frame(20);
    assert(e.Selected() == Idx(MenuId::Once));
    h->Release();
    h->Frame(500);
    assert(e.Selected() == Idx(MenuId::Once));
}

// 開閉では経路と選択を保ち、読み取り・条件・開発項目の変化だけを補正する。
void TestReopenPosition() {
    auto h = Make();
    Engine &e = h->engine;
    e.Open(h->now);
    Goto(*h, MenuId::Folder); e.Press(key::A, h->now);
    Goto(*h, MenuId::Hexv);
    assert(e.RequestClose(h->now)); e.Open(h->now);
    assert(e.Depth() == 1 && e.Folder() == Idx(MenuId::Folder) && e.Selected() == Idx(MenuId::Hexv));
    e.Press(key::B, h->now);
    assert(e.Selected() == Idx(MenuId::Folder)); // 親の選択も保つ。
    Goto(*h, MenuId::Multi);
    assert(e.RequestClose(h->now)); e.Open(h->now);
    assert(e.Depth() == 0 && e.Selected() == Idx(MenuId::Multi));

    Goto(*h, MenuId::Folder); e.Press(key::A, h->now);
    Goto(*h, MenuId::Wallet);
    assert(e.RequestClose(h->now)); g.wallet_ok = false; e.Open(h->now);
    assert(e.Selected() == Idx(MenuId::Weather) && e.Disabled(Idx(MenuId::Wallet)));
    // 無効な行への通常の移動は引き続き許す。
    assert(e.Select(Idx(MenuId::Wallet)));
    g.wallet_ok = true;
    assert(e.RequestClose(h->now)); e.Open(h->now);
    assert(e.Selected() == Idx(MenuId::Wallet));

    Goto(*h, MenuId::Amount);
    assert(e.RequestClose(h->now));
    assert(e.SetValue(Idx(MenuId::How), 1)); e.Open(h->now);
    assert(e.Selected() == Idx(MenuId::Wallet));
    e.DiscardAll();
    e.SetDevVisible(true); Goto(*h, MenuId::Devx);
    assert(e.RequestClose(h->now)); e.SetDevVisible(false); e.Open(h->now);
    assert(e.Selected() == Idx(MenuId::Keyc));
}

void TestReopenPathFallback() {
    ItemDef defs[6]{};
    for (auto &d : defs) {
        d.control = Control::Folder;
        d.parent = d.first_child = d.next = kNoItem;
    }
    // ルート → 常設 → 開発用 → 葉。ルートの次の兄弟は条件付きのフォルダ。
    defs[0].first_child = 1; defs[0].next = 4;
    defs[1].parent = 0; defs[1].first_child = 2; defs[1].next = 3; defs[1].dev = true;
    defs[2].parent = 1; defs[2].control = Control::Toggle;
    defs[3].parent = 0; defs[3].control = Control::Toggle;
    defs[4].first_child = 5; defs[4].cond = {3, CondOp::Eq, 0};
    defs[5].parent = 4; defs[5].control = Control::Toggle;
    Engine e(defs, 6);
    e.SetDevVisible(true); e.Open(0);
    e.Press(key::A, 0); e.Press(key::A, 0);
    assert(e.Depth() == 2 && e.Selected() == 2);
    assert(e.RequestClose(0)); e.SetDevVisible(false); e.Open(0);
    assert(e.Depth() == 1 && e.Folder() == 0 && e.Selected() == 3);
    // 全経路が隠れた時はルートへ戻る。
    e.SetDevVisible(true); e.Press(key::B, 0); e.Press(key::A, 0); e.Press(key::A, 0);
    assert(e.RequestClose(0)); defs[0].dev = true; e.SetDevVisible(false); e.Open(0);
    assert(e.Depth() == 0 && e.Selected() == 4);
    e.Press(key::A, 0); assert(e.Folder() == 4);
    assert(e.RequestClose(0));
    defs[4].cond.rhs = 1; e.Open(0);
    assert(e.Depth() == 0 && e.Selected() == 4 && e.Disabled(4)); // 全て灰色でも安全な行に置く。
    // 空のフォルダ、全項目が隠れる場合、項目が無い場合。
    defs[4].cond = {}; e.Press(key::A, 0);
    assert(e.RequestClose(0)); defs[5].dev = true; e.Open(0);
    assert(e.Depth() == 0 && e.Selected() == 4);
    assert(e.RequestClose(0)); defs[4].dev = true; e.Open(0);
    assert(e.Selected() == kNoItem && e.Folder() == kNoItem);
    Engine empty(nullptr, 0); empty.Open(0);
    assert(empty.Selected() == kNoItem && empty.RequestClose(0));
    empty.Open(1); assert(empty.Selected() == kNoItem);
}

void TestHiddenAndReserved() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wall = Idx(MenuId::Wall);
    e.SetReservedHotkeys(key::Select | key::Start);
    e.Open(h->now);
    assert(!e.SetHotkey(wall, key::Start));
    // 同じ組み合わせは他の項目で使われている。通知して受け付けない。
    assert(!e.SetHotkey(wall, key::L | key::Down));
    assert(h->notes.back().red && h->notes.back().title == "壁抜け");
    assert(h->notes.back().message == "そのホットキーは解除で使われています");
    assert(e.SetHotkey(wall, key::L | key::Y));
    assert(e.SetHotkey(wall, e.State(wall).hotkey)); // 自分自身は重複にならない
    assert(!e.SetHotkey(Idx(MenuId::Folder), key::A));
    assert(!e.SetHotkey(Idx(MenuId::Keyc), key::A));
}

void TestKeyControl() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 keyc = Idx(MenuId::Keyc);
    e.Open(h->now);
    Goto(*h, MenuId::Keyc);
    e.Press(key::A, h->now);
    assert(e.Ui().kind == UiKind::Key);
    e.SubmitUi(key::R | key::A, h->now);
    assert(e.State(keyc).value == (key::R | key::A) && e.Dirty(keyc));
    assert(Is(e.View(keyc).value, "R+A"));
    assert(e.Commit(keyc, h->now) && e.Applied(keyc) == (key::R | key::A));
    // Y: 選んだ項目のホットキーの設定。結果は適用待ちになる。
    Goto(*h, MenuId::Num);
    e.Press(key::Y, h->now);
    assert(e.Ui().kind == UiKind::Key && e.Ui().item == Idx(MenuId::Num));
    e.SubmitUi(key::L | key::R, h->now);
    assert(e.State(Idx(MenuId::Num)).hotkey == (key::L | key::R) && e.State(Idx(MenuId::Num)).applied_hotkey == (key::ZR | key::A));
    assert(e.Dirty(Idx(MenuId::Num)));
    e.Press(key::L, h->now);
    assert(e.State(Idx(MenuId::Num)).hotkey == (key::ZR | key::A));
}

// ---- 結び付け ----
void TestBind() {
    Engine e(kMenuItems, kMenuItemCount);
    Bound b;
    b.item = Idx(MenuId::Wall);
    b.behavior = Behavior::State;
    assert(e.Bind(b));
    assert(!e.Bind(b)); // 二重
    b.item = Idx(MenuId::Plain);
    b.behavior = Behavior::Hold;
    assert(!e.Bind(b)); // 振る舞いが違う
    b.behavior = Behavior::State;
    b.source = Source::Game;
    assert(!e.Bind(b)); // 出どころが違う
    b.item = 999;
    b.source = Source::None;
    assert(!e.Bind(b)); // 範囲外
    // 結び付けが無い項目でも落ちない（実行は失敗として知らせる）。
    Harness h(false);
    h.engine.Open(h.now);
    Goto(h, MenuId::Act);
    h.engine.Press(key::A, h.now);
    assert(h.notes.size() == 1 && h.notes[0].red && h.notes[0].message == FailureText(Result::Failed));
}

// ---- ホットキーの表記・表の整合 ----
void TestHotkeyText() {
    static const char *const names[] = {"ZL", "L", "R", "ZR", "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "Select", "Start"};
    for (unsigned bit = 0; bit < 14; ++bit) {
        u16 mask = 0;
        assert(ParseHotkey(names[bit], mask) && mask == (1u << bit));
        char buffer[32];
        FormatHotkey(mask, buffer, sizeof(buffer));
        assert(Is(buffer, names[bit]));
    }
    u16 mask = 0;
    assert(ParseHotkey("l + UP", mask) && mask == (key::L | key::Up));
    assert(ParseHotkey("select+a", mask) && mask == (key::Select | key::A));
    for (const char *bad : {"", "L+", "+A", "L+L", "Foo", "L Up", "L++A"}) assert(!ParseHotkey(bad, mask));
    char buffer[32];
    FormatHotkey(0, buffer, sizeof(buffer));
    assert(Is(buffer, "なし"));
    FormatHotkey(key::Start | key::ZL, buffer, sizeof(buffer));
    assert(Is(buffer, "ZL+Start"));
    // 生成した表のマスクが、spec の表記と同じ値になっている。
    assert(kMenuItems[Idx(MenuId::Wall)].hotkey == (key::L | key::Up));
    assert(kMenuItems[Idx(MenuId::Move)].hotkey == key::A);
    assert(kMenuItems[Idx(MenuId::Pocket)].hotkey == (key::R | key::X));
    assert(kMenuItems[Idx(MenuId::Keyc)].def == (key::ZL | key::B));
    for (std::size_t i = 0; i < kMenuItemCount; ++i) {
        if (kMenuItems[i].hotkey == 0) continue;
        FormatHotkey(kMenuItems[i].hotkey, buffer, sizeof(buffer));
        u16 round = 0;
        assert(ParseHotkey(buffer, round) && round == kMenuItems[i].hotkey);
    }
}

void TestTable() {
    const ItemDef &amount = kMenuItems[Idx(MenuId::Amount)];
    assert(amount.format == Format::Decimal1 && amount.def == 50 && amount.min == 1 && amount.max == 999 && amount.step == 5);
    assert(amount.cond.item == Idx(MenuId::How) && amount.cond.op == CondOp::Ne && amount.cond.rhs == 1);
    const ItemDef &multi = kMenuItems[Idx(MenuId::Multi)];
    assert(multi.def == 2 && multi.max == 7 && multi.option_count == 3 && Is(multi.options[1], "b"));
    assert(kMenuItems[Idx(MenuId::Wallet)].lockable && kMenuItems[Idx(MenuId::Wallet)].source == Source::Game);
    assert(!kMenuItems[Idx(MenuId::Wallet)].persist && kMenuItems[Idx(MenuId::Wall)].persist && !kMenuItems[Idx(MenuId::EdA)].persist);
    assert(kMenuItems[Idx(MenuId::Faint)].menu == MenuKind::OnApply && kMenuItems[Idx(MenuId::Faint)].confirm == Confirm::Hotkey);
    assert(kMenuItems[Idx(MenuId::Pocket)].input == InputKind::Hex);
    assert(kMenuItems[Idx(MenuId::EdA)].group == kMenuItems[Idx(MenuId::EdB)].group && kMenuItems[Idx(MenuId::EdA)].group != 0);
    assert(kMenuItems[Idx(MenuId::Devx)].dev && kMenuItems[Idx(MenuId::Folder)].first_child == Idx(MenuId::Num));
    assert(kMenuItems[Idx(MenuId::Hexv)].next == kNoItem && kMenuItems[Idx(MenuId::Folder)].next == Idx(MenuId::Multi));
}

// ---- 通知の共通の形 ----
void TestNoticeShape() {
    Notice n;
    NoticeEffect(n, "壁抜け", true);
    assert(Is(n.title, "壁抜け") && Is(n.message, "壁抜けを有効にしました") && !n.red);
    NoticeEffect(n, "壁抜け", false);
    assert(Is(n.message, "壁抜けを無効にしました") && n.red);
    NoticeExecuted(n, "気絶");
    assert(Is(n.title, "ACTION") && Is(n.message, "気絶を実行しました") && !n.red);
    NoticeHoldCancelled(n, true);
    assert(Is(n.title, "HOLD") && Is(n.message, "全適用をキャンセルしました"));
    NoticeHoldCancelled(n, false);
    assert(Is(n.message, "全戻しをキャンセルしました"));
    assert(Is(FailureText(Result::Ok), ""));
    const Result failures[] = {Result::NotReady, Result::Unsupported, Result::Busy, Result::Failed};
    for (std::size_t i = 0; i < 4; ++i) {
        assert(FailureText(failures[i])[0] != '\0');
        NoticeFailed(n, "項目", failures[i]);
        assert(Is(n.title, "項目") && n.red && Is(n.message, FailureText(failures[i])));
        for (std::size_t j = i + 1; j < 4; ++j) assert(!Is(FailureText(failures[i]), FailureText(failures[j])));
    }
}

// ---- 行の表示の一覧（§16.7 案1・2・7） ----
void TestRowDisplay() {
    auto h = Make();
    Engine &e = h->engine;
    const u16 wall = Idx(MenuId::Wall);
    const u16 plain = Idx(MenuId::Plain);
    e.Open(h->now);
    assert(Is(e.View(wall).value, "OFF") && !e.View(wall).button && !e.View(wall).pending);
    e.SetValue(wall, 1);
    assert(Is(e.View(wall).value, "ON") && e.View(wall).pending); // 編集中は編集値（黄色）
    e.Commit(wall, h->now);
    assert(Is(e.View(wall).value, "待機"));
    e.SetValue(plain, 1);
    e.Commit(plain, h->now);
    assert(Is(e.View(plain).value, "ON"));
    assert(e.View(Idx(MenuId::Faint)).button && e.View(Idx(MenuId::EdA)).button && e.View(Idx(MenuId::Act)).button);
    assert(!e.View(Idx(MenuId::Num)).button && !e.View(Idx(MenuId::Once)).button);
}

// 項目の操作の段 1。見た目はまだ有効にせず、公開 API の規則を確かめる。
void TestItemActionsTable() {
    auto h = Make();
    auto &e = h->engine;
    e.Open(h->now);
    struct Case { MenuId id; bool fix; bool hotkey; };
    const Case cases[] = {
        {MenuId::Folder, false, false}, {MenuId::Plain, true, true},
        {MenuId::Move, true, true}, {MenuId::Once, false, true},
        {MenuId::Wallet, true, true}, {MenuId::Weather, false, true},
        {MenuId::Num, false, true}, {MenuId::Act, false, true},
        {MenuId::EdA, false, true}, {MenuId::Keyc, false, false},
        {MenuId::How, false, true}, {MenuId::Multi, false, true},
    };
    for (const auto &c : cases) {
        const auto item = Idx(c.id);
        ItemAction out[kMaxActions];
        const auto rev = e.Revision();
        const auto notes = h->notes.size();
        const auto selected = e.Selected();
        const auto n = e.Actions(item, out);
        assert(n == std::size_t(1 + c.fix + c.hotkey + (c.hotkey && e.State(item).hotkey != 0)));
        std::size_t at = 0;
        if (c.fix) assert(out[at++].kind == ActionKind::Fix);
        assert(out[at++].kind == ActionKind::AddFavorite);
        if (c.hotkey) {
            assert(out[at++].kind == ActionKind::SetHotkey);
            if (e.State(item).hotkey) assert(out[at++].kind == ActionKind::RemoveHotkey);
        }
        for (std::size_t i = 0; i < n; ++i) assert(!out[i].disabled);
        assert(e.Revision() == rev && h->notes.size() == notes && e.Selected() == selected);
    }
    // persist が偽の状態・押下中と、別の見た目の値も同じ表で検査する。
    std::vector<ItemDef> defs(kMenuItems, kMenuItems + kMenuItemCount);
    for (auto id : {MenuId::Plain, MenuId::Move}) defs[Idx(id)].persist = false;
    defs[Idx(MenuId::Num)].control = Control::Slider;
    auto custom = std::make_unique<Engine>(defs.data(), defs.size());
    ItemAction out[kMaxActions];
    for (auto id : {MenuId::Plain, MenuId::Move, MenuId::Num}) {
        custom->Actions(Idx(id), out);
        assert(out[0].kind == ActionKind::AddFavorite);
        assert(!custom->SetFixed(Idx(id), true, 0));
    }
    assert(e.Actions(kFavoritesFolder, out) == 0 && e.Actions(kNoItem, out) == 0);
    assert(Is(e.Def(kFavoritesFolder).desc, "お気に入りに追加した項目を並べます。中の行の R で、外す・並べ替えができます。"));
    const auto plain = Idx(MenuId::Plain), wallet = Idx(MenuId::Wallet);
    e.Actions(plain, out); assert(Is(out[0].label, "OFFで固定"));
    e.SetValue(plain, 1); e.Actions(plain, out); assert(Is(out[0].label, "ONで固定"));
    assert(e.RunAction(plain, ActionKind::Fix, h->now));
    assert(h->notes.back().title == "ONで固定" && h->notes.back().message == e.Def(plain).label);
    e.Actions(plain, out); assert(Is(out[0].label, "固定を外す"));
    assert(e.RunAction(plain, ActionKind::Unfix, h->now));
    assert(h->notes.back().title == "固定を外す" && e.Applied(plain) == 1 && g.plain_state);
    assert(e.SetFixed(wallet, true, h->now));
    g.wallet_ok = false; e.Open(h->now);
    auto n = e.Actions(wallet, out);
    assert(n == 4 && out[0].kind == ActionKind::Unfix && !out[0].disabled && !out[1].disabled);
    assert(out[2].disabled && out[3].disabled);
    assert(e.RunAction(wallet, ActionKind::Unfix, h->now));
    e.Actions(wallet, out); assert(out[0].disabled && !out[1].disabled);
    assert(!e.RunAction(wallet, ActionKind::Fix, h->now));
    assert(!e.RunAction(wallet, ActionKind::RemoveHotkey, h->now));
    e.SetValue(Idx(MenuId::How), 1);
    n = e.Actions(Idx(MenuId::Amount), out);
    assert(n == 2 && !out[0].disabled && out[1].disabled);
    // enabled_if で無効のトグルでも固定解除とお気に入りは使える。
    defs[plain].cond = {Idx(MenuId::How), CondOp::Eq, 1};
    defs[plain].persist = true;
    custom = std::make_unique<Engine>(defs.data(), defs.size());
    custom->Actions(plain, out);
    assert(out[0].disabled && !out[1].disabled && out[2].disabled);
    assert(custom->SetValue(Idx(MenuId::How), 1));
    assert(custom->SetFixed(plain, true, 0));
    custom->SetValue(Idx(MenuId::How), 0);
    custom->Actions(plain, out); assert(!out[0].disabled);
    assert(custom->RunAction(plain, ActionKind::Unfix, 0));
}

// 効果が外れた観測の通知と、入れ直しの失敗の通知を分けて検査する。
void TestFixedNotReady() {
    auto h = Make(); auto &e = h->engine; e.Open(h->now);
    const auto plain = Idx(MenuId::Plain);
    assert(e.SetValue(plain, 1) && e.SetFixed(plain, true, h->now));
    g.plain_state = false;
    // DriveFixed を呼ぶ前に、効果が外れたことだけを Poll に観測させる。
    assert(!e.Commit(plain, h->now));
    h->notes.clear();
    const auto attempts = g.plain_set;
    const auto revision = e.Revision();
    g.plain_result = Result::NotReady;
    h->Frame(); h->Frame(); h->Frame();
    assert(g.plain_set == attempts + 3 && !g.plain_state);
    assert(h->notes.empty() && !e.State(plain).fixed_failed);
    assert(e.State(plain).fixed && e.Applied(plain) == 1 && e.Revision() == revision);
    g.plain_result = Result::Ok;
    h->Frame();
    assert(g.plain_state && !e.State(plain).fixed_failed);
    assert(g.plain_set == attempts + 4 && e.Revision() == revision);
    for (const auto &note : h->notes) assert(!note.red);
    // 待機は失敗通知の枠を使わない。その後の本当の失敗は 1 回だけ知らせる。
    g.plain_state = false; g.plain_result = Result::Failed;
    h->Frame();
    assert(e.State(plain).fixed_failed);
    const auto notes = h->notes.size();
    h->Frame(); h->Frame();
    assert(h->notes.size() == notes);
    std::size_t failures = 0;
    for (const auto &note : h->notes) if (note.message == FailureText(Result::Failed)) ++failures;
    assert(failures == 1);
}

void TestToggleFixed() {
    auto h = Make(); auto &e = h->engine; e.Open(h->now);
    const auto plain = Idx(MenuId::Plain), wall = Idx(MenuId::Wall), move = Idx(MenuId::Move);
    assert(e.SetHotkey(plain, key::ZR | key::Y)); // 編集中のホットキーを残す
    assert(e.SetValue(plain, 1) && e.SetFixed(plain, true, h->now));
    assert(e.State(plain).fixed && e.Applied(plain) == 1 && g.plain_state);
    assert(e.State(plain).applied_hotkey == 0 && e.Dirty(plain));
    assert(h->notes.back().title == "ONで固定");
    e.Discard(plain);
    g.plain_state = false; const int before = g.plain_set; h->Frame();
    assert(g.plain_state && g.plain_set == before + 1);
    h->Frame(); assert(g.plain_set == before + 1);
    // 失敗は毎フレーム試し、固定の間に通知するのは 1 回だけ。
    g.plain_state = false; g.plain_result = Result::Failed;
    h->Frame(); const auto notes = h->notes.size(); const auto attempts = g.plain_set;
    h->Frame(); h->Frame(); assert(g.plain_set == attempts + 2 && h->notes.size() == notes);
    g.plain_result = Result::Ok; h->Frame(); assert(g.plain_state);
    g.plain_state = false; g.plain_result = Result::Failed; h->Frame();
    std::size_t failures = 0;
    for (const auto &note : h->notes) if (note.message == FailureText(Result::Failed)) ++failures;
    assert(failures == 1); // 固定中の失敗通知は、成功を挟んでも 1 回だけ
    g.plain_result = Result::Ok;
    assert(e.SetValue(plain, 0));
    const auto rev = e.Revision(); assert(e.Commit(plain, h->now));
    assert(e.State(plain).fixed && e.State(plain).fixed_value == 0 && e.Revision() > rev);
    g.plain_state = true; const int off_before = g.plain_set; h->Frame();
    assert(g.plain_state && g.plain_set == off_before); // OFF の固定はゲームの効果を外さない
    e.SetValue(plain, 1); e.Discard(plain); assert(e.State(plain).value == 0);
    assert(e.SetFixed(plain, false, h->now));
    assert(e.SetFixed(plain, true, h->now));
    assert(h->notes.back().title == "OFFで固定");
    // ホットキー付きの状態は待機のまま。is_active が無い項目も入れ直さない。
    e.SetValue(wall, 1); assert(e.SetFixed(wall, true, h->now));
    const auto calls = g.wall_set; h->Frame(); assert(g.wall_set == calls);
    e.SetValue(move, 1); assert(e.SetFixed(move, true, h->now));
    h->Frame(); assert(!g.move_last && e.State(move).fixed);
    e.SetValue(move, 0); e.Commit(move, h->now); assert(e.State(move).fixed && e.Applied(move) == 0);
    const auto a = Idx(MenuId::RadA), b = Idx(MenuId::RadB);
    e.SetValue(a, 1); e.SetFixed(a, true, h->now);
    e.SetValue(b, 1); e.SetFixed(b, true, h->now);
    assert(e.State(a).fixed && e.State(b).fixed && e.Applied(a) == 0 && e.Applied(b) == 1);
    e.SetValue(a, 1); e.Commit(a, h->now);
    assert(e.State(a).fixed && e.State(b).fixed && e.Applied(a) == 1 && e.Applied(b) == 0);
    assert(e.State(b).fixed_value == 0);
    // ホットキー付きの固定でも、編集中の新しい割り当ては適用しない。
    e.SetHotkey(wall, 0); e.SetValue(wall, 0); e.SetFixed(wall, true, h->now);
    assert(e.State(wall).applied_hotkey != 0 && e.Dirty(wall));
    e.SetHotkey(wall, 0); e.Commit(wall, h->now);
    e.SetValue(wall, 1); e.SetFixed(wall, true, h->now);
    assert(g.wall_on);
    g.wall_on = false; const auto unobserved = g.wall_set; h->Frame();
    assert(g.wall_set == unobserved); // is_active が無ければエンジンの記録を使う
    e.SetHotkey(plain, key::ZR | key::Y); e.Commit(plain, h->now);
    e.SetValue(plain, 1); e.SetFixed(plain, true, h->now);
    assert(!g.plain_state);
    const auto waiting = g.plain_set; h->Frame();
    assert(g.plain_set == waiting && !g.plain_state); // ホットキーが駆動する待機には介入しない
}

void TestFavoriteActionsAndResume() {
    auto h = Make(); auto &e = h->engine; e.Open(h->now);
    const auto folder = Idx(MenuId::Folder), num = Idx(MenuId::Num), plain = Idx(MenuId::Plain);
    assert(e.FirstVisible(kNoItem) != kFavoritesFolder);
    assert(e.RunAction(folder, ActionKind::AddFavorite, h->now));
    assert(h->notes.back().title == "お気に入りに追加" && h->notes.back().message == e.Def(folder).label);
    assert(e.SetFavorite(num, true) && e.SetFavorite(plain, true));
    assert(e.FirstVisible(kNoItem) == kFavoritesFolder && e.FavoriteCount() == 3);
    assert(e.Select(kFavoritesFolder)); e.Press(key::A, h->now);
    assert(e.Folder() == kFavoritesFolder && e.Selected() == folder);
    assert(e.NextVisible(folder) == num && e.NextVisible(num) == plain);
    e.Press(key::A, h->now); assert(e.Folder() == folder);
    e.Select(num); assert(e.RequestClose(h->now)); e.Open(h->now);
    assert(e.Depth() == 2 && e.Folder() == folder && e.Selected() == num);
    e.Press(key::B, h->now); assert(e.Folder() == kFavoritesFolder && e.Selected() == folder);
    e.Select(plain); e.SetValue(plain, 1); e.Commit(plain, h->now);
    assert(e.Applied(plain) == 1 && e.View(plain).def == &e.Def(plain));
    ItemAction out[kMaxActions]; e.Actions(plain, out);
    assert(out[2].kind == ActionKind::Reorder);
    e.SetHotkey(plain, key::Y); assert(e.Actions(plain, out) == 5);
    const auto rev = e.Revision();
    assert(e.RunAction(plain, ActionKind::Reorder, h->now) && e.Ui().kind == UiKind::Reorder);
    u16 draft[] = {plain, folder, num};
    e.CancelUi(); // 取消では写しを捨てるだけ
    assert(e.Revision() == rev && e.FavoriteOrder()[0] == folder);
    assert(e.SetFavoriteOrder(draft, 3) && e.Revision() == rev + 1);
    assert(e.SetFavoriteOrder(draft, 3) && e.Revision() == rev + 1);
    const u16 bad[] = {num, num, folder};
    assert(!e.SetFavoriteOrder(bad, 3) && !e.SetFavoriteOrder(draft, 2));
    assert(!e.SetFavoriteOrder(nullptr, 3));
    assert(e.Selected() == plain && e.FirstVisible(kFavoritesFolder) == plain);
    e.Discard(plain);
    assert(e.RequestClose(h->now)); e.Open(h->now);
    assert(e.Selected() == plain && e.Folder() == kFavoritesFolder);
    assert(e.RunAction(plain, ActionKind::RemoveFavorite, h->now));
    assert(e.Selected() == folder); // ID が小さい次の行でも、並びの順に選ぶ
    assert(e.SetFavorite(num, false));
    assert(e.Selected() == folder && e.FavoriteCount() == 1);
    e.Actions(folder, out); assert(e.Actions(folder, out) == 1);
    assert(!e.RunAction(folder, ActionKind::Reorder, h->now));
    assert(e.SetFavorite(folder, false));
    assert(e.Depth() == 0 && e.Selected() == e.FirstVisible(kNoItem));
    assert(e.FirstVisible(kNoItem) != kFavoritesFolder);
    // 開発項目だけなら仮のフォルダを出さず、隠れた経路から上へ戻る。
    e.SetDevVisible(true); e.SetFavorite(Idx(MenuId::Devx), true);
    e.Select(kFavoritesFolder); e.Press(key::A, h->now);
    assert(e.Selected() == Idx(MenuId::Devx));
    e.RequestClose(h->now); e.SetDevVisible(false); e.Open(h->now);
    assert(e.Depth() == 0 && e.FirstVisible(kNoItem) != kFavoritesFolder);
    e.SetDevVisible(true); e.SetFavorite(folder, true); e.SetFavorite(num, true);
    e.Select(kFavoritesFolder); e.Press(key::A, h->now);
    e.Select(folder); e.Press(key::A, h->now); e.Select(num);
    e.SetFavorite(folder, false);
    assert(e.Depth() == 1 && e.Folder() == kFavoritesFolder && e.Selected() == num);
    e.Select(Idx(MenuId::Devx)); e.RequestClose(h->now);
    e.SetDevVisible(false); e.Open(h->now);
    assert(e.Folder() == kFavoritesFolder && e.Selected() == num);
    assert(e.Actions(num, out) == 3); // 見える行が 1 件なら並べ替えを出さない
}

void TestFavoriteCapacity() {
    std::vector<ItemDef> defs(gohan::core::limits::MenuItems, kMenuItems[Idx(MenuId::Plain)]);
    for (u16 i = 0; i < defs.size(); ++i) {
        defs[i].parent = kNoItem;
        defs[i].next = std::size_t(i) + 1 < defs.size() ? i + 1 : kNoItem;
    }
    auto engine = std::make_unique<Engine>(defs.data(), defs.size());
    for (u16 i = 0; i < defs.size(); ++i) assert(engine->SetFavorite(i, true));
    assert(engine->FavoriteCount() == defs.size());
    u16 children[gohan::core::limits::MenuItems + 1];
    assert(engine->Children(kNoItem, children, defs.size() + 1) == defs.size() + 1);
    assert(children[0] == kFavoritesFolder && children[defs.size()] == defs.size() - 1);
    assert(engine->Children(kFavoritesFolder, children, defs.size() + 1) == defs.size());
    assert(engine->Children(kFavoritesFolder, children, 1) == 1);
    assert(!engine->SetFavorite(kNoItem, true));
}

void TestActionRequestsAndRevision() {
    auto h = Make(); auto &e = h->engine; e.Open(h->now);
    const auto plain = Idx(MenuId::Plain), num = Idx(MenuId::Num);
    e.Select(plain); h->Tap(key::R); assert(e.Ui().kind == UiKind::Actions && e.Ui().item == plain);
    assert(e.RunAction(plain, ActionKind::SetHotkey, h->now));
    assert(e.Ui().kind == UiKind::Key); e.SubmitUi(key::ZR | key::Y, h->now);
    const auto rev = e.Revision(); assert(e.Dirty(plain) && rev == 0);
    assert(e.RunAction(plain, ActionKind::RemoveHotkey, h->now)); assert(e.State(plain).hotkey == 0);
    e.SetHotkey(plain, key::ZR | key::Y); e.Commit(plain, h->now); assert(e.Revision() == rev + 1);
    const auto before = e.Revision(); e.SetValue(plain, 1); e.Commit(plain, h->now);
    assert(e.Revision() == before); // 固定も全保持もない ON は保存しない
    e.SetKeepToggles(true); const auto keeping = e.Revision();
    e.SetValue(plain, 0); e.Commit(plain, h->now); assert(e.Revision() == keeping + 1);
    e.SetValue(num, 15); const auto setting = e.Revision(); e.Commit(num, h->now);
    assert(e.Revision() == setting + 1);
    h->Tap(key::Start); assert(e.Ui().kind == UiKind::Settings); e.CancelUi();
    h->Push(key::X); h->Push(key::X | key::R | key::Start);
    assert(e.Ui().kind == UiKind::None); h->Release(500);
}
} // namespace

int main() {
    TestItemActionsTable();
    TestFixedNotReady();
    TestToggleFixed();
    TestFavoriteActionsAndResume();
    TestActionRequestsAndRevision();
    TestFavoriteCapacity();
    TestStateHotkeyRules();
    TestStateOffAndHotkeyOnlyChange();
    TestHotkeyOnlyChangeKeepsEffect();
    TestStateWithoutHotkey();
    TestHold();
    TestOnce();
    TestAction();
    TestFaint();
    TestTriggerInput();
    TestModeAndGroup();
    TestStateGroup();
    TestSettingValue();
    TestValueFormats();
    TestEnabledIf();
    TestGameValue();
    TestGameValueUnreadable();
    TestFixed();
    TestCloseDialog();
    TestLongPress();
    TestNavigation();
    TestReopenPosition();
    TestReopenPathFallback();
    TestRepeat();
    TestHiddenAndReserved();
    TestKeyControl();
    TestBind();
    TestHotkeyText();
    TestTable();
    TestNoticeShape();
    TestRowDisplay();
    std::puts("menu: ok");
    return 0;
}
