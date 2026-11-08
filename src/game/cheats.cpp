#include "game/cheats.hpp"
#include "game/field_caves.hpp"
#include "game/generated/api.hpp"
#include "game/weather.hpp"

namespace gohan::game::cheats {
namespace {
// 座標保存の補助語を先にし、判定の入口を最後に変更する。OFF は逆順。
const PatchWord kWall[] = {
    {generated::PatchWall1_addr, 0xE1A00000u, generated::PatchWall1_crc32},
    {generated::PatchWall2_addr, 0xE1A00000u, generated::PatchWall2_crc32},
    {generated::PatchWall0_addr, 0xE3A07000u, generated::PatchWall0_crc32},
};
// 入口の 1 語だけを変更する。
const PatchWord kFlower[] = {
    {generated::PatchFlower0_addr, 0xE3A0001Du, generated::PatchFlower0_crc32},
};
// 個別の判定を置き換える表。各判定は独立しており順は問わない。
const PatchWord kTrap[] = {
    {generated::PatchTrap0_addr, 0xEA000014u, generated::PatchTrap0_crc32},
    {generated::PatchTrap1_addr, 0xEA00002Du, generated::PatchTrap1_crc32},
};
// 入口の 1 語だけを変更する。
const PatchWord kBedHead[] = {
    {generated::PatchBedHead0_addr, 0xE1A00000u, generated::PatchBedHead0_crc32},
};
// 個別の判定を置き換える表。各判定は独立しており順は問わない。
const PatchWord kTrample[] = {
    {generated::PatchTrample0_addr, 0xE1A00000u, generated::PatchTrample0_crc32},
    {generated::PatchTrample1_addr, 0xE1A00000u, generated::PatchTrample1_crc32},
    {generated::PatchTrample2_addr, 0xE1A00000u, generated::PatchTrample2_crc32},
    {generated::PatchTrample3_addr, 0xE1A00000u, generated::PatchTrample3_crc32},
    {generated::PatchTrample4_addr, 0xE1A00000u, generated::PatchTrample4_crc32},
    {generated::PatchTrample5_addr, 0xE1A00000u, generated::PatchTrample5_crc32},
};
// 個別の判定を置き換える表。各判定は独立しており順は問わない。
const PatchWord kDig[] = {
    {generated::PatchDig0_addr, 0xE320F000u, generated::PatchDig0_crc32},
    {generated::PatchDig1_addr, 0xEA000095u, generated::PatchDig1_crc32},
    {generated::PatchDig2_addr, 0xEA00001Cu, generated::PatchDig2_crc32},
    {generated::PatchDig3_addr, 0xE320F000u, generated::PatchDig3_crc32},
    {generated::PatchDig4_addr, 0xE320F000u, generated::PatchDig4_crc32},
    {generated::PatchDig5_addr, 0xE320F000u, generated::PatchDig5_crc32},
    {generated::PatchDig6_addr, 0xEA000028u, generated::PatchDig6_crc32},
    {generated::PatchDig7_addr, 0xE320F000u, generated::PatchDig7_crc32},
    {generated::PatchDig8_addr, 0xEA000017u, generated::PatchDig8_crc32},
};
// 入口の 1 語だけを変更する。
const PatchWord kLookUp[] = {
    {generated::PatchLookUp0_addr, 0xEAFFFFDEu, generated::PatchLookUp0_crc32},
};
// 個別の判定を置き換える表。各判定は独立しており順は問わない。
const PatchWord kShop[] = {
    {generated::PatchShop0_addr, 0xE3A00001u, generated::PatchShop0_crc32},
    {generated::PatchShop1_addr, 0xE3A00001u, generated::PatchShop1_crc32},
    {generated::PatchShop2_addr, 0xE3A00001u, generated::PatchShop2_crc32},
    {generated::PatchShop3_addr, 0xE3A00001u, generated::PatchShop3_crc32},
    {generated::PatchShop4_addr, 0xE3A00001u, generated::PatchShop4_crc32},
    {generated::PatchShop5_addr, 0xE3A00001u, generated::PatchShop5_crc32},
    {generated::PatchShop6_addr, 0xE3A00001u, generated::PatchShop6_crc32},
    {generated::PatchShop7_addr, 0xE3A00001u, generated::PatchShop7_crc32},
    {generated::PatchShop8_addr, 0xE3A00001u, generated::PatchShop8_crc32},
};
// 入口の 1 語だけを変更する。
const PatchWord kText[] = {
    {generated::PatchText0_addr, 0xE3A00001u, generated::PatchText0_crc32},
};
// 入口の 1 語だけを変更する。
const PatchWord kFps[] = {
    {generated::PatchFps0_addr, 0xE3E004FFu, generated::PatchFps0_crc32},
};
// 各入口は独立。ケーブは Wire で全て配置済みなので入口の順は問わない。
const PatchWord kTree[] = {
    {generated::PatchTree0_addr, field_caves::Branch(generated::PatchTree0_addr, generated::g_TextPagePadding_addr + field_caves::kAllocations[0].offset + 0, 0xEB000000u), generated::PatchTree0_crc32},
};
// 各入口は独立。ケーブは Wire で全て配置済みなので入口の順は問わない。
const PatchWord kDig3[] = {
    {generated::PatchDig30_addr, field_caves::Branch(generated::PatchDig30_addr, generated::g_TextPagePadding_addr + field_caves::kAllocations[1].offset + 0, 0xEB000000u), generated::PatchDig30_crc32},
    {generated::PatchDig31_addr, field_caves::Branch(generated::PatchDig31_addr, generated::g_TextPagePadding_addr + field_caves::kAllocations[2].offset + 0, 0xEB000000u), generated::PatchDig31_crc32},
    {generated::PatchDig32_addr, field_caves::Branch(generated::PatchDig32_addr, generated::g_TextPagePadding_addr + field_caves::kAllocations[2].offset + 0, 0xEB000000u), generated::PatchDig32_crc32},
    {generated::PatchDig33_addr, field_caves::Branch(generated::PatchDig33_addr, generated::g_TextPagePadding_addr + field_caves::kAllocations[3].offset + 0, 0xEB000000u), generated::PatchDig33_crc32},
};
const PatchWord kWeather[] = {{generated::WeatherInstruction_addr, 0xE3A00000u, generated::WeatherInstruction_crc32}};
const auto tree = field_caves::Tree(generated::g_TextPagePadding_addr);
const auto DigHoles = field_caves::DigHoles(generated::g_TextPagePadding_addr);
const auto DigScrape = field_caves::DigScrape(generated::g_TextPagePadding_addr);
const auto DigDirt = field_caves::DigDirt(generated::g_TextPagePadding_addr);
const CaveWords kTreeCaves[] = {{field_caves::Address(0), tree}};
const CaveWords kDigCaves[] = {{field_caves::Address(1), DigHoles}, {field_caves::Address(2), DigScrape}, {field_caves::Address(3), DigDirt}};
PatchSet Wall(NativePatchMemory(), kWall);
PatchSet Flower(NativePatchMemory(), kFlower);
PatchSet Trap(NativePatchMemory(), kTrap);
PatchSet BedHead(NativePatchMemory(), kBedHead);
PatchSet Trample(NativePatchMemory(), kTrample);
PatchSet Dig(NativePatchMemory(), kDig);
PatchSet LookUp(NativePatchMemory(), kLookUp);
PatchSet Shop(NativePatchMemory(), kShop);
PatchSet Text(NativePatchMemory(), kText);
PatchSet Fps(NativePatchMemory(), kFps);
PatchSet Tree(NativePatchMemory(), kTree, kTreeCaves);
PatchSet Dig3(NativePatchMemory(), kDig3, kDigCaves);
PatchSet Weather(NativePatchMemory(), kWeather);
PatchSet *const sets[] = {&Wall, &Flower, &Trap, &BedHead, &Trample, &Dig, &LookUp, &Shop, &Text, &Fps, &Tree, &Dig3, &Weather};
game::Weather weather(Weather);
} // namespace
Result Wire(Patch id) {
    return IsSupportedTitle() ? sets[static_cast<unsigned>(id)]->Wire() : Result::Unsupported;
}
Result Set(Patch id, bool on) { return sets[static_cast<unsigned>(id)]->Set(on); }
bool IsApplied(Patch id) { return sets[static_cast<unsigned>(id)]->IsApplied(); }
void Shutdown(Patch id) { sets[static_cast<unsigned>(id)]->Shutdown(); }
bool ReadWeather(std::int32_t *value) { return weather.Read(value); }
Result WriteWeather(std::int32_t value) { return weather.Write(value); }
} // namespace gohan::game::cheats
