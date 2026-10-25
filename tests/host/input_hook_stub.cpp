#include "game/input_hook.hpp"
#include <CTRPluginFramework.hpp>

// プレビューと画面の試験ではゲームの接続の結果だけを代役で返す。
namespace gohan::game {
bool InputControllerReady() { return CTRPluginFramework::Fake::inputReady; }
bool InputTakeCtrpfResumed() {
    const bool resumed = CTRPluginFramework::Fake::ctrpfResumed;
    CTRPluginFramework::Fake::ctrpfResumed = false;
    return resumed;
}
} // namespace gohan::game
