#include "features/status/status.hpp"
#include "core/format.hpp"
#include "game/frame_hook.hpp"
#include "game/input_hook.hpp"
#include "ui/renderer/renderer.hpp"
#include <CTRPluginFramework.hpp>
#include <cassert>
#include <ctime>
#include <string>

using CTRPluginFramework::StatusFake;
namespace gohan::game {
const char *FrameHookStatus() { return "毎フレーム: 導入済み"; }
void AppendInputStatus(core::TextWriter &writer) {
    writer.Append("入力の遮断: 有効\n所有者: 2 / 下画面: 遮断");
    if (StatusFake::overflow) writer.Append(std::string(5000, 'x'));
}
} // namespace gohan::game
namespace gohan::ui::renderer {
const char *StatusText() { return "描画: 導入済み"; }
const char *TitleBackgroundStatusText() { return "背景: 常駐\n全画像の常駐: 成功"; }
const char *MemoryStatusText() {
    return "描画資源のヒープ（バイト）\n現在: 合計 123 / 最大 45\n帯: 導入時の合成 67 us\n帯の毎フレーム: 今 8 / 平均 9 / 最大 10";
}
} // namespace gohan::ui::renderer

int main() {
    namespace status = gohan::features::status;
    StatusFake::Reset();
    status::Show(nullptr);
    const auto displayed = StatusFake::message;
    const std::string expected = "版: 3.0.0\n"
#ifdef GOHAN_DEV
        "開発版\n"
#else
        "通常版\n"
#endif
        "骨格: 機能登録・毎フレーム処理・通信・文字処理\n"
        "毎フレーム: 導入済み\n描画: 導入済み\n背景: 常駐\n全画像の常駐: 成功\n"
        "入力の遮断: 有効\n所有者: 2 / 下画面: 遮断"
#ifdef GOHAN_DEV
        "\n描画資源のヒープ（バイト）\n現在: 合計 123 / 最大 45\n帯: 導入時の合成 67 us\n帯の毎フレーム: 今 8 / 平均 9 / 最大 10"
        "\ngohan 毎フレーム us（今/平均/最大）標本 0\n合計 0/0/0\n更新 0/0/0\nメニュー 0/0/0\n通知 0/0/0\nうち組み版 0/0/0\n確定 0/0/0\n描画部品 0/0/0\nうち木・提出 0/0/0\nゲームの記録 0/0/0"
#endif
        ;
    assert(displayed == expected);
#ifdef GOHAN_DEV
    // 実際の時計の前後いずれかの日時を使う（秒の境界をまたぐ場合も許す）。
    const auto before = std::time(nullptr);
    status::Export(nullptr);
    const auto after = std::time(nullptr);
    const auto name = StatusFake::created;
    bool dated = false;
    for (auto value : {before, after}) {
        char stamp[32];
        assert(std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&value)));
        dated |= name == std::string("gohan-status-") + stamp + ".txt";
    }
    assert(dated);
    assert(StatusFake::files.at(name) == displayed);
    assert(StatusFake::files.at(name).find('\r') == std::string::npos);
    assert(StatusFake::files.at(name).find('\0') == std::string::npos);
    assert(StatusFake::message == "SD: /luma/plugins/gohan/" + name);
    assert(StatusFake::closes == 1);

    StatusFake::Reset();
    StatusFake::collisions = 2;
    status::Export(nullptr);
    assert(StatusFake::created.ends_with("-002.txt"));
    assert(StatusFake::files.size() == 3);
    for (const auto &[path, text] : StatusFake::files)
        assert(text == (path == StatusFake::created ? displayed : "既存"));

    for (int *error : {&StatusFake::existsError, &StatusFake::createError,
                       &StatusFake::openError, &StatusFake::writeError,
                       &StatusFake::flushError, &StatusFake::closeError}) {
        StatusFake::Reset();
        *error = -1;
        status::Export(nullptr);
        assert(StatusFake::title == "状態の書き出し失敗");
        assert(!StatusFake::message.empty());
    }
    StatusFake::Reset();
    StatusFake::shortWrite = true;
    status::Export(nullptr);
    assert(StatusFake::title == "状態の書き出し失敗" && StatusFake::closes == 1);

    StatusFake::Reset();
    StatusFake::race = true;
    status::Export(nullptr);
    assert(StatusFake::title == "状態の書き出し失敗");
    assert(StatusFake::files.begin()->second == "競合");

    StatusFake::Reset();
    StatusFake::collisions = 1000;
    status::Export(nullptr);
    assert(StatusFake::title == "状態の書き出し失敗" && StatusFake::creates == 0);

    StatusFake::Reset();
    StatusFake::overflow = true;
    status::Export(nullptr);
    assert(StatusFake::title == "状態の書き出し失敗" && StatusFake::creates == 0);
#endif
}
