#include "menu/persist.hpp"
#include <cstring>

// 形式の書き出しと読み込みの検査（docs/design/menu-item-actions.md の 7.2）。CTRPF には触れない。
namespace gohan::menu::persist {
namespace {
// ボタンのビットは 14 個（ZL から Start まで）。
constexpr u16 kButtonBits = 0x3FFF;

u16 ReadU16(const u8 *p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 ReadU32(const u8 *p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
void WriteU16(u8 *p, u16 value) {
    p[0] = static_cast<u8>(value);
    p[1] = static_cast<u8>(value >> 8);
}
void WriteU32(u8 *p, u32 value) {
    for (std::size_t i = 0; i < 4; ++i) p[i] = static_cast<u8>(value >> (8 * i));
}

// 欄の旗から、その欄の大きさ（バイト）。
std::size_t FieldBytes(u8 fields) {
    return ((fields & kValue) ? 4u : 0u) + ((fields & kHotkey) ? 2u : 0u) + ((fields & kFavorite) ? 2u : 0u);
}

// 項目が設定画面の根の下にあるか。設定画面の物は記録に書かない（状態はヘッダーに書く）。
bool InSettings(const ItemDef *defs, std::size_t count, u16 item, u16 root) {
    if (root == kNoItem) return false;
    u16 i = item;
    for (std::size_t guard = 0; guard < count && i != kNoItem && i < count; ++guard) {
        if (i == root) return true;
        i = defs[i].parent;
    }
    return false;
}

// 項目が隠れているか（項目か親が開発版で、開発版の項目を出していない）。エンジンの Hidden と同じ決まり。
bool Hidden(const ItemDef *defs, std::size_t count, u16 item, bool dev_visible) {
    if (dev_visible) return false;
    u16 i = item;
    for (std::size_t guard = 0; guard < count && i != kNoItem && i < count; ++guard) {
        if (defs[i].dev) return true;
        i = defs[i].parent;
    }
    return false;
}

// 読んだ後（まだ捨てる前）の、項目のホットキー。記録が無ければ既定。
u16 HotkeyOf(const ItemDef *defs, const u8 *slot, const Saved &saved, u16 item) {
    const u8 index = slot[item];
    if (index != 0 && (saved.records[index - 1].fields & kHotkey)) return saved.records[index - 1].hotkey;
    return defs[item].hotkey;
}
} // namespace

u16 FindItem(const ItemDef *defs, std::size_t count, const char *key, std::size_t length) {
    if (!defs || !key || length == 0) return kNoItem;
    for (std::size_t i = 0; i < count; ++i) {
        const char *name = defs[i].key;
        if (!name) continue;
        std::size_t n = 0;
        while (n < length && name[n] != '\0' && name[n] == key[n]) ++n;
        if (n == length && name[n] == '\0') return static_cast<u16>(i);
    }
    return kNoItem;
}

std::size_t Encode(const Saved &saved, const ItemDef *defs, std::size_t count, u8 *out, std::size_t capacity) {
    if (!out || !defs || saved.count > core::limits::MenuItems) return 0;
    std::size_t size = kHeaderBytes;
    for (std::size_t n = 0; n < saved.count; ++n) {
        const Record &r = saved.records[n];
        if (r.item >= count || !defs[r.item].key) return 0;
        const std::size_t id_bytes = std::strlen(defs[r.item].key);
        if (id_bytes == 0 || id_bytes > kMaxIdBytes) return 0;
        size += 1 + id_bytes + 1 + FieldBytes(r.fields & kFieldMask);
    }
    if (size > capacity) return 0;
    for (std::size_t n = 0; n < 4; ++n) out[n] = static_cast<u8>(kMagic[n]);
    WriteU16(out + 4, kVersion);
    out[6] = saved.keep_toggles ? kKeepToggles : 0;
    out[7] = 0; // 予備
    WriteU16(out + 8, saved.press_keys);
    WriteU16(out + 10, static_cast<u16>(saved.count));
    std::size_t pos = kHeaderBytes;
    for (std::size_t n = 0; n < saved.count; ++n) {
        const Record &r = saved.records[n];
        const u8 fields = r.fields & kFieldMask;
        const char *key = defs[r.item].key;
        const std::size_t id_bytes = std::strlen(key);
        out[pos++] = static_cast<u8>(id_bytes);
        std::memcpy(out + pos, key, id_bytes);
        pos += id_bytes;
        out[pos++] = fields;
        if (fields & kValue) {
            WriteU32(out + pos, static_cast<u32>(r.value));
            pos += 4;
        }
        if (fields & kHotkey) {
            WriteU16(out + pos, r.hotkey);
            pos += 2;
        }
        if (fields & kFavorite) {
            WriteU16(out + pos, r.order);
            pos += 2;
        }
    }
    return pos;
}

bool Decode(const u8 *data, std::size_t size, const ItemDef *defs, std::size_t count, const Rules &rules, Saved &saved) {
    saved.keep_toggles = false;
    saved.press_keys = 0;
    saved.count = 0;
    if (!data || !defs || size < kHeaderBytes || count > core::limits::MenuItems) return false;
    for (std::size_t n = 0; n < 4; ++n)
        if (data[n] != static_cast<u8>(kMagic[n])) return false;
    if (ReadU16(data + 4) != kVersion) return false;
    // 記録の数は項目の上限まで。表の項目数より多くても、知っている鍵の記録は読む（未知の鍵の記録はその記録だけ捨てる）。
    const std::size_t total = ReadU16(data + 10);
    if (total > core::limits::MenuItems) return false;
    saved.keep_toggles = (data[6] & kKeepToggles) != 0;
    saved.press_keys = static_cast<u16>(ReadU16(data + 8) & rules.press_valid);

    // 読む。slot は 項目 → 記録の番号 + 1（0 は記録が無い）。同じ項目の 2 つ目の記録は捨てる。
    u8 slot[core::limits::MenuItems] = {};
    std::size_t pos = kHeaderBytes;
    for (std::size_t n = 0; n < total; ++n) {
        if (pos >= size) break;
        const std::size_t id_bytes = data[pos++];
        if (id_bytes > size - pos) break; // 途中で切れた記録は、それ以降を読まずに捨てる
        const u8 *id = data + pos;
        pos += id_bytes;
        if (pos >= size) break;
        const u8 fields = static_cast<u8>(data[pos++] & kFieldMask);
        if (FieldBytes(fields) > size - pos) break;
        s32 value = 0;
        u16 hotkey = 0;
        u16 order = 0;
        if (fields & kValue) {
            value = static_cast<s32>(ReadU32(data + pos));
            pos += 4;
        }
        if (fields & kHotkey) {
            hotkey = ReadU16(data + pos);
            pos += 2;
        }
        if (fields & kFavorite) {
            order = ReadU16(data + pos);
            pos += 2;
        }
        // 鍵の長さが 1〜64 でない・未知の鍵・設定画面の物・2 つ目の記録は、この記録だけを捨てる（欄の長さは読んだ）。
        if (id_bytes == 0 || id_bytes > kMaxIdBytes) continue;
        const u16 item = FindItem(defs, count, reinterpret_cast<const char *>(id), id_bytes);
        if (item == kNoItem || slot[item] != 0 || InSettings(defs, count, item, rules.settings_root) ||
            Hidden(defs, count, item, rules.dev_visible))
            continue;
        const ItemDef &d = defs[item];
        Record r;
        r.item = item;
        u8 kept = 0;
        // ホットキーは項目の種類によらず持ち越す（フォルダと値のキーだけは持てない）。予約と範囲外の物は捨てる。
        if ((fields & kHotkey) && d.control != Control::Folder && d.control != Control::Key && (hotkey & ~kButtonBits) == 0 &&
            (hotkey & rules.reserved) == 0) {
            kept |= kHotkey;
            r.hotkey = hotkey;
        }
        if (fields & kFavorite) {
            kept |= kFavorite;
            r.order = order;
        }
        if (d.control == Control::Toggle && (d.behavior == Behavior::State || d.behavior == Behavior::Hold)) {
            // 固定できるトグル。ON は固定の時か、「トグルを全て保持」の時だけ。
            if (d.persist && (fields & kFixed)) kept |= kFixed;
            if (d.persist && (fields & kOn) && ((kept & kFixed) || saved.keep_toggles)) kept |= kOn;
        } else if (d.behavior == Behavior::Value && d.source == Source::Game) {
            // ゲームの値は固定した物だけ（固定値を持つ）。固定できない値は持ち越さない。
            if (d.lockable && (fields & kFixed) && (fields & kValue)) {
                kept |= kFixed | kValue;
                r.value = NormalizeValue(d, value);
            }
        } else if (d.behavior == Behavior::Value && d.persist) {
            if (d.control == Control::Key) {
                // キーの項目は、ホットキーと同じく持ち越す（固定はできない）。
                if (fields & kValue) {
                    kept |= kValue;
                    r.value = NormalizeValue(d, value);
                }
            } else if ((fields & kFixed) && (fields & kValue)) {
                // 設定の値は固定した物だけ。前の版が書いた kFixed の無い kValue は、固定していない物なので捨てる。
                kept |= kFixed | kValue;
                r.value = NormalizeValue(d, value);
            }
        }
        if (kept & kOn) {
            // 同じ group の ON は 1 つだけ（最初に読んだ物を残す。排他の決まり）。
            for (std::size_t k = 0; d.group != 0 && k < saved.count; ++k)
                if ((saved.records[k].fields & kOn) && defs[saved.records[k].item].group == d.group) {
                    kept = static_cast<u8>(kept & ~kOn);
                    break;
                }
        }
        if (kept == 0) continue;
        r.fields = kept;
        slot[item] = static_cast<u8>(saved.count + 1);
        saved.records[saved.count++] = r;
    }

    // お気に入りの並び: order の小さい順（同じ順は記録の順）に 0 から振り直す。
    u8 favorites[core::limits::MenuItems] = {};
    std::size_t favorite_count = 0;
    for (std::size_t k = 0; k < saved.count; ++k)
        if (saved.records[k].fields & kFavorite) favorites[favorite_count++] = static_cast<u8>(k);
    for (std::size_t a = 1; a < favorite_count; ++a) {
        const u8 current = favorites[a];
        std::size_t b = a;
        while (b > 0 && saved.records[favorites[b - 1]].order > saved.records[current].order) {
            favorites[b] = favorites[b - 1];
            --b;
        }
        favorites[b] = current;
    }
    for (std::size_t a = 0; a < favorite_count; ++a) saved.records[favorites[a]].order = static_cast<u16>(a);

    // 重なるホットキー: 他の項目の今のホットキー（記録が無ければ既定）と同じ物は捨てる。
    // 1 回の判定で重なる物を全て同時に捨て、捨てた後にまだ重なる物が残れば、重なりが無くなるまで同じ判定を繰り返す
    // （捨てた物は既定に戻り、別の記録と新しく重なるかもしれない）。捨てるたびに記録の欄が 1 つ減るので、必ず終わる。
    for (;;) {
        bool drop[core::limits::MenuItems] = {};
        bool any = false;
        for (std::size_t k = 0; k < saved.count; ++k) {
            const Record &r = saved.records[k];
            if (!(r.fields & kHotkey) || r.hotkey == 0) continue;
            for (u16 j = 0; j < count; ++j) {
                if (j == r.item || Hidden(defs, count, j, rules.dev_visible) || HotkeyOf(defs, slot, saved, j) != r.hotkey) continue;
                drop[k] = true;
                any = true;
                break;
            }
        }
        if (!any) break;
        for (std::size_t k = 0; k < saved.count; ++k)
            if (drop[k]) saved.records[k].fields = static_cast<u8>(saved.records[k].fields & ~kHotkey);
    }
    // 欄が無くなった記録は書かない（既定のまま）。残った記録の順は変えない。
    std::size_t kept_count = 0;
    for (std::size_t k = 0; k < saved.count; ++k) {
        if (saved.records[k].fields == 0) continue;
        saved.records[kept_count++] = saved.records[k];
    }
    saved.count = kept_count;
    return true;
}
} // namespace gohan::menu::persist
