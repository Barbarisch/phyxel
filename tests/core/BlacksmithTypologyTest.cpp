#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

#include "core/RoomProgram.h"
#include "core/FurniturePlacer.h"
#include "core/FurnitureCatalog.h"
#include "core/RoomLayout.h"
#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// BLACKSMITH / SMITHY — the SECOND functional (non-residential) typology, after the
// tavern. A town is its workshops, not just its dwellings. This proves a smithy is a
// real, grounded, navigable structure AND that its DEFINING relationship — the
// forge -> anvil -> quench work triangle, vented and fire-safe — actually holds on the
// realized output (the archetype's function testers F1-F3), not just "a shop with an anvil".
//   (data)  the shipped canon carries a `blacksmith` typology, grounded, 2-bay
//           (forge floor + storefront), on the timber-frame bay system;
//   (wiring) the forge recipe places a FORGE + ANVIL; the new smithy assets resolve to
//            templates and conform to grounded canon (FurnitureConformanceTest pins them);
//   (L3)     a character-box WALKS the storefront -> forge floor on the realized voxels;
//   (F1-F3)  the placed forge is on a building EXTERIOR wall (venting); the anvil is within
//            a step of the forge; the quench (slack tub) is adjacent to the anvil.
// Grounding: forge/anvil/quench program (beautifuliron.com; Wikipedia Forge); footprint
// envelope from the Anderson Blacksmith Shop, Colonial Williamsburg. See room_program.json
// + docs/structure-generation/archetypes/blacksmith.md.
// ============================================================================

namespace {
bool loadShippedCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"}) {
        std::ifstream f(p);
        if (f.good()) return reg.loadFromFile(p);
    }
    return false;
}

StyleProfile smithyStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}

const ProgRoom* roomByPurpose(const ProgStory& s, const std::string& purpose) {
    for (const auto& r : s.rooms) if (r.purpose == purpose) return &r;
    return nullptr;
}

// Autofill the blacksmith typology onto an 8x6 single-story footprint (2 bays x ~4 m, ~6 m wide —
// within the Anderson <9 m / cruck <=6 m envelope).
BuildingProgram smithyProgram(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "smithy"; p.style = "timber_cottage"; p.footprintW = 8; p.footprintD = 6;
    p.substructure = "slab"; p.typology = "blacksmith";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 11u, rp);
    return p;
}

bool walkBetween(const StructureRealizer::ShellResult& sh, const ProgRoom& from, const ProgRoom& to,
                 int W, int D) {
    const int floorY = sh.floorTopByStory.empty() ? 12 : sh.floorTopByStory[0];
    TraversalProbe probe([&](int x, int y, int z) { return sh.canvas.occupiedMicro(x, y, z); },
                         AgentBox{2, 16, 4});
    const glm::ivec3 start((from.rect.x + from.rect.w / 2) * 9 + 4, floorY,
                           (from.rect.z + from.rect.d / 2) * 9 + 4);
    const int gx = (to.rect.x + to.rect.w / 2) * 9 + 4, gz = (to.rect.z + to.rect.d / 2) * 9 + 4;
    return probe.reachable(start, glm::ivec3(gx - 2, floorY - 1, gz - 2),
                           glm::ivec3(gx + 2, floorY + 1, gz + 2),
                           glm::ivec3(0, floorY - 2, 0), glm::ivec3(W * 9, floorY + 28, D * 9));
}

// First placement of `type` in a furnish() result; .type empty if absent.
FurniturePlacement firstOfType(const std::vector<FurniturePlacement>& ps, const std::string& type) {
    for (const auto& p : ps) if (p.type == type) return p;
    return {};
}
int cheb(const glm::ivec3& a, const glm::ivec3& b) {   // Chebyshev distance in the XZ plane (cubes)
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}
} // namespace

// The shipped canon must carry a grounded `blacksmith` typology: a forge floor + a storefront,
// bay-driven, single-story, with a source.
TEST(BlacksmithTypologyTest, ShippedCanonHasGroundedBlacksmith) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";

    const RoomProgram* b = reg.get("blacksmith");
    ASSERT_NE(b, nullptr) << "no 'blacksmith' typology in the shipped canon";
    EXPECT_GT(b->bayLength, 0.0) << "blacksmith not bay-driven";
    EXPECT_GT(b->bays, 0);
    EXPECT_FALSE(b->source.empty() && b->sources.empty()) << "blacksmith is UNSOURCED";

    bool hasForge = false, hasStorefront = false;
    for (const auto& r : b->rooms) {
        if (r.purpose == "forge")   hasForge = true;
        if (r.purpose == "service") hasStorefront = true;   // the customer-facing storefront/yard
    }
    EXPECT_TRUE(hasForge) << "blacksmith has no forge floor (the defining space)";
    EXPECT_TRUE(hasStorefront) << "blacksmith has no storefront/service end";
}

// The forge recipe places the DEFINING fixtures (a forge + an anvil), and the new smithy assets
// resolve to real, loadable templates (no silent drop).
TEST(BlacksmithTypologyTest, ForgeRecipeHasForgeAndAnvilAndAssetsResolve) {
    const auto req = FurniturePlacer::requiredFurniture("forge");
    bool wantsForge = false, wantsAnvil = false;
    for (const auto& t : req) { if (t == "forge_hearth") wantsForge = true; if (t == "anvil") wantsAnvil = true; }
    EXPECT_TRUE(wantsForge) << "the forge recipe has no forge_hearth";
    EXPECT_TRUE(wantsAnvil) << "the forge recipe has no anvil";

    auto fileExists = [](const std::string& name) {
        for (const char* d : {"resources/templates/", "../resources/templates/",
                              "../../resources/templates/", "../../../resources/templates/"}) {
            std::ifstream f(std::string(d) + name + ".voxel");
            if (f.good()) return true;
        }
        return false;
    };
    for (const char* type : {"forge_hearth", "anvil", "bellows", "tool_rack"}) {
        ASSERT_FALSE(FurnitureCatalog::templateFor(type).empty()) << type << " unmapped in the catalog";
        EXPECT_TRUE(fileExists(FurnitureCatalog::templateFor(type))) << type << " template missing on disk";
    }
}

// L3: a character-box must WALK the storefront -> forge floor on the realized voxels.
TEST(BlacksmithTypologyTest, CharacterWalksStorefrontToForgeFloor) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("blacksmith");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = smithyProgram(rp);
    const ProgRoom* forge = roomByPurpose(p.stories[0], "forge");
    const ProgRoom* shop  = roomByPurpose(p.stories[0], "service");
    ASSERT_NE(forge, nullptr); ASSERT_NE(shop, nullptr) << "blacksmith typology didn't produce its rooms";

    auto sh = StructureRealizer::realizeShell(p, smithyStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_TRUE(walkBetween(sh, *shop, *forge, p.footprintW, p.footprintD))
        << "character could NOT walk storefront -> forge floor — the smithy interior is not passable";
}

// TEETH: seal the interior (exterior door only). The walk must now FAIL — the positive relies on a
// carved interior door, not a probe wandering through walls.
TEST(BlacksmithTypologyTest, SealedSmithyBlocksStorefrontToForge) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("blacksmith");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = smithyProgram(rp);
    auto& portals = p.stories[0].portals;
    std::vector<ProgPortal> exteriorOnly;
    for (const auto& po : portals)
        if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
    portals = exteriorOnly;

    const ProgRoom* forge = roomByPurpose(p.stories[0], "forge");
    const ProgRoom* shop  = roomByPurpose(p.stories[0], "service");
    ASSERT_NE(forge, nullptr); ASSERT_NE(shop, nullptr);

    auto sh = StructureRealizer::realizeShell(p, smithyStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_FALSE(walkBetween(sh, *shop, *forge, p.footprintW, p.footprintD))
        << "character crossed SOLID partitions — the smithy traversal proof has no teeth";
}

// F1-F3 (the archetype function testers): the forge -> anvil -> quench WORK TRIANGLE must hold on the
// placed furniture, not just be in the recipe. The forge must be vented (on a building EXTERIOR wall),
// the anvil within a step of it, and the quench (slack tub = barrel) adjacent to the anvil.
TEST(BlacksmithTypologyTest, ForgeAnvilQuenchWorkTriangleHolds) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("blacksmith");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = smithyProgram(rp);
    const ProgRoom* forgeRoom = roomByPurpose(p.stories[0], "forge");
    ASSERT_NE(forgeRoom, nullptr);

    auto sh = StructureRealizer::realizeShell(p, smithyStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const int floorY = sh.floorTopByStory.empty() ? 12 : sh.floorTopByStory[0];

    const auto placements = FurniturePlacer::furnish(p.stories[0], glm::ivec3(0, 0, 0), floorY);
    const auto forge = firstOfType(placements, "forge_hearth");
    const auto anvil = firstOfType(placements, "anvil");
    const auto quench = firstOfType(placements, "barrel");   // the slack/quench tub
    ASSERT_FALSE(forge.type.empty())  << "no forge_hearth was placed on the forge floor";
    ASSERT_FALSE(anvil.type.empty())  << "no anvil was placed on the forge floor";
    ASSERT_FALSE(quench.type.empty()) << "no quench (barrel) was placed on the forge floor";

    // F1 — forge VENTED: it backs onto a building EXTERIOR (perimeter) wall. NOTE (solution-auditor
    // 2026-06-28): this is a SANITY INVARIANT, *not* a red-before-green proof. For this linear 2-bay
    // layout the forge room's two x-walls are both door walls (exterior entrance + interior partition),
    // so the placer seats the forge on a perimeter (south/north) wall by elimination — F1 holds
    // structurally and was never observed RED. A FALSIFIABLE F1 (a layout where the forge room has an
    // interior, non-door wall the forge could wrongly pick) is OWED for courtyard/T-plan smithies.
    // Only F2/F3 below are the genuine red->green work-triangle proof.
    const bool forgeOnPerimeter = forge.worldPos.x <= 1 || forge.worldPos.x >= p.footprintW - 2 ||
                                  forge.worldPos.z <= 1 || forge.worldPos.z >= p.footprintD - 2;
    EXPECT_TRUE(forgeOnPerimeter)
        << "forge at (" << forge.worldPos.x << "," << forge.worldPos.z << ") is not on a building "
        << "exterior wall (W=" << p.footprintW << ",D=" << p.footprintD << ") — it cannot vent (F1)";

    // F2 — anvil WITHIN A STEP of the forge (~1 m): the heart of the work triangle.
    EXPECT_LE(cheb(anvil.worldPos, forge.worldPos), 2)
        << "anvil is " << cheb(anvil.worldPos, forge.worldPos) << " cubes from the forge — not within a step (F2)";

    // F3 — quench ADJACENT to the anvil (~beside it).
    EXPECT_LE(cheb(quench.worldPos, anvil.worldPos), 2)
        << "quench is " << cheb(quench.worldPos, anvil.worldPos) << " cubes from the anvil — not adjacent (F3)";
}
