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

// The metrics->footprint wiring (Slice B): an asset's .metrics.json bounding box (metres) becomes a
// cube footprint by ceil, floored at 1. bed_single is x=1.0 z=2.0 -> 1 wide x 2 deep. This is the
// computation the handler feeds into furnish(); previously only runtime-observed.
TEST(FurnitureCatalogTest, FootprintFromExtentsCeilsToCubes) {
    EXPECT_EQ(footprintFromExtents(1.0, 2.0).width, 1);   // bed_single x
    EXPECT_EQ(footprintFromExtents(1.0, 2.0).depth, 2);   // bed_single z (long axis backs the wall)
    EXPECT_EQ(footprintFromExtents(0.9, 1.9).width, 1);   // 0.9 -> 1 (ceil)
    EXPECT_EQ(footprintFromExtents(0.9, 1.9).depth, 2);   // 1.9 -> 2 (ceil)
    EXPECT_EQ(footprintFromExtents(1.2, 0.55).width, 2);  // chest 1.2 -> 2
    EXPECT_EQ(footprintFromExtents(0.0, 0.0).width, 1);   // floored at 1 (no zero/negative footprint)
    EXPECT_EQ(footprintFromExtents(0.0, 0.0).depth, 1);
}

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
    // Library taxonomy (2026-08-07): furniture templates live under
    // furniture/ — probe that path to find the library root, and accept a
    // mapped template in any category dir the furnisher may draw from.
    const char* roots[] = {"resources/templates/", "../resources/templates/",
                           "../../resources/templates/", "../../../resources/templates/"};
    std::string root;
    for (const char* r : roots) {
        std::ifstream f(std::string(r) + "furniture/barrel.voxel");
        if (f.good()) { root = r; break; }
    }
    if (root.empty()) GTEST_SKIP() << "resources/templates not reachable from CWD";
    auto onDisk = [&](const std::string& name) {
        for (const char* cat : {"furniture/", "architecture/", "items/", "decor/"}) {
            std::ifstream f(root + cat + name + ".voxel");
            if (f.good()) return true;
        }
        return false;
    };
    const auto rep = validateFurnitureCoverage(onDisk);
    EXPECT_TRUE(rep.ok()) << "mapped templates with no .voxel on disk:\n" << dump(rep);
}
