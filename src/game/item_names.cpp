#include "game/item_names.hpp"
#include "core/utf.hpp"
#include "game/romfs.hpp"

namespace gohan::game::items {
namespace {
constexpr char normalPath[] = "Script/Str/STR_Item_name.umsbt";
}
void Names::Request() { requested_ = true; Poll(); }
void Names::Poll() {
    if (!requested_) return;
    if (!hiddenStarted_) {
        char path[96];
        if (core::files::TitlePath(path, sizeof(path), "hidden_items.tsv"))
            hiddenStarted_ = hiddenLoad_.Start(path, 1024 * 1024);
    }
    if (!normalStarted_) normalStarted_ = normalLoad_.Start(normalPath, 1024 * 1024, romfs::ReadAll);
    core::files::Buffer buffer;
    if (hiddenLoad_.Take(buffer) && buffer)
        hiddenReady_.Set(hidden_.Parse({reinterpret_cast<const char *>(buffer.data.get()), buffer.size}));
    if (normalLoad_.Take(buffer)) {
        normalData_ = std::move(buffer);
        if (normalData_) normal_.Parse(normalData_.data.get(), normalData_.size);
    }
}
void Names::SetCustom(bool on) { custom_.Set(on); if (on) Request(); }
const HiddenEntry *Names::CustomName(std::uint16_t id) const {
    return custom_.Get() && hiddenReady_.Get() ? hidden_.Find(id & 0x7FFF) : nullptr;
}
bool Names::NameUtf8(std::uint16_t id, char *out, std::size_t capacity) {
    if (!out || !capacity) return false;
    out[0] = 0;
    Request();
    if (const auto *entry = hidden_.Find(id & 0x7FFF)) {
        core::ToUtf8(entry->name16, out, capacity);
        return true;
    }
    const auto text = normal_.Find(id);
    if (text.empty()) return false;
    char16_t clean[256];
    std::size_t used = 0;
    for (std::size_t i = 0; i < text.size() && used + 1 < std::size(clean);) {
        const auto c = text[i++];
        if (c == 0xE) {
            if (text.size() - i < 3) break;
            const auto units = 3 + (text[i + 2] + 1u) / 2;
            if (units > text.size() - i) break;
            i += units;
        } else if (c == 0xF) {
            if (text.size() - i < 2) break;
            i += 2;
        } else clean[used++] = c;
    }
    core::ToUtf8({clean, used}, out, capacity);
    return used != 0;
}
void Names::Shutdown() {
    custom_.Set(false); hiddenReady_.Set(false); requested_ = false;
    // 名前をゲームへ渡した後は、後続の読み取りに備えて停止するまで保持する。
    hidden_.Clear(); normalData_ = {}; normal_ = {};
}
Names &SharedNames() { static Names names; return names; }
} // namespace gohan::game::items
