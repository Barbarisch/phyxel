#include <gtest/gtest.h>

#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// "Passable house" — L3 PHYSICAL navigability. The validator proves rooms are
// TOPOLOGICALLY reachable (a portal edge exists); this proves a character-box can
// actually WALK service -> hall -> solar through the REALIZED voxels (interior
// walls built, doors carved wide/tall enough). Same TraversalProbe used for
// stairs/doors. Without this, a "house" can validate yet be a sealed box.
// ============================================================================

namespace {
RoomProgram hallHouse() {
    RoomProgram rp;
    rp.name = "hall_house"; rp.bays = 4; rp.bayLength = 4; rp.widthMin = 6; rp.widthMax = 8;
    rp.rooms = {{"service", "service", 1.0}, {"hall", "hall", 2.0}, {"solar", "solar", 1.0}};
    return rp;
}
StyleProfile cottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}
// Autofill a hall_house onto a 16x7 footprint (service|hall|solar along the length).
BuildingProgram hallHouseProgram() {
    BuildingProgram p;
    p.name = "hh"; p.style = "timber_cottage"; p.footprintW = 16; p.footprintD = 7;
    p.substructure = "slab"; p.typology = "hall_house";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    const RoomProgram rp = hallHouse();
    autofillRoomLayout(p, 1u, &rp);
    return p;
}
const ProgRoom* roomByPurpose(const ProgStory& s, const std::string& purpose) {
    for (const auto& r : s.rooms) if (r.purpose == purpose) return &r;
    return nullptr;
}
// Walk the character-box from one room's centre to another's, across the realized interior.
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
} // namespace

// A character-box must walk from the service end all the way to the solar (bedroom) — across the
// hall and THROUGH both interior doors — on the real realized voxels. This is the actual "passable".
TEST(TypologyHouseTraversalTest, CharacterWalksServiceToSolarThroughHall) {
    const BuildingProgram p = hallHouseProgram();
    const ProgRoom* service = roomByPurpose(p.stories[0], "service");
    const ProgRoom* solar   = roomByPurpose(p.stories[0], "solar");
    ASSERT_NE(service, nullptr); ASSERT_NE(solar, nullptr) << "typology didn't produce the rooms";

    auto sh = StructureRealizer::realizeShell(p, cottageStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_TRUE(walkBetween(sh, *service, *solar, p.footprintW, p.footprintD))
        << "character could NOT walk service -> solar — the generated house is not actually passable";
}

// TEETH: seal the interior (drop the interior doors, keep the exterior entrance). The SAME walk must
// now FAIL — proving the positive test relies on the carved doors, not a probe that wanders walls.
TEST(TypologyHouseTraversalTest, SealedInteriorBlocksRoomToRoom) {
    BuildingProgram p = hallHouseProgram();
    auto& portals = p.stories[0].portals;
    std::vector<ProgPortal> exteriorOnly;
    for (const auto& po : portals)
        if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
    portals = exteriorOnly;   // interior doors removed -> solid partitions

    const ProgRoom* service = roomByPurpose(p.stories[0], "service");
    const ProgRoom* solar   = roomByPurpose(p.stories[0], "solar");
    ASSERT_NE(service, nullptr); ASSERT_NE(solar, nullptr);

    auto sh = StructureRealizer::realizeShell(p, cottageStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_FALSE(walkBetween(sh, *service, *solar, p.footprintW, p.footprintD))
        << "character crossed SOLID interior partitions — the traversal proof has no teeth";
}
