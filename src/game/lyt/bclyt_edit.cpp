#include "game/lyt/bclyt_edit.hpp"
#include <cstring>

namespace gohan::game::lyt {
namespace {
std::uint32_t Rd(const std::uint8_t *p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
void Wr(std::uint8_t *p, std::uint32_t v) { std::memcpy(p, &v, 4); }
std::uint16_t Rd16(const std::uint8_t *p) { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
void Wr16(std::uint8_t *p, std::uint16_t v) { std::memcpy(p, &v, 2); }
} // namespace

bool BclytEdit::Load(const std::uint8_t *data, std::size_t size) {
    sections_.clear(); head_.clear();
    if (!data || size < Header() || std::memcmp(data, "FLYT", 4) != 0) return false;
    const std::size_t total = Rd(data + 0x0C), count = Rd16(data + 0x10);
    if (total != size || Rd16(data + 0x06) != Header()) return false;
    std::size_t pos = Header();
    for (std::size_t i = 0; i < count; ++i) {
        if (size - pos < 8) { sections_.clear(); return false; }
        const std::size_t len = Rd(data + pos + 4);
        if (len < 8 || len > size - pos || (len & 3)) { sections_.clear(); return false; }
        Section s;
        std::memcpy(s.tag, data + pos, 4);
        s.body.assign(data + pos + 8, data + pos + len);
        sections_.push_back(std::move(s));
        pos += len;
    }
    if (pos != size) { sections_.clear(); return false; }
    head_.assign(data, data + Header());
    return true;
}

std::size_t BclytEdit::Body() const {
    std::size_t n = 0;
    for (const auto &s : sections_) n += 8 + s.body.size();
    return n;
}

int BclytEdit::Find(const char tag[4], int nth) const {
    for (std::size_t i = 0; i < sections_.size(); ++i)
        if (std::memcmp(sections_[i].tag, tag, 4) == 0 && nth-- == 0) return static_cast<int>(i);
    return -1;
}

int BclytEdit::FindByName(const char *name) const {
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        const auto &b = sections_[i].body;
        if (b.size() < NameOffset + NameBytes) continue;
        if (std::strncmp(reinterpret_cast<const char *>(&b[NameOffset]), name, NameBytes) == 0) return static_cast<int>(i);
    }
    return -1;
}

int BclytEdit::Duplicate(int index, const char *new_name) {
    if (index < 0 || static_cast<std::size_t>(index) >= sections_.size()) return -1;
    Section copy = sections_[index];
    sections_.insert(sections_.begin() + index + 1, std::move(copy));
    if (!Rename(index + 1, new_name)) { sections_.erase(sections_.begin() + index + 1); return -1; }
    return index + 1;
}

bool BclytEdit::Rename(int index, const char *name) {
    if (index < 0 || static_cast<std::size_t>(index) >= sections_.size() || !name) return false;
    auto &b = sections_[index].body;
    if (b.size() < NameOffset + NameBytes || std::strlen(name) >= NameBytes) return false;
    std::memset(&b[NameOffset], 0, NameBytes);
    std::memcpy(&b[NameOffset], name, std::strlen(name));
    return true;
}

bool BclytEdit::SetU32(int index, std::size_t offset, std::uint32_t v) {
    if (index < 0 || static_cast<std::size_t>(index) >= sections_.size()) return false;
    auto &b = sections_[index].body;
    if (offset > b.size() || b.size() - offset < 4) return false;
    Wr(&b[offset], v);
    return true;
}

bool BclytEdit::SetFloat(int index, std::size_t offset, float v) {
    std::uint32_t bits; std::memcpy(&bits, &v, 4);
    return SetU32(index, offset, bits);
}

bool BclytEdit::SetRect(int index, float x, float y, float w, float h) {
    const float v[4] = {x, y, w, h};
    for (int i = 0; i < 4; ++i)
        if (!SetFloat(index, RectOffset + 4 * i, v[i])) return false;
    return true;
}

bool BclytEdit::GetRect(int index, float out[4]) const {
    if (index < 0 || static_cast<std::size_t>(index) >= sections_.size()) return false;
    const auto &b = sections_[index].body;
    if (b.size() < RectOffset + 16) return false;
    std::memcpy(out, &b[RectOffset], 16);
    return true;
}

std::vector<std::uint8_t> BclytEdit::Build() const {
    std::vector<std::uint8_t> out(head_);
    for (const auto &s : sections_) {
        std::uint8_t h[8];
        std::memcpy(h, s.tag, 4);
        Wr(h + 4, static_cast<std::uint32_t>(8 + s.body.size()));
        out.insert(out.end(), h, h + 8);
        out.insert(out.end(), s.body.begin(), s.body.end());
    }
    Wr(out.data() + 0x0C, static_cast<std::uint32_t>(out.size()));
    Wr16(out.data() + 0x10, static_cast<std::uint16_t>(sections_.size()));
    return out;
}
} // namespace gohan::game::lyt
