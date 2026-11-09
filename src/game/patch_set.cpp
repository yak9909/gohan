#include "game/patch_set.hpp"
#include "game/code_ownership.hpp"

namespace gohan::game {
PatchSet::~PatchSet() { Shutdown(); code_ownership::Release(this); }
std::uint32_t WordCrc32(std::uint32_t word) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned byte = 0; byte < 4; ++byte) {
        crc ^= (word >> (byte * 8)) & 255;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
    }
    return crc ^ 0xFFFFFFFFu;
}

Result PatchSet::Wire() {
    if (ready_) return Result::Ok;
    if (words_.empty() || words_.size() > 9) return Result::Unsupported;
    // 全部を先に照合する。一つでも違えば、ケーブを含めて何も書かない。
    for (std::size_t i = 0; i < words_.size(); ++i) {
        if ((words_[i].address & 3) || !memory_.read(words_[i].address, original_[i]) ||
            WordCrc32(original_[i]) != words_[i].crc) return Result::Unsupported;
    }
    for (const auto &cave : caves_) {
        if (cave.address & 3) return Result::Unsupported;
        for (std::size_t i = 0; i < cave.words.size(); ++i) {
            std::uint32_t word;
            if (!memory_.read(cave.address + i * 4, word) || word != 0) return Result::Unsupported;
        }
    }
    for (const auto &word : words_) {
        if (!code_ownership::Take(this, word.address)) {
            code_ownership::Release(this); return Result::Unsupported;
        }
    }
    for (const auto &cave : caves_) for (std::size_t i = 0; i < cave.words.size(); ++i) {
        if (!code_ownership::Take(this, cave.address + i * 4)) {
            code_ownership::Release(this); return Result::Unsupported;
        }
    }
    // 入口はまだ元のまま。失敗しても途中まで置いたケーブへは入れない。
    for (const auto &cave : caves_) {
        for (std::size_t i = 0; i < cave.words.size(); ++i) {
            if (!memory_.write(cave.address + i * 4, cave.words[i])) {
                code_ownership::Release(this); return Result::Failed;
            }
            memory_.sync(cave.address + i * 4, 4);
        }
    }
    ready_ = true;
    return Result::Ok;
}

bool PatchSet::Write(std::size_t index, std::uint32_t word) {
    if (!memory_.write(words_[index].address, word)) return false;
    memory_.sync(words_[index].address, 4);
    return true;
}

Result PatchSet::Set(bool on) {
    if (!ready_) return Result::Unsupported;
    if (on) {
        changed_ = true;
        for (std::size_t i = 0; i < words_.size(); ++i) {
            if (!Write(i, words_[i].on)) {
                // 入口を先に閉じてから補助語を戻す。復元失敗は終了時にも再試行する。
                Set(false);
                return Result::Failed;
            }
        }
    } else {
        bool ok = true;
        for (std::size_t i = words_.size(); i > 0; --i) ok = Write(i - 1, original_[i - 1]) && ok;
        if (!ok) return Result::Failed;
        changed_ = false;
    }
    return Result::Ok;
}

bool PatchSet::IsApplied() const {
    if (!ready_) return false;
    for (const auto &entry : words_) {
        std::uint32_t word;
        if (!memory_.read(entry.address, word) || word != entry.on) return false;
    }
    return true;
}

Result PatchSet::SetSingleWord(std::uint32_t word) {
    if (!ready_ || words_.size() != 1) return Result::Unsupported;
    if (!Write(0, word)) return Result::Failed;
    changed_ = word != original_[0];
    return Result::Ok;
}

bool PatchSet::ReadSingleWord(std::uint32_t &word) const {
    return ready_ && words_.size() == 1 && memory_.read(words_[0].address, word);
}

void PatchSet::Shutdown() {
    if (ready_ && (changed_ || IsApplied())) {
        changed_ = true;
        if (Set(false) != Result::Ok) return;
    }
    ready_ = false;
    code_ownership::Release(this);
}
} // namespace gohan::game
