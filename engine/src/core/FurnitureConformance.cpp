#include "core/FurnitureConformance.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

#include "core/FurnitureCatalog.h"

namespace Phyxel {
namespace Core {

namespace {
// type (placer vocabulary) -> archetype id in object_dimensions.json. 'barrel' is intentionally
// ABSENT: object_dimensions.json has no barrel archetype, so barrel conformance can't be measured
// (-> no_canon, the signal to add a grounded barrel archetype).
const std::map<std::string, std::string>& typeArchetype() {
    static const std::map<std::string, std::string> m = {
        {"bed", "bed_single"}, {"chest", "chest"},   {"table", "table_dining"},
        {"counter", "counter_kitchen"}, {"bench", "bench"}, {"fireplace", "hearth"},
        {"tavern_bar", "tavern_bar"}, {"back_bar", "back_bar"}, {"bar_stool", "bar_stool"},
        {"candle_stand", "candle_stand"}, {"wall_lantern", "wall_lantern"}, {"chandelier", "chandelier"},
        {"mug", "mug"}, {"bottle", "bottle"}, {"barrel", "barrel"}, {"tavern_table", "tavern_table"},
        {"forge_hearth", "forge_hearth"}, {"anvil", "anvil"}, {"bellows", "bellows"},
        {"tool_rack", "tool_rack"},
    };
    return m;
}

std::string fmt(double v) {
    std::ostringstream os; os.setf(std::ios::fixed); os.precision(2); os << v; return os.str();
}

// Compare one overall-size dim to canon (if the archetype declares it); append a note on a miss.
void cmpDim(const ArchetypeDims& a, const std::string& key, double actual, std::string& detail) {
    if (!a.has(key)) return;
    const double canon = a.value(key), tol = a.tolerance;
    if (std::fabs(actual - canon) > tol) {
        if (!detail.empty()) detail += "; ";
        detail += key + " " + fmt(actual) + " vs canon " + fmt(canon) + " +/-" + fmt(tol);
    }
}
} // namespace

std::string archetypeForType(const std::string& type) {
    auto it = typeArchetype().find(type);
    return it == typeArchetype().end() ? std::string() : it->second;
}

FurnitureConformanceReport checkFurnitureConformance(
    const DimensionCanonRegistry& canon,
    const std::function<AssetExtents(const std::string&)>& extentsOf) {
    FurnitureConformanceReport rep;
    for (const auto& type : FurnitureCatalog::mappedTypes()) {
        ConformanceFinding f;
        f.type = type;
        f.templateName = FurnitureCatalog::templateFor(type);
        f.archetype = archetypeForType(type);

        if (f.archetype.empty()) {
            f.status = "no_canon";
            f.detail = "no grounded archetype for '" + type + "' in object_dimensions.json";
            rep.findings.push_back(f); continue;
        }
        const ArchetypeDims* arch = canon.get(f.archetype);
        if (!arch) {
            f.status = "no_canon";
            f.detail = "archetype '" + f.archetype + "' not present in the loaded canon";
            rep.findings.push_back(f); continue;
        }
        const AssetExtents e = extentsOf(f.templateName);
        if (!e.present) {
            f.status = "no_metrics";
            f.detail = "template '" + f.templateName + "' has no .metrics.json sidecar";
            rep.findings.push_back(f); continue;
        }
        // If the archetype declares NO overall-size key, "ok" would be a vacuous pass — we measured
        // nothing. Flag it: the canon needs a bounding-box dim (or the asset can't be size-validated).
        const bool hasBoundingKey = arch->has("height") || arch->has("width") || arch->has("depth")
                                 || arch->has("length") || arch->has("diameter");
        if (!hasBoundingKey) {
            f.status = "no_checkable_dims";
            f.detail = "canon '" + f.archetype + "' has only feature dims (no height/width/depth/"
                       "length) — overall size can't be measured against canon";
            rep.findings.push_back(f); continue;
        }
        std::string detail;
        const double horiz = std::max(e.width, e.depth);
        cmpDim(*arch, "height", e.height, detail);
        cmpDim(*arch, "width",  e.width,  detail);
        cmpDim(*arch, "depth",  e.depth,  detail);
        cmpDim(*arch, "length", horiz,    detail);
        cmpDim(*arch, "diameter", horiz,  detail);
        if (detail.empty()) { f.status = "ok"; }
        else { f.status = "out_of_tolerance"; f.detail = detail; }
        rep.findings.push_back(f);
    }
    return rep;
}

} // namespace Core
} // namespace Phyxel
