#include "status.hpp"
#include "core/format.hpp"
#include "core/limits.hpp"
#include "game/frame_hook.hpp"
#include "game/frame_timing.hpp"
#include "game/input_hook.hpp"
#include "ui/renderer/renderer.hpp"
#include <CTRPluginFramework.hpp>
#ifdef GOHAN_DEV
#include <cstdio>
#include <ctime>
#endif

namespace gohan::features::status {
namespace {
void Compose(core::TextWriter &writer) {
    writer.Append("版: ").Unsigned(3).Append(".0.0\n");
#ifdef GOHAN_DEV
    writer.Append("開発版\n");
#else
    writer.Append("通常版\n");
#endif
    writer.Append("骨格: 機能登録・毎フレーム処理・通信・文字処理\n")
        .Append(gohan::game::FrameHookStatus());
    writer.Append("\n").Append(gohan::ui::renderer::StatusText());
    writer.Append("\n").Append(gohan::ui::renderer::TitleBackgroundStatusText());
    writer.Append("\n");
    gohan::game::AppendInputStatus(writer);
#ifdef GOHAN_DEV
    writer.Append("\n").Append(gohan::ui::renderer::MemoryStatusText());
    writer.Append("\n");
    game::frame_timing::AppendStatus(writer);
#endif
}
} // namespace

void Show(CTRPluginFramework::MenuEntry *) {
    char text[core::limits::StatusText];
    core::TextWriter writer(text, sizeof(text));
    Compose(writer);
    CTRPluginFramework::MessageBox("gohan（作り直し中）", text)();
}

#ifdef GOHAN_DEV
namespace {
void Failed(const char *step, const std::string &path = {}) {
    CTRPluginFramework::MessageBox("状態の書き出し失敗", std::string(step) + "\n" + path)();
}
} // namespace

void Export(CTRPluginFramework::MenuEntry *) {
    using CTRPluginFramework::File;
    char text[core::limits::StatusText];
    core::TextWriter writer(text, sizeof(text));
    Compose(writer);
    if (!writer.Complete()) {
        Failed("状態の文字列が容量を超えました。");
        return;
    }
    const auto now = std::time(nullptr);
    const auto *date = now == std::time_t(-1) ? nullptr : std::localtime(&now);
    char stamp[32];
    if (!date || !std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", date)) {
        Failed("日時を取得できません。");
        return;
    }
    // 設定ファイルと同じ CTRPF の作業フォルダ。時刻の巻き戻しや同秒の再操作にも対応する。
    for (unsigned sequence = 0; sequence < 1000; ++sequence) {
        char name[80];
        if (sequence == 0)
            std::snprintf(name, sizeof(name), "gohan-status-%s.txt", stamp);
        else
            std::snprintf(name, sizeof(name), "gohan-status-%s-%03u.txt", stamp, sequence);
        const int exists = File::Exists(name);
        if (exists == 1) continue;
        if (exists != 0) {
            Failed("保存先を確認できません。", name);
            return;
        }
        // Create は既存ファイルがあると失敗する。確認後に同名が作られても上書きしない。
        if (File::Create(name) != 0) {
            Failed("ファイルを作成できません。", name);
            return;
        }
        File file;
        if (File::Open(file, name, File::WRITE) != 0) {
            Failed("ファイルを開けません。", name);
            return;
        }
        const auto path = file.GetFullName();
        // WriteLine は CRLF を加えるため使わない。終端や BOM も付けず、表示の UTF-8 を保存する。
        const bool written = file.Write(text, writer.Size()) == 0 && file.Tell() == writer.Size();
        const bool flushed = written && file.Flush() == 0;
        const bool closed = file.Close() == 0;
        if (!flushed || !closed) {
            Failed("保存を完了できません。ファイルが不完全な可能性があります。", path);
            return;
        }
        CTRPluginFramework::MessageBox("状態を書き出しました", "SD: " + path)();
        return;
    }
    Failed("同じ日時のファイルが多すぎます。日時を変えて再試行してください。");
}
#endif
} // namespace gohan::features::status
