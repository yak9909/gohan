#include "menu/engine.hpp"
#include "menu/hotkey.hpp"

// 保存の写し（Capture）と起動時の戻し（Restore）。ファイルには触れない（形式と検査は persist.{hpp,cpp}）。
// 戻しは 8 節の表のとおり: 読み込みの時に入れる物と、ゲームから読めた時・効果を入れられた時に数える物を分ける。
namespace gohan::menu {
namespace {
// 押し切るボタンの選択肢の名前 → キーのビット（ホットキーの名前と同じ。大文字小文字は問わない）。
u16 KeyOfOption(const char *name) {
    u16 bit = 0;
    return name && ParseHotkey(name, bit) ? bit : 0;
}

// 押し切るボタンのキーから、選択肢の番号のビット（SetValue の多重選択と同じ並び）。
u32 OptionMask(const ItemDef &d, u16 keys) {
    u32 mask = 0;
    for (std::size_t n = 0; n < d.option_count && n < 32; ++n)
        if ((keys & KeyOfOption(d.options[n])) != 0) mask |= 1u << n;
    return mask;
}
} // namespace

persist::Rules Engine::PersistRules() const {
    persist::Rules rules;
    rules.reserved = reserved_;
    rules.settings_root = settings_.root;
    rules.dev_visible = dev_visible_;
    if (Has(settings_.press_through)) {
        const ItemDef &d = defs_[settings_.press_through];
        for (std::size_t n = 0; n < d.option_count; ++n) rules.press_valid = static_cast<u16>(rules.press_valid | KeyOfOption(d.options[n]));
    }
    return rules;
}

void Engine::Capture(persist::Saved &out) const {
    out.keep_toggles = keep_toggles_;
    out.press_keys = DeferKeys();
    out.count = 0;
    // 固定している物は固定の順に書く（記録の順が固定の並びになる）。続けて、固定していない物を項目の順に書く。
    for (std::size_t n = 0; n < fixed_count_; ++n) AppendRecord(out, fixed_order_[n]);
    for (u16 i = 0; i < count_; ++i)
        if (!state_[i].fixed) AppendRecord(out, i);
}

// 項目の覚える欄を書く。覚える物が無ければ書かない。隠れている項目（開発版の項目を製品版で出していない時）は書かない。
void Engine::AppendRecord(persist::Saved &out, u16 item) const {
    if (!Has(item) || out.count >= core::limits::MenuItems || InSettingsTree(item) || Hidden(item)) return;
    const ItemDef &d = defs_[item];
    const ItemState &st = state_[item];
    persist::Record r;
    r.item = item;
    // ホットキーは既定と違う物だけ（フォルダと値のキーには無い）。
    if (d.control != Control::Folder && d.control != Control::Key && st.applied_hotkey != d.hotkey) {
        r.fields |= persist::kHotkey;
        r.hotkey = st.applied_hotkey;
    }
    for (std::size_t n = 0; n < favorite_count_; ++n)
        if (favorites_[n] == item) {
            r.fields |= persist::kFavorite;
            r.order = static_cast<u16>(n);
            break;
        }
    if (st.fixed) r.fields |= persist::kFixed;
    if (d.control == Control::Toggle && (d.behavior == Behavior::State || d.behavior == Behavior::Hold)) {
        // 固定のトグルは ON/OFF を書く。固定していない物は「トグルを全て保持」の時だけ ON を書く。
        if (st.applied != 0 && (st.fixed || (keep_toggles_ && d.persist))) r.fields |= persist::kOn;
    } else if (d.behavior == Behavior::Value && d.source == Source::Game) {
        // ゲームの値は固定した物だけ（固定値を書く）。
        if (st.fixed) {
            r.fields |= persist::kValue;
            r.value = st.fixed_value;
        }
    } else if (d.control == Control::Key) {
        // キーの項目は、ホットキーと同じく既定と違う物だけ書く（既定は spec が決める）。
        if (d.persist && st.applied != d.def) {
            r.fields |= persist::kValue;
            r.value = st.applied;
        }
    } else if (d.behavior == Behavior::Value && st.fixed) {
        // 設定の値は固定した物だけ（既定と同じ値でも書く。固定のまま往復させるため）。固定していない物は書かない。
        r.fields |= persist::kValue;
        r.value = st.fixed_value;
    }
    if (r.fields == 0) return;
    out.records[out.count++] = r;
}

void Engine::Restore(const persist::Saved &saved) {
    restore_notice_ = true;
    fixed_count_ = 0;
    favorite_count_ = 0;
    // メニューの設定: トグルを全て保持と、押し切るボタン（設定の値。適用待ちを経ずに入れる）。
    keep_toggles_ = settings_.keep != kNoItem && saved.keep_toggles;
    if (Has(settings_.keep)) SetRestored(settings_.keep, keep_toggles_ ? 1 : 0);
    if (Has(settings_.press_through))
        SetRestored(settings_.press_through, static_cast<s32>(OptionMask(defs_[settings_.press_through], saved.press_keys)));
    for (std::size_t n = 0; n < saved.count; ++n) RestoreRecord(saved.records[n]);
    // お気に入りの並び: Decode が 0 から振り直した順（連番）。隠れた項目を飛ばしても、残りの順は変えない。
    for (std::size_t rank = 0; rank < saved.count; ++rank) {
        for (std::size_t n = 0; n < saved.count; ++n) {
            const persist::Record &r = saved.records[n];
            if ((r.fields & persist::kFavorite) && static_cast<std::size_t>(r.order) == rank) {
                if (!Hidden(r.item)) favorites_[favorite_count_++] = r.item;
                break;
            }
        }
    }
    // 戻しでは効果の通知を出さない（今の効果を覚えておく）。
    for (u16 i = 0; i < count_; ++i)
        if (defs_[i].behavior == Behavior::State) state_[i].seen = EffectOn(i);
}

// 1 項目を入れる。戻しの待ちは、ゲームの値（読めるまで）と ON のトグルの効果（入るまで）だけ。
// 隠れている項目（開発版の項目を製品版で出していない）は、何も入れず、数えもしない。
void Engine::RestoreRecord(const persist::Record &r) {
    const u16 item = r.item;
    if (!Has(item) || Hidden(item)) return;
    const ItemDef &d = defs_[item];
    ItemState &st = state_[item];
    if (r.fields & persist::kHotkey) st.hotkey = st.applied_hotkey = r.hotkey;
    const bool fixed = (r.fields & persist::kFixed) != 0;
    if (d.control == Control::Toggle && (d.behavior == Behavior::State || d.behavior == Behavior::Hold)) {
        const bool on = (r.fields & persist::kOn) != 0;
        st.value = st.applied = on ? 1 : 0;
        if (fixed) {
            st.fixed = true;
            st.fixed_value = st.applied;
            fixed_order_[fixed_count_++] = item;
        }
        // OFF の固定は OFF のまま。効果を入れず、数えもしない（spec の既定が真のトグルでも同じ）。
        if (!on) return;
        // ON: 押下中は常に待機で終わる。ホットキーがあれば待機で終わる。効果を入れる物が無ければ、そこで終わる。
        if (d.behavior == Behavior::Hold || st.applied_hotkey != 0 || !is_bound_[item] || !bound_[item].patch.set) {
            ++restore_done_;
        } else {
            st.restore_wait = true;
            ++restore_waiting_;
        }
        return;
    }
    if (d.behavior == Behavior::Value && d.source == Source::Game) {
        if (!fixed) return;
        st.fixed = true;
        st.fixed_value = r.value;
        st.value = st.applied = r.value;
        fixed_order_[fixed_count_++] = item;
        if (is_bound_[item] && bound_[item].game.read && bound_[item].game.write) {
            st.restore_wait = true; // 読めた最初のフレームに数え、書くのは DriveFixed
            ++restore_waiting_;
        } else {
            ++restore_failed_; // 読み書きの結び付けが無い。ゲームの値に手が届かない
        }
        return;
    }
    if (d.behavior == Behavior::Value && d.control == Control::Key) {
        // キーの項目は、ホットキーと同じく持ち越す（固定はできない）。
        if (r.fields & persist::kValue) SetRestored(item, r.value);
        return;
    }
    // 設定の値は固定した物だけ。固定と固定値・固定の並びを入れ、その場で戻るので待たずに「戻した」に数える（通知の n に入る）。
    if (d.behavior == Behavior::Value && fixed && (r.fields & persist::kValue)) {
        st.fixed = true;
        st.fixed_value = r.value;
        fixed_order_[fixed_count_++] = item;
        SetRestored(item, r.value);
        ++restore_done_;
    }
}

void Engine::SetRestored(u16 item, s32 value) {
    ItemState &st = state_[item];
    st.value = st.applied = value;
    if (is_bound_[item] && bound_[item].setting.applied) bound_[item].setting.applied(value);
}

// 戻しを待っている物が、まだ戻す対象か。固定を外した・OFF にした・ホットキーを付けた・トグルを全て保持を切った物は、数えずにやめる。
bool Engine::RestoreWanted(u16 item) const {
    const ItemDef &d = defs_[item];
    const ItemState &st = state_[item];
    if (d.behavior == Behavior::Value) return st.fixed;
    return st.applied != 0 && st.applied_hotkey == 0 && (st.fixed || keep_toggles_);
}

// 戻しを待つ物を 1 フレーム進める（DriveFixed から毎フレーム呼ぶ）。
void Engine::DriveRestore(u16 item) {
    ItemState &st = state_[item];
    if (!RestoreWanted(item)) {
        st.restore_wait = false;
        --restore_waiting_;
        return;
    }
    if (defs_[item].behavior == Behavior::Value) {
        s32 value = 0;
        if (bound_[item].game.read && bound_[item].game.read(&value)) FinishRestore(item, true);
        return;
    }
    const Result result = bound_[item].patch.set ? bound_[item].patch.set(true) : Result::Failed;
    if (result == Result::Ok) {
        st.effect = true;
        st.seen = EffectOn(item); // 戻しでは効果の通知を出さない
        FinishRestore(item, true);
    } else if (result != Result::NotReady) {
        // 失敗は戻しの通知の件数にまとめる。固定の間は黙って試す（3.1）。
        st.fixed_failed = true;
        FinishRestore(item, false);
    }
}

void Engine::FinishRestore(u16 item, bool restored) {
    state_[item].restore_wait = false;
    --restore_waiting_;
    if (restored) ++restore_done_;
    else ++restore_failed_;
}

// 待ちが全て片付いた時に、通知を 1 つ出す（数える物が 0 件なら出さない）。
void Engine::NotifyRestore() {
    if (!restore_notice_ || restore_waiting_ != 0) return;
    restore_notice_ = false;
    if (restore_done_ + restore_failed_ == 0) return;
    Notice notice;
    if (restore_failed_ == 0) NoticeRestored(notice, restore_done_);
    else NoticeRestoreFailed(notice, restore_done_, restore_failed_);
    Emit(notice);
}
} // namespace gohan::menu
