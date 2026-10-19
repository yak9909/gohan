#include "menu/notice.hpp"
#include "core/format.hpp"

namespace gohan::menu {
namespace {
void Fill(Notice &notice, const char *title, bool red) {
    core::TextWriter(notice.title, sizeof(notice.title)).Append(title);
    notice.message[0] = '\0';
    notice.red = red;
}
} // namespace

const char *FailureText(Result result) {
    switch (result) {
    case Result::Ok: return "";
    case Result::NotReady: return "ゲームの準備ができていません。";
    case Result::Unsupported: return "対応していないゲームの版です。";
    case Result::Busy: return "処理中です。しばらくしてからもう一度お試しください。";
    case Result::Failed: break;
    }
    return "実行できませんでした。";
}

void NoticeEffect(Notice &notice, const char *label, bool on) {
    Fill(notice, label, !on);
    core::TextWriter(notice.message, sizeof(notice.message)).Append(label).Append(on ? "を有効にしました" : "を無効にしました");
}

void NoticeExecuted(Notice &notice, const char *label) {
    Fill(notice, "ACTION", false);
    core::TextWriter(notice.message, sizeof(notice.message)).Append(label).Append("を実行しました");
}

void NoticeFailed(Notice &notice, const char *label, Result result) {
    Fill(notice, label, true);
    core::TextWriter(notice.message, sizeof(notice.message)).Append(FailureText(result));
}

void NoticeHoldCancelled(Notice &notice, bool apply) {
    Fill(notice, "HOLD", false);
    core::TextWriter(notice.message, sizeof(notice.message))
        .Append(apply ? "全適用をキャンセルしました" : "全戻しをキャンセルしました");
}

void NoticeHotkeyConflict(Notice &notice, const char *label, const char *other) {
    Fill(notice, label, true);
    core::TextWriter(notice.message, sizeof(notice.message)).Append("そのホットキーは").Append(other).Append("で使われています");
}
} // namespace gohan::menu
