// 通知の組み版と Canvas への記入を、製品と同じ ARM 命令で数えるための入口。
// OS・GPU・ゲームの記録は含めない。画像の画素を使わないので寸法だけを代役で渡す。
#include "features/menu_screen/canvas_surface.hpp"
#include "menu/notices.hpp"
#include <cstring>
#include <new>

namespace gohan::ui::renderer {
const game::lyt::TextureResource &TitleBackgroundImage(unsigned) {
    static const game::lyt::TextureResource image{nullptr, 32768, 256, 256, game::lyt::TextureFormat::L4};
    return image;
}
}
namespace {
namespace menu = gohan::menu;
namespace renderer = gohan::ui::renderer;
alignas(menu::NoticeQueue) unsigned char queue_memory[sizeof(menu::NoticeQueue)];
alignas(renderer::Canvas) unsigned char canvas_memory[sizeof(renderer::Canvas)];
menu::NoticeQueue *queue;
renderer::Canvas *canvas;
unsigned next;
const char *messages[] = {
    "見本のフラグを有効にしました", "見本のスタンプを実行しました",
    "処理中です。しばらくしてからもう一度お試しください。", "見本のフラグを無効にしました",
    "そのホットキーは見本のスタンプで使われています", "全適用をキャンセルしました",
    "とても長い名前の見本の項目の通知の内容がたくさん続いて三行を超えるほど長い場合の本文です。"};
const char *titles[] = {"見本のフラグ", "ACTION", "見本の所持金", "見本のフラグ", "ホットキー", "HOLD",
                        "とても長い名前の見本の項目の通知"};
}
extern "C" {
unsigned bench_commands, bench_texts;
void BenchAdd(unsigned now) {
    menu::Notice notice{};
    const unsigned index = next++ % 7;
    std::strncpy(notice.title, titles[index], sizeof(notice.title) - 1);
    std::strncpy(notice.message, messages[index], sizeof(notice.message) - 1);
    notice.red = index == 2 || index == 3 || index == 4 || index == 6;
    queue->Add(notice, now);
}
void BenchReset(unsigned count) {
    queue = new (queue_memory) menu::NoticeQueue;
    canvas = new (canvas_memory) renderer::Canvas;
    next = 0;
    for (unsigned i = 0; i < count; ++i) BenchAdd(100000);
}
void BenchDraw(unsigned now) {
    canvas->Begin(renderer::Screen::Top);
    gohan::features::menu_screen::CanvasSurface surface(*canvas, renderer::Screen::Top);
    queue->Draw(surface, now, false);
    bench_commands = unsigned(canvas->Data().screens[0].count);
    bench_texts = unsigned(canvas->Data().screens[0].texts);
}
// newlib の文字列用ヒープ。エミュレータ内だけで使う。
void *_sbrk(int amount) {
    alignas(16) static char heap[1024 * 1024];
    static unsigned used = 0;
    if (amount < 0 || unsigned(amount) > sizeof(heap) - used) return reinterpret_cast<void *>(-1);
    void *result = heap + used;
    used += unsigned(amount);
    return result;
}
}
