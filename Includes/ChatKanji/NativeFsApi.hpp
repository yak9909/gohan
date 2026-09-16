#pragma once
#include <3ds.h>
#include <CTRPluginFramework/System/FwkSettings.hpp>
#include <malloc.h>
#include "Probe.hpp"
class NativeFsApi : public SwkbdProbe::Api {
public:
    void *Allocate(size_t n) override { return memalign(4096, n); }
    void Free(void *p) override { free(p); }
    bool Validate(void *p, size_t n) override {
        const CTRPluginFramework::PluginHeader *h = CTRPluginFramework::FwkSettings::Header;
        const uint64_t begin = reinterpret_cast<uintptr_t>(p), end = begin + n;
        if ((begin & 4095) || begin < h->heapVA || end > uint64_t(h->heapVA) + h->heapSize)
            return false;
        uint64_t cursor = begin;
        while (cursor < end) {
            MemInfo info = {}; PageInfo page = {};
            if (R_FAILED(svcQueryMemory(&info, &page, static_cast<u32>(cursor)))) return false;
            const uint64_t next = uint64_t(info.base_addr) + info.size;
            if (info.base_addr > cursor || next <= cursor || (info.perm & 3) != 3) return false;
            cursor = next;
        }
        return true;
    }
    uint32_t Open(bool code, uint32_t &handle) override {
        // Archive path: title ID, NAND media ID, zero. Binary file path: kind 0 RomFS / 2 ExeFS.
        const u32 archive[4] = { code ? 0x0000C002u : 0x00011902u,
                                code ? 0x00040030u : 0x0004009Bu, 0, 0 };
        const u32 file[5] = { 0, 0, code ? 2u : 0u, code ? 0x646F632Eu : 0u, code ? 0x65u : 0u };
        FS_Path ap = {PATH_BINARY, sizeof(archive), archive};
        FS_Path fp = {PATH_BINARY, sizeof(file), file};
        Handle out = 0;
        Result result = FSUSER_OpenFileDirectly(&out, static_cast<FS_ArchiveID>(0x2345678A), ap, fp, FS_OPEN_READ, 0);
        if (R_SUCCEEDED(result) && (!out || out == 0xFFFFFFFFu)) return 0xFFFFFFFF;
        handle = out;
        return result;
    }
    uint32_t Size(uint32_t h, uint64_t &s) override { return FSFILE_GetSize(h, &s); }
    uint32_t Read(uint32_t h, void *p, uint32_t n, uint32_t &got) override {
        return FSFILE_Read(h, &got, 0, p, n);
    }
    uint32_t Close(uint32_t h) override {
        u32 *cmd = getThreadCommandBuffer();
        cmd[0] = 0x08080000;
        Result r = svcSendSyncRequest(h);
        return R_FAILED(r) ? r : cmd[1];
    }
    uint32_t Release(uint32_t h) override { return svcCloseHandle(h); }
};
