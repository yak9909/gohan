#include "menu/persist_status.hpp"

// 保存の結果の受け渡し（Mailbox）。書き手と読み手が別のスレッドでも待たない。
namespace gohan::menu::persist {
namespace {
core::Mailbox<SaveStats> stats_box;
core::Mailbox<u32> written_box;
} // namespace

bool PublishSaveStats(const SaveStats &stats) { return stats_box.TryPublish(stats); }

core::MailRead ReadSaveStats(SaveStats &stats, u32 &generation) { return stats_box.ReadIfNew(stats, generation); }

bool PublishWrittenSize(u32 size) { return written_box.TryPublish(size); }

void AppendSaveStatus(core::TextWriter &writer) {
    // 読み手は状態表示だけ（CTRPF のメニューのスレッド）。読めた物を次の表示まで覚える。
    static SaveStats stats;
    static u32 stats_generation = 0;
    static u32 size = 0;
    static u32 size_generation = 0;
    stats_box.ReadIfNew(stats, stats_generation);
    written_box.ReadIfNew(size, size_generation);
    writer.Append("保存 ").Unsigned(stats.saves).Append(" 回（失敗 ").Unsigned(stats.failures)
        .Append(" 回）・最後 ").Unsigned(size).Append(" B");
}
} // namespace gohan::menu::persist
