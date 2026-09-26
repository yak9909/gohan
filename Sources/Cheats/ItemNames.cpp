// ============================================================================
// ItemNames — アイテム ID から名前（gohan.md §17.5.1 / 今後のアイテム検索）
// ============================================================================
//
// 通常アイテムの名前 = ROM の Script/Str/STR_Item_name.umsbt（解析 repo tools/items/dump_item_names.py と同じ読み方）:
//   先頭の表 {u32 オフセット, u32 大きさ} の先に MSBT が 1 本。区画は ATR1 と TXT2（LBL1 なし）。TXT2 の i 番目 = ID 0x2000 + i。
// 自前表示の名前 = ゲームの ItemName_ResolvePhrase 0x56D274(phrase, &item, a3) の後で、没アイテムなら
//   phrase を空にして（vt+28）、表の漢字名を指す script::WordRes {0x904954, 文字列, 0x3FFFFFFF} を入れる（vt+20）。
//   ゲーム自身も名前が空のとき同じ形で「???」（0x9303F8）を入れている（IDA-opus-5.5-F049）。

#include "ItemNames.hpp"

#include "RomfsIndex.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstring>
#include <new>

namespace ItemNames {

namespace {

using CTRPluginFramework::Hook;
using CTRPluginFramework::HookContext;
using CTRPluginFramework::HookResult;

const char kNamePath[] = "Script/Str/STR_Item_name.umsbt";
const u16 kIdBase = 0x2000;

u8 *s_file;         // umsbt 全体
u32 s_fileSize;
const u8 *s_txt;    // TXT2 の中身
u32 s_txtSize;
u32 s_count;

inline u32 Rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
inline u16 Rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }

bool Parse(void) {
    if (s_fileSize < 8)
        return false;
    const u32 off = Rd32(s_file), size = Rd32(s_file + 4);
    if (off > s_fileSize || size > s_fileSize - off || size < 0x20)
        return false;
    const u8 *d = s_file + off;
    if (std::memcmp(d, "MsgStdBn", 8) != 0 || d[8] != 0xFF || d[9] != 0xFE)
        return false;
    const u32 sections = Rd16(d + 0xE);
    u32 o = 0x20;
    for (u32 k = 0; k < sections && o + 16 <= size; k++) {
        const u32 sz = Rd32(d + o + 4);
        if (sz > size - o - 16)
            return false;
        if (std::memcmp(d + o, "TXT2", 4) == 0) {
            s_txt = d + o + 16;
            s_txtSize = sz;
            s_count = sz >= 4 ? Rd32(s_txt) : 0;
            return s_count > 0 && 4 + 4 * (u64)s_count <= sz;
        }
        o = (o + 16 + sz + 15) & ~15u;
    }
    return false;
}

// ---- 自前表示のフック ----
const u32 kResolvePhrase = 0x0056D274;      // ItemName_ResolvePhrase(phrase, &item, a3)
const u32 kResolvePhraseOrig = 0xE92D40F0;  // PUSH {R4-R7,LR}
const u32 kVtWordRes = 0x00904954;          // script::WordRes（sub_5FC344 が組む形）
const u32 kWordResMax = 0x3FFFFFFF;
Hook s_hook;
bool s_hooked;
volatile bool s_custom;

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }

__attribute__((noinline)) u32 ResolvePhraseHook(u32 phrase, u16 *item, u32 a3) {
    HookContext &ctx = HookContext::GetCurrent();
    const u32 r = ctx.OriginalFunction<u32>(phrase, item, a3);

    if (s_custom && phrase != 0 && item != nullptr) {
        const HiddenItemTable::Entry *e = FindHidden((u16)(*item & 0x7FFF));

        if (e != nullptr) {
            typedef void (*ClearFn)(u32);
            typedef void (*AssignFn)(u32, const u32 *);
            const u32 vt = R32(phrase);
            const u32 word[3] = { kVtWordRes, reinterpret_cast<u32>(e->name16), kWordResMax };

            reinterpret_cast<ClearFn>(R32(vt + 28))(phrase);
            reinterpret_cast<AssignFn>(R32(vt + 20))(phrase, word);
        }
    }
    return r;
}

}  // namespace

bool LoadNormal(void) {
    if (s_count != 0)
        return true;
    const u32 size = RomfsIndex::FileSize(kNamePath);
    if (size == 0)
        return false;
    u8 *buf = new (std::nothrow) u8[size];
    if (buf == nullptr)
        return false;
    if (RomfsIndex::ReadFile(kNamePath, buf, size) != size) {
        delete[] buf;
        return false;
    }
    s_file = buf;
    s_fileSize = size;
    if (!Parse()) {
        ReleaseNormal();
        return false;
    }
    return true;
}

void ReleaseNormal(void) {
    delete[] s_file;
    s_file = nullptr;
    s_fileSize = 0;
    s_txt = nullptr;
    s_txtSize = 0;
    s_count = 0;
}

u32 NormalCount(void) {
    return s_count;
}

const u16 *NormalName(u16 id, u32 &length) {
    length = 0;
    id &= 0x7FFF;
    if (s_count == 0 || id < kIdBase || (u32)(id - kIdBase) >= s_count)
        return nullptr;
    const u32 i = id - kIdBase;
    const u32 start = Rd32(s_txt + 4 + 4 * i);
    const u32 end = i + 1 < s_count ? Rd32(s_txt + 4 + 4 * (i + 1)) : s_txtSize;
    if (start >= s_txtSize || end > s_txtSize || end < start || (start & 1) != 0)
        return nullptr;
    const u16 *p = reinterpret_cast<const u16 *>(s_txt + start);
    const u32 units = (end - start) / 2;
    while (length < units && p[length] != 0)
        length++;
    return p;
}

const HiddenItemTable::Entry *FindHidden(u16 id) {
    u32 lo = 0, hi = HiddenItemTable::kCount;
    while (lo < hi) {
        const u32 mid = (lo + hi) / 2;
        const u16 v = HiddenItemTable::kEntries[mid].id;
        if (v == id)
            return &HiddenItemTable::kEntries[mid];
        if (v < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}

bool NameUtf8(u16 id, char *out, u32 cap, bool kana) {
    if (out == nullptr || cap == 0)
        return false;
    out[0] = '\0';
    const HiddenItemTable::Entry *e = FindHidden((u16)(id & 0x7FFF));
    if (e != nullptr) {
        const char *s = kana ? e->kana : e->name;
        std::strncpy(out, s, cap - 1);
        out[cap - 1] = '\0';
        return true;
    }
    u32 len = 0;
    const u16 *p = LoadNormal() ? NormalName(id, len) : nullptr;
    if (p == nullptr || len == 0)
        return false;
    u32 o = 0;
    for (u32 k = 0; k < len; k++) {
        u32 c = p[k];
        if (c == 0x000E) {                      // 制御タグ: 種類 u16, 番号 u16, 長さ u16, 引数
            if (k + 3 >= len)
                break;
            k += 3 + (p[k + 3] + 1) / 2;
            continue;
        }
        if (c == 0x000F) {
            k += 2;
            continue;
        }
        if (c >= 0xD800 && c <= 0xDBFF && k + 1 < len) {
            c = 0x10000 + ((c - 0xD800) << 10) + (p[k + 1] - 0xDC00);
            k++;
        }
        const u32 need = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (o + need >= cap)
            break;
        if (need == 1)
            out[o++] = (char)c;
        else if (need == 2) {
            out[o++] = (char)(0xC0 | c >> 6);
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else if (need == 3) {
            out[o++] = (char)(0xE0 | c >> 12);
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[o++] = (char)(0xF0 | c >> 18);
            out[o++] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = '\0';
    return o > 0;
}

bool SetCustomNames(bool on) {
    if (on && !s_hooked) {
        if (CTRPluginFramework::Process::GetTitleID() != 0x0004000000086200ULL || R32(kResolvePhrase) != kResolvePhraseOrig)
            return false;
        s_hook.InitializeForMitm(kResolvePhrase, reinterpret_cast<u32>(ResolvePhraseHook));
        if (s_hook.Enable() != HookResult::Success)
            return false;
        s_hooked = true;
    }
    s_custom = on;          // フックは入れたまま（切っている間は元の名前をそのまま返す）
    return true;
}

bool CustomNames(void) {
    return s_custom;
}

}  // namespace ItemNames
