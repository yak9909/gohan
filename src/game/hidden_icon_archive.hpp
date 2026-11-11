#pragma once
#include <cstddef>
#include <cstdint>

namespace gohan::game::items {
class HiddenIconArchive {
  public:
    static constexpr std::size_t Limit = 512 * 1024, MaxImages = 512;
    bool Parse(const std::uint8_t *data, std::size_t size);
    const std::uint8_t *Find(std::uint16_t number) const;
    std::size_t Count() const { return count_; }

  private:
    bool Read(const std::uint8_t *data, std::size_t size);
    struct Image {
        std::uint16_t number;
        const std::uint8_t *data;
    };
    Image images_[MaxImages]{};
    std::size_t count_ = 0;
};
} // namespace gohan::game::items
