#include "core/jobs.hpp"
#include "core/files.hpp"
#include "core/frame.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

using namespace gohan::core;
namespace {
std::thread::id gameThread;
std::vector<unsigned> completed;
struct Context { unsigned id; bool worked = false; };
void Work(void *p) { assert(std::this_thread::get_id() != gameThread); static_cast<Context *>(p)->worked = true; }
void Done(void *p) {
    auto &c = *static_cast<Context *>(p);
    assert(std::this_thread::get_id() == gameThread && c.worked);
    completed.push_back(c.id);
}
void QueueTest() {
    jobs::Queue queue;
    Context contexts[limits::Jobs];
    assert(!queue.Submit({}));
    for (unsigned i = 0; i < std::size(contexts); ++i) {
        contexts[i].id = i;
        assert(queue.Submit({&contexts[i], Work, Done}));
    }
    assert(!queue.Submit({contexts, Work, Done}));
    queue.Deliver(); assert(completed.empty());
    std::thread worker([&] { while (queue.WorkOne()) {} }); worker.join();
    assert(completed.empty());
    queue.Deliver(); assert(completed.size() == std::size(contexts));
    for (unsigned i = 0; i < completed.size(); ++i) assert(completed[i] == i);
    assert(!queue.WorkOne());
    queue.Stop(); assert(!queue.Submit({contexts, Work, Done}));
    jobs::Queue cancelled;
    unsigned discarded = 0;
    assert(cancelled.Submit({&discarded, [](void *) {}, [](void *) { assert(false); }, [](void *p) { ++*static_cast<unsigned *>(p); }}));
    cancelled.Stop(); cancelled.Discard(); assert(discarded == 1);
}
void FilesTest() {
    constexpr char path[] = "build/host/p3-file.txt";
    { std::ofstream out(path, std::ios::binary); out.write("a\0bc", 4); }
    auto buffer = files::ReadAll(path, 4);
    assert(buffer && buffer.size == 4 && !std::memcmp(buffer.data.get(), "a\0bc", 4) && buffer.data[4] == 0);
    assert(!files::ReadAll(path, 3));
    assert(!files::ReadAll(path, 0));
    assert(!files::ReadAll("build/host/p3-missing", 100));
    { std::ofstream out(path, std::ios::binary | std::ios::trunc); }
    assert(!files::ReadAll(path, 4));
    { std::ofstream out(path, std::ios::binary); out << "data"; }
    jobs::Queue queue;
    files::AsyncLoad load;
    using State = files::AsyncLoad::State;
    assert(load.GetState() == State::Empty && !load.Take(buffer));
    assert(load.Start(path, 4, files::ReadAll, queue));
    assert(load.GetState() == State::Reading && !load.Start(path, 4, files::ReadAll, queue));
    std::thread worker([&] { assert(queue.WorkOne()); }); worker.join();
    assert(load.GetState() == State::Reading && !load.Take(buffer));
    queue.Deliver(); assert(load.GetState() == State::Ready);
    assert(load.Take(buffer) && buffer && buffer.size == 4 && buffer.data[4] == 0 && load.GetState() == State::Empty);
    assert(!load.Take(buffer));
    assert(load.Start(path, 3, files::ReadAll, queue));
    queue.WorkOne(); queue.Deliver();
    assert(load.Take(buffer) && !buffer);
    // 所有者が先に消えても、キューの文脈は完了まで生きる。
    { files::AsyncLoad temporary; assert(temporary.Start(path, 4, files::ReadAll, queue)); }
    queue.WorkOne(); queue.Deliver();
    char out[128];
    assert(files::CommonPath(out, sizeof(out), "x") && !std::strcmp(out, "/luma/plugins/gohan/common/x"));
    assert(files::TitlePath(out, sizeof(out), 0x1234, "x") && !std::strcmp(out, "/luma/plugins/gohan/0000000000001234/x"));
    assert(!files::TitlePath(out, 8, 0x1234, "x") && !files::CommonPath(nullptr, 0, "x"));
    std::remove(path);
}
void PumpTest() {
    completed.clear();
    assert(jobs::JobsFeature().Wire());
    Context contexts[3]{{0}, {1}, {2}};
    for (auto &c : contexts) assert(jobs::Submit({&c, Work, Done}));
    for (unsigned i = 0; completed.size() != 3 && i < 1000; ++i) {
        frame::Dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert((completed == std::vector<unsigned>{0, 1, 2}));
    jobs::JobsFeature().Shutdown();
    assert(!jobs::Submit({contexts, Work, Done}));
    frame::Global().Reset();
}
}
int main() { gameThread = std::this_thread::get_id(); QueueTest(); FilesTest(); PumpTest(); }
