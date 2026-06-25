#pragma once

// ============================================================================
// FurnitureCatalog — the SINGLE SOURCE OF TRUTH mapping a furniture TYPE (what
// FurniturePlacer emits: "bed", "counter", "chest", ...) to a template asset name
// in resources/templates/. This used to live as a private `kFixtureTemplate` map
// inside the v2 build handler (editor/src/Application.cpp), decoupled from the
// placer's vocabulary — so it silently drifted: the placer emitted "chest" for
// every bedroom/pantry, the handler had no "chest" entry, and every chest was
// dropped with only a buried INFO log. Moving the map here makes it enumerable and
// lets `validateFurnitureCoverage` flag exactly that class of gap up front.
//
// Demand side = FurniturePlacer::requiredFurniture(purpose); supply side = this map.
// ============================================================================

#include <functional>
#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

class FurnitureCatalog {
public:
    /// Template asset name for a furniture type, or "" if the type is unmapped (a coverage gap).
    static std::string templateFor(const std::string& type);

    /// Every furniture type this catalog maps (the supply side, for enumeration/diagnostics).
    static std::vector<std::string> mappedTypes();
};

/// One missing-asset finding — what the pipeline surfaces instead of silently dropping a piece.
struct AssetGap {
    std::string purpose;       ///< room purpose that needs it (e.g. "kitchen", "bedchamber")
    std::string type;          ///< furniture type the placer wants (e.g. "chest")
    std::string templateName;  ///< the mapped template, or "" if the type is unmapped
    std::string message;       ///< human: "bedchamber requires a 'chest' but no template is mapped"
};

struct FurnitureCoverageReport {
    std::vector<AssetGap> gaps;
    bool ok() const { return gaps.empty(); }
};

/// Validate that every furniture type FurniturePlacer can emit (across every known purpose)
/// resolves to a real template asset. `templateExists` answers whether a template NAME is loadable:
///   - runtime: `[&](auto& n){ return objectTemplateManager->getTemplate(n) != nullptr; }`
///   - tests:   a file scan of resources/templates/.
/// If empty, only mapping coverage (L1) is checked; supplied, asset existence (L2) too. The report
/// names each gap by the ROOM that needs it, so the user sees "kitchen is missing its counter",
/// not a numeric skip count.
FurnitureCoverageReport validateFurnitureCoverage(
    const std::function<bool(const std::string&)>& templateExists = {});

} // namespace Core
} // namespace Phyxel
