#include "game/catalog_layout.hpp"
#include "game/lyt/bclyt_edit.hpp"
#include <cstdio>
namespace gohan::game {
std::vector<std::uint8_t> MakeCatalogExtraLayout(const std::uint8_t *src, std::size_t bytes) {
    if (bytes > 12288) return {};
    lyt::BclytEdit edit;
    if (!edit.Load(src, bytes)) return {};
    float r[4];
    const int clip = edit.FindByName("N_frame_00");
    if (!edit.GetRect(clip, r) || !edit.SetRect(clip, r[0] - 13, r[1], r[2] + 26, r[3])) return {};
    for (unsigned i = 0; i < 8; ++i) {
        char from[16], to[16]; std::snprintf(from, sizeof(from), "T_itm_%02u", i);
        std::snprintf(to, sizeof(to), "T_id_%02u", i);
        const int source = edit.FindByName(from);
        float width, height;
        if (!edit.GetRect(source, r) || !edit.GetFloat(source, 92, width) || !edit.GetFloat(source, 96, height))
            return {};
        const int n = edit.Duplicate(source, to);
        if (n < 0 || !edit.SetRect(n, -47, r[1], 47, r[3]) ||
            !edit.SetByte(n, 76, 4) || !edit.SetFloat(n, 92, width * 0.65f) ||
            !edit.SetFloat(n, 96, height * 0.65f)) return {};
    }
    return edit.Build();
}
}
