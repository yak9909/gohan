#include "game/lyt/scene.hpp"
#include <bit>
#include <algorithm>
#include <cstring>

namespace gohan::game::lyt::detail {
namespace {
using namespace generated;

// 32 ビットのゲームでは、型の大きさがゲームの確保の大きさと一致していること。
static_assert(sizeof(void *) != 4 || (sizeof(NwLytPane) == 0xD4 && sizeof(NwLytPicture) == 0x158 &&
                                     sizeof(NwLytTextBox) == 0x108 && sizeof(NwLytMaterial) == 0x50 &&
                                     sizeof(NwFontResFont) == 0x18 && sizeof(NwFontGlyphTextureDescriptor) == 0x14 &&
                                     sizeof(NwFontTextBatchPrefix) == 0x28 && sizeof(NwFontTextBatchGlyph) == 0x2C &&
                                     sizeof(NwLytTexMap) == 0x20 && sizeof(NwLytTexSrt) == 0x14 &&
                                     sizeof(NwLytTexCoordGen) == 4 && sizeof(NwFontCmapScanPair) == 4));

constexpr std::uint8_t PaneVisible = 1;
// Pane の基準位置の値。0x10 は透明な Pane の描画を省く印。下位は文字の位置の取り方。
constexpr std::uint8_t BasePositionCenter = 0x14;
constexpr std::uint8_t BasePositionTextLeft = 0x13;
// 文字列の位置は「横 0（左）＋縦 1（中央）× 3」。
constexpr std::uint8_t TextPositionLeftMiddle = 3;
// Material の欄の「UV を計算済み」の印。
constexpr std::uint8_t MaterialUvDone = 0x04;
// 画像の書式。ゲームの Layout の番号と GPU の番号は別。
constexpr unsigned SamplerFormatLa4 = 2;
constexpr unsigned GpuFormatLa4 = 9;
constexpr unsigned SamplerFormatRgba8 = 0;
constexpr unsigned GpuFormatRgba8 = 0;
// 文字ストリームの読み出し方。この値以外ではゲームが止まる。
constexpr std::uint8_t FontStreamUtf16 = 1;
constexpr std::uint16_t NoCachedCharacter = 0xFFFF;
constexpr unsigned CmapScan = 2;
constexpr unsigned FontLineFeed = 8;
constexpr unsigned FontBaseline = 8;
constexpr unsigned FontMetric = 8;
constexpr int ScreenHeight = 240;
constexpr float TextDepth = -1.0f;

constexpr unsigned Pack(unsigned mask, unsigned count) { return count << std::countr_zero(mask); }
constexpr unsigned SharedCapacity = Pack(NwLytMaterialCountMask_TexMaps, 1) |
                                    Pack(NwLytMaterialCountMask_TexSrts, 1) |
                                    Pack(NwLytMaterialCountMask_TexCoordGens, 1);

unsigned ScreenWidth(Screen screen) { return screen == Screen::Bottom ? 320 : 400; }

// 画面の色（0xAARRGGBB）→ ゲームの頂点色（メモリ上は R, G, B, A の順）。
std::uint32_t GameColor(std::uint32_t argb) {
    return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
}

// 親のいない Pane の位置は、ローカルと大域の行列の両方に同じ値を入れる（描画は大域の行列を読む）。
void Place(NwLytPane &pane, float width, float height, float x, float y, float z) {
    pane.width = width;
    pane.height = height;
    pane.translate[0] = x;
    pane.translate[1] = y;
    pane.translate[2] = z;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            const float value = row == column ? 1.0f : 0.0f;
            pane.local_mtx[row][column] = value;
            pane.global_mtx[row][column] = value;
        }
    }
    pane.local_mtx[0][3] = pane.global_mtx[0][3] = x;
    pane.local_mtx[1][3] = pane.global_mtx[1][3] = y;
    pane.local_mtx[2][3] = pane.global_mtx[2][3] = z;
}

void InitPane(NwLytPane &pane, std::uintptr_t table, float width, float height) {
    std::memset(&pane, 0, sizeof(pane));
    pane.vptr = reinterpret_cast<NwLytPaneVtable *>(table);
    pane.children.next = pane.children.prev = &pane.children;
    pane.animation_links.sentinel.next = pane.animation_links.sentinel.prev =
        &pane.animation_links.sentinel;
    pane.scale[0] = pane.scale[1] = 1.0f;
    Place(pane, width, height, 0.0f, 0.0f, 0.0f);
    pane.alpha = pane.global_alpha = 0xFF;
    pane.base_position = BasePositionCenter;
    pane.flags = PaneVisible;
}

void InitMaterial(NwLytMaterial &material) {
    std::memset(&material, 0, sizeof(material));
    material.vptr = reinterpret_cast<NwLytMaterialVtable *>(vtbl_nw_lyt_Material_addr);
    material.animation_links.sentinel.next = material.animation_links.sentinel.prev =
        &material.animation_links.sentinel;
}

// 字形のコードから字形番号を引く（コードの昇順の二分探索）。
int FindGlyph(const FontResource &font, Font which, char16_t code) {
    const auto *map = which == Font::Numeric ? font.numericMap : font.mainMap;
    std::size_t low = 0, high = which == Font::Numeric ? font.numericCount : font.mainCount;
    while (low < high) {
        const auto mid = low + (high - low) / 2;
        if (map[mid * 2] == code)
            return map[mid * 2 + 1];
        if (map[mid * 2] < code)
            low = mid + 1;
        else
            high = mid;
    }
    return -1;
}

// 切り抜き範囲に完全に入る字だけを残す（横は連続した範囲、縦は文字列全体）。
// 戻り値は残す字数。origin は残した先頭の字の左端。
unsigned ClipText(const FontResource &font, const Command &command, unsigned *first, int *origin) {
    const auto &bounds = command.bounds;
    const auto &clip = command.clip;
    *first = 0;
    *origin = bounds.x;
    if (bounds.y < clip.y || bounds.y + bounds.height > clip.y + clip.height)
        return 0;
    const int scale = static_cast<int>(command.scale);
    int x = bounds.x;
    unsigned kept = 0;
    bool started = false;
    for (unsigned i = 0; i < command.length; ++i) {
        const int glyph = FindGlyph(font, command.font, command.text[i]);
        const int advance = glyph < 0 ? 0 : font.widths[glyph * 3 + 2] * scale;
        const bool inside = x >= clip.x && x + advance <= clip.x + clip.width;
        if (inside && !started) {
            started = true;
            *first = i;
            *origin = x;
        }
        if (started) {
            if (!inside)
                break;
            ++kept;
        }
        x += advance;
    }
    return kept;
}

int TextWidth(const FontResource &font, const Command &command, unsigned first, unsigned count) {
    int width = 0;
    for (unsigned i = first; i < first + count; ++i) {
        const int glyph = FindGlyph(font, command.font, command.text[i]);
        if (glyph >= 0)
            width += font.widths[glyph * 3 + 2];
    }
    return width * static_cast<int>(command.scale);
}
} // namespace

bool Scene::Prepare(const ResourceApi &api, const FontResource &font) const {
    if (!font.atlas || !font.widths || !font.mainMap || !font.numericMap || !font.mainCount ||
        !font.numericCount || font.glyphs == 0 || font.glyphs > MaxGlyphs ||
        font.mainCount > MaxGlyphs || font.numericCount > MaxGlyphs || font.solidGlyph >= font.glyphs ||
        !font.sheetWidth || !font.sheetHeight || !font.cellWidth || !font.cellHeight || !font.columns ||
        font.sheetWidth > 0xFFFF || font.sheetHeight > 0xFFFF)
        return false;
    if (font.textureCount > static_cast<unsigned>(Texture::Count) || (font.textureCount && !font.textures))
        return false;
    for (std::size_t i = 0; i < font.textureCount; ++i) {
        const auto &t = font.textures[i];
        if (!t.data || t.width < 8 || t.height < 8 || t.width > 1024 || t.height > 1024 ||
            !std::has_single_bit(t.width) || !std::has_single_bit(t.height) ||
            t.bytes != std::size_t(t.width) * t.height * 4)
            return false;
    }
    // ゲームが求める文字バッファが、ここで用意する大きさに収まること。
    for (unsigned i = 0; i < core::limits::RendererTexts; ++i) {
        const unsigned chars = core::limits::RendererSlotChars[i];
        if (api.bufferBytes(chars) > BatchBytes(chars))
            return false;
    }
    return true;
}

void Scene::Build(const ResourceApi &api, const FontResource &font, const void *atlas) {
    api_ = &api;
    font_ = &font;
    const std::uint32_t physical = api.physical(atlas);

    // 共有の Material。可変領域は TexMap・TexSrt・TexCoordGen を 1 つずつ。
    auto &material = material_.value;
    InitMaterial(material);
    for (auto &color : material.colors)
        color = 0xFFFFFFFFu;
    material.colors[0] = 0;
    material.capacity_bits = SharedCapacity;
    material.count_bits = SharedCapacity;
    material.variable_data = &shared_.value.map;
    material.flags = MaterialUvDone;
    std::memset(&shared_.value, 0, sizeof(shared_.value));
    auto &map = shared_.value.map;
    map.texture_address = physical;
    map.image_width = map.texture_width = static_cast<std::uint16_t>(font.sheetWidth);
    map.image_height = map.texture_height = static_cast<std::uint16_t>(font.sheetHeight);
    map.sampler_bits = SamplerFormatLa4 << 8;
    map.gpu_dimensions = (font.sheetWidth << 16) | font.sheetHeight;
    map.gpu_format = GpuFormatLa4;
    shared_.value.srt.scale[0] = shared_.value.srt.scale[1] = 1.0f;

    for (std::size_t i = 0; i < font.textureCount; ++i) {
        auto &own = imageMaterials_[i].value;
        own = material;
        own.animation_links.sentinel.next = own.animation_links.sentinel.prev = &own.animation_links.sentinel;
        auto &variable = imageShared_[i].value;
        variable = shared_.value;
        own.variable_data = &variable.map;
        const auto &t = font.textures[i];
        auto &texture = variable.map;
        texture.texture_address = api.physical(static_cast<const unsigned char *>(atlas) + TextureOffset(font, i));
        texture.image_width = texture.texture_width = static_cast<std::uint16_t>(t.width);
        texture.image_height = texture.texture_height = static_cast<std::uint16_t>(t.height);
        // RGBA8。UI はクランプ、独立した模様だけ繰り返す。フィルタは両方ニアレスト。
        texture.sampler_bits = (SamplerFormatRgba8 << 8) | (i ? 5u : 0u);
        texture.gpu_sampler_bits = i ? 0x2200u : 0u;
        texture.gpu_dimensions = (t.width << 16) | t.height;
        texture.gpu_format = GpuFormatRgba8;
    }

    // 字形の幅と、文字コードから字形を引く表（方式 2）。主と数字の 2 組が 1 枚のアトラスを共有する。
    std::memset(&fonts_, 0, sizeof(fonts_));
    auto *widths = reinterpret_cast<NwFontCwdh *>(fonts_.widths);
    widths->first_glyph = 0;
    widths->last_glyph = static_cast<std::uint16_t>(font.glyphs - 1);
    widths->next = nullptr;
    std::memcpy(widths->widths, font.widths, font.glyphs * sizeof(NwFontCharWidths));
    const std::uint16_t *maps[2] = {font.mainMap, font.numericMap};
    const std::size_t counts[2] = {font.mainCount, font.numericCount};
    for (unsigned f = 0; f < 2; ++f) {
        auto *cmap = reinterpret_cast<NwFontCmapScanPrefix *>(fonts_.characterMap[f]);
        cmap->cmap.first_character = maps[f][0];
        cmap->cmap.last_character = maps[f][(counts[f] - 1) * 2];
        cmap->cmap.method = CmapScan;
        cmap->cmap.next = nullptr;
        cmap->pair_count = static_cast<std::uint16_t>(counts[f]);
        for (std::size_t i = 0; i < counts[f]; ++i) {
            cmap->pairs[i].character = maps[f][i * 2];
            cmap->pairs[i].glyph = maps[f][i * 2 + 1];
        }
        auto &resFont = fonts_.resFont[f].value;
        auto &info = fonts_.info[f].value;
        auto &glyphs = fonts_.glyphs[f].value;
        auto &sheet = fonts_.sheet[f].value;
        resFont.vptr = reinterpret_cast<NwFontResFontVtable *>(vtbl_nw_font_ResFont_addr);
        resFont.font_info = &info;
        resFont.glyph_texture_descriptors = &sheet;
        resFont.cached_character = resFont.cached_glyph = NoCachedCharacter;
        info.unk_0[0] = 1;
        info.line_feed = FontLineFeed;
        info.default_widths.glyph_width = static_cast<std::uint8_t>(font.cellWidth);
        info.default_widths.char_width = static_cast<std::uint8_t>(font.cellWidth + 1);
        info.encoding = FontStreamUtf16;
        info.tglp = &glyphs;
        info.cwdh = widths;
        info.cmap = &cmap->cmap;
        info.height = info.width = FontMetric;
        info.ascent = FontBaseline;
        glyphs.cell_width = static_cast<std::uint8_t>(font.cellWidth);
        glyphs.cell_height = static_cast<std::uint8_t>(font.cellHeight);
        glyphs.baseline = FontBaseline;
        glyphs.max_char_width = static_cast<std::uint8_t>(font.cellWidth + 1);
        glyphs.sheet_bytes = static_cast<unsigned>(font.atlasBytes);
        glyphs.sheet_count = 1;
        glyphs.sheet_format = GpuFormatLa4;
        glyphs.sheet_columns = static_cast<std::uint16_t>(font.columns);
        glyphs.sheet_rows = static_cast<std::uint16_t>(font.sheetHeight / (font.cellHeight + 1));
        glyphs.sheet_width = static_cast<std::uint16_t>(font.sheetWidth);
        glyphs.sheet_height = static_cast<std::uint16_t>(font.sheetHeight);
        glyphs.sheet_data = const_cast<void *>(atlas);
        sheet.font = &resFont;
        sheet.physical_address_div8 = physical >> 3;
        sheet.height = static_cast<std::uint16_t>(font.sheetHeight);
        sheet.width = static_cast<std::uint16_t>(font.sheetWidth);
        sheet.gpu_format = GpuFormatLa4;
    }

    // 塗りつぶし用のセルの内側（縁の 1 画素を除く）の UV。v は下から数える。
    const unsigned column = font.solidGlyph % font.columns, row = font.solidGlyph / font.columns;
    const float u0 = static_cast<float>((font.cellWidth + 1) * column + 2) / font.sheetWidth;
    const float u1 = static_cast<float>((font.cellWidth + 1) * column + font.cellWidth) / font.sheetWidth;
    const float v0 = static_cast<float>(font.sheetHeight - ((font.cellHeight + 1) * row + 2)) / font.sheetHeight;
    const float v1 = static_cast<float>(font.sheetHeight - ((font.cellHeight + 1) * row + font.cellHeight)) / font.sheetHeight;
    solidUv_[0] = u0;
    solidUv_[1] = v0;
    solidUv_[2] = u1;
    solidUv_[3] = v1;

    for (unsigned s = 0; s < 2; ++s) {
        const Screen which = static_cast<Screen>(s);
        auto &screen = screens_[s];
        std::memset(&screen, 0, sizeof(screen));
        screen.pictures = s ? bottomPictures_ : topPictures_;
        InitPane(screen.root.value, vtbl_nw_lyt_Pane_addr, static_cast<float>(ScreenWidth(which)),
                 static_cast<float>(ScreenHeight));
        const auto pictureLimit = s ? core::limits::RendererBottomRects : core::limits::RendererRects;
        for (unsigned i = 0; i < pictureLimit; ++i) {
            auto &picture = screen.pictures[i].value;
            InitPane(picture.pane, vtbl_nw_lyt_Picture_addr, 1.0f, 1.0f);
            picture.uv_calculated = 1;
            picture.uv_output_count = 1;
            picture.uv_output[0][0] = u0;
            picture.uv_output[0][1] = v0;
            picture.uv_output[0][2] = u1;
            picture.uv_output[0][3] = v1;
            picture.material = &material;
        }
        for (unsigned i = 0; i < core::limits::RendererTexts; ++i) {
            const unsigned chars = core::limits::RendererSlotChars[i];
            const SlotLayout layout = MakeSlot(chars);
            unsigned char *base = screen.texts + Plan.offset[i];
            auto *box = reinterpret_cast<NwLytTextBox *>(base + layout.textBox);
            auto *own = reinterpret_cast<NwLytMaterial *>(base + layout.material);
            auto *string = reinterpret_cast<std::uint16_t *>(base + layout.string);
            auto *batch = reinterpret_cast<NwFontTextBatchPrefix *>(base + layout.batch);
            InitPane(box->pane, vtbl_nw_lyt_TextBox_addr, 0.0f, 0.0f);
            box->pane.base_position = BasePositionTextLeft;
            InitMaterial(*own);
            own->colors[1] = 0xFFFFFFFFu;
            std::memset(string, 0, 2 * (chars + 1));
            api.batchInit(batch, chars);
            batch->built = 0;
            batch->flags = 0;
            box->string = string;
            box->top_color = box->bottom_color = 0xFFFFFFFFu;
            box->font = &fonts_.resFont[0].value;
            box->font_width = box->font_height = static_cast<float>(FontMetric);
            box->string_buffer_bytes = static_cast<std::uint16_t>(2 * (chars + 1));
            box->text_position = TextPositionLeftMiddle;
            box->dirty = 1;
            box->material = own;
            box->batch = batch;
        }
    }
}

generated::NwLytPicture *Scene::Picture(ScreenObjects &screen, unsigned index) {
    return &screen.pictures[index].value;
}

bool Scene::SetText(ScreenObjects &screen, Screen which, const Command &command,
                    generated::NwLytTextBox **placed) {
    unsigned first = 0;
    int origin = 0;
    const unsigned count = ClipText(*font_, command, &first, &origin);
    if (!count)
        return false;
    const unsigned slot = command.slot;
    const unsigned chars = core::limits::RendererSlotChars[slot];
    if (count > chars)
        return false;
    const SlotLayout layout = MakeSlot(chars);
    unsigned char *base = screen.texts + Plan.offset[slot];
    auto *box = reinterpret_cast<NwLytTextBox *>(base + layout.textBox);
    auto *string = reinterpret_cast<std::uint16_t *>(base + layout.string);
    auto *batch = reinterpret_cast<NwFontTextBatchPrefix *>(base + layout.batch);
    for (unsigned i = 0; i < count; ++i)
        string[i] = command.text[first + i];
    string[count] = 0;
    box->string_length = static_cast<std::uint16_t>(count);
    // 幅はゲームの折り返しにしか使われない。高さは行送りと等しくする（半画素のずれを避ける）。
    const int width = TextWidth(*font_, command, first, count);
    const float scale = static_cast<float>(command.scale);
    const float paneHeight = static_cast<float>(FontLineFeed) * scale;
    const float x = static_cast<float>(origin) - static_cast<float>(ScreenWidth(which)) / 2.0f;
    const float y = static_cast<float>(ScreenHeight) / 2.0f -
                    (static_cast<float>(command.bounds.y) + paneHeight / 2.0f);
    Place(box->pane, static_cast<float>(width + (width & 1)), paneHeight, x, y, TextDepth);
    box->top_color = box->bottom_color = GameColor(command.color);
    box->font = &fonts_.resFont[command.font == Font::Numeric ? 1 : 0].value;
    box->font_width = box->font_height = static_cast<float>(FontMetric) * scale;
    // 文字列を替えたので、バッチを組み直させる。
    api_->batchInit(batch, chars);
    batch->built = 0;
    batch->flags = 0;
    box->dirty = 1;
    *placed = box;
    return true;
}

void Scene::Apply(Screen which, const ScreenDrawing &drawing) {
    auto &screen = screens_[static_cast<unsigned>(which)];
    NwLytPane *panes[core::limits::RendererCommands];
    bool used[core::limits::RendererTexts] = {};
    unsigned count = 0, rectangles = 0;
    const std::size_t total = drawing.count < core::limits::RendererCommands ? drawing.count
                                                                            : core::limits::RendererCommands;
    for (std::size_t i = 0; i < total; ++i) {
        const Command &command = drawing.commands[i];
        if (command.kind == Kind::Rectangle || command.kind == Kind::Image || command.kind == Kind::Repeat) {
            const bool image = command.kind != Kind::Rectangle;
            const auto textureIndex = static_cast<unsigned>(command.texture);
            if (image && (textureIndex >= font_->textureCount || command.source.width <= 0 ||
                          command.source.height <= 0 || command.scale < 1 || command.scale > 8))
                continue;
            const auto pictureLimit = which == Screen::Bottom ? core::limits::RendererBottomRects : core::limits::RendererRects;
            if (rectangles >= pictureLimit || command.bounds.width <= 0 ||
                command.bounds.height <= 0)
                continue;
            Rect bounds = command.bounds;
            if (image) {
                const auto right = std::min(std::int64_t(bounds.x) + bounds.width, std::int64_t(command.clip.x) + command.clip.width);
                const auto bottom = std::min(std::int64_t(bounds.y) + bounds.height, std::int64_t(command.clip.y) + command.clip.height);
                bounds.x = std::max(bounds.x, command.clip.x);
                bounds.y = std::max(bounds.y, command.clip.y);
                if (right <= bounds.x || bottom <= bounds.y)
                    continue;
                bounds.width = static_cast<int>(right - bounds.x);
                bounds.height = static_cast<int>(bottom - bounds.y);
            }
            auto *picture = Picture(screen, rectangles++);
            const float w = static_cast<float>(bounds.width);
            const float h = static_cast<float>(bounds.height);
            const float x = static_cast<float>(bounds.x) + w / 2.0f -
                            static_cast<float>(ScreenWidth(which)) / 2.0f;
            const float y = static_cast<float>(ScreenHeight) / 2.0f -
                            (static_cast<float>(bounds.y) + h / 2.0f);
            Place(picture->pane, w, h, x, y, TextDepth);
            picture->material = image ? &imageMaterials_[textureIndex].value : &material_.value;
            if (image) {
                const auto &t = font_->textures[textureIndex];
                const auto &src = command.source;
                const float factorX = command.kind == Kind::Repeat ? 1.0f / command.scale :
                    static_cast<float>(src.width) / command.bounds.width;
                const float factorY = command.kind == Kind::Repeat ? 1.0f / command.scale :
                    static_cast<float>(src.height) / command.bounds.height;
                const float sx = src.x + (static_cast<float>(bounds.x) - command.bounds.x) * factorX;
                const float sy = src.y + (static_cast<float>(bounds.y) - command.bounds.y) * factorY;
                picture->uv_output[0][0] = sx / t.width;
                picture->uv_output[0][1] = 1.0f - sy / t.height;
                picture->uv_output[0][2] = (sx + w * factorX) / t.width;
                picture->uv_output[0][3] = 1.0f - (sy + h * factorY) / t.height;
            } else {
                std::memcpy(picture->uv_output[0], solidUv_, sizeof(solidUv_));
            }
            picture->vertex_colors[0] = picture->vertex_colors[1] = picture->vertex_colors[2] =
                picture->vertex_colors[3] = GameColor(command.color);
            panes[count++] = &picture->pane;
        } else if (command.kind == Kind::Text && command.slot < core::limits::RendererTexts && !used[command.slot]) {
            NwLytTextBox *box = nullptr;
            if (SetText(screen, which, command, &box)) {
                used[command.slot] = true;
                panes[count++] = &box->pane;
            }
        }
    }
    // 子の輪を、つなぎ終えてから公開する。
    auto &root = screen.root.value;
    NwLytPaneLink *sentinel = &root.children;
    for (unsigned i = 0; i < count; ++i) {
        panes[i]->sibling_link.next = i + 1 < count ? &panes[i + 1]->sibling_link : sentinel;
        panes[i]->sibling_link.prev = i ? &panes[i - 1]->sibling_link : sentinel;
    }
    if (count) {
        root.children.prev = &panes[count - 1]->sibling_link;
        root.children.next = &panes[0]->sibling_link;
    } else {
        root.children.next = root.children.prev = sentinel;
    }
    screen.items = count;
}
} // namespace gohan::game::lyt::detail
