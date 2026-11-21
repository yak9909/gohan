#include "ui/lease/bottom_screen_lease.hpp"
#include "core/frame.hpp"
#include "core/mailbox.hpp"
#include "core/sync.hpp"
namespace gohan::ui::bottom_screen_lease {
namespace {
struct Request { std::uint32_t owners = 0, chat = 0, returned = 0; bool stop = false; };
BottomScreenLease lease;
Request desired;
core::Mailbox<Request> requests;
core::Mailbox<LeaseStatus> published;
std::uint32_t request_generation = 0;
std::uint32_t applied_owners = 0, applied_chat = 0;
bool wired = false;
core::SpinMutex request_mutex, status_mutex;
bool Publish(const Request &next) {
    if (!requests.TryPublish(next)) return false;
    desired = next; return true;
}
void FrameStep() {
    Request request;
    if (requests.ReadIfNew(request, request_generation) == core::MailRead::Updated) {
        lease.Release(applied_owners & ~request.owners);
        const auto changed = (request.owners & ~applied_owners) | (request.owners & (request.chat ^ applied_chat));
        lease.Acquire(changed & ~request.chat);
        lease.Acquire(changed & request.chat, true);
        lease.ResourcesReturned(request.returned);
        if (request.stop) lease.RequestStop();
        applied_owners = request.owners; applied_chat = request.chat;
    }
    switch (lease.Step(game::ReadBottomScreen())) {
    case LeaseCommand::HideMap: case LeaseCommand::HideOther: case LeaseCommand::HideTab:
        game::HideBottomScreen(); break;
    case LeaseCommand::CloseMenu: game::BeginBottomMenuClose(); break;
    case LeaseCommand::FinishMenuClose: game::FinishBottomMenuClose(); break;
    case LeaseCommand::Restore: game::RestoreBottomScreen(); break;
    case LeaseCommand::None: break;
    }
    published.TryPublish(lease.Status());
}
}
bool ConfigureCleanup(unsigned owner, BottomScreenLease::Cleanup cleanup) {
    return !wired && lease.SetCleanup(owner, cleanup);
}
bool Acquire(std::uint32_t owners, bool over_chat) {
    core::LockGuard lock(request_mutex);
    if (desired.stop) return false;
    auto next = desired; next.owners |= owners; next.returned &= ~owners;
    if (over_chat) next.chat |= owners; else next.chat &= ~owners;
    return Publish(next);
}
bool Release(std::uint32_t owners) {
    core::LockGuard lock(request_mutex);
    auto next = desired; next.owners &= ~owners; next.chat &= ~owners; return Publish(next);
}
bool ResourcesReturned(std::uint32_t owners) {
    core::LockGuard lock(request_mutex);
    auto next = desired; next.returned |= owners; return Publish(next);
}
bool Wire() {
    if (wired) return true;
    const auto change = core::frame::Global().Add(FrameStep, 110);
    wired = change.result == core::frame::Result::Queued || change.result == core::frame::Result::Unchanged;
    return wired;
}
bool Shutdown() {
    const auto pending = desired.owners;
    auto next = desired; next.stop = true; next.owners = next.chat = 0;
    for (unsigned n = 0; !Publish(next); ++n) {
        if (n == 100) return false;
        game::BottomScreenWait(1);
    }
    if (!wired) {
        // Wire が失敗していればゲームの参照はまだ作れない。CPU 側の所有分だけ片付ける。
        lease.Acquire(pending);
        lease.ResourcesReturned(desired.returned);
        lease.RequestStop();
        for (unsigned n = 0; n < 200; ++n) {
            lease.Step({});
            if (lease.Status().drained) return true;
            game::BottomScreenWait(10);
        }
        return false;
    }
    LeaseStatus state;
    std::uint32_t generation = 0;
    for (unsigned waited = 0; waited <= 2000; waited += 10) {
        if (published.ReadIfNew(state, generation) == core::MailRead::Updated && state.drained &&
            state.stopping) {
            // 要求を読んだフレームからのみ排出完了を通知する。
            core::frame::Global().Remove(FrameStep); wired = false; return true;
        }
        game::BottomScreenWait(10);
    }
    return false;
}
LeaseStatus Status() {
    core::LockGuard lock(status_mutex);
    static LeaseStatus last;
    static std::uint32_t generation = 0;
    published.ReadIfNew(last, generation); return last;
}
} // namespace gohan::ui::bottom_screen_lease
