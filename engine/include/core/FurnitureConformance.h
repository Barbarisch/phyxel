#pragma once

// ============================================================================
// FurnitureConformance — tracks which furniture TEMPLATES are NOT dimensionally
// valid against their grounded canon, so we know which assets to regenerate.
//
// The placer now reserves each fixture's real footprint (from .metrics.json), and
// the asset-coverage gate ensures every needed type has a loadable template. This
// is the third leg: does each template's ACTUAL size match the grounded archetype
// canon (resources/object_dimensions.json, via DimensionCanon, ± tolerance)? A
// template that drifts (a bed that comes out 5 m long), has no .metrics sidecar,
// or has no canon archetype at all is FLAGGED — the signal to regenerate it.
//
// Pure: the caller injects the per-template extents (loaded from .metrics.json) and
// the loaded DimensionCanon, so this is unit-testable without file I/O.
// ============================================================================

#include <functional>
#include <string>
#include <vector>

#include "core/DimensionCanon.h"

namespace Phyxel {
namespace Core {

/// An asset's actual bounding extents in cubes (== metres), from its .metrics.json overall box.
struct AssetExtents {
    double width = 0.0;    ///< x-extent
    double height = 0.0;   ///< y-extent
    double depth = 0.0;    ///< z-extent
    bool   present = false;///< false => the template has no .metrics.json sidecar
};

/// One furniture template's conformance verdict.
struct ConformanceFinding {
    std::string type;          ///< furniture type (placer vocabulary, e.g. "bed")
    std::string templateName;  ///< asset (e.g. "bed_single")
    std::string archetype;     ///< grounded canon archetype (e.g. "bed_single"); "" if none
    std::string status;        ///< "ok" | "no_metrics" | "no_canon" | "out_of_tolerance"
    std::string detail;        ///< human-readable: which dim is off / what is missing
    bool conforms() const { return status == "ok"; }
};

struct FurnitureConformanceReport {
    std::vector<ConformanceFinding> findings;
    std::vector<ConformanceFinding> nonConforming() const {
        std::vector<ConformanceFinding> v;
        for (const auto& f : findings) if (!f.conforms()) v.push_back(f);
        return v;
    }
    bool allConform() const { return nonConforming().empty(); }
};

/// Map a furniture TYPE (placer vocabulary) to its grounded archetype id in object_dimensions.json,
/// or "" if the type has no canon archetype (then its dims can't be measured against canon).
std::string archetypeForType(const std::string& type);

/// Audit every furniture type the placer can emit (FurnitureCatalog::mappedTypes) against canon:
/// load the template's actual extents via `extentsOf` and compare to the archetype's grounded dims
/// (± the archetype tolerance, using the same height/width/depth/length mapping as AssetValidator).
/// Findings flag templates that drift, have no .metrics, or have no canon archetype.
FurnitureConformanceReport checkFurnitureConformance(
    const DimensionCanonRegistry& canon,
    const std::function<AssetExtents(const std::string& templateName)>& extentsOf);

} // namespace Core
} // namespace Phyxel
