// 通知が続けて入る場面の毎フレームの処理を、実際のメニュー・描画の部品・Pane の木で通して測る（PC）。
// 時間は PC の時計で、実機の値ではない。実機向けの見積もりは命令数（ARM 向けにビルドして数える）で別に出す。
// 使い方: bench_notice [繰り返し回数] [台本名...]。表は標準出力へ出す。
#include "core/frame.hpp"
#include "features/menu_screen/canvas_surface.hpp"
#include "game/lyt/engine.hpp"
#include "lyt_game.hpp"
#include "menu/sample_items.hpp"
#include "menu/shell.hpp"
#include "menu_ids.hpp"
#include "ui/renderer/renderer.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace gohan::game::lyt {
namespace {
detail::Engine *engine = nullptr;
std::chrono::steady_clock::duration presentTime{};
bool applied = false;
std::uint32_t lastRevision[2] = {0, 0};
} // namespace
InstallResult InstallRenderer(const FontResource &font, BackgroundAllocation *) {
    constexpr auto alignment = alignof(detail::Engine);
    void *raw = std::calloc(1, sizeof(detail::Engine) + alignment);
    void *memory = reinterpret_cast<void *>((reinterpret_cast<std::uintptr_t>(raw) + alignment - 1) & ~(alignment - 1));
    engine = new (memory) detail::Engine(fakegame::Api());
    return engine->Install(font);
}
bool Present(const Drawing &drawing) {
    const auto start = std::chrono::steady_clock::now();
    const bool result = engine->Present(drawing);
    presentTime += std::chrono::steady_clock::now() - start;
    applied = drawing.revisions[0] != lastRevision[0] || drawing.revisions[1] != lastRevision[1];
    lastRevision[0] = drawing.revisions[0];
    lastRevision[1] = drawing.revisions[1];
    return result;
}
bool UpdateSelection(const std::uint8_t *, unsigned &) { return false; }
bool DetachRenderer() { return true; }
RendererMemory ReadRendererMemory() { return {}; }
void WaitMilliseconds(unsigned) {}
} // namespace gohan::game::lyt

namespace {
using Clock = std::chrono::steady_clock;
namespace menu = gohan::menu;
namespace lyt = gohan::game::lyt;
namespace renderer = gohan::ui::renderer;
using Fake = fakegame::Game;
using gohan::features::menu_screen::CanvasSurface;

double Micros(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }

// 実際に出る通知の文言の見本（短い・2 行・長い・失敗・衝突）。
struct Sample {
    const char *title;
    const char *message;
    bool red;
};
const Sample kSamples[] = {
    {"見本のフラグ", "見本のフラグを有効にしました", false},
    {"ACTION", "見本のスタンプを実行しました", false},
    {"見本の所持金", "処理中です。しばらくしてからもう一度お試しください。", true},
    {"見本のフラグ", "見本のフラグを無効にしました", true},
    {"ホットキー", "そのホットキーは見本のスタンプで使われています", true},
    {"HOLD", "全適用をキャンセルしました", false},
    {"とても長い名前の見本の項目の通知", "とても長い名前の見本の項目の通知の内容がたくさん続いて三行を超えるほど長い場合の本文です。", true},
};

menu::Notice Make(unsigned index) {
    const Sample &s = kSamples[index % (sizeof(kSamples) / sizeof(kSamples[0]))];
    menu::Notice notice{};
    std::strncpy(notice.title, s.title, sizeof(notice.title) - 1);
    std::strncpy(notice.message, s.message, sizeof(notice.message) - 1);
    notice.red = s.red;
    return notice;
}

struct Scenario {
    const char *name;
    bool menu_open;
    unsigned burst;  // 開始時に一度に入れる通知の数
    unsigned stream; // その後、streamEveryMs ごとに入れる通知の数
    unsigned streamEveryMs;
    unsigned tailMs; // 最後の通知を入れてから測り続ける時間
};

const Scenario kScenarios[] = {
    {"closed-0", false, 0, 0, 0, 1000},
    {"open-0", true, 0, 0, 0, 1000},
    {"closed-1", false, 1, 0, 0, 4000},
    {"closed-7", false, 7, 0, 0, 5000},
    {"closed-16", false, 16, 0, 0, 12000},
    {"closed-stream", false, 0, 40, 220, 5000},
    {"open-1", true, 1, 0, 0, 4000},
    {"open-7", true, 7, 0, 0, 5000},
    {"open-16", true, 16, 0, 0, 12000},
    {"open-stream", true, 0, 40, 220, 5000},
};

struct FrameRecord {
    double step = 0, menu = 0, notice = 0, commit = 0, dispatch = 0, present = 0;
    unsigned commands = 0, rects = 0, texts = 0, chars = 0, batches = 0;
    std::uint64_t hash = 0;
    bool drawn = false, applied = false;
};

// 描画指示の全欄の指紋。変更の前後で同じ台本の全フレームを比べるのに使う。
std::uint64_t Fingerprint(const lyt::ScreenDrawing &drawing) {
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    auto rect = [&](const lyt::Rect &r) {
        for (const int v : {r.x, r.y, r.width, r.height}) mix(static_cast<std::uint32_t>(v));
    };
    mix(drawing.count); mix(drawing.rectangles); mix(drawing.texts); mix(drawing.characters);
    mix(drawing.estimatedBytes); mix(drawing.pictureLimit);
    for (std::size_t i = 0; i < drawing.count; ++i) {
        const lyt::Command &c = drawing.commands[i];
        mix(static_cast<unsigned>(c.kind)); rect(c.bounds); rect(c.clip); mix(c.color);
        mix(static_cast<unsigned>(c.font)); mix(c.scale); mix(c.slot); mix(c.length);
        for (unsigned j = 0; j <= c.length && j < sizeof(c.text) / sizeof(c.text[0]); ++j) mix(c.text[j]);
        rect(c.source); mix(static_cast<unsigned>(c.texture));
    }
    return hash;
}

constexpr unsigned kFrameMs = 33; // 30 fps
void Run(const Scenario &scenario, std::vector<FrameRecord> &frames) {
    namespace sample = menu::sample;
    sample::Get() = sample::State{};
    menu::Engine engine(menu::kMenuItems, menu::kMenuItemCount);
    engine.SetReservedHotkeys(menu::key::Select);
    engine.BindAll(sample::kBindings, sample::kBindingCount);
    menu::Shell shell(engine, {400});
    menu::NoticeQueue queue;
    renderer::Canvas canvas;
    menu::Input in;
    std::uint32_t now = 100000;
    auto setup = [&](unsigned ms) {
        for (unsigned t = 0; t < ms; t += 16) {
            now += 16;
            shell.Step(now, in, false);
        }
    };
    auto tap = [&](menu::u16 keys) {
        in.held = keys;
        now += 16;
        shell.Step(now, in, false);
        in.held = 0;
        now += 16;
        shell.Step(now, in, false);
    };
    if (scenario.menu_open) {
        tap(menu::key::Select);
        setup(400);
        tap(menu::key::A);
        setup(500);
    }
    bool drawn = false;
    unsigned next = 0, streamed = 0;
    std::uint32_t nextStream = now + kFrameMs * 2;
    const std::uint32_t start = now + kFrameMs;
    std::uint32_t lastInject = start;
    for (unsigned frame = 0;; ++frame) {
        now = start + frame * kFrameMs;
        if (frame == 0)
            for (unsigned i = 0; i < scenario.burst; ++i) queue.Add(Make(next++), now);
        while (streamed < scenario.stream && static_cast<std::int32_t>(now - nextStream) >= 0) {
            queue.Add(Make(next++), now);
            ++streamed;
            nextStream += scenario.streamEveryMs;
            lastInject = now;
        }
        FrameRecord rec;
        const auto t0 = Clock::now();
        shell.Step(now, in, false);
        const bool need = shell.NeedsDraw(now) || queue.Alive(now);
        const auto t1 = Clock::now();
        rec.step = Micros(t1 - t0);
        if (need || drawn) {
            canvas.Begin(lyt::Screen::Top);
            Clock::time_point t2 = t1, t3 = t1;
            if (need) {
                CanvasSurface surface(canvas, lyt::Screen::Top);
                shell.Draw(surface, now);
                t2 = Clock::now();
                queue.Draw(surface, now, shell.panel().Visible(now));
                t3 = Clock::now();
            }
            rec.menu = Micros(t2 - t1);
            rec.notice = Micros(t3 - t2);
            const auto t4 = Clock::now();
            if (renderer::Commit(canvas, lyt::Screen::Top)) drawn = need;
            rec.commit = Micros(Clock::now() - t4);
            rec.drawn = need;
        }
        const auto &top = canvas.Data().screens[0];
        rec.commands = static_cast<unsigned>(top.count);
        rec.rects = static_cast<unsigned>(top.rectangles);
        rec.texts = static_cast<unsigned>(top.texts);
        rec.chars = static_cast<unsigned>(top.characters);
        rec.hash = Fingerprint(top);
        const int batches = Fake::batchInits;
        lyt::presentTime = {};
        const auto t5 = Clock::now();
        gohan::core::frame::Dispatch();
        rec.dispatch = Micros(Clock::now() - t5);
        rec.present = Micros(lyt::presentTime);
        rec.applied = lyt::applied;
        rec.batches = static_cast<unsigned>(Fake::batchInits - batches);
        Fake::EndPasses();
        frames.push_back(rec);
        const bool finished = streamed >= scenario.stream && frame > 0 && !queue.Alive(now) &&
                              static_cast<std::int32_t>(now - lastInject) > static_cast<std::int32_t>(scenario.tailMs);
        if (finished || frame > 3000 || (!scenario.burst && !scenario.stream && frame * kFrameMs > scenario.tailMs)) break;
    }
}

// 同じ手順を繰り返し、フレームの順番ごとに平均する（OS の揺らぎを減らす）。
std::vector<FrameRecord> Measure(const Scenario &scenario, unsigned repeats) {
    std::vector<FrameRecord> sum;
    for (unsigned r = 0; r < repeats; ++r) {
        std::vector<FrameRecord> one;
        Run(scenario, one);
        if (sum.empty()) sum.resize(one.size());
        if (sum.size() != one.size()) {
            std::fprintf(stderr, "繰り返しでフレーム数が変わりました\n");
            std::exit(2);
        }
        for (std::size_t i = 0; i < one.size(); ++i) {
            sum[i].step += one[i].step;
            sum[i].menu += one[i].menu;
            sum[i].notice += one[i].notice;
            sum[i].commit += one[i].commit;
            sum[i].dispatch += one[i].dispatch;
            sum[i].present += one[i].present;
            sum[i].commands = one[i].commands;
            sum[i].rects = one[i].rects;
            sum[i].texts = one[i].texts;
            sum[i].chars = one[i].chars;
            sum[i].batches = one[i].batches;
            if (r && sum[i].hash != one[i].hash) {
                std::fprintf(stderr, "繰り返しで描画指示が変わりました\n");
                std::exit(2);
            }
            sum[i].hash = one[i].hash;
            sum[i].drawn = one[i].drawn;
            sum[i].applied = one[i].applied;
        }
    }
    for (auto &f : sum) {
        const double n = static_cast<double>(repeats);
        f.step /= n; f.menu /= n; f.notice /= n; f.commit /= n; f.dispatch /= n; f.present /= n;
    }
    return sum;
}

void Report(const Scenario &scenario, const std::vector<FrameRecord> &frames) {
    const bool idle = !scenario.menu_open && !scenario.burst && !scenario.stream;
    unsigned active = 0, applies = 0;
    double total = 0, step = 0, menuT = 0, notice = 0, commit = 0, dispatch = 0, present = 0;
    double commands = 0, rects = 0, texts = 0, chars = 0, batches = 0, peak = 0, peakCommands = 0, peakTexts = 0;
    std::uint64_t fingerprint = 1469598103934665603ull;
    for (const auto &f : frames) {
        fingerprint = (fingerprint ^ f.hash) * 1099511628211ull;
        if (!idle && !f.drawn && f.commands == 0) continue;
        ++active;
        const double all = f.step + f.menu + f.notice + f.commit + f.dispatch;
        total += all;
        step += f.step; menuT += f.menu; notice += f.notice; commit += f.commit;
        dispatch += f.dispatch; present += f.present;
        commands += f.commands; rects += f.rects; texts += f.texts; chars += f.chars; batches += f.batches;
        applies += f.applied;
        peak = std::max(peak, all);
        peakCommands = std::max<double>(peakCommands, f.commands);
        peakTexts = std::max<double>(peakTexts, f.texts);
    }
    const double n = active ? active : 1;
    std::printf("%-14s 描画フレーム %4u/%4zu  合計 平均 %7.1f us 最大 %7.1f us | 状態 %6.1f 通知 %7.1f メニュー %7.1f 確定 %6.1f 描画の部品 %7.1f (うち木 %7.1f)"
                " | 指示 %5.1f (最大 %3.0f) 板 %5.1f 文字枠 %4.1f (最大 %2.0f) 字数 %5.1f 木の組み直し %4.2f/f 文字バッチ初期化 %5.1f/f"
                " | 全フレームの指紋 %016llx\n",
                scenario.name, idle ? 0 : active, frames.size(), total / n, peak, step / n, notice / n, menuT / n, commit / n,
                dispatch / n, present / n, commands / n, peakCommands, rects / n, texts / n, peakTexts, chars / n,
                applies / n, batches / n, static_cast<unsigned long long>(fingerprint));
}
} // namespace

int main(int argc, char **argv) {
    unsigned repeats = 20;
    std::vector<std::string> names;
    for (int i = 1; i < argc; ++i) {
        if (i == 1 && std::atoi(argv[i]) > 0) repeats = static_cast<unsigned>(std::atoi(argv[i]));
        else names.push_back(argv[i]);
    }
    renderer::PrepareTitleBackground();
    Fake::Reset();
    if (!renderer::RendererFeature().Wire() || !renderer::SetEnabled(renderer::Client::Menu, true)) return 3;
    gohan::core::frame::Dispatch();
    Fake::EndPasses();
    if (renderer::ReadStatus() != renderer::Status::Ready) return 4;
    std::printf("sizeof(Command)=%zu sizeof(ScreenDrawing)=%zu 繰り返し=%u（フレーム順に平均）\n", sizeof(lyt::Command),
                sizeof(lyt::ScreenDrawing), repeats);
    for (const auto &scenario : kScenarios) {
        if (!names.empty() && std::find(names.begin(), names.end(), scenario.name) == names.end()) continue;
        Report(scenario, Measure(scenario, repeats));
    }
    return 0;
}
