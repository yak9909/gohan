#pragma once
#include "core/feature.hpp"
#include "core/files.hpp"
#include "core/signal.hpp"
#include <string_view>

namespace gohan::game::items {
struct HiddenEntry {
    std::uint16_t id, icon;
    const char *kana, *name;
    const char16_t *name16;
};
class HiddenTable {
  public:
    bool Parse(std::string_view text);
    void Clear();
    const HiddenEntry *Find(std::uint16_t id) const;
    std::size_t Count() const { return count_; }
  private:
    std::unique_ptr<char[]> text_;
    std::unique_ptr<char16_t[]> wide_;
    std::unique_ptr<HiddenEntry[]> entries_;
    std::size_t count_ = 0;
};
class MsbtNames {
  public:
    bool Parse(const std::uint8_t *data, std::size_t size);
    std::u16string_view Find(std::uint16_t id) const;
    std::size_t Count() const { return count_; }
  private:
    const std::uint8_t *text_ = nullptr;
    std::size_t size_ = 0, count_ = 0;
};
class Names {
  public:
    void Request(); // 初めて使う時だけ開始。ゲームのスレッドで待たない。
    void Poll();
    bool NameUtf8(std::uint16_t id, char *out, std::size_t capacity);
    const HiddenEntry *CustomName(std::uint16_t id) const;
    void SetCustom(bool on);
    bool Custom() const { return custom_.Get(); }
    bool Settled() const { return hiddenStarted_ && normalStarted_ && hiddenLoad_.GetState() == core::files::AsyncLoad::State::Empty && normalLoad_.GetState() == core::files::AsyncLoad::State::Empty; }
    void Shutdown();
  private:
    bool requested_ = false, hiddenStarted_ = false, normalStarted_ = false;
    core::Signal custom_, hiddenReady_;
    core::files::AsyncLoad hiddenLoad_, normalLoad_;
    HiddenTable hidden_;
    core::files::Buffer normalData_;
    MsbtNames normal_;
};
Names &SharedNames();
const core::Feature &NamesFeature();
bool NamesHookReady();
} // namespace gohan::game::items
