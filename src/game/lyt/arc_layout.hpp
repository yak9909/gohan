#pragma once
// ゲームの Arc/Layout。自前描画の Scene とは別の所有単位。
// 全操作はゲームの毎フレーム処理で行う。描画登録を止めた後の待ちは Panel の責任。
#include "game/generated/api.hpp"
#include "game/write_log.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace gohan::game::lyt {
class Arc {
  public:
    Arc() = default;
    Arc(const Arc &) = delete;
    Arc &operator=(const Arc &) = delete;
    ~Arc() { Reset(); }
    bool LoadStep(const char *path);
    bool Copy(const void *data, std::size_t bytes);
    bool Reset(); // Layout が残っている間は返さない。
    bool Ready() const { return ready_; }
    generated::ArcResAccReader *Reader() { return &reader_; }
  private:
    friend class ArcLayout;
    bool Register();
    generated::ArcResAccReader reader_{};
    generated::SsysMaAllocator *owner_ = nullptr;
    void *copy_ = nullptr;
    unsigned users_ = 0;
    bool made_ = false, ready_ = false;
};
class ArcLayout {
  public:
    ArcLayout() = default;
    ArcLayout(const ArcLayout &) = delete;
    ArcLayout &operator=(const ArcLayout &) = delete;
    ~ArcLayout() { Reset(); }
    bool Build(Arc &, const char *name, unsigned command_bytes, std::uint8_t priority = 2);
    bool BuildData(Arc &, std::vector<std::uint8_t> data, unsigned command_bytes, std::uint8_t priority = 2);
    bool Commit(unsigned screen = 1);
    void Detach();
    void Reset(); // 描画登録をやめ、GPU の参照が無くなってから呼ぶ。
    generated::NwLytPane *Find(const char *name);
    generated::SsysMaLayoutNode *Node() { return made_ ? &node_ : nullptr; }
    bool Ready() const { return built_; }
  private:
    generated::SsysMaLayoutNode node_{};
    std::vector<std::uint8_t> data_;
    Arc *arc_ = nullptr;
    generated::SsysMaLayoutMgr *manager_ = nullptr;
    unsigned screen_ = 1;
    bool made_ = false, built_ = false;
};
// 入退場の途中で反転した時は、反対のアニメへ進捗を写す。
class InOut {
  public:
    InOut() = default;
    InOut(const InOut &) = delete;
    InOut &operator=(const InOut &) = delete;
    ~InOut() { Reset(); }
    bool Load(ArcLayout &, Arc &, const char *in, const char *out, const char *group = nullptr);
    void Show(bool shown);
    bool Step();
    bool Done() const;
    void Reset(); // Layout より先に外す。
  private:
    generated::UiAnim anim_[2]{};
    generated::SsysMaLayoutNode *layout_ = nullptr;
    void *group_ = nullptr;
    unsigned made_ = 0;
    int active_ = -1;
};
using PaneLog = WriteLog<64>;
bool SetPosition(generated::NwLytPane *, float x, float y, PaneLog *borrowed = nullptr);
bool SetSize(generated::NwLytPane *, float w, float h, PaneLog *borrowed = nullptr);
bool SetVisible(generated::NwLytPane *, bool shown, PaneLog *borrowed = nullptr);
bool Reparent(generated::NwLytPane *, generated::NwLytPane *parent, PaneLog *borrowed = nullptr);
bool MirrorPane(const generated::NwLytPane *src, generated::NwLytPane *dst, PaneLog *borrowed = nullptr);
bool MirrorPane(const generated::NwLytTextBox *src, generated::NwLytTextBox *dst, PaneLog *borrowed = nullptr);
bool SetTexture(generated::NwLytPicture *, Arc &, const char *name);
} // namespace gohan::game::lyt
