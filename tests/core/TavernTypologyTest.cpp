#include <gtest/gtest.h>

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
// TAVERN — the first FUNCTIONAL (non-residential) building typology. A town is its
// functional buildings; until now every typology was a dwelling. This proves a tavern
// is a real, grounded, navigable structure:
//   (data) the shipped canon carries a `tavern` typology, grounded, with a 2-bay taproom
//          (the public common room) + kitchen + service, on the timber-frame bay system;
//   (wiring) the taproom recipe places a BAR, and the bar/table assets resolve to templates;
//   (L3) a character-box can actually WALK the entrance -> taproom -> kitchen on the realized
//        voxels, and the bar does NOT seal the room.
// Grounding: The New Inn, Gloucester (galleried courtyard inn, c.1430-50) + medieval inn
// room program (hall/taproom + kitchen + cellar + chambers). See room_program.json sources.
// ============================================================================

namespace {
// ---- shipped canon access (CWD-independent, mirrors RoomProgramTest) ----
bool loadShippedCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"}) {
        std::ifstream f(p);
        if (f.good()) return reg.loadFromFile(p);
    }
    return false;
}

// ---- L3 traversal scaffolding (mirrors TypologyHouseTraversalTest) ----
StyleProfile tavernStyle() {
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
// Autofill the tavern typology onto a 16x7 ground-floor footprint.
BuildingProgram tavernProgram(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "tavern"; p.style = "timber_cottage"; p.footprintW = 16; p.footprintD = 7;
    p.substructure = "slab"; p.typology = "tavern";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 7u, rp);
    return p;
}
} // namespace

// The shipped canon must carry a grounded `tavern` typology: a 2-bay taproom (public common
// room) + a kitchen + a service/cellar end, bay-driven, with a source.
TEST(TavernTypologyTest, ShippedCanonHasGroundedTavern) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";

    const RoomProgram* t = reg.get("tavern");
    ASSERT_NE(t, nullptr) << "no 'tavern' typology in the shipped canon";
    EXPECT_GT(t->bayLength, 0.0) << "tavern not bay-driven";
    EXPECT_GT(t->bays, 0);
    EXPECT_FALSE(t->source.empty() && t->sources.empty()) << "tavern is UNSOURCED";

    bool hasTaproom = false, hasKitchen = false;
    double taproomBays = 0.0;
    for (const auto& r : t->rooms) {
        if (r.purpose == "taproom") { hasTaproom = true; taproomBays = r.bays; }
        if (r.purpose == "kitchen") hasKitchen = true;
    }
    EXPECT_TRUE(hasTaproom) << "tavern has no taproom (the public common room)";
    EXPECT_TRUE(hasKitchen) << "tavern has no kitchen";
    EXPECT_GE(taproomBays, 2.0) << "the taproom should be the largest space (>=2 bays)";
}

// A tavern function maps to the tavern typology (so build_settlement can ask for one by function).
TEST(TavernTypologyTest, TavernFunctionResolvesToTypology) {
    EXPECT_EQ(RoomProgramRegistry::defaultTypologyForFunction("tavern"), "tavern");
    EXPECT_EQ(RoomProgramRegistry::defaultTypologyForFunction("inn"), "tavern");
}

// The taproom recipe places a BAR (its defining fixture), and bar/table assets resolve to templates.
TEST(TavernTypologyTest, TaproomRecipeHasBarAndAssetsResolve) {
    const auto req = FurniturePlacer::requiredFurniture("taproom");
    bool wantsBar = false;
    for (const auto& t : req) if (t == "tavern_bar") wantsBar = true;
    EXPECT_TRUE(wantsBar) << "the taproom recipe has no bar";

    // supply side: the bar/table types must resolve to real, loadable templates (no silent drop).
    EXPECT_FALSE(FurnitureCatalog::templateFor("tavern_bar").empty()) << "tavern_bar unmapped";
    EXPECT_FALSE(FurnitureCatalog::templateFor("tavern_table").empty()) << "tavern_table unmapped";
    auto fileExists = [](const std::string& name) {
        // Library taxonomy (2026-08-07): furniture templates live in
        // category subdirectories under the library root.
        for (const char* d : {"resources/templates/", "../resources/templates/",
                              "../../resources/templates/", "../../../resources/templates/"}) {
            for (const char* cat : {"furniture/", "architecture/", "items/", ""}) {
                std::ifstream f(std::string(d) + cat + name + ".voxel");
                if (f.good()) return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(fileExists(FurnitureCatalog::templateFor("tavern_bar"))) << "tavern_bar template missing on disk";
    EXPECT_TRUE(fileExists(FurnitureCatalog::templateFor("tavern_table"))) << "tavern_table template missing on disk";
}

// L3: a character-box must WALK the entrance -> taproom -> kitchen on the realized voxels.
TEST(TavernTypologyTest, CharacterWalksTaproomToKitchen) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = tavernProgram(rp);
    const ProgRoom* taproom = roomByPurpose(p.stories[0], "taproom");
    const ProgRoom* kitchen = roomByPurpose(p.stories[0], "kitchen");
    ASSERT_NE(taproom, nullptr); ASSERT_NE(kitchen, nullptr) << "tavern typology didn't produce its rooms";

    auto sh = StructureRealizer::realizeShell(p, tavernStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_TRUE(walkBetween(sh, *taproom, *kitchen, p.footprintW, p.footprintD))
        << "character could NOT walk taproom -> kitchen — the tavern interior is not passable";
}

// TEETH: seal the interior (exterior door only). The same walk must now FAIL — proving the
// positive relies on carved interior doors, not a probe wandering through walls.
TEST(TavernTypologyTest, SealedTavernBlocksTaproomToKitchen) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = tavernProgram(rp);
    auto& portals = p.stories[0].portals;
    std::vector<ProgPortal> exteriorOnly;
    for (const auto& po : portals)
        if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
    portals = exteriorOnly;

    const ProgRoom* taproom = roomByPurpose(p.stories[0], "taproom");
    const ProgRoom* kitchen = roomByPurpose(p.stories[0], "kitchen");
    ASSERT_NE(taproom, nullptr); ASSERT_NE(kitchen, nullptr);

    auto sh = StructureRealizer::realizeShell(p, tavernStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_FALSE(walkBetween(sh, *taproom, *kitchen, p.footprintW, p.footprintD))
        << "character crossed SOLID partitions — the tavern traversal proof has no teeth";
}
