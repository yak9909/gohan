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

// 既定の1枚テクスチャ合成を、GPU が読む RGBA と材質の実値で確かめる。
std::uint32_t PictureColor(const generated::NwLytMaterial &material, std::uint32_t texel,
                           std::uint32_t vertex) {
    std::uint32_t result = 0;
    for (unsigned shift : {0u, 8u, 16u, 24u}) {
        const unsigned level = (texel >> shift) & 255;
        const unsigned black = (material.colors[0] >> shift) & 255;
        const unsigned white = (material.colors[1] >> shift) & 255;
        const unsigned tint = (vertex >> shift) & 255;
        const unsigned channel = (black * (255 - level) + white * level) * tint / (255 * 255);
        result |= channel << shift;
    }
    return result;
}

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
    Fake::freeBytes = font.resource.atlasBytes + limits::RendererGameAllocationSlack - 1;
    expect("全画像と管理分を確保できない");
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
    assert(material->capacity_bits == 0x15 && material->count_bits == 0x15 && material->colors[0] == 0x00FFFFFFu &&
           material->colors[3] == 0xFFFFFFFFu && (material->flags & 4));
    assert(material->animation_links.sentinel.next == &material->animation_links.sentinel);
    assert(material->variable_data->texture_address == Fake::Physical(engine.Resources().Atlas()));
    assert(material->variable_data->image_width == 256 && material->variable_data->gpu_format == 11);
    assert(material->variable_data->sampler_bits == (4u << 8));
    // A4 の RGB=0 でも、矩形の指定色・半透明度を保つ。下地色を黒に戻すと症状を再現する。
    for (auto color : {0xFF332211u, 0x8055AA33u, 0xFF000000u, 0xFFFFFFFFu}) {
        assert(PictureColor(*material, 0xFF000000u, color) == color);
        assert(PictureColor(*material, 0, color) == (color & 0x00FFFFFFu));
    }
    auto oldMaterial = *material;
    oldMaterial.colors[0] = 0;
    assert(PictureColor(oldMaterial, 0xFF000000u, picture->vertex_colors[0]) == 0xFF000000u);

    auto *box = reinterpret_cast<generated::NwLytTextBox *>(panes[1]);
    assert(StringOf(box) == u"Hi" && box->string_length == 2);
    assert(box->pane.width == 24.0f && box->pane.height == 16.0f);
    assert(box->pane.global_mtx[0][3] == 30 - 200 && box->pane.global_mtx[1][3] == 120 - (50 + 8));
    assert(box->top_color == 0x800000FFu && box->bottom_color == 0x800000FFu);
    assert(box->font_width == 16.0f && box->font_height == 16.0f && box->dirty == 1);
    assert(box->text_position == 3 && box->tag_processor == nullptr && box->batch->built == 0);
    assert(box->material->colors[0] == 0 && box->material->colors[1] == 0xFFFFFFFFu);
    assert(box->string_buffer_bytes == 2 * (limits::RendererSlotChars[0] + 1));
    assert(box->pane.base_position == 0x13 && picture->pane.base_position == 0x14);
    auto *info = box->font->font_info;
    assert(box->font->vptr == reinterpret_cast<generated::NwFontResFontVtable *>(generated::vtbl_nw_font_ResFont_addr));
    assert(info->encoding == 1 && info->tglp->sheet_data == engine.Resources().Atlas());
    assert(info->tglp->sheet_format == 11 && info->tglp->sheet_bytes == 32768);
    assert(info->cwdh->last_glyph == TestFont::Total - 1 && info->cwdh->widths[1].char_width == 6);
    assert(box->font->glyph_texture_descriptors->font == box->font);
    assert(box->font->glyph_texture_descriptors->gpu_format == 11);
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
    Fake::freeBytes = 32768 + 128;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::allocations == 0);
    Fake::Reset();
    constexpr unsigned totalBytes = 32768 + limits::RendererUiTextureBytes;
    Fake::freeBytes = totalBytes + limits::RendererGameAllocationSlack - 1;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::allocations == 0);
    Fake::Reset();
    Fake::largestBlock = totalBytes + 128 - 1;
    assert(engine.Install(font.resource) == InstallResult::ResourceFailed && Fake::allocations == 0);
    Fake::Reset();
    Fake::freeBytes = totalBytes + limits::RendererGameAllocationSlack;
    Fake::largestBlock = totalBytes + 128;
    assert(engine.Install(font.resource) == InstallResult::Ready);
    assert(Fake::blocks.at(engine.Resources().Atlas()) == 32768 + limits::RendererUiTextureBytes);
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
    assert(picture->material->colors[0] == 0 && picture->material->colors[1] == 0xFFFFFFFFu);
    assert(map->gpu_format == 0 && map->sampler_bits == 0 && map->gpu_sampler_bits == 0);
    assert(map->gpu_dimensions == (256u << 16 | 32u));
    assert(map->texture_address == Fake::Physical(data + 32768));
    auto *pattern = reinterpret_cast<generated::NwLytPicture *>(children[1]);
    assert(pattern->uv_output[0][0] == 7.0f / 8 && pattern->uv_output[0][2] == 107.0f / 8);
    assert(pattern->material->variable_data->gpu_sampler_bits == 0x2200u);
    Fake::EndPasses();
    // 同じ Picture を単色に戻す際、UI の UV と Material が残らない。
    drawing.revisions[0] = 2;
    screen.commands[0] = Rectangle({0, 0, 10, 10}, 0xFFFFFFFFu, FullTop);
    screen.count = 1;
    assert(engine.Present(drawing));
    assert(picture->material->variable_data->gpu_format == 11);
    assert(PictureColor(*picture->material, 0xFF000000u, picture->vertex_colors[0]) == 0xFFFFFFFFu);
    assert(picture->uv_output[0][0] != 8.0f / 256);
    Fake::EndPasses();
    for (unsigned i = 1; i < Engine::DrainFrames; ++i)
        assert(!engine.Detach());
    assert(engine.Detach() && Fake::Balanced());
}
// 背景を含めた一括常駐。低空きでも開閉時には追加の確保も省略もない。
void BackgroundLifetime(Engine &engine) {
    Fake::Reset();
    fakegame::UiFont font;
    Fake::freeBytes = 228580; Fake::largestBlock = 200000;
    assert(engine.Install(font.resource) == InstallResult::Ready);
    const auto *allocation = engine.Resources().Atlas();
    Drawing open, closed;
    open.revisions[0] = 1;
    auto &image = open.screens[0].commands[0];
    image.kind = Kind::Image; image.texture = Texture::TitleWindow;
    image.bounds = image.clip = image.source = {0, 0, 100, 100};
    image.scale = 1; image.color = 0xFFFFFFFFu;
    open.screens[0].count = open.screens[0].rectangles = 1;
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        Fake::freeBytes = 119296 + (cycle % 2 ? 1 : 50000);
        open.revisions[0] = cycle * 2 + 1;
        assert(engine.Present(open));
        const auto panes = Children(engine.Nodes().Node(Screen::Top).root);
        assert(panes.size() == 1);
        const auto *picture = reinterpret_cast<const generated::NwLytPicture *>(panes[0]);
        const auto *map = picture->material->variable_data;
        assert(map->gpu_format == 10 && map->sampler_bits == (3u << 8 | 5u));
        assert(map->texture_address == Fake::Physical(static_cast<const unsigned char *>(allocation) + TextureOffset(font.resource, 5)));
        assert(picture->pane.alpha == 36 && picture->material->colors[0] == 0xFFC9E6F4u);
        Fake::EndPasses();
        closed.revisions[0] = open.revisions[0] + 1;
        for (unsigned n = 0; n < Engine::DrainFrames + 4; ++n) {
            assert(engine.Present(closed)); Fake::EndPasses();
        }
        assert(engine.Resources().Atlas() == allocation && Fake::allocateCalls == 1 && Fake::frees == 0);
        const auto m = engine.Resources().MemoryStatus();
        assert(m.gameBytes == 119296 && m.gameUpperBytes == 120320 && m.commandBytes == 2 * (68 + limits::RendererRecordBytes + 28 * limits::RendererQueueEntries));
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
    BackgroundLifetime(engine);
    assert(Fake::Balanced());
    std::puts("lyt engine: ok");
}
