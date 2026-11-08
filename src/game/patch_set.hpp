#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace gohan::game {
enum class Result { Ok, NotReady, Unsupported, Failed };

// メモリとキャッシュの操作をまとめる。PC では同じ契約の代役を渡す。
struct PatchMemory {
    bool (*read)(std::uintptr_t, std::uint32_t &);
    bool (*write)(std::uintptr_t, std::uint32_t);
    void (*sync)(std::uintptr_t, std::size_t);
};
const PatchMemory &NativePatchMemory();
bool IsSupportedTitle();
std::uint32_t WordCrc32(std::uint32_t word);

struct PatchWord {
    std::uintptr_t address;
    std::uint32_t on;
    std::uint32_t crc;
};
struct CaveWords {
    std::uintptr_t address;
    std::span<const std::uint32_t> words;
};

class PatchSet {
  public:
    PatchSet(const PatchMemory &memory, std::span<const PatchWord> words,
             std::span<const CaveWords> caves = {}) : memory_(memory), words_(words), caves_(caves) {}
    Result Wire();
    Result Set(bool on);
    bool IsApplied() const;
    void Shutdown();
    // 1 語の選択値用。Wire で保存した元の語も含め、呼び手が選択肢を検査する。
    Result SetSingleWord(std::uint32_t word);
    bool ReadSingleWord(std::uint32_t &word) const;
    std::uint32_t Original() const { return original_[0]; }

  private:
    bool Write(std::size_t index, std::uint32_t word);
    const PatchMemory &memory_;
    std::span<const PatchWord> words_;
    std::span<const CaveWords> caves_;
    std::uint32_t original_[9]{};
    bool ready_ = false;
    bool changed_ = false;
};
} // namespace gohan::game
