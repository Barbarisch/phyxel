#include "core/FurnitureCatalog.h"

#include <algorithm>
#include <cmath>
#include <map>

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
        {"chest",     "chest_closed"},
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
