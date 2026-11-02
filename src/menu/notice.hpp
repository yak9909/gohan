#pragma once
#include "core/limits.hpp"
#include "menu/binding.hpp"

namespace gohan::menu {
// 通知の共通の形。題と本文を文字列で持ち、失敗は赤で出す。文言はここだけが決める。
struct Notice {
    char title[core::limits::MenuText];
    char message[core::limits::MenuText];
    bool red;
};

void NoticeEffect(Notice &notice, const char *label, bool on); // 〈名前〉を有効／無効にしました（無効は赤）
void NoticeExecuted(Notice &notice, const char *label);        // ACTION / 〈名前〉を実行しました
void NoticeFailed(Notice &notice, const char *label, Result result);
void NoticeHoldCancelled(Notice &notice, bool apply); // HOLD / 全適用（全戻し）をキャンセルしました
void NoticeHotkeyConflict(Notice &notice, const char *label, const char *other);
void NoticeItemAction(Notice &notice, const char *action, const char *label);
void NoticeEraseAll(Notice &notice); // 保存を全て消す（全て最初に戻しました）
void NoticeRestored(Notice &notice, unsigned count);                    // 前回の状態 / n 件を戻しました
void NoticeRestoreFailed(Notice &notice, unsigned restored, unsigned failed); // 前回の状態 / n 件を戻し、m 件は失敗（赤）
const char *FailureText(Result result);
} // namespace gohan::menu
