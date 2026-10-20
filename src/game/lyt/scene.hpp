#pragma once
#include "game/lyt/drawing.hpp"
#include "game/lyt/resource_api.hpp"

namespace gohan::game::lyt::detail {
// プラグインのメモリに置く、ゲームの Layout の部品（Pane・Picture・TextBox・Material・ResFont）。
// GPU が読むのは字形のアトラスの画素だけで、ほかは CPU（コマンドの記録）しか読まない。
template <class T> struct alignas(16) Aligned {
    T value;
};

constexpr unsigned AlignUp(unsigned value, unsigned alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// 文字 1 枠（TextBox・Material・文字列・文字のバッチ）の置き場の配置。
struct SlotLayout {
    unsigned textBox, material, string, batch, bytes;
};
constexpr unsigned BatchCommandWords(unsigned chars) { return (78 * chars + 57) & ~3u; }
constexpr unsigned BatchBytes(unsigned chars) {
    const unsigned bitmap = AlignUp((chars + 7) / 8, 4);
    return AlignUp(sizeof(generated::NwFontTextBatchPrefix) +
                       sizeof(generated::NwFontTextBatchGlyph) * chars + bitmap,
                   16) +
           4 * BatchCommandWords(chars);
}
constexpr SlotLayout MakeSlot(unsigned chars) {
    SlotLayout slot{};
    slot.textBox = 0;
    slot.material = AlignUp(sizeof(generated::NwLytTextBox), 4);
    slot.string = AlignUp(slot.material + sizeof(generated::NwLytMaterial), 4);
    slot.batch = AlignUp(slot.string + 2 * (chars + 1), 16);
    slot.bytes = AlignUp(slot.batch + BatchBytes(chars), 16);
    return slot;
}
struct TextPlan {
    unsigned offset[core::limits::RendererTexts];
    unsigned bytes;
};
constexpr TextPlan MakeTextPlan() {
    TextPlan plan{};
    for (unsigned i = 0; i < core::limits::RendererTexts; ++i) {
        plan.offset[i] = plan.bytes;
        plan.bytes += MakeSlot(core::limits::RendererSlotChars[i]).bytes;
    }
    return plan;
}
inline constexpr TextPlan Plan = MakeTextPlan();

// アトラスの格子の上限（256 x 256 で 8 x 9 の格子）。
inline constexpr unsigned MaxGlyphs = 896;

struct SharedVariable {
    generated::NwLytTexMap map;
    generated::NwLytTexSrt srt;
    generated::NwLytTexCoordGen coordinateGen;
};
struct FontObjects {
    Aligned<generated::NwFontResFont> resFont[2];
    Aligned<generated::NwFontFinf> info[2];
    Aligned<generated::NwFontTglp> glyphs[2];
    Aligned<generated::NwFontGlyphTextureDescriptor> sheet[2];
    alignas(16) unsigned char widths[sizeof(generated::NwFontCwdh) + 3 * MaxGlyphs];
    alignas(16) unsigned char characterMap[2][sizeof(generated::NwFontCmapScanPrefix) + 4 * MaxGlyphs];
};
struct ScreenObjects {
    Aligned<generated::NwLytPane> root;
    Aligned<generated::NwLytPicture> *pictures;
    alignas(16) unsigned char texts[Plan.bytes];
    unsigned items;
};

class Scene {
  public:
    // 確保の前に、字形の表の大きさとゲームの文字バッファの必要量を確かめる。
    bool Prepare(const ResourceApi &api, const FontResource &font) const;
    // Prepare に成功した後、アトラス（GPU が読める確保済みの領域）が決まってから組む。
    void Build(const ResourceApi &api, const FontResource &font, const void *atlas);
    // 1 画面の描画指示から Pane の木を組み直す。呼び出し側は、その画面のノードが
    // ゲームのリストに載っていない時だけ呼ぶ。
    void Apply(Screen screen, const ScreenDrawing &drawing);
    // 木に載っている部品の数（0 ならノードを登録しない）。
    unsigned Items(Screen screen) const { return screens_[static_cast<unsigned>(screen)].items; }
    generated::NwLytPane *Root(Screen screen) {
        return &screens_[static_cast<unsigned>(screen)].root.value;
    }

  private:
    generated::NwLytPicture *Picture(ScreenObjects &screen, unsigned index);
    bool SetText(ScreenObjects &screen, Screen which, const Command &command,
                 generated::NwLytTextBox **placed);
    const ResourceApi *api_ = nullptr;
    const FontResource *font_ = nullptr;
    Aligned<generated::NwLytMaterial> material_;
    Aligned<SharedVariable> shared_;
    Aligned<generated::NwLytMaterial> imageMaterials_[static_cast<unsigned>(Texture::Count)];
    Aligned<SharedVariable> imageShared_[static_cast<unsigned>(Texture::Count)];
    float solidUv_[4]{};
    Aligned<generated::NwLytPicture> topPictures_[core::limits::RendererRects];
    Aligned<generated::NwLytPicture> bottomPictures_[core::limits::RendererBottomRects];
    FontObjects fonts_;
    ScreenObjects screens_[2];
};
} // namespace gohan::game::lyt::detail
