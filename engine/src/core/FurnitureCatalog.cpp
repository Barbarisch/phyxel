#include "core/FurnitureCatalog.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "core/FurniturePlacer.h"

namespace Phyxel {
namespace Core {

Footprint footprintFromExtents(double xExtentMetres, double zExtentMetres) {
    Footprint fp;
    fp.width = std::max(1, (int)std::ceil(xExtentMetres));
    fp.depth = std::max(1, (int)std::ceil(zExtentMetres));
    return fp;
}

namespace {
// type -> template asset (resources/templates/<name>.voxel). The complete v2 FurniturePlacer
// vocabulary (FurniturePlacer::recipeFor) is the demand side; this is the supply side. Every type
// the placer can emit must appear here (enforced by FurnitureCatalogTest), else the coverage gate
// flags it. 'chest' was the gap the old handler map silently dropped — chest_closed.voxel exists.
const std::map<std::string, std::string>& table() {
    static const std::map<std::string, std::string> t = {
        {"fireplace", "fireplace"},
        {"table",     "table_wood"},
        {"counter",   "counter"},
        {"bed",       "bed_single"},
        {"bench",     "bench_wood"},
        {"barrel",    "barrel"},
        {"keg",       "keg"},           // TAPPED serving cask on chocks — the working
                                        // taproom cask (kilderkin). `barrel` is the
                                        // upright STORAGE cask; they are not the same
                                        // object and a tavern wants both.
        {"chest",     "chest_closed"},
        {"tavern_bar",   "tavern_bar"},    // the taproom's defining fixture (the serving counter)
        {"tavern_table", "tavern_table"},  // long communal drinking table
        {"back_bar",     "back_bar"},      // shelving behind the bar (bottles)
        {"bar_stool",    "bar_stool"},     // tall stool at the bar
        {"candle_stand", "candle_stand"},  // floor candelabra (emissive glow) — lighting
        {"wall_lantern", "wall_lantern"},  // wall sconce lantern (Mount::Wall, 60 in mount height)
        {"chandelier",   "chandelier"},    // hanging candle ring (Mount::Ceiling, hangs below ceiling)
        {"mug",          "mug"},           // tankard — table clutter (surface placement)
        {"bottle",       "bottle"},        // bottle — table/back-bar clutter
        {"forge_hearth", "forge_hearth"},  // smithy: the forge (firepot + chimney) — back wall
        {"anvil",        "anvil"},         // smithy: anvil on a stump (work-triangle centre)
        {"bellows",      "bellows"},       // smithy: great double-lung bellows (beside the forge)
        {"tool_rack",    "tool_rack"},     // smithy: wall rack of hammers/tongs
        {"oven_bread",   "oven_bread"},    // bakery: masonry dome bread oven (vented) — back wall
        {"chopping_block", "chopping_block"}, // butcher: the chopping block (defining work fixture)
        {"meat_rail",      "meat_rail"},      // butcher: freestanding rail of iron hooks (hanging meat)
        {"well",       "well"},        // yard/square: masonry well-head (settlement placer, not rooms)
        {"market_stall", "market_stall"}, // square: trestle stall w/ striped canopy (planSquareDressing)
        {"statue_hero",  "statue_hero"},  // square: civic statue on plinth (planSquareDressing)
        {"woodpile",   "woodpile"},    // yard: firewood stack (rear toft — planYardProps)
        {"garden_bed", "garden_bed"},  // yard: raised kitchen-garden bed (rear toft — planYardProps)
        {"chair",      "chair"},       // walnut side chair — the head-of-table seat (quality A)
        {"stool",      "stool"},       // backless stool — THE common medieval seat (quality A)
        {"wardrobe",   "wardrobe"},    // clothes press (canon-flagged post-medieval; middling+)
        {"rug",        "rug"},         // woven floor rug, wool border (quality A)
    };
    return t;
}
} // namespace

std::string FurnitureCatalog::templateFor(const std::string& type) {
    auto it = table().find(type);
    return it == table().end() ? std::string() : it->second;
}

std::vector<std::string> FurnitureCatalog::mappedTypes() {
    std::vector<std::string> v;
    v.reserve(table().size());
    for (const auto& kv : table()) v.push_back(kv.first);
    return v;
}

FurnitureCoverageReport validateFurnitureCoverageFor(
    const std::vector<std::string>& purposes,
    const std::function<bool(const std::string&)>& templateExists) {
    FurnitureCoverageReport rep;
    std::set<std::string> seen;   // one finding per (purpose,type), not per room
    for (const auto& purpose : purposes) {
        if (!seen.insert(purpose).second) continue;
        for (const auto& type : FurniturePlacer::requiredFurniture(purpose)) {
            const std::string tmpl = FurnitureCatalog::templateFor(type);
            if (tmpl.empty()) {
                rep.gaps.push_back({purpose, type, "",
                    purpose + " requires a '" + type + "' but no template is mapped for it"});
            } else if (templateExists && !templateExists(tmpl)) {
                rep.gaps.push_back({purpose, type, tmpl,
                    purpose + " requires a '" + type + "' -> template '" + tmpl +
                        "' but that asset is not loaded"});
            }
        }
    }
    return rep;
}

FurnitureCoverageReport validateFurnitureCoverage(
    const std::function<bool(const std::string&)>& templateExists) {
    FurnitureCoverageReport rep;
    for (const auto& purpose : FurniturePlacer::knownPurposes()) {
        for (const auto& type : FurniturePlacer::requiredFurniture(purpose)) {
            const std::string tmpl = FurnitureCatalog::templateFor(type);
            if (tmpl.empty()) {
                rep.gaps.push_back({purpose, type, "",
                    purpose + " requires a '" + type + "' but no template is mapped for it"});
            } else if (templateExists && !templateExists(tmpl)) {
                rep.gaps.push_back({purpose, type, tmpl,
                    purpose + " requires a '" + type + "' -> template '" + tmpl +
                        "' but that asset is not loaded"});
            }
        }
    }
    return rep;
}

} // namespace Core
} // namespace Phyxel
