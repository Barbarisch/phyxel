#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>

#include "core/FurnitureCatalog.h"
#include "core/FurniturePlacer.h"

using namespace Phyxel::Core;

// ============================================================================
// Asset-coverage validation (the "flag a missing required object" the pipeline
// lacked). FurniturePlacer decides WHAT each room needs by purpose; the catalog
// maps each type to a template. Nothing used to assert the two agreed — so the
// placer emitted 'chest' for every bedroom, no template was mapped, and the chest
// was silently dropped. These tests make that a SURFACED gap, named by room.
// ============================================================================

namespace {
bool hasGapForType(const FurnitureCoverageReport& r, const std::string& type) {
    for (const auto& g : r.gaps) if (g.type == type) return true;
    return false;
}
std::string dump(const FurnitureCoverageReport& r) {
    std::string s;
    for (const auto& g : r.gaps) s += "  - " + g.message + "\n";
    return s;
}
bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

// The semantic requirement contract is exposed (the user's ask: know what a room NEEDS): a kitchen
// needs a counter; a bedchamber needs a bed AND storage (chest). This is the demand side the
// coverage gate scans.
TEST(FurnitureCatalogTest, RequirementContractIsExposed) {
    EXPECT_TRUE(contains(FurniturePlacer::requiredFurniture("kitchen"), "counter"));
    const auto bed = FurniturePlacer::requiredFurniture("bedchamber");
    EXPECT_TRUE(contains(bed, "bed"))   << "a bedchamber must require a bed";
    EXPECT_TRUE(contains(bed, "chest")) << "a bedchamber must require storage (chest)";
}

// L1 COVERAGE — every furniture type the placer can emit (across every purpose) must map to a
// template, else the pipeline silently drops it. This is the gate that catches the live 'chest'
// gap. (Red-first: with chest unmapped this FAILS, naming bedchamber + store; green once the
// catalog maps chest -> chest_closed.)
TEST(FurnitureCatalogTest, EveryRequiredFurnitureTypeIsMapped) {
    const auto rep = validateFurnitureCoverage();   // L1: mapping only (no existence predicate)
    EXPECT_TRUE(rep.ok())
        << "furniture types the pipeline would SILENTLY DROP (no template mapped):\n" << dump(rep);
}

// TEETH — the detector must actually FLAG a missing asset, not pass vacuously. Pretend the chest
// template is absent from the loaded catalog: the L2 existence check must report a chest gap, named
// by the room, while NOT spuriously flagging a type whose asset is present.
TEST(FurnitureCatalogTest, DetectorFlagsAMissingTemplate) {
    auto chestMissing = [](const std::string& name) { return name != "chest_closed"; };
    const auto rep = validateFurnitureCoverage(chestMissing);
    EXPECT_FALSE(rep.ok()) << "a missing chest asset slipped through — detector has no teeth";
    EXPECT_TRUE(hasGapForType(rep, "chest")) << "the missing chest was not flagged";
    EXPECT_FALSE(hasGapForType(rep, "bed"))  << "a present asset (bed) was spuriously flagged";
    // the gap message names the room that needs it (the user's "missing X for room Y")
    bool named = false;
    for (const auto& g : rep.gaps)
        if (g.type == "chest" && !g.purpose.empty() && g.message.find(g.purpose) != std::string::npos) named = true;
    EXPECT_TRUE(named) << "the gap must name the room that needs the missing piece";
}

// L2 EXISTENCE on the REAL catalog — every mapped template has a .voxel on disk. CWD-tolerant;
// skips if resources/ isn't reachable (same convention as DimensionCanonTest).
TEST(FurnitureCatalogTest, EveryMappedTemplateExistsOnDisk) {
    const char* roots[] = {"resources/templates/", "../resources/templates/",
                           "../../resources/templates/", "../../../resources/templates/"};
    std::string root;
    for (const char* r : roots) {
        std::ifstream f(std::string(r) + "barrel.voxel");
        if (f.good()) { root = r; break; }
    }
    if (root.empty()) GTEST_SKIP() << "resources/templates not reachable from CWD";
    auto onDisk = [&](const std::string& name) {
        std::ifstream f(root + name + ".voxel");
        return f.good();
    };
    const auto rep = validateFurnitureCoverage(onDisk);
    EXPECT_TRUE(rep.ok()) << "mapped templates with no .voxel on disk:\n" << dump(rep);
}
