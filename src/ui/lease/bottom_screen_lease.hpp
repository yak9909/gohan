#pragma once
#include "ui/lease/lease_machine.hpp"
#include "game/bottom_screen.hpp"
namespace gohan::ui {
struct LeaseStatus {
    LeaseStage stage = LeaseStage::Shown;
    bool requested = false, hidden = false, restoring = false, restored = true, drained = true, stopping = false;
};
// 要求・返却はプラグインのメインスレッド一つから呼ぶ。
// Cleanup はゲームのフレームで停止・参照の取り外しを進め、全資源を返したら true。
// 設定は Wire より前に済ませる。
class BottomScreenLease {
  public:
    using Cleanup = bool (*)();
    bool SetCleanup(unsigned owner, Cleanup cleanup);
    void Acquire(std::uint32_t owners, bool over_chat = false);
    void Release(std::uint32_t owners);
    void ResourcesReturned(std::uint32_t owners);
    LeaseCommand Step(const game::BottomScreenSnapshot &);
    void RequestStop();
    LeaseStatus Status() const;
    bool ResourcesPending() const { return pending_ != 0; }
  private:
    Cleanup cleanup_[32]{};
    LeaseMachine machine_;
    std::uint32_t owners_ = 0, chat_ = 0, pending_ = 0;
    bool stopping_ = false;
};
// 共通の毎フレーム接続。複数の利用者は owner ビットを分ける。
// 参照の排出が期限に間に合わなければ false。戻りきりは待たない。
namespace bottom_screen_lease {
bool ConfigureCleanup(unsigned owner, BottomScreenLease::Cleanup);
bool Acquire(std::uint32_t owners, bool over_chat = false);
bool Release(std::uint32_t owners);
bool ResourcesReturned(std::uint32_t owners);
bool Wire();
bool Shutdown();
LeaseStatus Status();
}
} // namespace gohan::ui
