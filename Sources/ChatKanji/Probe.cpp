#include "Probe.hpp"
#include <string.h>

namespace SwkbdProbe {
static bool Success(uint32_t result) { return (result & 0x80000000u) == 0; }
uint32_t Crc32(const void *ptr, size_t size) {
    const uint8_t *p = static_cast<const uint8_t *>(ptr);
    uint32_t crc = 0xFFFFFFFFu;
    while (size--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
void Reset(Report &r) {
    memset(&r, 0, sizeof(r));
    r.magic = 0x53575034; r.version = 1; r.requested = ArenaSize;
    memset(&r.dictionary, 0xFF, sizeof(FileReport));
    memset(&r.code, 0xFF, sizeof(FileReport));
    r.logResult = 0xFFFFFFFF;
}
bool Acquire(Api &api, bool code, void *ptr, FileReport &r) {
    const uint32_t expectedSize = code ? CompressedSize : DictionarySize;
    const uint32_t expectedCrc = code ? 0xB75F8ACBu : 0x1A191078u;
    uint32_t handle = 0;
    r.openResult = api.Open(code, handle);
    if (!Success(r.openResult)) return false;
    // The target bridge rejects an invalid handle on successful Open.
    uint64_t size = 0;
    r.sizeResult = api.Size(handle, size);
    r.sizeLow = static_cast<uint32_t>(size);
    r.sizeHigh = static_cast<uint32_t>(size >> 32);
    bool ok = Success(r.sizeResult) && size == expectedSize;
    if (ok) {
        uint32_t got = 0;
        // ExeFS .code must be read in a single full-length request (F-402).
        r.readResult = api.Read(handle, ptr, expectedSize, got);
        r.bytesRead = got;
        ok = Success(r.readResult) && got == expectedSize;
        if (ok) {
            r.crc32 = Crc32(ptr, expectedSize);
            ok = r.crc32 == expectedCrc;
        }
    }
    // Release the handle even if GetSize/Read/Close failed; never double-close.
    r.closeResult = api.Close(handle);
    r.releaseResult = api.Release(handle);
    return ok && Success(r.closeResult) && Success(r.releaseResult);
}
void Run(Api &api, Report &r) {
    r.state = 1;
    void *arena = api.Allocate(ArenaSize);
    r.arena = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arena));
    if (!arena) { r.failure = 2; r.state = 3; return; }
    if (!api.Validate(arena, ArenaSize)) r.failure = 3;
    else {
        volatile uint32_t *words = static_cast<volatile uint32_t *>(arena);
        for (uint32_t i = 0; i < ArenaSize / sizeof(uint32_t); ++i)
            words[i] = i ^ 0xA53C96E7u;
        r.touchPass = 1;
        for (uint32_t i = 0; i < ArenaSize / sizeof(uint32_t); ++i)
            if (words[i] != (i ^ 0xA53C96E7u)) { r.touchPass = 0; break; }
        if (!r.touchPass) r.failure = 4;
        else if (!Acquire(api, false, arena, r.dictionary)) r.failure = 5;
        else if (!Acquire(api, true, static_cast<uint8_t *>(arena) + CompressedOffset, r.code)) r.failure = 6;
    }
    api.Free(arena);
    r.freed = 1;
    r.state = r.failure ? 3 : 2;
}
}
