// 自前の Layout ノードと Pane の木、資源の確保と排出を、ゲームの代役の上で通して試験する。
// 実機のゲームの代わりにはならない（番地・GPU・実際の描画は確かめない）。
#include "game/lyt/engine.hpp"
#include "lyt_font.hpp"
#include "lyt_game.hpp"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace gohan::game;
using namespace gohan::game::lyt;
using Fake = fakegame::Game;
using detail::Engine;
using fakegame::TestFont;
namespace limits = gohan::core::limits;

constexpr Rect FullTop{0, 0, 400, 240};
constexpr Rect FullBottom{0, 0, 320, 240};

Command Rectangle(Rect bounds, std::uint32_t color, Rect clip) {
    Command command;
    command.kind = Kind::Rectangle;
    command.bounds = bounds;
    command.clip = clip;
    command.color = color;
    return command;
}

void Add(ScreenDrawing &drawing, const Command &command) {
    drawing.commands[drawing.count++] = command;
    command.kind == Kind::Text ? ++drawing.texts : ++drawing.rectangles;
}

// 文字列（ASCII と「予」「約」）の指示。字数は枠の大きさに合う最小の空き枠を選ぶ。
Command Text(ScreenDrawing &drawing, int x, int y, const char16_t *text, std::uint32_t color, unsigned scale,
             Rect clip, Font font = Font::Main) {
    Command command;
    command.kind = Kind::Text;
    command.font = font;
    command.scale = scale;
    command.color = color;
    command.clip = clip;
    unsigned width = 0;
    for (; text[command.length]; ++command.length) {
        command.text[command.length] = text[command.length];
        const auto c = text[command.length];
        width += font == Font::Numeric ? 5 : c < 0x7F ? 6 : 8;
    }
    command.bounds = {x, y, static_cast<int>(width * scale), static_cast<int>(8 * scale)};
    for (unsigned slot = 0; slot < limits::RendererTexts; ++slot) {
        bool used = false;
        for (std::size_t i = 0; i < drawing.count; ++i)
            used = used || (drawing.commands[i].kind == Kind::Text && drawing.commands[i].slot == slot);
        if (!used && limits::RendererSlotChars[slot] >= command.length) {
            command.slot = slot;
            break;
        }
    }
    return command;
}

template <class T> T *Cast(generated::NwLytPaneLink *link, std::size_t offset) {
    return reinterpret_cast<T *>(reinterpret_cast<char *>(link) - offset);
}

std::vector<generated::NwLytPane *> Children(generated::NwLytPane *root) {
    std::vector<generated::NwLytPane *> panes;
    generated::NwLytPaneLink *sentinel = &root->children;
    generated::NwLytPaneLink *previous = sentinel;
    for (auto *link = root->children.next; link != sentinel; link = link->next) {
        assert(link->prev == previous);
        previous = link;
        panes.push_back(Cast<generated::NwLytPane>(link, offsetof(generated::NwLytPane, sibling_link)));
        assert(panes.size() <= limits::RendererCommands);
    }
    assert(root->children.prev == previous);
    return panes;
}

bool IsPicture(const generated::NwLytPane *pane) {
    return pane->vptr == reinterpret_cast<generated::NwLytPaneVtable *>(generated::vtbl_nw_lyt_Picture_addr);
}
bool IsTextBox(const generated::NwLytPane *pane) {
    return pane->vptr == reinterpret_cast<generated::NwLytPaneVtable *>(generated::vtbl_nw_lyt_TextBox_addr);
}
std::u16string StringOf(const generated::NwLytTextBox *box) {
    return std::u16string(reinterpret_cast<const char16_t *>(box->string), box->string_length);
}

constexpr unsigned Top = 0, Bottom = 1;

void Rejections(Engine &engine, const TestFont &font) {
    auto expect = [&](const char *what) {
        assert(engine.Install(font.resource) == InstallResult::ResourceFailed && what);
        assert(!engine.Installed() && Fake::Balanced());
        assert(Fake::allocations == 0 && Fake::attempts == 0);
        assert(engine.Detach());
        Fake::Reset();
    };
    Fake::haveManager = false;
    expect("管理者が無い");
    Fake::textReady = false;
    expect("既定の文字の処理器が無い");
    Fake::extraBufferBytes = 4;
    expect("ゲームの文字バッファの必要量が想定より大きい");
    Fake::haveAllocator = false;
    expect("アロケータが無い");
    Fake::largestBlock = 1000;
    expect("最大ブロックが小さい");
    Fake::freeBytes = 100000;
    expect("空きが残る量に足りない");
    FontResource broken = font.resource;
    broken.glyphs = detail::MaxGlyphs + 1;
    assert(engine.Install(broken) == InstallResult::ResourceFailed && Fake::Balanced() && Fake::allocations == 0);
    broken = font.resource;
    broken.solidGlyph = static_cast<unsigned>(broken.glyphs);
    assert(engine.Install(broken) == InstallResult::ResourceFailed && Fake::Balanced());
    // 確保の途中の失敗は、確保した物を全部返す。
    Fake::allocateFails = true;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
    Fake::Reset();
    Fake::flushFails = true;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
    Fake::Reset();
    for (int fail : {1, 2}) {
        Fake::failCreate = fail;
        assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
        assert(!engine.Installed() && engine.Present(Drawing{}) == false);
        Fake::Reset();
    }
    assert(!engine.Installed() && engine.Detach());
}

Drawing Sample(std::uint32_t revision) {
    Drawing drawing;
    drawing.revisions[Top] = drawing.revisions[Bottom] = revision;
    auto &top = drawing.screens[Top];
    Add(top, Rectangle({10, 20, 100, 30}, 0xFF112233u, FullTop));
    Add(top, Text(top, 30, 50, u"Hi", 0x80FF0000u, 2, FullTop));
    auto &bottom = drawing.screens[Bottom];
    Add(bottom, Rectangle({0, 0, 160, 240}, 0xCC000000u, FullBottom));
    Add(bottom, Text(bottom, 8, 10, u"2+3", 0xFFFFFFFFu, 1, FullBottom, Font::Numeric));
    return drawing;
}

void TreeAndNodes(Engine &engine, const TestFont &font) {
    assert(engine.Install(font.resource) == InstallResult::Ready && engine.Installed());
    assert(engine.Install(font.resource) == InstallResult::Ready && Fake::allocations == 1);
    assert(Fake::lists.size() == 2 && Fake::bound == 7);
    const auto &top = engine.Nodes().Node(Screen::Top);
    const auto &bottom = engine.Nodes().Node(Screen::Bottom);
    // 仮想表: 記録・再生・投影の設定だけをゲームの関数へ向ける。
    const auto &table = engine.Nodes().Table();
    assert(top.vptr == &table && bottom.vptr == &table);
    assert(table.replay_recorded_list == reinterpret_cast<decltype(table.replay_recorded_list)>(
                                              generated::ssys_ma_lyt_Layout_ReplayRecordedList_addr));
    assert(table.record_pane_tree == reinterpret_cast<decltype(table.record_pane_tree)>(
                                         generated::ssys_ma_lyt_Layout_RecordPaneTree_addr));
    assert(table.setup_projection_matrix == reinterpret_cast<decltype(table.setup_projection_matrix)>(
                                                generated::ssys_ma_lyt_Layout_SetupProjectionMatrix_addr));
    assert(table.calculate && table.build && table.unk_0 && table.unk_4);
    assert(top.priority == 0xFF && top.width == 400.0f && top.height == 240.0f);
    assert(bottom.width == 320.0f && bottom.height == 240.0f);
    assert(top.command_list_id == engine.Resources().List(Screen::Top) && top.command_list_id != 0);
    assert(bottom.command_list_id == engine.Resources().List(Screen::Bottom));
    assert(top.command_list_id != bottom.command_list_id);
    assert(top.command_capacity_bytes == limits::RendererRecordBytes && top.record_dirty == 1);
    assert(top.draw_info.vptr == reinterpret_cast<generated::NwLytDrawInfoVtable *>(generated::vtbl_nw_lyt_DrawInfo_addr));
    assert(top.draw_info.global_alpha == 1.0f && top.draw_info.view_mtx[1][1] == 1.0f);
    assert(top.root && top.root != bottom.root && top.root->children.next == &top.root->children);

    auto drawing = Sample(1);
    assert(engine.Present(drawing));
    // 上: 矩形 1 つと文字 1 つが、描いた順に Pane の木になる。ノードは上下のリストへ登録される。
    auto panes = Children(top.root);
    assert(panes.size() == 2 && IsPicture(panes[0]) && IsTextBox(panes[1]));
    auto *picture = reinterpret_cast<generated::NwLytPicture *>(panes[0]);
    assert(picture->pane.width == 100.0f && picture->pane.height == 30.0f);
    assert(picture->pane.global_mtx[0][3] == 10 + 50 - 200 && picture->pane.global_mtx[1][3] == 120 - (20 + 15));
    assert(picture->pane.global_mtx[2][3] == -1.0f && picture->pane.local_mtx[0][3] == picture->pane.global_mtx[0][3]);
    for (auto color : picture->vertex_colors)
        assert(color == 0xFF332211u); // 0xAARRGGBB → R, G, B, A の順
    assert(picture->pane.flags & 1 && picture->pane.alpha == 0xFF && picture->pane.parent == nullptr);
    assert(picture->uv_calculated == 1 && picture->uv_output_count == 1);
    assert(picture->uv_output[0][0] < picture->uv_output[0][2] && picture->uv_output[0][1] > picture->uv_output[0][3]);
    auto *material = picture->material;
    assert(material->vptr == reinterpret_cast<generated::NwLytMaterialVtable *>(generated::vtbl_nw_lyt_Material_addr));
    assert(material->capacity_bits == 0x15 && material->count_bits == 0x15 && material->colors[0] == 0 &&
           material->colors[3] == 0xFFFFFFFFu && (material->flags & 4));
    assert(material->animation_links.sentinel.next == &material->animation_links.sentinel);
    assert(material->variable_data->texture_address == Fake::Physical(engine.Resources().Atlas()));
    assert(material->variable_data->image_width == 256 && material->variable_data->gpu_format == 9);

    auto *box = reinterpret_cast<generated::NwLytTextBox *>(panes[1]);
    assert(StringOf(box) == u"Hi" && box->string_length == 2);
    assert(box->pane.width == 24.0f && box->pane.height == 16.0f);
    assert(box->pane.global_mtx[0][3] == 30 - 200 && box->pane.global_mtx[1][3] == 120 - (50 + 8));
    assert(box->top_color == 0x800000FFu && box->bottom_color == 0x800000FFu);
    assert(box->font_width == 16.0f && box->font_height == 16.0f && box->dirty == 1);
    assert(box->text_position == 3 && box->tag_processor == nullptr && box->batch->built == 0);
    assert(box->string_buffer_bytes == 2 * (limits::RendererSlotChars[0] + 1));
    assert(box->pane.base_position == 0x13 && picture->pane.base_position == 0x14);
    auto *info = box->font->font_info;
    assert(box->font->vptr == reinterpret_cast<generated::NwFontResFontVtable *>(generated::vtbl_nw_font_ResFont_addr));
    assert(info->encoding == 1 && info->tglp->sheet_data == engine.Resources().Atlas());
    assert(info->cwdh->last_glyph == TestFont::Total - 1 && info->cwdh->widths[1].char_width == 6);
    assert(box->font->glyph_texture_descriptors->font == box->font);
    assert(box->font->glyph_texture_descriptors->physical_address_div8 ==
           Fake::Physical(engine.Resources().Atlas()) >> 3);
    auto *map = reinterpret_cast<generated::NwFontCmapScanPrefix *>(info->cmap);
    assert(map->cmap.method == 2 && map->pair_count == font.resource.mainCount && map->pairs[1].character == 0x21);
    assert(map->cmap.first_character == 0x20 && map->cmap.last_character == 0x7D04);

    // 下: 数字用の書体の文字は、数字用の字形表を使う。
    auto lower = Children(bottom.root);
    assert(lower.size() == 2 && IsPicture(lower[0]) && IsTextBox(lower[1]));
    auto *digits = reinterpret_cast<generated::NwLytTextBox *>(lower[1]);
    assert(StringOf(digits) == u"2+3" && digits->font != box->font);
    auto *numeric = reinterpret_cast<generated::NwFontCmapScanPrefix *>(digits->font->font_info->cmap);
    assert(numeric->cmap.first_character == 0x2B && numeric->cmap.last_character == 0x39);
    assert(digits->font->font_info->cwdh == info->cwdh);

    // 両方のノードがリストに載った。載っている間は何度呼んでも二重登録しない。
    assert(Fake::addCalls == 2 && Fake::manager.top.head == const_cast<generated::SsysMaLayoutNode *>(&top));
    assert(Fake::manager.bottom.tail == &bottom && engine.Nodes().AnyLinked());
    assert(engine.Present(drawing) && Fake::addCalls == 2);
    // 載っている間に確定が替わっても、木は替えない（ゲームが辿っている最中のため）。次のフレームに回す。
    drawing = Sample(2);
    drawing.screens[Top].count = 1;
    assert(engine.Present(drawing) && Children(top.root).size() == 2 && Fake::addCalls == 2);
    // 描画が済んでリストが空になった。同じ確定なら組み直さずに登録だけ戻す。
    Fake::EndPasses();
    assert(!engine.Nodes().AnyLinked());
    const_cast<generated::SsysMaLayoutNode &>(top).record_dirty = 0;
    const auto inits = Fake::batchInits;
    assert(engine.Present(drawing) && Fake::addCalls == 4);
    assert(Children(top.root).size() == 1 && top.record_dirty == 1);
    assert(Fake::batchInits == inits + 1); // 上は矩形だけ。下の文字だけを組み直した
    Fake::EndPasses();
    const_cast<generated::SsysMaLayoutNode &>(top).record_dirty = 0;
    assert(engine.Present(drawing) && top.record_dirty == 0 && Fake::addCalls == 6);
    Fake::EndPasses();
    // 画面が空なら、その画面のノードは登録しない（消える）。
    drawing.revisions[Top] = 3;
    drawing.screens[Top] = ScreenDrawing{};
    assert(engine.Present(drawing) && Fake::addCalls == 7 && Fake::manager.top.head == nullptr);
    assert(Fake::manager.bottom.head == &bottom);
    Fake::EndPasses();
}

void Clipping(Engine &engine) {
    Drawing drawing;
    drawing.revisions[Top] = 10;
    auto &screen = drawing.screens[Top];
    const Rect clip{10, 40, 18, 16};
    Add(screen, Text(screen, 0, 40, u"ABCDE", 0xFFFFFFFFu, 1, clip));       // 横に一部だけ入る → 完全に入る字だけ
    Add(screen, Text(screen, 0, 50, u"ABCDE", 0xFFFFFFFFu, 1, clip));       // 縦に切れる → 描かない
    Add(screen, Text(screen, 100, 40, u"AB", 0xFFFFFFFFu, 1, clip));        // 範囲の外 → 描かない
    Add(screen, Text(screen, 12, 44, u"AB", 0xFFFFFFFFu, 1, clip));         // 全部入る
    Add(screen, Text(screen, 12, 44, u"予約", 0xFFFFFFFFu, 1, {0, 0, 400, 240}));
    assert(engine.Present(drawing));
    const auto &node = engine.Nodes().Node(Screen::Top);
    auto panes = Children(node.root);
    assert(panes.size() == 3);
    auto *first = reinterpret_cast<generated::NwLytTextBox *>(panes[0]);
    assert(StringOf(first) == u"CD" && first->pane.global_mtx[0][3] == 12 - 200);
    auto *second = reinterpret_cast<generated::NwLytTextBox *>(panes[1]);
    assert(StringOf(second) == u"AB" && second->pane.global_mtx[0][3] == 12 - 200);
    auto *kanji = reinterpret_cast<generated::NwLytTextBox *>(panes[2]);
    assert(StringOf(kanji) == u"予約" && kanji->pane.width == 16.0f);
    Fake::EndPasses();
}

void Drain(Engine &engine) {
    Drawing drawing = Sample(20);
    assert(engine.Present(drawing));
    const auto adds = Fake::addCalls;
    // リストに載っている間は排出が終わらない。載っていない状態が続いてから資源を返す。
    assert(!engine.Detach() && engine.Stopping() && engine.Installed());
    assert(engine.Present(drawing) && Fake::addCalls == adds); // 取り外し中は登録しない
    Fake::EndPasses();
    for (unsigned i = 1; i < Engine::DrainFrames; ++i)
        assert(!engine.Detach() && Fake::lists.size() == 2 && Fake::allocations == 1 + Fake::frees);
    // GPU が使っている（リストが束縛中）なら返さない。
    Fake::bound = engine.Resources().List(Screen::Bottom);
    assert(!engine.Detach() && engine.Installed() && Fake::lists.size() == 2);
    Fake::bound = 7;
    assert(engine.Detach() && !engine.Installed() && Fake::Balanced());
    assert(Fake::releases.substr(Fake::releases.size() - 3) == "LLA");
    assert(engine.Detach() && !engine.Present(drawing));
    // 取り外しの途中でリストに載り直したら、待ちをやり直す。
    TestFont font;
    assert(engine.Install(font.resource) == InstallResult::Ready);
    assert(engine.Present(drawing) && !engine.Detach());
    Fake::EndPasses();
    for (unsigned i = 1; i + 1 < Engine::DrainFrames; ++i)
        assert(!engine.Detach());
    const_cast<generated::SsysMaLayoutNode &>(engine.Nodes().Node(Screen::Bottom)).prev =
        const_cast<generated::SsysMaLayoutNode *>(&engine.Nodes().Node(Screen::Top));
    Fake::manager.bottom.head = const_cast<generated::SsysMaLayoutNode *>(&engine.Nodes().Node(Screen::Bottom));
    assert(!engine.Detach() && Fake::lists.size() == 2);
    Fake::EndPasses();
    for (unsigned i = 1; i < Engine::DrainFrames; ++i)
        assert(!engine.Detach());
    assert(engine.Detach() && Fake::Balanced());
}

void Cycles(Engine &engine) {
    TestFont font;
    for (int i = 0; i < 30; ++i) {
        assert(engine.Install(font.resource) == InstallResult::Ready);
        auto drawing = Sample(static_cast<std::uint32_t>(i + 1));
        for (int frame = 0; frame < 5; ++frame) {
            assert(engine.Present(drawing));
            Fake::EndPasses();
        }
        bool done = false;
        for (unsigned frame = 0; frame < 20 && !done; ++frame)
            done = engine.Detach();
        assert(done && Fake::Balanced() && !engine.Installed());
    }
    assert(Fake::allocations == Fake::frees && Fake::Balanced());
}

void Images(Engine &engine) {
    TestFont font;
    std::vector<std::uint8_t> ui(256 * 32 * 4, 0xF1), dash(8 * 8 * 4, 0xAB);
    const TextureResource textures[] = {{ui.data(), ui.size(), 256, 32},
                                        {dash.data(), dash.size(), 8, 8},
                                        {dash.data(), dash.size(), 8, 8}};
    font.resource.textures = textures;
    font.resource.textureCount = 3;
    Fake::Reset();
    // UI を含む総量を見積もってから確保する。残す空きの量は減らさない。
    Fake::freeBytes = limits::RendererHeapReserve + 65536 + 128;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::allocations == 0);
    Fake::Reset();
    constexpr unsigned totalBytes = 65536 + limits::RendererUiTextureBytes;
    Fake::freeBytes = limits::RendererHeapReserve + totalBytes + limits::RendererGameAllocationSlack - 1;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::allocations == 0);
    Fake::Reset();
    Fake::largestBlock = totalBytes + 128 - 1;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::allocations == 0);
    Fake::Reset();
    Fake::freeBytes = limits::RendererHeapReserve + totalBytes + limits::RendererGameAllocationSlack;
    Fake::largestBlock = totalBytes + 128;
    assert(engine.Install(font.resource) == InstallResult::Ready);
    assert(Fake::blocks.at(engine.Resources().Atlas()) == 65536 + limits::RendererUiTextureBytes);
    const auto *data = static_cast<const unsigned char *>(engine.Resources().Atlas());
    assert(std::memcmp(data + TextureOffset(font.resource, 0), ui.data(), ui.size()) == 0);
    assert(std::memcmp(data + TextureOffset(font.resource, 2), dash.data(), dash.size()) == 0);
    Drawing drawing;
    drawing.revisions[0] = 1;
    auto &screen = drawing.screens[0];
    Command image;
    image.kind = Kind::Image;
    image.bounds = {-2, 10, 26, 16};
    image.clip = {0, 11, 10, 6};
    image.source = {7, 3, 13, 8};
    image.scale = 2;
    image.color = 0x8044AAFFu;
    Add(screen, image);
    Command repeat;
    repeat.kind = Kind::Repeat;
    repeat.bounds = {20, 30, 100, 2};
    repeat.clip = FullTop;
    repeat.source = {7, 0, 8, 8};
    repeat.texture = Texture::DashHorizontal;
    repeat.scale = 1;
    repeat.color = 0xFFFFFFFFu;
    Add(screen, repeat);
    assert(engine.Present(drawing));
    auto children = Children(engine.Nodes().Node(Screen::Top).root);
    assert(children.size() == 2);
    auto *picture = reinterpret_cast<generated::NwLytPicture *>(children[0]);
    assert(picture->pane.width == 10 && picture->pane.height == 6);
    assert(picture->vertex_colors[0] == 0x80FFAA44u);
    assert(picture->uv_output[0][0] == 8.0f / 256 && picture->uv_output[0][1] == 1.0f - 3.5f / 32);
    assert(picture->uv_output[0][2] == 13.0f / 256 && picture->uv_output[0][3] == 1.0f - 6.5f / 32);
    auto *map = picture->material->variable_data;
    assert(map->gpu_format == 0 && map->sampler_bits == 0 && map->gpu_sampler_bits == 0);
    assert(map->gpu_dimensions == (256u << 16 | 32u));
    assert(map->texture_address == Fake::Physical(data + 65536));
    auto *pattern = reinterpret_cast<generated::NwLytPicture *>(children[1]);
    assert(pattern->uv_output[0][0] == 7.0f / 8 && pattern->uv_output[0][2] == 107.0f / 8);
    assert(pattern->material->variable_data->gpu_sampler_bits == 0x2200u);
    Fake::EndPasses();
    // 同じ Picture を単色に戻す際、UI の UV と Material が残らない。
    drawing.revisions[0] = 2;
    screen.commands[0] = Rectangle({0, 0, 10, 10}, 0xFFFFFFFFu, FullTop);
    screen.count = 1;
    assert(engine.Present(drawing));
    assert(picture->material->variable_data->gpu_format == 9);
    assert(picture->uv_output[0][0] != 8.0f / 256);
    Fake::EndPasses();
    for (unsigned i = 1; i < Engine::DrainFrames; ++i)
        assert(!engine.Detach());
    assert(engine.Detach() && Fake::Balanced());
}
void OptionalBackground(Engine &engine) {
    TestFont font;
    std::vector<std::uint8_t> ui(256 * 32 * 4, 0xF1), dash(8 * 8 * 4, 0xAB);
    std::vector<std::uint8_t> source(16 * 16 * 4, 0xCD), window(256 * 256 / 2, 0x31);
    constexpr unsigned backgroundBytes = 2 * 16 * 16 * 4 + 256 * 256 / 2;
    static_assert(backgroundBytes == 34816 && backgroundBytes <= 40000);
    const TextureResource textures[] = {
        {ui.data(), ui.size(), 256, 32}, {dash.data(), dash.size(), 8, 8}, {dash.data(), dash.size(), 8, 8},
        {source.data(), source.size(), 16, 16}, {source.data(), source.size(), 16, 16},
        {window.data(), window.size(), 256, 256, TextureFormat::L4, 0xFFF4E6C9u, 0xFFFFFBEFu, 36}};
    font.resource.textures = textures;
    font.resource.textureCount = 6;
    font.resource.requiredTextureCount = 3;
    constexpr unsigned essentialBytes = 65536 + limits::RendererUiTextureBytes;
    // 合計の空き不足・最大の塊の不足・確認/確保/排出失敗を背景だけに起こす。
    for (unsigned mode = 0; mode < 7; ++mode) {
        Fake::Reset();
        if (mode == 0) Fake::freeBytes = 500000;
        if (mode == 1) Fake::largestBlock = essentialBytes + backgroundBytes + 127;
        if (mode == 2) Fake::failSpace = 3;
        if (mode == 3) Fake::failAllocate = 2;
        if (mode == 4) Fake::failFlush = 2;
        if (mode == 5) {
            Fake::freeBytes = limits::RendererHeapReserve + essentialBytes + backgroundBytes + limits::RendererGameAllocationSlack;
            Fake::largestBlock = essentialBytes + backgroundBytes + 128;
        }
        const unsigned beforeFree = Fake::freeBytes - essentialBytes;
        const unsigned beforeLargest = Fake::largestBlock - essentialBytes;
        assert(engine.Install(font.resource) == InstallResult::Ready && engine.Installed());
        Drawing drawing;
        drawing.revisions[0] = 1;
        auto &screen = drawing.screens[0];
        Command image;
        image.kind = Kind::Image;
        image.bounds = {4, 4, 8, 8}; image.clip = FullTop; image.source = {0, 0, 8, 8};
        image.scale = 1; image.color = 0xFFFFFFFFu;
        Add(screen, image); // 自作 UI
        Add(screen, Text(screen, 20, 4, u"ABC", 0xFFFFFFFFu, 1, FullTop));
        Add(screen, Rectangle({10, 20, 186, 13}, 0xFF6DBB4Bu, FullTop)); // 帯の地
        image.texture = Texture::TitleWindow;
        Add(screen, image);
        assert(engine.Resources().Background() == nullptr && Fake::allocateCalls == 1);
        assert(engine.Present(drawing));
        const bool ready = mode >= 5;
        const auto &status = engine.Resources().BackgroundStatus();
        const BackgroundResult results[] = {BackgroundResult::InsufficientSpace, BackgroundResult::BlockTooSmall,
            BackgroundResult::HeapUnavailable, BackgroundResult::AllocateFailed, BackgroundResult::FlushFailed,
            BackgroundResult::Ready, BackgroundResult::Ready};
        assert(status.result == results[mode] && status.measured == (mode != 2));
        if (status.measured) assert(status.freeBytes == beforeFree && status.largestBlock == beforeLargest);
        assert((engine.Resources().Background() != nullptr) == ready);
        assert(Fake::blocks.size() == (ready ? 2 : 1));
        assert(Fake::blocks.at(engine.Resources().Atlas()) == essentialBytes && Fake::lists.size() == 2);
        if (ready) {
            const auto *data = static_cast<const unsigned char *>(engine.Resources().Background());
            assert(Fake::blocks.at(engine.Resources().Background()) == backgroundBytes);
            assert(std::memcmp(data + TextureOffset(font.resource, 5), window.data(), window.size()) == 0);
        } else if (mode < 3) {
            assert(Fake::allocateCalls == 1); // 安全でないゲームの確保関数は呼ばない。
        }
        auto children = Children(engine.Nodes().Node(Screen::Top).root);
        assert(children.size() == (ready ? 4 : 3));
        assert(IsPicture(children[0]) && IsTextBox(children[1]));
        assert(StringOf(reinterpret_cast<generated::NwLytTextBox *>(children[1])) == u"ABC");
        auto *band = reinterpret_cast<generated::NwLytPicture *>(children[2]);
        assert(band->vertex_colors[0] == 0xFF4BBB6Du);
        if (ready) {
            auto *picture = reinterpret_cast<generated::NwLytPicture *>(children[3]);
            const auto *map = picture->material->variable_data;
            assert(map->gpu_format == 10 && map->sampler_bits == (3u << 8 | 5u));
            assert(map->texture_address == Fake::Physical(static_cast<const unsigned char *>(engine.Resources().Background()) + 2048));
            assert(picture->material->colors[0] == 0xFFC9E6F4u && picture->material->colors[1] == 0xFFEFFBFFu);
            assert(picture->pane.alpha == 36 && picture->pane.global_alpha == 36);
            // 同じ板を再利用するとき、窓の不透明度を UI に残さない。
            Fake::EndPasses();
            drawing.revisions[0] = 2;
            screen.commands[3].texture = Texture::Ui;
            assert(engine.Present(drawing));
            assert(picture->pane.alpha == 255 && picture->pane.global_alpha == 255);
        }
        Fake::EndPasses();
        for (unsigned i = 1; i < Engine::DrainFrames; ++i) assert(!engine.Detach());
        assert(engine.Detach() && Fake::Balanced());
    }
    // リスト作成失敗は常駐だけを巻き戻し、背景には触れない。
    Fake::Reset();
    Fake::failCreate = 2;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed);
    assert(engine.Resources().Empty() && Fake::Balanced() && Fake::allocations == 1);
    Fake::Reset();
}
// 実際と同じ画像容量で、閉じた時の取り分・GPU排出・反復・低メモリ時の代替を確認する。
void BackgroundLifetime(Engine &engine) {
    Fake::Reset();
    TestFont font;
    std::vector<std::uint8_t> pixels(131072, 0x46);
    const unsigned sizes[] = {32768, 256, 256, 1024, 1024, 32768, 131072, 32768};
    const unsigned widths[] = {256, 8, 8, 16, 16, 256, 1024, 1024};
    const unsigned heights[] = {32, 8, 8, 16, 16, 256, 256, 64};
    TextureResource textures[8];
    for (unsigned i = 0; i < 8; ++i)
        textures[i] = {pixels.data(), sizes[i], widths[i], heights[i], i >= 5 ? TextureFormat::L4 : TextureFormat::Rgba8};
    textures[3].gpuResident = textures[4].gpuResident = false;
    font.resource.textures = textures; font.resource.textureCount = 8; font.resource.requiredTextureCount = 3;
    assert(engine.Install(font.resource) == InstallResult::Ready);
    assert(Fake::allocateCalls == 1 && !engine.Resources().Background());
    Drawing open, closed;
    open.revisions[0] = 1; closed.revisions[0] = 2;
    // メニュー以外の下画面の描画が続いていても、閉じた後の背景は返す。
    open.revisions[1] = closed.revisions[1] = 1;
    Add(open.screens[1], Rectangle({20, 20, 120, 40}, 0xFFFFFFFFu, {0, 0, 320, 240}));
    closed.screens[1] = open.screens[1];
    Add(open.screens[0], Rectangle({10, 20, 186, 13}, 0xFF6DBB4Bu, FullTop));
    Command band;
    band.kind = Kind::Image; band.texture = Texture::TitleSelection;
    band.bounds = {12, 21, 8, 8}; band.clip = FullTop; band.source = {0, 0, 8, 8};
    band.scale = 1; band.color = 0xFFFFFFFFu;
    Add(open.screens[0], band);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        auto m = engine.Resources().MemoryStatus();
        assert(m.residentBytes == 98816 && m.residentUpperBytes == 99072);
        assert(m.gameUpperBytes <= limits::RendererResidentHeapLimit && m.openBytes == 0);
        // 予備を1バイト割る場合と、ちょうど残せる場合を交互に試す。
        const bool ready = cycle % 2 == 0;
        Fake::freeBytes = 98816 + 196608 + limits::RendererGameAllocationSlack + limits::RendererHeapReserve - (ready ? 0 : 1);
        const int calls = Fake::allocateCalls;
        assert(engine.Present(open));
        m = engine.Resources().MemoryStatus();
        assert(Fake::allocateCalls == calls + (ready ? 1 : 0));
        assert(m.background.result == (ready ? BackgroundResult::Ready : BackgroundResult::InsufficientSpace));
        assert(m.openBytes == (ready ? 196608u : 0u));
        assert(m.gameUpperBytes <= limits::RendererGameHeapLimit && Fake::lists.size() == 2);
        const auto children = Children(engine.Nodes().Node(Screen::Top).root);
        assert(children.size() == (ready ? 2 : 1)); // 未確保の画像を記録しない。単色の地は残す。
        if (ready) {
            // 登録中のノードが残っている間は、空を渡し続けても返却を進めない。
            for (unsigned n = 0; n < 12; ++n) {
                assert(engine.Present(closed));
                assert(engine.Resources().Background());
            }
        }
        Fake::EndPasses();
        for (unsigned n = 1; n < Engine::DrainFrames; ++n) {
            assert(engine.Present(closed)); Fake::EndPasses();
            assert((engine.Resources().Background() != nullptr) == ready);
        }
        if (ready) {
            // 閉じ切っても排出前に開き直せば、同じ画像を使い、二重に借りない。
            const auto *saved = engine.Resources().Background();
            const int before = Fake::allocateCalls;
            assert(engine.Present(open)); Fake::EndPasses();
            assert(engine.Resources().Background() == saved && Fake::allocateCalls == before);
            for (unsigned n = 1; n < Engine::DrainFrames; ++n) {
                assert(engine.Present(closed)); Fake::EndPasses();
                assert(engine.Resources().Background() == saved);
            }
            Fake::bound = engine.Resources().List(Screen::Top);
            assert(engine.Present(closed) && engine.Resources().Background() == saved);
            Fake::EndPasses();
            Fake::bound = 7;
        }
        assert(engine.Present(closed)); Fake::EndPasses();
        m = engine.Resources().MemoryStatus();
        assert(!engine.Resources().Background() && m.openBytes == 0 && m.openUpperBytes == 0);
        assert(m.gameBytes == 98816 && m.gameUpperBytes == 99072 && Fake::blocks.size() == 1);
        assert(Fake::allocations - Fake::frees == 1);
        assert(m.commandBytes == 295944); // 閉じても別ヒープの記録列は維持する。
    }
    for (unsigned n = 1; n < Engine::DrainFrames; ++n) assert(!engine.Detach());
    assert(engine.Detach() && Fake::Balanced());
    Fake::Reset();
}

} // namespace

int main() {
    // 長く使うので一度だけ作る。
    static TestFont font;
    static Engine engine(fakegame::Api());
    Fake::Reset();
    Rejections(engine, font);
    TreeAndNodes(engine, font);
    Clipping(engine);
    Drain(engine);
    Cycles(engine);
    Images(engine);
    OptionalBackground(engine);
    BackgroundLifetime(engine);
    assert(Fake::Balanced());
    std::puts("lyt engine: ok");
}
