// Dispatch の後の記録も前フレームへ足し、無処理の段も平均の分母へ含める。
#include "game/frame_timing.hpp"
#include <cassert>
#include <string>

int main() {
    using namespace gohan::game::frame_timing;
    Begin();
#ifdef GOHAN_DEV
    Add(Stage::Total, 100);
    Add(Stage::Notice, 60);
    Add(Stage::Record, 20);
    Begin();
    Add(Stage::Total, 50);
    Add(Stage::Record, 10);
    Begin();
    char buffer[2048];
    gohan::core::TextWriter writer(buffer, sizeof(buffer));
    AppendStatus(writer);
    assert(writer.Complete());
    const std::string text(buffer);
    assert(text.find("標本 2") != std::string::npos);
    assert(text.find("合計 60/90/120") != std::string::npos);
    assert(text.find("通知 0/30/60") != std::string::npos);
    assert(text.find("ゲームの記録 10/15/20") != std::string::npos);
#else
    Scope watch(Stage::Total);
    watch.Stop();
#endif
}
