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
bool s_open;
Entry *s_index;
u32 s_count;
volatile u32 s_slots;                         // ヒープの枠の先頭（0 = 無し）
volatile u16 s_want[kSlots];                  // ゲームのスレッドが書く
volatile u16 s_have[kSlots];                  // メニューのスレッドが書く（読み終えて掃き出した後）

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
    if (s_open)
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
    for (u32 i = 0; i < kSlots; ++i)
        s_want[i] = s_have[i] = 0;
    s_open = true;
    return true;
}

void Close(void) {
    if (!s_open)
        return;
    s_open = false;
    s_slots = 0;
    s_file.Close();
    delete[] s_index;
    s_index = nullptr;
    s_count = 0;
}

bool IsOpen(void) { return s_open; }

bool Has(u16 hhdId) { return s_open && Find(hhdId) >= 0; }

void SetSlots(u32 base) {
    for (u32 i = 0; i < kSlots; ++i)
        s_have[i] = 0;                          // 枠が替わった（借り直した・返した）ので中身は無い
    s_slots = base;
}

void Want(u32 slot, u16 hhdId) {
    if (slot < kSlots)
        s_want[slot] = hhdId;
}

u32 Ready(u32 slot, u16 hhdId) {
    const u32 base = s_slots;
    if (slot >= kSlots || base == 0 || hhdId == 0 || s_have[slot] != hhdId || s_want[slot] != hhdId)
        return 0;
    return base + slot * kIconBytes;
}

void Service(u32 maxReads) {
    if (!s_open)
        return;
    for (u32 i = 0; i < kSlots && maxReads > 0; ++i) {
        const u32 base = s_slots;
        const u16 want = s_want[i];
        if (base == 0 || want == s_have[i])
            continue;
        s_have[i] = 0;                          // 読み替えている間は貼らない
        const int k = want != 0 ? Find(want) : -1;
        if (k < 0)
            continue;
        u8 *dst = reinterpret_cast<u8 *>(base + i * kIconBytes);
        if (s_file.Seek(s_index[k].offset, File::SET) != File::SUCCESS || s_file.Read(dst, kIconBytes) != File::SUCCESS)
            continue;
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)dst, kIconBytes);   // GPU は D-cache を見ない（HiddenIcons と同じ）
        if (s_slots == base && s_want[i] == want)
            s_have[i] = want;
        --maxReads;
    }
}

}  // namespace DecorIcons
