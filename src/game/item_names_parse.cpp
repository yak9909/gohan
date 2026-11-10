#include "game/item_names.hpp"
#include "core/utf.hpp"
#include "game/romfs.hpp"
#include <cstring>
#include <new>

namespace gohan::game::items {
namespace {
std::uint32_t Word(const std::uint8_t *p) {
    return p[0] | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
unsigned Half(const std::uint8_t *p) { return p[0] | unsigned(p[1]) << 8; }
bool Number(const char *s, unsigned base, unsigned &value) {
    if (base == 16) { if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return false; s += 2; }
    if (!*s) return false;
    value = 0;
    for (; *s; ++s) {
        const unsigned d = *s >= '0' && *s <= '9' ? unsigned(*s - '0') :
            *s >= 'a' && *s <= 'f' ? unsigned(*s - 'a' + 10) :
            *s >= 'A' && *s <= 'F' ? unsigned(*s - 'A' + 10) : 16;
        if (d >= base || value > (0xFFFFu - d) / base) return false;
        value = value * base + d;
    }
    return true;
}
}
void HiddenTable::Clear() { entries_.reset(); text_.reset(); wide_.reset(); count_ = 0; }
bool HiddenTable::Parse(std::string_view input) {
    Clear();
    if (input.empty() || input.size() > 1024 * 1024 || input.find('\0') != input.npos) return false;
    std::size_t lines = 1;
    for (char c : input) lines += c == '\n';
    auto text = std::unique_ptr<char[]>(new (std::nothrow) char[input.size() + 1]);
    auto wide = std::unique_ptr<char16_t[]>(new (std::nothrow) char16_t[input.size() + 1]);
    auto entries = std::unique_ptr<HiddenEntry[]>(new (std::nothrow) HiddenEntry[lines]);
    if (!text || !wide || !entries) return false;
    std::memcpy(text.get(), input.data(), input.size());
    text[input.size()] = 0;
    char *line = text.get(), *endText = line + input.size();
    std::size_t count = 0, used = 0;
    while (line < endText) {
        auto *end = line;
        while (end < endText && *end != '\n') ++end;
        char *next = end == endText ? endText : end + 1;
        *end = 0;
        if (end > line && end[-1] == '\r') end[-1] = 0;
        if (*line && *line != '#' && std::strncmp(line, "id\t", 3)) {
            char *columns[4] = {line};
            unsigned n = 1;
            bool extra = false;
            for (char *p = line; *p; ++p) if (*p == '\t') {
                if (n == 4) { extra = true; break; }
                *p = 0; columns[n++] = p + 1;
            }
            unsigned id, icon;
            if (!extra && n == 4 && Number(columns[0], 16, id) && id <= 0x7FFF && Number(columns[1], 10, icon) && *columns[3]) {
                const auto converted = core::ToUtf16(columns[3], wide.get() + used, input.size() + 1 - used);
                // かなも UTF-8 の正しさを確かめる（保存には元の UTF-8 を使う）。
                const auto kana = core::ToUtf16(columns[2], wide.get() + used + converted.written + 1,
                    input.size() - used - converted.written);
                if (converted.valid && !converted.truncated && kana.valid && !kana.truncated) {
                    entries[count++] = {static_cast<std::uint16_t>(id), static_cast<std::uint16_t>(icon), columns[2], columns[3], wide.get() + used};
                    used += converted.written + 1;
                }
            }
        }
        line = next;
    }
    // 安定な並べ替え。同じ ID の先にあった行を残す。
    for (std::size_t i = 1; i < count; ++i) {
        const auto entry = entries[i];
        auto j = i;
        while (j && entries[j - 1].id > entry.id) { entries[j] = entries[j - 1]; --j; }
        entries[j] = entry;
    }
    std::size_t unique = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (!unique || entries[unique - 1].id != entries[i].id) entries[unique++] = entries[i];
    text_ = std::move(text); wide_ = std::move(wide); entries_ = std::move(entries); count_ = unique;
    return true;
}
const HiddenEntry *HiddenTable::Find(std::uint16_t id) const {
    std::size_t lo = 0, hi = count_;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        if (entries_[mid].id == id) return &entries_[mid];
        if (entries_[mid].id < id) lo = mid + 1; else hi = mid;
    }
    return nullptr;
}
bool MsbtNames::Parse(const std::uint8_t *data, std::size_t size) {
    text_ = nullptr; size_ = count_ = 0;
    if (!data || size < 8) return false;
    const auto offset = Word(data), bytes = Word(data + 4);
    if ((offset & 1) || !romfs::Fits(offset, bytes, size) || bytes < 32) return false;
    data += offset;
    if (std::memcmp(data, "MsgStdBn", 8) || Half(data + 8) != 0xFEFF || data[12] != 1) return false;
    const auto sections = Half(data + 14);
    std::size_t at = 32;
    const std::uint8_t *text = nullptr;
    std::size_t textSize = 0, count = 0;
    for (unsigned i = 0; i < sections; ++i) {
        if (!romfs::Fits(at, 16, bytes)) return false;
        const auto length = Word(data + at + 4);
        if (!romfs::Fits(at + 16, length, bytes)) return false;
        if (!std::memcmp(data + at, "TXT2", 4)) {
            if (text || length < 4) return false;
            text = data + at + 16; textSize = length; count = Word(text);
            if (!count || count > (textSize - 4) / 4 || count > 0x6000) return false;
            for (std::size_t j = 0; j < count; ++j) {
                const auto start = Word(text + 4 + j * 4);
                const auto end = j + 1 < count ? Word(text + 8 + j * 4) : textSize;
                if (start < 4 + count * 4 || (start & 1) || (end & 1) || start >= end || end > textSize) return false;
                bool terminated = false;
                for (auto p = start; p < end; p += 2) if (!Half(text + p)) { terminated = true; break; }
                if (!terminated) return false;
            }
        }
        at = (at + 16 + length + 15) & ~std::size_t(15);
    }
    if (!text) return false;
    text_ = text; size_ = textSize; count_ = count;
    return true;
}
std::u16string_view MsbtNames::Find(std::uint16_t id) const {
    id &= 0x7FFF;
    if (id < 0x2000 || std::size_t(id - 0x2000) >= count_) return {};
    const auto i = id - 0x2000;
    const auto start = Word(text_ + 4 + i * 4);
    const auto end = std::size_t(i + 1) < count_ ? Word(text_ + 8 + i * 4) : size_;
    std::size_t length = 0;
    while (start + length * 2 < end && Half(text_ + start + length * 2)) ++length;
    return {reinterpret_cast<const char16_t *>(text_ + start), length};
}
} // namespace gohan::game::items
