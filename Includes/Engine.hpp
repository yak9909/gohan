#pragma once
#include "Probe.hpp"

namespace SwkbdEngine {
constexpr uint32_t ImageOffset = 0x200000, ImageSize = 0xC6000, CodeSize = 770048;
constexpr uint32_t ContextOffset = ImageOffset + ImageSize, ContextSize = 0x100000;
constexpr uint32_t StackOffset = ContextOffset + ContextSize, StackSize = 0x10000;
constexpr uint32_t SourceOffset = StackOffset + StackSize;
static_assert(SourceOffset + SwkbdProbe::CompressedSize < SwkbdProbe::ArenaSize, "arena overflow");
constexpr uint32_t MaxCandidates = 300, TextUnits = 56;
struct Report {
    uint32_t magic, version, state, failure;
    volatile uint32_t stage;
    uint32_t arena, freed;
    uint32_t codeCrc, relocationCount, dictionaryUsed, prepared, executed;
    int32_t initResult, conversionResult;
    uint32_t count, exhausted, stackGuard;
    SwkbdProbe::FileReport dictionary, code;
    uint16_t input[TextUnits];
    uint16_t lengths[MaxCandidates];
    uint16_t candidates[MaxCandidates][TextUnits];
};
// stage: 1 allocate, 2 dictionary read, 3 code read, 4 prepare, 5 init,
// 6 bind dictionaries, 7 Japanese init, 8 convert, 9 enumerate, 10 returned.
// failure: 1 header/title, 2 allocate, 3 memory range, 4 dict read, 5 code read,
// 6 input, 7 BLZ, 8 code identity, 9 RomFS, 10 dictionary layout/header,
// 11 relocation, 12 language init, 13 conversion/getter, 14 stack guard.
bool Decompress(const uint8_t *src, uint32_t size, uint8_t *dst, uint32_t capacity);
void Reset(Report &r);
void Run(SwkbdProbe::Api &api, bool execute, const uint16_t *input, Report &r);
}
extern "C" void SwkbdEngine_Process(uint8_t *arena, uint32_t execute, SwkbdEngine::Report *report);
extern "C" int32_t SwkbdEngine_Call(uint32_t entry, void *stackTop, const uint32_t *args);
extern "C" void SwkbdEngine_SyncCode();
