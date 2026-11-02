// 次の起動に残す物（段 4a）を、エンジンと見本の表で確かめる。
// 往復（持ち越す物が戻り、持ち越さない物は書かれない・固定とお気に入りの順）、壊れた入力（印・版・長さ・件数・鍵・種類・
// 範囲・予約と重なるホットキー・途中で切れた入力）、起動時の戻しの待ちと通知の件数、版の数の増減。
#include "menu/engine.hpp"
#include "menu/persist.hpp"
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace gohan::menu;
namespace core = gohan::core;

namespace {
u16 Idx(MenuId id) { return static_cast<u16>(id); }
bool Is(const char *a, const char *b) { return std::strcmp(a, b) == 0; }
using Buffer = std::vector<u8>;

struct Sink {
    std::vector<Notice> notices;
};
void Collect(void *user, const Notice &notice) { static_cast<Sink *>(user)->notices.push_back(notice); }

// 見本の一覧を開いたエンジン（見本の結び付けと設定画面の項目）。見本の状態は毎回リセットする。
struct Fixture {
    Engine engine{kMenuItems, kMenuItemCount};
    Sink sink;
    u32 now = 1000;
    Fixture() {
        sample::Get() = sample::State{};
        engine.SetReservedHotkeys(key::Select);
        engine.SetNoticeSink({Collect, &sink});
        assert(engine.BindAll(sample::kBindings, sample::kBindingCount));
        engine.BindSettings({Idx(kSettingsRoot), Idx(MenuId::SettingsKeepErase), Idx(MenuId::SettingsKeepToggles),
                             Idx(MenuId::SettingsPressThrough)});
        engine.Open(now);
    }
    void Frame(u32 frames = 1) {
        for (u32 n = 0; n < frames; ++n) {
            now += 16;
            engine.Frame(now, 0);
        }
    }
};

const persist::Record *Find(const persist::Saved &saved, u16 item) {
    for (std::size_t n = 0; n < saved.count; ++n)
        if (saved.records[n].item == item) return &saved.records[n];
    return nullptr;
}

bool SameSaved(const persist::Saved &x, const persist::Saved &y) {
    if (x.keep_toggles != y.keep_toggles || x.press_keys != y.press_keys || x.count != y.count) return false;
    for (std::size_t n = 0; n < x.count; ++n) {
        const persist::Record &p = x.records[n];
        const persist::Record &q = y.records[n];
        if (p.item != q.item || p.fields != q.fields || p.value != q.value || p.hotkey != q.hotkey || p.order != q.order) return false;
    }
    return true;
}

// 試験の入力を作る。印・版・旗・予備・押し切るボタン・記録の数。
Buffer Header(std::size_t count, u8 flags = 0, u16 keys = 0) {
    return Buffer{'G', 'H', 'N', '3', 1, 0, flags, 0, static_cast<u8>(keys), static_cast<u8>(keys >> 8), static_cast<u8>(count),
                  static_cast<u8>(count >> 8)};
}
// 記録を 1 つ足す（鍵の長さを指定できる。欄は旗が示す順）。
void PutRaw(Buffer &d, u8 id_length, const std::string &id, u8 fields, s32 value = 0, u16 hotkey = 0, u16 order = 0) {
    d.push_back(id_length);
    d.insert(d.end(), id.begin(), id.end());
    d.push_back(fields);
    if (fields & persist::kValue)
        for (std::size_t i = 0; i < 4; ++i) d.push_back(static_cast<u8>(static_cast<u32>(value) >> (8 * i)));
    if (fields & persist::kHotkey) {
        d.push_back(static_cast<u8>(hotkey));
        d.push_back(static_cast<u8>(hotkey >> 8));
    }
    if (fields & persist::kFavorite) {
        d.push_back(static_cast<u8>(order));
        d.push_back(static_cast<u8>(order >> 8));
    }
}
void Put(Buffer &d, const std::string &id, u8 fields, s32 value = 0, u16 hotkey = 0, u16 order = 0) {
    PutRaw(d, static_cast<u8>(id.size()), id, fields, value, hotkey, order);
}

// 読み込みの検査に使う、見本の表の決まり（予約は SELECT、押し切るボタンは A・B・X・Y・START）。
persist::Rules SampleRules() {
    persist::Rules rules;
    rules.reserved = key::Select;
    rules.press_valid = key::A | key::B | key::X | key::Y | key::Start;
    rules.settings_root = Idx(kSettingsRoot);
    return rules;
}
bool Load(const Buffer &d, persist::Saved &saved) {
    return persist::Decode(d.data(), d.size(), kMenuItems, kMenuItemCount, SampleRules(), saved);
}

// 1. 往復: 6.1 の表の各行が戻り、持ち越さない物は書かれない。固定とお気に入りの並びの順も戻る。
void RoundTrip() {
    Fixture a;
    Engine &e = a.engine;
    const u16 flag = Idx(MenuId::SampleFlag), hold = Idx(MenuId::SampleHold), stamp = Idx(MenuId::SampleStamp),
              editor = Idx(MenuId::SampleEditorA), mode = Idx(MenuId::SampleMode), amount = Idx(MenuId::SampleAmount),
              wallet = Idx(MenuId::SampleWallet), keep = Idx(MenuId::SettingsKeepToggles), press = Idx(MenuId::SettingsPressThrough);
    // 固定: 見本のフラグ（ホットキーを外して ON にし、固定する）と見本の所持金（77）。
    assert(e.SetHotkey(flag, 0) && e.Commit(flag, a.now));
    assert(e.SetValue(flag, 1) && e.Commit(flag, a.now) && sample::Get().flag);
    assert(e.SetFixed(flag, true, a.now));
    assert(e.SetValue(wallet, 77) && e.SetFixed(wallet, true, a.now) && sample::Get().wallet == 77);
    // 持ち越す設定: トグルを全て保持（ON）・固定していない ON の押下中・量 7.0・方法（マス単位）・画面 A のホットキー・押し切る A と X。
    assert(e.SetValue(keep, 1) && e.KeepToggles());
    assert(e.SetValue(hold, 1) && e.Commit(hold, a.now));
    assert(e.SetValue(amount, 70) && e.Commit(amount, a.now));
    assert(e.SetValue(mode, 1) && e.Commit(mode, a.now));
    assert(e.SetHotkey(editor, key::ZL) && e.Commit(editor, a.now));
    assert(e.SetValue(press, 5) && e.DeferKeys() == (key::A | key::X)); // 選択肢の番号 0 と 2（A と X）
    // お気に入り: 追加は スタンプ → フラグ の順。並べ替えでフラグを先頭にする。
    assert(e.SetFavorite(stamp, true) && e.SetFavorite(flag, true));
    const u16 order[] = {flag, stamp};
    assert(e.SetFavoriteOrder(order, 2));

    persist::Saved saved;
    e.Capture(saved);
    assert(saved.keep_toggles && saved.press_keys == (key::A | key::X));
    // 固定の並びは記録の順。フラグ（固定の ON・お気に入り・ホットキー 0）→ 所持金（固定の値）。
    assert(saved.count == 7);
    assert(saved.records[0].item == flag && saved.records[1].item == wallet);
    const persist::Record *r = Find(saved, flag);
    assert(r && r->fields == (persist::kHotkey | persist::kFavorite | persist::kFixed | persist::kOn) && r->hotkey == 0 && r->order == 0);
    r = Find(saved, wallet);
    assert(r && r->fields == (persist::kFixed | persist::kValue) && r->value == 77);
    r = Find(saved, hold); // 固定していない押下中の ON は、トグルを全て保持の時だけ
    assert(r && r->fields == persist::kOn);
    r = Find(saved, stamp); // 実行は持ち越さない。お気に入りだけ
    assert(r && r->fields == persist::kFavorite && r->order == 1);
    r = Find(saved, editor);
    assert(r && r->fields == persist::kHotkey && r->hotkey == key::ZL);
    r = Find(saved, mode);
    assert(r && r->fields == persist::kValue && r->value == 1);
    r = Find(saved, amount);
    assert(r && r->fields == persist::kValue && r->value == 70);
    // 持ち越さない物は書かれない: 見本のフォルダ・設定画面の項目（押し切るボタンとトグルを全て保持はヘッダー）・既定のままの物。
    assert(!Find(saved, Idx(MenuId::SampleValues)) && !Find(saved, keep) && !Find(saved, press));
    assert(!Find(saved, Idx(MenuId::SampleDevNote)));

    // 書いて、読んで、同じ物になる。
    Buffer buffer(core::limits::PersistBytes);
    const std::size_t size = persist::Encode(saved, kMenuItems, kMenuItemCount, buffer.data(), buffer.size());
    assert(size > persist::kHeaderBytes && size <= core::limits::PersistBytes);
    persist::Saved decoded;
    assert(persist::Decode(buffer.data(), size, kMenuItems, kMenuItemCount, e.PersistRules(), decoded));
    assert(SameSaved(saved, decoded));

    // 別のエンジンに戻す。戻す物は即座に入り、待つ物（所持金・ON のフラグ）は次のフレームで戻る。
    Fixture b;
    b.engine.Restore(decoded);
    assert(b.engine.RestoreWaiting() == 2);
    assert(b.engine.Revision() == 0); // 戻しでは版の数を増やさない
    assert(b.engine.KeepToggles() && b.engine.DeferKeys() == (key::A | key::X));
    assert(b.engine.Applied(amount) == 70 && b.engine.Applied(mode) == 1 && b.engine.State(editor).hotkey == key::ZL);
    assert(b.engine.Applied(hold) == 1 && b.engine.State(flag).hotkey == 0 && b.engine.State(flag).applied_hotkey == 0);
    assert(b.engine.FixedCount() == 2 && b.engine.FixedOrder()[0] == flag && b.engine.FixedOrder()[1] == wallet);
    assert(b.engine.FavoriteCount() == 2 && b.engine.FavoriteOrder()[0] == flag && b.engine.FavoriteOrder()[1] == stamp);
    persist::Saved again;
    b.engine.Capture(again);
    assert(SameSaved(saved, again));
    b.Frame();
    assert(b.engine.RestoreWaiting() == 0 && sample::Get().flag && sample::Get().wallet == 77 && b.engine.Revision() == 0);
    // 通知は 1 つ。戻した数は 3（フラグ・押下中・所持金）。効果の通知は出さない。
    assert(b.sink.notices.size() == 1);
    assert(Is(b.sink.notices[0].title, "前回の状態") && Is(b.sink.notices[0].message, "3 件を戻しました") && !b.sink.notices[0].red);
}

// 2. 持ち越さない物: ゲームの値は固定していなければ書かない（読めても書かない）。
void UnfixedGameValueIsNotWritten() {
    Fixture f;
    assert(f.engine.SetValue(Idx(MenuId::SampleWallet), 5));
    persist::Saved saved;
    f.engine.Capture(saved);
    assert(saved.count == 0 && !saved.keep_toggles && saved.press_keys == key::Start);
}

// 3. 壊れた入力: 落ちず、既定か、その欄だけ捨てて読む。
void BrokenInputs() {
    persist::Saved saved;
    // 大きさ 0・短い・旧版の印（GMP1）・版違い・記録の数のあふれ（項目の数より多い）は、全て既定で始める（false）。
    assert(!Load(Buffer{}, saved) && saved.count == 0);
    Buffer short_header = Header(0);
    short_header.resize(11);
    assert(!Load(short_header, saved));
    Buffer legacy = Header(0);
    legacy[3] = '1';
    assert(!Load(legacy, saved));
    Buffer version = Header(0);
    version[4] = 2;
    assert(!Load(version, saved));
    assert(!Load(Header(kMenuItemCount + 1), saved));
    // 空の入力（記録が無い）は読める。
    assert(Load(Header(0), saved) && saved.count == 0);
    // 記録の数が実際より多い（途中で切れた）時は、読めた分だけ残す。
    Buffer short_count = Header(3);
    Put(short_count, "sample.amount", persist::kValue, 70);
    assert(Load(short_count, saved) && saved.count == 1 && saved.records[0].value == 70);

    // 鍵の長さが 0 と 65: その記録だけ捨て、次の記録は読む（欄の長さは読んでいるので、ずれない）。
    Buffer lengths = Header(3);
    PutRaw(lengths, 0, "", persist::kValue, 5);
    PutRaw(lengths, 65, std::string(65, 's'), persist::kValue, 6);
    Put(lengths, "sample.amount", persist::kValue, 70);
    assert(Load(lengths, saved) && saved.count == 1 && saved.records[0].item == Idx(MenuId::SampleAmount) && saved.records[0].value == 70);

    // 未知の鍵・種類の合わない欄・設定画面の物・フォルダのホットキー・値のないトリガー。
    Buffer kinds = Header(8);
    Put(kinds, "no.such.item", persist::kValue, 1);
    Put(kinds, "sample.stamp", persist::kValue, 1);                   // 実行は値を持たない: 記録ごと捨てる
    Put(kinds, "sample.flag", persist::kValue | persist::kFixed, 1);  // 状態は値を持たない: 値だけ捨て、固定 OFF は残す
    Put(kinds, "sample.values", persist::kHotkey, 0, key::L);         // フォルダはホットキーを持てない
    Put(kinds, "settings.keep.toggles", persist::kValue, 1);          // 設定画面の物は記録に書かない（ヘッダーの旗）
    Put(kinds, "settings.press_through", persist::kValue, key::A);
    Put(kinds, "sample.wallet", persist::kValue, 5);                  // ゲームの値は固定していなければ持たない
    Put(kinds, "sample.hold", persist::kOn);                          // 固定していない ON は、トグルを全て保持が OFF なら持たない
    assert(Load(kinds, saved) && saved.count == 1 && saved.records[0].item == Idx(MenuId::SampleFlag) &&
           saved.records[0].fields == persist::kFixed);

    // 範囲外の値は範囲に収める（SetValue と同じ）。見本の量は 0.1〜99.9（整数では 1〜999）、方法は 0〜1。
    Buffer ranges = Header(2);
    Put(ranges, "sample.amount", persist::kValue, 9999);
    Put(ranges, "sample.mode", persist::kValue, 9);
    assert(Load(ranges, saved) && saved.count == 2 && saved.records[0].value == 999 && saved.records[1].value == 1);

    // 押し切るボタン: 選択肢に無いボタン（L）は捨てる。
    Buffer keys = Header(0, 0, static_cast<u16>(key::L | key::A));
    assert(Load(keys, saved) && saved.press_keys == key::A);

    // 予約（SELECT）を含むホットキー・範囲外のビットは、欄だけ捨てる（既定のまま）。他の項目と重ならない物と、外す（0）は有効。
    Buffer hotkeys = Header(4);
    Put(hotkeys, "sample.editor_a", persist::kHotkey, 0, key::Select);
    Put(hotkeys, "sample.editor_b", persist::kHotkey, 0, 0x8000);
    Put(hotkeys, "sample.stamp", persist::kHotkey, 0, key::L | key::Down);
    Put(hotkeys, "sample.flag", persist::kHotkey, 0, 0);
    assert(Load(hotkeys, saved) && saved.count == 2);
    assert(Find(saved, Idx(MenuId::SampleEditorA)) == nullptr && Find(saved, Idx(MenuId::SampleEditorB)) == nullptr);
    assert(Find(saved, Idx(MenuId::SampleStamp))->hotkey == (key::L | key::Down));
    assert(Find(saved, Idx(MenuId::SampleFlag))->hotkey == 0);
    // 同じ項目の 2 つ目の記録は捨てる（最初の物を残す）。
    Buffer twice = Header(2);
    Put(twice, "sample.amount", persist::kValue, 70);
    Put(twice, "sample.amount", persist::kValue, 60);
    assert(Load(twice, saved) && saved.count == 1 && saved.records[0].value == 70);
    // 2 つの記録が同じ物を指す時は、両方とも捨てる（順番に依らない）。
    Buffer same = Header(2);
    Put(same, "sample.editor_a", persist::kHotkey, 0, key::ZL);
    Put(same, "sample.editor_b", persist::kHotkey, 0, key::ZL);
    assert(Load(same, saved) && saved.count == 0);
    // 他の項目の既定と同じ物（見本の画面 B に、フラグの既定 L+Up を付ける）は捨てる。
    Buffer defaults = Header(1);
    Put(defaults, "sample.editor_b", persist::kHotkey, 0, key::L | key::Up);
    assert(Load(defaults, saved) && saved.count == 0);

    // お気に入り: 並びの値（10・3）は 0 から振り直し、小さい方が先。
    Buffer favorites = Header(2);
    Put(favorites, "sample.stamp", persist::kFavorite, 0, 0, 10);
    Put(favorites, "sample.flag", persist::kFavorite, 0, 0, 3);
    assert(Load(favorites, saved) && saved.count == 2);
    assert(saved.records[0].item == Idx(MenuId::SampleStamp) && saved.records[0].order == 1);
    assert(saved.records[1].item == Idx(MenuId::SampleFlag) && saved.records[1].order == 0);
}

// 4. 途中で切れた入力: どの長さで切っても落ちず、読めた物は正しい項目だけ。
void TruncatedInputs() {
    Buffer whole = Header(4);
    Put(whole, "sample.amount", persist::kValue, 70);
    Put(whole, "sample.flag", persist::kFixed | persist::kOn | persist::kHotkey, 0, key::L | key::Up);
    Put(whole, "sample.wallet", persist::kFixed | persist::kValue, 5);
    Put(whole, "sample.stamp", persist::kFavorite, 0, 0, 0);
    for (std::size_t length = 0; length <= whole.size(); ++length) {
        Buffer prefix(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
        persist::Saved saved;
        const bool ok = Load(prefix, saved);
        assert(ok == (length >= persist::kHeaderBytes));
        assert(saved.count <= 4);
        for (std::size_t n = 0; n < saved.count; ++n) {
            assert(saved.records[n].item < kMenuItemCount && saved.records[n].fields != 0);
        }
    }
    // 途中で切れた記録より前は残す。
    persist::Saved saved;
    Buffer cut(whole.begin(), whole.end() - 3);
    assert(Load(cut, saved) && saved.count == 3);
}

// 5. 書き出し: 入らない・鍵が不正なら 0。鍵は spec の id で引ける。
void EncodeAndLookup() {
    persist::Saved saved;
    saved.count = 1;
    saved.records[0].item = Idx(MenuId::SampleAmount);
    saved.records[0].fields = persist::kValue;
    saved.records[0].value = 70;
    Buffer buffer(core::limits::PersistBytes);
    assert(persist::Encode(saved, kMenuItems, kMenuItemCount, buffer.data(), persist::kHeaderBytes) == 0); // 入らない
    const std::size_t size = persist::Encode(saved, kMenuItems, kMenuItemCount, buffer.data(), buffer.size());
    assert(size == persist::kHeaderBytes + 1 + std::strlen("sample.amount") + 1 + 4);
    assert(buffer[0] == 'G' && buffer[1] == 'H' && buffer[2] == 'N' && buffer[3] == '3');
    saved.records[0].item = kMenuItemCount; // 範囲外の項目
    assert(persist::Encode(saved, kMenuItems, kMenuItemCount, buffer.data(), buffer.size()) == 0);
    assert(persist::FindItem(kMenuItems, kMenuItemCount, "sample.amount", 13) == Idx(MenuId::SampleAmount));
    assert(persist::FindItem(kMenuItems, kMenuItemCount, "sample.amou", 11) == kNoItem);
    assert(persist::FindItem(kMenuItems, kMenuItemCount, "sample.amountx", 14) == kNoItem);
    assert(persist::FindItem(kMenuItems, kMenuItemCount, "", 0) == kNoItem);
    // 設定画面と開発版の項目も、鍵で引ける。
    assert(persist::FindItem(kMenuItems, kMenuItemCount, "settings.press_through", 22) == Idx(MenuId::SettingsPressThrough));
    assert(persist::FindItem(kMenuItems, kMenuItemCount, "sample.dev_note", 15) == Idx(MenuId::SampleDevNote));
}

// 小さな表: 既定が ON のトグル（lamp）と、同じ group の 2 つのトグル（rad_a・rad_b）。
ItemDef Toggle(const char *key_name, s32 def, u16 group) {
    ItemDef d{};
    d.key = key_name;
    d.label = key_name;
    d.desc = "";
    d.control = Control::Toggle;
    d.behavior = Behavior::State;
    d.persist = true;
    d.def = def;
    d.min = 0;
    d.max = 1;
    d.step = 1;
    d.group = group;
    d.parent = kNoItem;
    d.first_child = kNoItem;
    d.next = kNoItem;
    return d;
}
const ItemDef *SmallTable() {
    static const ItemDef table[3] = {Toggle("lamp", 1, 0), Toggle("rad_a", 0, 1), Toggle("rad_b", 0, 1)};
    return table;
}
bool LoadSmall(const Buffer &d, persist::Saved &saved) {
    return persist::Decode(d.data(), d.size(), SmallTable(), 3, persist::Rules{}, saved);
}

// 6. 既定が ON の物を OFF で固定した時は OFF のまま。同じ group の ON は 1 つだけ。
void SmallTableRules() {
    persist::Saved saved;
    // lamp（既定 ON）の OFF の固定、rad_a と rad_b の固定 ON（同じ group）。
    Buffer d = Header(3, 0);
    Put(d, "lamp", persist::kFixed);
    Put(d, "rad_a", persist::kFixed | persist::kOn);
    Put(d, "rad_b", persist::kFixed | persist::kOn);
    assert(LoadSmall(d, saved) && saved.count == 3);
    assert(saved.records[0].item == 0 && saved.records[0].fields == persist::kFixed);
    assert(saved.records[1].item == 1 && saved.records[1].fields == (persist::kFixed | persist::kOn));
    assert(saved.records[2].item == 2 && saved.records[2].fields == persist::kFixed); // 2 つ目の ON は OFF の固定になる
    // 固定していない ON は、トグルを全て保持が ON の時だけ。
    Buffer off = Header(1, 0);
    Put(off, "rad_a", persist::kOn);
    assert(LoadSmall(off, saved) && saved.count == 0);
    Buffer on = Header(1, persist::kKeepToggles);
    Put(on, "rad_a", persist::kOn);
    assert(LoadSmall(on, saved) && saved.count == 1 && saved.records[0].fields == persist::kOn);
}

// 7. 形式の往復（小さな表）: 書いて読んだ物が同じ。固定の並びの順。
void SmallRoundTrip() {
    persist::Saved saved;
    saved.keep_toggles = true;
    saved.press_keys = key::Start;
    saved.count = 2;
    saved.records[0] = {1, persist::kFixed | persist::kOn, 0, 0, 0};
    saved.records[1] = {0, persist::kFixed, 0, 0, 0};
    Buffer buffer(core::limits::PersistBytes);
    const std::size_t size = persist::Encode(saved, SmallTable(), 3, buffer.data(), buffer.size());
    assert(size == persist::kHeaderBytes + (1 + 5 + 1) + (1 + 4 + 1));
    persist::Saved back;
    persist::Rules rules;
    rules.press_valid = key::Start; // 押し切るボタンの選択肢（この表では START）
    assert(persist::Decode(buffer.data(), size, SmallTable(), 3, rules, back));
    assert(SameSaved(saved, back));
}

// 8. 全てのバイト列の切り方と、1 バイトずつの書き換えで、落ちない（読めた物は正しい項目で、欄の旗の矛盾は残らない）。
void MutatedInputs() {
    Buffer base = Header(3, persist::kKeepToggles);
    Put(base, "sample.amount", persist::kValue, 70);
    Put(base, "sample.flag", persist::kFixed | persist::kOn | persist::kHotkey, 0, key::L | key::Up);
    Put(base, "sample.stamp", persist::kFavorite, 0, 0, 0);
    unsigned seed = 1;
    for (int round = 0; round < 2000; ++round) {
        Buffer mutated = base;
        seed = seed * 1103515245u + 12345u;
        mutated[(seed >> 8) % mutated.size()] = static_cast<u8>(seed >> 16);
        persist::Saved saved;
        if (!Load(mutated, saved)) continue;
        assert(saved.count <= kMenuItemCount);
        for (std::size_t n = 0; n < saved.count; ++n) {
            const persist::Record &r = saved.records[n];
            assert(r.item < kMenuItemCount && r.fields != 0 && (r.fields & ~persist::kFieldMask) == 0);
            assert(!(r.fields & persist::kOn) || (r.fields & persist::kFixed) || (saved.keep_toggles));
        }
    }
}

// ---- 起動時の戻し（8 節） ----

// 待つ物: ゲームの値（所持金）は、読めた最初のフレームに数えて、そこで書く。読めるまで通知は出さない。
void RestoreWaitsForReadableGame() {
    Fixture f;
    sample::Get().wallet_readable = false;
    f.engine.Open(f.now); // 読めない状態で開く（編集できない）
    persist::Saved saved;
    saved.count = 2;
    saved.records[0] = {Idx(MenuId::SampleWallet), persist::kFixed | persist::kValue, 55, 0, 0};
    saved.records[1] = {Idx(MenuId::SampleHold), persist::kOn, 0, 0, 0};
    saved.keep_toggles = true;
    f.engine.Restore(saved);
    assert(f.engine.RestoreWaiting() == 1 && f.engine.Revision() == 0);
    f.Frame(3);
    assert(f.engine.RestoreWaiting() == 1 && sample::Get().wallet == 0 && sample::Get().wallet_writes == 0 && f.sink.notices.empty());
    sample::Get().wallet_readable = true;
    f.Frame();
    assert(f.engine.RestoreWaiting() == 0 && sample::Get().wallet == 55 && sample::Get().wallet_writes == 1);
    assert(f.sink.notices.size() == 1 && Is(f.sink.notices[0].message, "2 件を戻しました") && !f.sink.notices[0].red);
    // 片付いた後は、もう通知を出さない。
    f.Frame(5);
    assert(f.sink.notices.size() == 1);
}

// ON のフラグは、効果が入ったフレームに数える。NotReady の間は黙って待ち、Ok で「戻した」。効果の通知は出さない。
unsigned g_calls = 0;
Result SetSlowly(bool on) {
    if (!on) return Result::Ok;
    ++g_calls;
    return g_calls < 3 ? Result::NotReady : Result::Ok;
}
Result SetBroken(bool on) {
    if (on) ++g_calls;
    return on ? Result::Failed : Result::Ok;
}
void RestoreWaitsForPatch() {
    Sink sink;
    Engine e(kMenuItems, kMenuItemCount);
    g_calls = 0;
    Bound bound[] = {GOHAN_ITEM(MenuId::SampleFlag, Patch{.set = SetSlowly})};
    assert(e.BindAll(bound, 1));
    e.SetNoticeSink({Collect, &sink});
    persist::Saved saved;
    saved.count = 1;
    // ホットキーを外した ON（既定の L+Up が付いたままだと待機で終わるため）。
    saved.records[0] = {Idx(MenuId::SampleFlag), persist::kFixed | persist::kOn | persist::kHotkey, 0, 0, 0};
    e.Restore(saved);
    assert(e.RestoreWaiting() == 1 && g_calls == 0);
    u32 now = 1000;
    for (int n = 0; n < 2; ++n) e.Frame(now += 16, 0);
    assert(g_calls == 2 && e.RestoreWaiting() == 1 && sink.notices.empty() && !e.EffectOn(Idx(MenuId::SampleFlag)));
    e.Frame(now += 16, 0);
    assert(g_calls == 3 && e.RestoreWaiting() == 0 && e.EffectOn(Idx(MenuId::SampleFlag)));
    // 通知は 1 つ。効果の通知（「有効にしました」）は出さない。
    assert(sink.notices.size() == 1 && Is(sink.notices[0].title, "前回の状態") && Is(sink.notices[0].message, "1 件を戻しました"));
    e.Frame(now += 16, 0);
    assert(sink.notices.size() == 1 && g_calls == 3);
}

// 戻せない（Failed）: 「戻せない」に数え、赤で「0 件を戻し、1 件は失敗」。失敗の通知は 1 つだけ（固定の間は黙って試す）。
void RestoreFailureIsCountedOnce() {
    Sink sink;
    Engine e(kMenuItems, kMenuItemCount);
    g_calls = 0;
    Bound bound[] = {GOHAN_ITEM(MenuId::SampleFlag, Patch{.set = SetBroken})};
    assert(e.BindAll(bound, 1));
    e.SetNoticeSink({Collect, &sink});
    persist::Saved saved;
    saved.count = 1;
    saved.records[0] = {Idx(MenuId::SampleFlag), persist::kFixed | persist::kOn | persist::kHotkey, 0, 0, 0};
    e.Restore(saved);
    u32 now = 1000;
    e.Frame(now += 16, 0);
    assert(e.RestoreWaiting() == 0 && sink.notices.size() == 1);
    assert(Is(sink.notices[0].title, "前回の状態") && Is(sink.notices[0].message, "0 件を戻し、1 件は失敗") && sink.notices[0].red);
    e.Frame(now += 16, 0);
    e.Frame(now += 16, 0);
    assert(sink.notices.size() == 1);
}

// 数える物が無ければ通知は出さない（設定の値・ホットキー・お気に入りだけ）。OFF の固定は数えない（既定が ON の物も）。
void NoNoticeWithoutCountedItems() {
    Sink sink;
    Engine e(kMenuItems, kMenuItemCount);
    g_calls = 0;
    Bound bound[] = {GOHAN_ITEM(MenuId::SampleFlag, Patch{.set = SetSlowly})};
    assert(e.BindAll(bound, 1));
    e.SetNoticeSink({Collect, &sink});
    persist::Saved saved;
    saved.count = 3;
    saved.records[0] = {Idx(MenuId::SampleAmount), persist::kValue, 70, 0, 0};
    saved.records[1] = {Idx(MenuId::SampleStamp), persist::kFavorite, 0, 0, 0};
    saved.records[2] = {Idx(MenuId::SampleFlag), persist::kFixed, 0, 0, 0}; // OFF の固定
    e.Restore(saved);
    assert(e.RestoreWaiting() == 0 && e.Applied(Idx(MenuId::SampleAmount)) == 70 && e.FavoriteCount() == 1);
    assert(!e.EffectOn(Idx(MenuId::SampleFlag)) && e.Applied(Idx(MenuId::SampleFlag)) == 0 && g_calls == 0);
    u32 now = 1000;
    e.Frame(now += 16, 0);
    e.Frame(now += 16, 0);
    assert(sink.notices.empty() && g_calls == 0);
}

// 既定が ON のトグルを OFF で固定すると、OFF で始め、効果を入れない。ON で固定したトグルは、そのまま数える（待ちは無い）。
void DefaultOnToggleStaysOffWhenFixed() {
    Sink sink;
    static unsigned lamp_calls = 0, other_calls = 0;
    lamp_calls = other_calls = 0;
    Engine e(SmallTable(), 3);
    Bound lamp{};
    lamp.item = 0;
    lamp.behavior = Behavior::State;
    lamp.patch.set = [](bool) -> Result { ++lamp_calls; return Result::Ok; };
    Bound other{};
    other.item = 1;
    other.behavior = Behavior::State;
    other.patch.set = [](bool) -> Result { ++other_calls; return Result::Ok; };
    assert(e.Bind(lamp) && e.Bind(other));
    e.SetNoticeSink({Collect, &sink});
    persist::Saved saved;
    saved.count = 2;
    saved.records[0] = {0, persist::kFixed, 0, 0, 0};
    saved.records[1] = {1, persist::kFixed | persist::kOn, 0, 0, 0};
    e.Restore(saved);
    assert(e.Applied(0) == 0 && e.FixedCount() == 2 && e.RestoreWaiting() == 1);
    e.Frame(1016, 0);
    assert(lamp_calls == 0 && other_calls == 1 && !e.EffectOn(0) && e.EffectOn(1) && e.RestoreWaiting() == 0);
    assert(sink.notices.size() == 1 && Is(sink.notices[0].message, "1 件を戻しました"));
    // 持ち越す時の OFF の固定は、そのまま OFF の固定として残る（書き直しても同じ）。
    persist::Saved again;
    e.Capture(again);
    assert(again.count == 2 && again.records[0].fields == persist::kFixed && again.records[1].fields == (persist::kFixed | persist::kOn));
}

// ホットキーで ON のフラグ（ホットキーあり）は、待機で終わり、効果は入れない（数える）。押下中も待機で数える。
void HotkeyAndHoldCountAtOnce() {
    Fixture f;
    persist::Saved saved;
    saved.count = 2;
    saved.records[0] = {Idx(MenuId::SampleFlag), persist::kFixed | persist::kOn, 0, 0, 0};
    saved.records[1] = {Idx(MenuId::SampleHold), persist::kFixed | persist::kOn, 0, 0, 0};
    f.engine.Restore(saved);
    assert(f.engine.RestoreWaiting() == 0 && !sample::Get().flag && f.engine.Applied(Idx(MenuId::SampleFlag)) == 1);
    f.Frame();
    assert(f.sink.notices.size() == 1 && Is(f.sink.notices[0].message, "2 件を戻しました") && !sample::Get().flag);
    assert(!f.engine.EffectOn(Idx(MenuId::SampleFlag)));
}

// 版の数: 覚える物が変わるたびに増え、覚えない物では増えない。戻しと待ちの処理では増えない。
void RevisionFollowsRememberedValues() {
    Fixture f;
    Engine &e = f.engine;
    const u16 flag = Idx(MenuId::SampleFlag), stamp = Idx(MenuId::SampleStamp), editor = Idx(MenuId::SampleEditorA),
              amount = Idx(MenuId::SampleAmount), mode = Idx(MenuId::SampleMode), hold = Idx(MenuId::SampleHold),
              keep = Idx(MenuId::SettingsKeepToggles), keep_folder = Idx(MenuId::SettingsKeep), erase = Idx(MenuId::SettingsKeepErase);
    u32 r = e.Revision();
    // 覚えないトグル（固定していない・トグルを全て保持が OFF）の ON/OFF は増えない。
    assert(e.SetValue(flag, 1) && e.Commit(flag, f.now) && e.Revision() == r);
    // 固定: 増える。固定したトグルの ON/OFF の変更で増える。固定を外しても増える。
    assert(e.SetFixed(flag, true, f.now) && e.Revision() == r + 1);
    r = e.Revision();
    assert(e.SetValue(flag, 0) && e.Commit(flag, f.now) && e.Revision() == r + 1);
    r = e.Revision();
    assert(e.SetFixed(flag, false, f.now) && e.Revision() == r + 1);
    r = e.Revision();
    // お気に入り: 追加・外す・並び。同じ並びへの変更は増えない。
    assert(e.SetFavorite(stamp, true) && e.Revision() == r + 1);
    r = e.Revision();
    assert(e.SetFavorite(stamp, true) && e.Revision() == r);
    assert(e.SetFavorite(flag, true) && e.Revision() == r + 1);
    r = e.Revision();
    const u16 order[] = {flag, stamp};
    assert(e.SetFavoriteOrder(order, 2) && e.Revision() == r + 1);
    r = e.Revision();
    assert(e.SetFavoriteOrder(order, 2) && e.Revision() == r);
    // ホットキー: 編集だけでは増えない。適用で増える。
    assert(e.SetHotkey(editor, key::ZL) && e.Revision() == r);
    assert(e.Commit(editor, f.now) && e.Revision() == r + 1);
    r = e.Revision();
    // 設定の値: 編集だけでは増えない。適用で増える。
    assert(e.SetValue(amount, 70) && e.Revision() == r);
    assert(e.Commit(amount, f.now) && e.Revision() == r + 1);
    r = e.Revision();
    assert(e.SetValue(mode, 1) && e.Commit(mode, f.now) && e.Revision() == r + 1);
    r = e.Revision();
    // 実行（覚えない）: 増えない。見本の実行は動く。
    assert(e.SetValue(Idx(MenuId::SampleStamp), 1) && e.Commit(Idx(MenuId::SampleStamp), f.now) && e.Revision() == r);
    assert(sample::Get().stamps == 1);
    // トグルを全て保持: 切り替えで増える。覚える押下中の ON/OFF も増える。
    assert(e.SetValue(keep, 1) && e.Revision() == r + 1);
    r = e.Revision();
    assert(e.SetValue(hold, 1) && e.Commit(hold, f.now) && e.Revision() == r + 1);
    r = e.Revision();
    assert(e.SetValue(hold, 0) && e.Commit(hold, f.now) && e.Revision() == r + 1);
    r = e.Revision();
    e.SetKeepToggles(true);
    assert(e.Revision() == r); // 同じ値への切り替えは増えない
    // 効果だけの変化（ホットキーで効果を入れ替える）は覚えない。
    assert(e.SetValue(flag, 1) && e.Commit(flag, f.now));
    r = e.Revision();
    assert(e.RequestClose(f.now));
    e.Frame(f.now += 16, key::L | key::Up);
    assert(sample::Get().flag && e.Revision() == r);
    // 保存を全て消す: 増える。
    e.Open(f.now);
    assert(e.OpenSettings(f.now) && e.Select(keep_folder));
    e.Press(key::A, f.now);
    assert(e.Select(erase));
    r = e.Revision();
    e.Press(key::A, f.now);
    assert(e.Dialog() == DialogKind::Erase);
    e.Press(key::Left, f.now);
    e.Press(key::A, f.now);
    assert(e.Dialog() == DialogKind::None && e.EraseCount() == 1 && e.Revision() == r + 1);
}

// 戻し（Restore）の版の数は増えない。読み込んだ直後に保存を頼まないため。
void RestoreDoesNotBumpRevision() {
    Fixture f;
    persist::Saved saved;
    saved.keep_toggles = true;
    saved.press_keys = key::Start;
    saved.count = 1;
    saved.records[0] = {Idx(MenuId::SampleAmount), persist::kValue, 70, 0, 0};
    f.engine.Restore(saved);
    f.Frame(4);
    assert(f.engine.Revision() == 0 && f.engine.Applied(Idx(MenuId::SampleAmount)) == 70 && f.engine.KeepToggles());
}
} // namespace

int main() {
    RoundTrip();
    UnfixedGameValueIsNotWritten();
    BrokenInputs();
    TruncatedInputs();
    EncodeAndLookup();
    SmallTableRules();
    SmallRoundTrip();
    MutatedInputs();
    RestoreWaitsForReadableGame();
    RestoreWaitsForPatch();
    RestoreFailureIsCountedOnce();
    NoNoticeWithoutCountedItems();
    DefaultOnToggleStaysOffWhenFixed();
    HotkeyAndHoldCountAtOnce();
    RevisionFollowsRememberedValues();
    RestoreDoesNotBumpRevision();
    std::puts("menu persist: ok");
    return 0;
}
