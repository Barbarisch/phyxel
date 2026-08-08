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
// BUTCHER (the Shambles) — a commercial typology with NEW grounded assets (a chopping block + a meat
// rail of iron hooks). Proves it is a real, grounded, navigable structure whose defining space — a
// shopfront with the chopping block + a meat rail, connected to a back slaughter/storage room — holds
// on the realized output. Grounding: medieval Shambles record (historyofyork.org.uk; asparadventures.com;
// buildingourpast.com) + burgage frontage (Tait PSAS 138). See room_program.json `butcher`.
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

StyleProfile shopStyle() {
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

BuildingProgram butcherProgram(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "butcher"; p.style = "timber_cottage"; p.footprintW = 8; p.footprintD = 6;
    p.substructure = "slab"; p.typology = "butcher";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 23u, rp);
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

TEST(ButcherTypologyTest, ShippedCanonHasGroundedButcher) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";

    const RoomProgram* b = reg.get("butcher");
    ASSERT_NE(b, nullptr) << "no 'butcher' typology in the shipped canon";
    EXPECT_GT(b->bayLength, 0.0) << "butcher not bay-driven";
    EXPECT_GT(b->bays, 0);
    EXPECT_FALSE(b->source.empty() && b->sources.empty()) << "butcher is UNSOURCED";

    bool hasShambles = false, hasBack = false;
    for (const auto& r : b->rooms) {
        if (r.purpose == "shambles") hasShambles = true;
        if (r.purpose == "service")  hasBack = true;
    }
    EXPECT_TRUE(hasShambles) << "butcher has no shopfront (shambles)";
    EXPECT_TRUE(hasBack) << "butcher has no back slaughter/storage room";
}

TEST(ButcherTypologyTest, ShamblesRecipeHasBlockAndRailAndAssetsResolve) {
    const auto req = FurniturePlacer::requiredFurniture("shambles");
    bool wantsBlock = false, wantsRail = false;
    for (const auto& t : req) { if (t == "chopping_block") wantsBlock = true; if (t == "meat_rail") wantsRail = true; }
    EXPECT_TRUE(wantsBlock) << "the shambles recipe has no chopping block";
    EXPECT_TRUE(wantsRail) << "the shambles recipe has no meat rail";

    auto fileExists = [](const std::string& name) {
        for (const char* d : {"resources/templates/", "../resources/templates/",
                              "../../resources/templates/", "../../../resources/templates/"}) {
            // Asset-library reorg 2026-08-07: templates live in category subdirs.
            for (const char* cat : {"furniture/", "architecture/", "items/", ""}) {
                std::ifstream f(std::string(d) + cat + name + ".voxel");
                if (f.good()) return true;
            }
        }
        return false;
    };
    for (const char* type : {"chopping_block", "meat_rail", "counter", "barrel"}) {
        ASSERT_FALSE(FurnitureCatalog::templateFor(type).empty()) << type << " unmapped in the catalog";
        EXPECT_TRUE(fileExists(FurnitureCatalog::templateFor(type))) << type << " template missing on disk";
    }
}

// F: the chopping block (the defining work fixture) lands in the shopfront (shambles) room.
TEST(ButcherTypologyTest, ChoppingBlockInShopfront) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("butcher");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = butcherProgram(rp);
    const ProgRoom* shop = roomById(p.stories[0], "shopfront");
    ASSERT_NE(shop, nullptr) << "butcher typology didn't produce its shopfront";

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const int floorY = sh.floorTopByStory.empty() ? 12 : sh.floorTopByStory[0];
    const auto placements = FurniturePlacer::furnish(p.stories[0], glm::ivec3(0, 0, 0), floorY);

    const auto block = firstOfType(placements, "chopping_block");
    ASSERT_FALSE(block.type.empty()) << "no chopping block was placed";
    const bool inShop = block.worldPos.x >= shop->rect.x && block.worldPos.x < shop->rect.x + shop->rect.w &&
                        block.worldPos.z >= shop->rect.z && block.worldPos.z < shop->rect.z + shop->rect.d;
    EXPECT_TRUE(inShop) << "the chopping block is not in the shopfront room";
}

TEST(ButcherTypologyTest, CharacterWalksShopfrontToBackroom) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("butcher");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = butcherProgram(rp);
    const ProgRoom* shop = roomById(p.stories[0], "shopfront");
    const ProgRoom* back = roomById(p.stories[0], "backroom");
    ASSERT_NE(shop, nullptr); ASSERT_NE(back, nullptr);

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_TRUE(walkBetween(sh, *shop, *back, p.footprintW, p.footprintD))
        << "character could NOT walk shopfront -> backroom — the butcher interior is not passable";
}

TEST(ButcherTypologyTest, SealedButcherBlocksShopfrontToBackroom) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("butcher");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = butcherProgram(rp);
    auto& portals = p.stories[0].portals;
    std::vector<ProgPortal> exteriorOnly;
    for (const auto& po : portals)
        if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
    portals = exteriorOnly;

    const ProgRoom* shop = roomById(p.stories[0], "shopfront");
    const ProgRoom* back = roomById(p.stories[0], "backroom");
    ASSERT_NE(shop, nullptr); ASSERT_NE(back, nullptr);

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_FALSE(walkBetween(sh, *shop, *back, p.footprintW, p.footprintD))
        << "character crossed SOLID partitions — the butcher traversal proof has no teeth";
}
