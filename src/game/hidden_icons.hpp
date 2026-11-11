#pragma once
#include "game/hidden_icon_archive.hpp"
#include "game/item_names.hpp"
#include "game/generated/api.hpp"
#include "game/patch_set.hpp"

namespace gohan::game::items {
const std::uint8_t *SelectHiddenIcon(bool on, unsigned kind, std::uint16_t id, const HiddenEntry *entry,
                                     const HiddenIconArchive &archive);
bool WriteHiddenIcon(generated::NwLytMaterial &material, std::uint32_t physical,
                     generated::nwlyt_TexMap_UpdateGpuRegs_t update);
Result SetHiddenIcons(bool on);
bool PollHiddenIcons(); // 読み込み完了を一度だけ返す。
bool HiddenIconsSettled();
bool HiddenIconsAvailable();
void ShutdownHiddenIcons();
} // namespace gohan::game::items
