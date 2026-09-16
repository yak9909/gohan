#pragma once
#include <stdint.h>
#include <stddef.h>

namespace SwkbdProbe {
constexpr uint32_t ArenaSize = 5u * 1024u * 1024u;
constexpr uint32_t DictionarySize = 2045891;
constexpr uint32_t CompressedSize = 471668;
constexpr uint32_t CompressedOffset = 0x200000;
// Future resident layout: dictionary + image/BSS + context + stack/I/O + compressed source.
constexpr uint32_t PlannedSize = 0x200000 + 0xC6000 + 0x100000 + 0x10000 + 0x74000;
static_assert(PlannedSize <= ArenaSize, "resident plan exceeds arena");
static_assert(CompressedOffset >= DictionarySize, "source buffers overlap");
static_assert(CompressedOffset + CompressedSize <= ArenaSize, "source overflow");

struct FileReport {
    uint32_t openResult, sizeResult, readResult, closeResult, releaseResult;
    uint32_t sizeLow, sizeHigh, bytesRead, crc32;
};
struct Report {
    uint32_t magic, version, state, failure;
    uint32_t heapBase, heapSize, exeSize, arena, requested, touchPass, freed;
    FileReport dictionary, code;
    uint32_t logResult;
};
static_assert(sizeof(Report) == 120, "mailbox ABI changed");
// state: 0 idle, 1 running, 2 passed, 3 failed. Unknown Result = 0xFFFFFFFF.
// failure: 1 title/header, 2 allocation, 3 range, 4 write/readback, 5 dictionary, 6 code.
struct Api {
    virtual void *Allocate(size_t size) = 0;
    virtual void Free(void *ptr) = 0;
    virtual bool Validate(void *ptr, size_t size) = 0;
    virtual uint32_t Open(bool code, uint32_t &handle) = 0;
    virtual uint32_t Size(uint32_t handle, uint64_t &size) = 0;
    virtual uint32_t Read(uint32_t handle, void *ptr, uint32_t size, uint32_t &read) = 0;
    virtual uint32_t Close(uint32_t handle) = 0;
    virtual uint32_t Release(uint32_t handle) = 0;
};
uint32_t Crc32(const void *ptr, size_t size);
void Reset(Report &report);
bool Acquire(Api &api, bool code, void *ptr, FileReport &report);
void Run(Api &api, Report &report);
}
