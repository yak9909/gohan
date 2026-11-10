#include "game/item_names.hpp"
#include "game/pocket_items.hpp"
#include "game/script_word_res.hpp"
#include "game/romfs.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
using namespace gohan::game;
using namespace gohan::game::items;
using Bytes = std::vector<std::uint8_t>;
void Put(Bytes &data, std::size_t at, std::uint32_t value, unsigned size = 4) {
    for (unsigned i = 0; i < size; ++i) data.at(at + i) = value >> (i * 8);
}
Bytes Msbt() {
    Bytes data(128);
    Put(data, 0, 16); Put(data, 4, 112);
    std::memcpy(data.data() + 16, "MsgStdBn", 8);
    Put(data, 24, 0xFEFF, 2); data[28] = 1; Put(data, 30, 2, 2);
    std::memcpy(data.data() + 48, "ATR1", 4); Put(data, 52, 4);
    std::memcpy(data.data() + 80, "TXT2", 4); Put(data, 84, 28);
    Put(data, 96, 3); Put(data, 100, 16); Put(data, 104, 20); Put(data, 108, 22);
    Put(data, 112, u'木', 2); Put(data, 114, 0, 2); Put(data, 116, 0, 2);
    Put(data, 118, u'花', 2); Put(data, 120, u'束', 2);
    return data;
}
void MsbtTest(const char *real) {
    MsbtNames names;
    auto data = Msbt();
    assert(names.Parse(data.data(), data.size()) && names.Count() == 3);
    assert(names.Find(0x2000) == u"木" && names.Find(0xA000) == u"木");
    assert(names.Find(0x2001).empty() && names.Find(0x2002) == u"花束");
    assert(names.Find(0x1FFF).empty() && names.Find(0x2003).empty());
    Put(data, 104, 17); assert(!names.Parse(data.data(), data.size()));
    data = Msbt(); Put(data, 100, 4); assert(!names.Parse(data.data(), data.size()));
    data = Msbt(); Put(data, 84, 1024); assert(!names.Parse(data.data(), data.size()));
    data = Msbt(); Put(data, 96, 0xFFFFFFFF); assert(!names.Parse(data.data(), data.size()));
    data = Msbt(); Put(data, 4, 1000); assert(!names.Parse(data.data(), data.size()));
    data = Msbt(); Put(data, 122, u'無', 2); assert(!names.Parse(data.data(), data.size()));
    if (real) {
        auto buffer = gohan::core::files::ReadAll(real, 1024 * 1024);
        assert(buffer && names.Parse(buffer.data.get(), buffer.size));
        assert(names.Count() > 5000 && !names.Find(0x2000).empty());
    }
}
void HiddenTest() {
    HiddenTable table;
    assert(table.Parse("# 表\r\n\nid\ticon\tkana\tname\r\n0x0030\t2\tき\t木\r\n0x0005\t0\tはな\t花\n0x0030\t3\tあと\t後\n"
        "broken\n0x8000\t0\tかな\t名\n0x6\tx\tかな\t名\n0x7\t0\tかな\t\n0x8\t0\tかな\t名\textra\n"
        "0x9\t0\tかな\t\xff\n0xFFFFF\t0\tかな\t名\n0x10\t0\tかな\t末"));
    assert(table.Count() == 3 && table.Find(5) && table.Find(0x10));
    assert(table.Find(0x30)->icon == 2 && std::u16string_view(table.Find(0x30)->name16) == u"木");
    assert(!std::strcmp(table.Find(5)->kana, "はな") && !table.Find(6) && !table.Find(0xFFFF));
    const auto *kept = table.Find(0x30)->name16;
    assert(std::u16string_view(kept) == u"木");
    table.Clear(); assert(!table.Find(5) && table.Count() == 0);
    assert(!table.Parse(std::string_view("a\0b", 3)));
    auto file = gohan::core::files::ReadAll("spec/hidden_items.tsv", 1024 * 1024);
    assert(file && table.Parse({reinterpret_cast<char *>(file.data.get()), file.size}) && table.Count() == 332);
}
generated::AcnlPlayerInventoryFinanceF010 player{};
bool saving = false, icons = true;
int conventional = 0;
std::vector<unsigned> loaded;
generated::AcnlPlayerInventoryFinanceF010 *Player() { return saving ? nullptr : &player; }
bool Empty(const generated::ItemID *item) { return item->id == 0x7FFE; }
int Find(void *inventory) { assert(inventory == player.pocket); return conventional; }
void Init(generated::ItemID *item, std::uint16_t id) { *item = {id, 0x1234}; }
void *Grid() { return icons ? &player : nullptr; }
void Load(void *grid, unsigned slot) { assert(grid == &player); loaded.push_back(slot); }
void PocketTest() {
    const PocketAccess access{Player, Empty, Find, Init, Grid, Load};
    for (auto &item : player.pocket) item = {0x2000, 0};
    player.pocket[0].id = 5; player.pocket[3].id = 0x7FFE;
    for (auto mode : {HiddenMode::Hide, HiddenMode::Show, HiddenMode::Custom}) {
        assert(FindPocketSlot(access, player, mode) == (mode == HiddenMode::Hide ? 0 : 3));
    }
    player.pocket_flags[3] = 0xFF;
    assert(AddPocket(access, 5, HiddenMode::Custom) == Result::Ok);
    assert(player.pocket[3].id == 5 && player.pocket[3].flags == 0x1234 && player.pocket_flags[3] == 0);
    assert((loaded == std::vector<unsigned>{3}));
    assert(AddPocket(access, 6, HiddenMode::Show) == Result::Failed);
    saving = true; assert(AddPocket(access, 6, HiddenMode::Hide) == Result::NotReady); saving = false;
    for (int invalid : {-1, 16, 100}) { conventional = invalid; assert(AddPocket(access, 6, HiddenMode::Hide) == Result::Failed); }
    conventional = 0; icons = false;
    assert(AddPocket(access, 0x7FFF, HiddenMode::Hide) == Result::Ok && player.pocket[0].id == 0x7FFF && loaded.size() == 1);
    assert(AddPocket(access, 0x8000, HiddenMode::Hide) == Result::Failed);
    assert(AddPocket(access, 0x7FFE, HiddenMode::Hide) == Result::Ok); // 指定の範囲内をそのまま組む。
    icons = true; loaded.clear(); RefreshPocket(access);
    assert(loaded.size() == 16 && loaded.front() == 0 && loaded.back() == 15);
    const char16_t text[] = u"名前";
    const script::WordRes word(text);
    assert(word.vtable == generated::script_WordRes_vtable && word.text == text && word.length == 0x3FFFFFFF);
}
}
int main(int argc, char **argv) { HiddenTest(); MsbtTest(argc > 1 ? argv[1] : nullptr); PocketTest(); }
