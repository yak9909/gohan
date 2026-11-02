#pragma once
#include "core/format.hpp"
#include "core/mailbox.hpp"
#include "menu/types.hpp"

// 次の起動に持ち越す物の保存の結果（段 4b）。features が書き、毎フレームの処理（失敗の後の頼み直し）と
// 状態表示（開発版）が読む。features どうしを結ばないよう、下の層に置く。
namespace gohan::menu::persist {
struct SaveStats {
    u32 saves = 0;    // 保存を試した回数（作業のスレッドが数える）
    u32 failures = 0; // そのうち失敗した回数
};

// 作業のスレッドだけが書く。Busy なら false（呼び手が次の機会に出し直す）。
bool PublishSaveStats(const SaveStats &stats);
// 毎フレームの処理と状態表示が読む。新しい物が無ければ Unchanged。
core::MailRead ReadSaveStats(SaveStats &stats, u32 &generation);
// 書き手（CTRPF の書き込みの中）が欄を書けた時の大きさ。Busy なら false。
bool PublishWrittenSize(u32 size);

// 状態表示の 1 行（開発版）: 保存の回数・失敗の回数・最後に書いた大きさ。
void AppendSaveStatus(core::TextWriter &writer);
} // namespace gohan::menu::persist
