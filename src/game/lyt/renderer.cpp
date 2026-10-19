#include "game/lyt/renderer.hpp"
#include "game/lyt/verification.hpp"

namespace gohan::game::lyt {
InstallResult InstallRenderer(const FontResource &) {
    const auto result = detail::VerifyRenderer();
    if (result != InstallResult::Ready)
        return result;
    // 本体の型は公開済みだが、ノードの仮想表・有効フラグ、Material の
    // 可変領域、フォントのシート記述子、文字バッチの可変部分が不足する。
    // これらを unk_* や生オフセットへ書いて補わず、資源確保より前に拒否する。
    return InstallResult::MissingBindings;
}

bool Present(const Drawing &) { return false; }
void DetachRenderer() {
    // 導入に成功する経路はまだ無く、ゲーム側に所有する資源も無い。
}
} // namespace gohan::game::lyt
