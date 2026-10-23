#pragma once
#include "core/limits.hpp"
#include "core/mailbox.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace gohan::core::input_block {
using Mask = std::uint32_t;
namespace part {
inline constexpr Mask ZL = 1u << 0, L = 1u << 1, R = 1u << 2, ZR = 1u << 3;
inline constexpr Mask Up = 1u << 4, Down = 1u << 5, Left = 1u << 6, Right = 1u << 7;
inline constexpr Mask A = 1u << 8, B = 1u << 9, X = 1u << 10, Y = 1u << 11;
inline constexpr Mask Select = 1u << 12, Start = 1u << 13;
inline constexpr Mask SlidePad = 1u << 14, Touch = 1u << 15;
} // namespace part
namespace group {
inline constexpr Mask Face = part::A | part::B | part::X | part::Y;
inline constexpr Mask Shoulder = part::L | part::R | part::ZL | part::ZR;
inline constexpr Mask DPad = part::Up | part::Down | part::Left | part::Right;
inline constexpr Mask Buttons = Face | Shoulder | DPad | part::Start;
inline constexpr Mask Everything = Buttons | part::SlidePad | part::Touch;
} // namespace group

struct Claim {
    Mask block = 0;
    Mask defer = 0;
    constexpr bool operator==(const Claim &) const = default;
};
struct Effective {
    Mask block = part::Select;
    Mask defer = 0;
    std::uint32_t generation = 0;
};
enum class Result { Ok, Unchanged, Busy, Full, Invalid };
struct Owner {
    std::uint8_t slot = 0xFF;
};
struct OwnerInfo {
    const char *name = nullptr;
    Claim claim{};
};
struct Activity { std::uint32_t writes = 0, reads = 0; };

class Table {
  public:
    Table();
    // name は空でない静的文字列。owner は未取得の物を渡す。
    Result Acquire(const char *name, Owner &owner);
    Result Set(const Owner &owner, const Claim &claim);
    // 成功時だけ owner を無効にする。コピーした Owner は返却後に使わない。
    Result Release(Owner &owner);
    MailRead Read(Effective &out, std::uint32_t &generation);
    // 待たない。競合時は 0、成功時は書き込んだ数（capacity まで）。
    std::size_t List(OwnerInfo *out, std::size_t capacity);
    Activity ActivityCounts() const { return {write_busy_.load(), read_busy_.load()}; }

  private:
    // 試験でロック中の停止を再現する。実行時の分岐は増やさない。
    friend struct TableTestAccess;
    bool Valid(const Owner &owner) const;
    bool Publish(std::size_t slot, const Claim &replacement);
    Result BusyWrite() { ++write_busy_; return Result::Busy; }
    SpinMutex mutex_;
    std::array<OwnerInfo, limits::InputBlockOwners> owners_{};
    Mailbox<Effective> published_;
    std::uint32_t generation_ = 0;
    std::atomic<std::uint32_t> write_busy_{0}, read_busy_{0};
};
Table &Global();
} // namespace gohan::core::input_block
