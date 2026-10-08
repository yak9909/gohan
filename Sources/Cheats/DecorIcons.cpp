// DecorIcons — カタログのアイコンの束を読む。説明は DecorIcons.hpp。

#include "DecorIcons.hpp"
#include "GohanFiles.hpp"

#include <CTRPluginFramework.hpp>
#include <3ds.h>

#include <cstring>
#include <new>

namespace DecorIcons {

namespace {

using CTRPluginFramework::File;

const char kBankName[] = "hhd_icons.bin";
const u32 kHeaderBytes = 32, kEntryBytes = 8, kMaxCount = 8192;

struct Entry { u16 id; u16 zero; u32 offset; };

File s_file;
bool s_open;                                  // atomic。索引の初期化が済んでから公開する
Entry *s_index;
u32 s_count;
u32 s_slots;                                  // atomic。0 = 無し
u16 s_want[kSlots], s_have[kSlots];            // atomic。have はキャッシュ掃き出し成功後に公開
u32 s_busy, s_serviceSeq;                     // atomic。枠を借りる前に busy を公開する
volatile u32 s_serviceStage, s_serviceSlot, s_serviceWant, s_serviceResult; // 停止時の診断だけ

inline u32 Le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

int Find(u16 id) {
    u32 lo = 0, hi = s_count;
    while (lo < hi) {
        const u32 mid = (lo + hi) / 2;
        if (s_index[mid].id == id)
            return (int)mid;
        if (s_index[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

}  // namespace

bool Open(void) {
    if (__atomic_load_n(&s_open, __ATOMIC_ACQUIRE))
        return true;
    char path[96];
    if (!GohanFiles::CommonPath(path, sizeof(path), kBankName) || File::Open(s_file, path, File::READ) != File::SUCCESS)
        return false;
    u8 head[kHeaderBytes];
    const u64 size = s_file.GetSize();
    if (size < kHeaderBytes || s_file.Read(head, kHeaderBytes) != File::SUCCESS || std::memcmp(head, "HHDICN1\0", 8) != 0
        || Le32(head + 8) != 1 || Le32(head + 20) != kEntryBytes || Le32(head + 28) != kIconBytes) {
        s_file.Close();
        return false;
    }
    const u32 count = Le32(head + 12), indexAt = Le32(head + 16), dataAt = Le32(head + 24);
    if (count == 0 || count > kMaxCount || (u64)indexAt + (u64)count * kEntryBytes > dataAt
        || (u64)dataAt + (u64)count * kIconBytes > size) {
        s_file.Close();
        return false;
    }
    s_index = new (std::nothrow) Entry[count];
    if (s_index == nullptr || s_file.Seek(indexAt, File::SET) != File::SUCCESS
        || s_file.Read(s_index, count * kEntryBytes) != File::SUCCESS) {
        delete[] s_index;
        s_index = nullptr;
        s_file.Close();
        return false;
    }
    for (u32 i = 0; i < count; ++i)              // 昇順・範囲・境界を確かめる（壊れた束で範囲外を読まない）
        if ((i > 0 && s_index[i].id <= s_index[i - 1].id) || s_index[i].offset < dataAt
            || (u64)s_index[i].offset + kIconBytes > size) {
            delete[] s_index;
            s_index = nullptr;
            s_file.Close();
            return false;
        }
    s_count = count;
    for (u32 i = 0; i < kSlots; ++i) {
        __atomic_store_n(&s_want[i], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&s_have[i], 0, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&s_open, true, __ATOMIC_RELEASE);
    return true;
}

void Close(void) {
    if (!__atomic_load_n(&s_open, __ATOMIC_ACQUIRE))
        return;
    __atomic_store_n(&s_open, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_slots, 0, __ATOMIC_SEQ_CST);
    s_file.Close();
    delete[] s_index;
    s_index = nullptr;
    s_count = 0;
}

bool IsOpen(void) { return __atomic_load_n(&s_open, __ATOMIC_ACQUIRE); }

bool Has(u16 hhdId) { return IsOpen() && Find(hhdId) >= 0; }

void SetSlots(u32 base) {
    // Service の busy→base と、この base→busy（CanRelease）を同じ全順序へ置く。
    // 外す操作より前に旧baseを取ったServiceがいれば、必ずbusyが見えて解放を待つ。
    __atomic_store_n(&s_slots, 0, __ATOMIC_SEQ_CST);
    for (u32 i = 0; i < kSlots; ++i)
        __atomic_store_n(&s_have[i], 0, __ATOMIC_RELEASE);
    if (base != 0)
        __atomic_store_n(&s_slots, base, __ATOMIC_SEQ_CST);
}

void Want(u32 slot, u16 hhdId) {
    if (slot < kSlots)
        __atomic_store_n(&s_want[slot], hhdId, __ATOMIC_RELEASE);
}

u32 Ready(u32 slot, u16 hhdId) {
    const u32 base = __atomic_load_n(&s_slots, __ATOMIC_SEQ_CST);
    if (slot >= kSlots || base == 0 || hhdId == 0 || __atomic_load_n(&s_have[slot], __ATOMIC_ACQUIRE) != hhdId
        || __atomic_load_n(&s_want[slot], __ATOMIC_ACQUIRE) != hhdId)
        return 0;
    return base + slot * kIconBytes;
}

void Service(u32 maxReads) {
    s_serviceStage = 1;
    if (!IsOpen()) {
        __atomic_add_fetch(&s_serviceSeq, 1u, __ATOMIC_RELEASE);
        s_serviceStage = 0;
        return;
    }
    __atomic_store_n(&s_busy, 1u, __ATOMIC_SEQ_CST);
    for (u32 i = 0; i < kSlots && maxReads > 0; ++i) {
        const u32 base = __atomic_load_n(&s_slots, __ATOMIC_SEQ_CST);
        const u16 want = __atomic_load_n(&s_want[i], __ATOMIC_ACQUIRE);
        if (base == 0 || want == __atomic_load_n(&s_have[i], __ATOMIC_ACQUIRE))
            continue;
        __atomic_store_n(&s_have[i], 0, __ATOMIC_RELEASE);
        const int k = want != 0 ? Find(want) : -1;
        if (k < 0)
            continue;
        u8 *dst = reinterpret_cast<u8 *>(base + i * kIconBytes);
        --maxReads;                             // SDエラーのときも1ティックの試行数を守る
        s_serviceSlot = i;
        s_serviceWant = want;
        s_serviceStage = 2;
        s_serviceResult = s_file.Seek(s_index[k].offset, File::SET);
        if (s_serviceResult != File::SUCCESS)
            continue;
        s_serviceStage = 3;
        s_serviceResult = s_file.Read(dst, kIconBytes);
        if (s_serviceResult != File::SUCCESS)
            continue;
        s_serviceStage = 4;
        s_serviceResult = svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)dst, kIconBytes);
        if (s_serviceResult == 0 && __atomic_load_n(&s_slots, __ATOMIC_SEQ_CST) == base
            && __atomic_load_n(&s_want[i], __ATOMIC_ACQUIRE) == want)
            __atomic_store_n(&s_have[i], want, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&s_busy, 0u, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&s_serviceSeq, 1u, __ATOMIC_RELEASE);
    s_serviceStage = 0;
}

u32 ReleaseTicket(void) { return __atomic_load_n(&s_serviceSeq, __ATOMIC_ACQUIRE); }

bool CanRelease(u32 ticket) {
    // 枠を外した（SetSlots(0)）後に Service が 1 回終わった = 外す前から読んでいた分も書き終えた。読んでいなければ（busy でない）すぐ返してよい
    return __atomic_load_n(&s_busy, __ATOMIC_SEQ_CST) == 0
        && (__atomic_load_n(&s_serviceSeq, __ATOMIC_ACQUIRE) != ticket || __atomic_load_n(&s_slots, __ATOMIC_SEQ_CST) == 0);
}

}  // namespace DecorIcons
