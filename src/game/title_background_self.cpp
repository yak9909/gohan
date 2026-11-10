#include "game/title_background.hpp"
#include "ui_values.hpp"

namespace gohan::game::titlebg {
Status LoadSelf(Sources &out) {
    romfs::Session session;
    return session.Visit(core::design::ca2::Titlebg::Source::Archive, [](const Reader &reader, void *context) {
        return DecodeArchive(reader, *static_cast<Sources *>(context));
    }, &out);
}
} // namespace gohan::game::titlebg
