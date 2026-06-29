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
// BAKERY — a commercial typology with a VENTED process fixture (the masonry bread oven), like the
// smithy's forge. Proves a bakery is a real, grounded, navigable structure whose defining space — a
// BAKEHOUSE with the oven on an exterior (ventable) wall, connected to a street SALESROOM — holds on
// the realized output.
//   (data)  the shipped canon carries a `bakery` typology, grounded, 2-bay (bakehouse + salesroom);
//   (wiring) the bakehouse recipe places an OVEN (oven_bread) + a kneading counter; assets resolve;
//   (L3)    a character-box WALKS bakehouse <-> salesroom (sealed = teeth);
//   (F)     the oven lands in the bakehouse, on a building EXTERIOR wall (so it can vent / chimney).
// Grounding: bakehouse + baker's-shop record (Wikipedia Bakehouse; Assize of Bread) + commercial dome
// oven ~0.9 m (williamrubel.com; object_dimensions 'oven_bread') + burgage frontage (Tait PSAS 138).
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

StyleProfile bakeryStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}

const ProgRoom* roomById(const ProgStory& s, const std::string& id) {
    for (const auto& r : s.rooms) if (r.id == id) return &r;
    return nullptr;
}

BuildingProgram bakeryProgram(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "bakery"; p.style = "timber_cottage"; p.footprintW = 8; p.footprintD = 6;
    p.substructure = "slab"; p.typology = "bakery";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 17u, rp);
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

FurniturePlacement firstOfType(const std::vector<FurniturePlacement>& ps, const std::string& type) {
    for (const auto& p : ps) if (p.type == type) return p;
    return {};
}
} // namespace

TEST(BakeryTypologyTest, ShippedCanonHasGroundedBakery) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";

    const RoomProgram* b = reg.get("bakery");
    ASSERT_NE(b, nullptr) << "no 'bakery' typology in the shipped canon";
    EXPECT_GT(b->bayLength, 0.0) << "bakery not bay-driven";
    EXPECT_GT(b->bays, 0);
    EXPECT_FALSE(b->source.empty() && b->sources.empty()) << "bakery is UNSOURCED";

    bool hasBakehouse = false, hasSales = false;
    for (const auto& r : b->rooms) {
        if (r.purpose == "bakehouse") hasBakehouse = true;
        if (r.purpose == "salesroom") hasSales = true;
    }
    EXPECT_TRUE(hasBakehouse) << "bakery has no bakehouse (the oven room)";
    EXPECT_TRUE(hasSales) << "bakery has no salesroom/shopfront";
}

TEST(BakeryTypologyTest, BakehouseRecipeHasOvenAndAssetsResolve) {
    const auto req = FurniturePlacer::requiredFurniture("bakehouse");
    bool wantsOven = false, wantsCounter = false;
    for (const auto& t : req) { if (t == "oven_bread") wantsOven = true; if (t == "counter") wantsCounter = true; }
    EXPECT_TRUE(wantsOven) << "the bakehouse recipe has no bread oven";
    EXPECT_TRUE(wantsCounter) << "the bakehouse recipe has no kneading counter";

    auto fileExists = [](const std::string& name) {
        for (const char* d : {"resources/templates/", "../resources/templates/",
                              "../../resources/templates/", "../../../resources/templates/"}) {
            std::ifstream f(std::string(d) + name + ".voxel");
            if (f.good()) return true;
        }
        return false;
    };
    for (const char* type : {"oven_bread", "counter", "barrel"}) {
        ASSERT_FALSE(FurnitureCatalog::templateFor(type).empty()) << type << " unmapped in the catalog";
        EXPECT_TRUE(fileExists(FurnitureCatalog::templateFor(type))) << type << " template missing on disk";
    }
}

// F: the oven lands IN the bakehouse and ON a building EXTERIOR wall (so it can vent / chimney).
TEST(BakeryTypologyTest, OvenInBakehouseOnExteriorWall) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("bakery");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = bakeryProgram(rp);
    const ProgRoom* bake = roomById(p.stories[0], "bakehouse");
    ASSERT_NE(bake, nullptr) << "bakery typology didn't produce its bakehouse";

    auto sh = StructureRealizer::realizeShell(p, bakeryStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const int floorY = sh.floorTopByStory.empty() ? 12 : sh.floorTopByStory[0];
    const auto placements = FurniturePlacer::furnish(p.stories[0], glm::ivec3(0, 0, 0), floorY);

    const auto oven = firstOfType(placements, "oven_bread");
    ASSERT_FALSE(oven.type.empty()) << "no bread oven was placed in the bakehouse";
    auto inRoom = [](const FurniturePlacement& f, const ProgRoom& r) {
        return f.worldPos.x >= r.rect.x && f.worldPos.x < r.rect.x + r.rect.w &&
               f.worldPos.z >= r.rect.z && f.worldPos.z < r.rect.z + r.rect.d;
    };
    EXPECT_TRUE(inRoom(oven, *bake)) << "the oven is not in the bakehouse room";
    const bool onPerimeter = oven.worldPos.x <= 1 || oven.worldPos.x >= p.footprintW - 2 ||
                             oven.worldPos.z <= 1 || oven.worldPos.z >= p.footprintD - 2;
    EXPECT_TRUE(onPerimeter)
        << "oven at (" << oven.worldPos.x << "," << oven.worldPos.z << ") is not on a building exterior "
        << "wall (W=" << p.footprintW << ",D=" << p.footprintD << ") — it cannot vent";
}

TEST(BakeryTypologyTest, CharacterWalksBakehouseToSalesroom) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("bakery");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = bakeryProgram(rp);
    const ProgRoom* bake = roomById(p.stories[0], "bakehouse");
    const ProgRoom* sales = roomById(p.stories[0], "salesroom");
    ASSERT_NE(bake, nullptr); ASSERT_NE(sales, nullptr);

    auto sh = StructureRealizer::realizeShell(p, bakeryStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_TRUE(walkBetween(sh, *sales, *bake, p.footprintW, p.footprintD))
        << "character could NOT walk salesroom -> bakehouse — the bakery interior is not passable";
}

TEST(BakeryTypologyTest, SealedBakeryBlocksSalesroomToBakehouse) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("bakery");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = bakeryProgram(rp);
    auto& portals = p.stories[0].portals;
    std::vector<ProgPortal> exteriorOnly;
    for (const auto& po : portals)
        if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
    portals = exteriorOnly;

    const ProgRoom* bake = roomById(p.stories[0], "bakehouse");
    const ProgRoom* sales = roomById(p.stories[0], "salesroom");
    ASSERT_NE(bake, nullptr); ASSERT_NE(sales, nullptr);

    auto sh = StructureRealizer::realizeShell(p, bakeryStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_FALSE(walkBetween(sh, *sales, *bake, p.footprintW, p.footprintD))
        << "character crossed SOLID partitions — the bakery traversal proof has no teeth";
}
