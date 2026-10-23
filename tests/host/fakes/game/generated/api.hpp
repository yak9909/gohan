#pragma once
#include <cstdint>

// 型は製品が使う生成物を読む。照合する入力だけは自作の ASCII 列へ置き換える。
#define FrameDispatch_addr OriginalFrameDispatch_addr
#define FrameDispatch_len OriginalFrameDispatch_len
#define FrameDispatch_crc32 OriginalFrameDispatch_crc32
#define InputPadRead_addr OriginalInputPadRead_addr
#define InputPadRead_len OriginalInputPadRead_len
#define InputPadRead_crc32 OriginalInputPadRead_crc32
#define InputControllerCalc_addr OriginalInputControllerCalc_addr
#define InputControllerCalc_len OriginalInputControllerCalc_len
#define InputControllerCalc_crc32 OriginalInputControllerCalc_crc32
#define vtbl_sead_CtrController_addr Original_vtbl_sead_CtrController_addr
#include "../../../../../src/game/generated/api.hpp"
#undef FrameDispatch_addr
#undef FrameDispatch_len
#undef FrameDispatch_crc32
#undef InputPadRead_addr
#undef InputPadRead_len
#undef InputPadRead_crc32
#undef InputControllerCalc_addr
#undef InputControllerCalc_len
#undef InputControllerCalc_crc32
#undef vtbl_sead_CtrController_addr

namespace gohan::game::generated {
// 自作の ASCII 入力用。ゲームの番地・命令・ハッシュは使わない。
inline constexpr std::uintptr_t FrameDispatch_addr = 512;
inline constexpr std::uint32_t FrameDispatch_len = 9;
inline constexpr std::uint32_t FrameDispatch_crc32 = 0xCBF43926;
inline constexpr std::uintptr_t InputPadRead_addr = 1024, InputControllerCalc_addr = 2048;
inline constexpr std::uint32_t InputPadRead_len = 9, InputControllerCalc_len = 9;
inline constexpr std::uint32_t InputPadRead_crc32 = 0xCBF43926, InputControllerCalc_crc32 = 0xCBF43926;
inline constexpr std::uintptr_t vtbl_sead_CtrController_addr = 4096;
} // namespace gohan::game::generated
