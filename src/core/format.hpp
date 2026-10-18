#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gohan::core {
// 容量は終端を含む。あふれたら切り詰め、Complete() で検出する。
class TextWriter {
  public:
    TextWriter(char *buffer, std::size_t capacity);
    TextWriter &Append(std::string_view text);
    TextWriter &Unsigned(std::uint32_t value, unsigned base = 10, unsigned width = 0);
    TextWriter &Signed(std::int32_t value);
    bool Complete() const { return complete_; }
    std::size_t Size() const { return size_; }

  private:
    void Put(char value);
    char *buffer_;
    std::size_t capacity_;
    std::size_t size_ = 0;
    bool complete_ = true;
};
} // namespace gohan::core
