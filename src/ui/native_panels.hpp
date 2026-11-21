#pragma once
#include "core/feature.hpp"
#include "ui/lists/catalog_list.hpp"
#include "ui/game_label.hpp"
namespace gohan::ui::native_panels {
lists::CatalogList &Catalog();
GameLabel &Labels();
bool LeaseOnly(bool shown);
bool Wire();
bool Shutdown();
const core::Feature &Feature();
}
