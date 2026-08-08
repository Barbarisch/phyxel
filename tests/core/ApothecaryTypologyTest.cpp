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
// APOTHECARY — a commercial typology whose signature is a DISPENSARY: a dispensing counter with
// shelves of jars/bottles behind it, connected to a rear storeroom. Reuses grounded assets (counter,
// back_bar shelving which carries bottles, coffer, candelabra). Proves it is a real, grounded,
// navigable structure with its dispensary fixtures placed and its interior passable.
// Grounding: apothecary-shop record (larsdatter.com; manyheadedmonster.com 'The World in a Jar') +
// burgage frontage (Tait PSAS 138). See room_program.json `apothecary`.
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

BuildingProgram apothecaryProgram(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "apothecary"; p.style = "timber_cottage"; p.footprintW = 8; p.footprintD = 6;
    p.substructure = "slab"; p.typology = "apothecary";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 19u, rp);
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

TEST(ApothecaryTypologyTest, ShippedCanonHasGroundedApothecary) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";

    const RoomProgram* a = reg.get("apothecary");
    ASSERT_NE(a, nullptr) << "no 'apothecary' typology in the shipped canon";
    EXPECT_GT(a->bayLength, 0.0) << "apothecary not bay-driven";
    EXPECT_GT(a->bays, 0);
    EXPECT_FALSE(a->source.empty() && a->sources.empty()) << "apothecary is UNSOURCED";

    bool hasDispensary = false, hasStore = false;
    for (const auto& r : a->rooms) {
        if (r.purpose == "dispensary") hasDispensary = true;
        if (r.purpose == "service")    hasStore = true;
    }
    EXPECT_TRUE(hasDispensary) << "apothecary has no dispensary";
    EXPECT_TRUE(hasStore) << "apothecary has no rear storeroom";
}

TEST(ApothecaryTypologyTest, DispensaryRecipeHasCounterAndShelvingAndAssetsResolve) {
    const auto req = FurniturePlacer::requiredFurniture("dispensary");
    bool wantsCounter = false, wantsShelf = false;
    for (const auto& t : req) { if (t == "counter") wantsCounter = true; if (t == "back_bar") wantsShelf = true; }
    EXPECT_TRUE(wantsCounter) << "the dispensary recipe has no dispensing counter";
    EXPECT_TRUE(wantsShelf) << "the dispensary recipe has no shelving (jars/bottles)";

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
    for (const char* type : {"counter", "back_bar", "chest", "candle_stand"}) {
        ASSERT_FALSE(FurnitureCatalog::templateFor(type).empty()) << type << " unmapped in the catalog";
        EXPECT_TRUE(fileExists(FurnitureCatalog::templateFor(type))) << type << " template missing on disk";
    }
}

TEST(ApothecaryTypologyTest, CounterInDispensary) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("apothecary");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = apothecaryProgram(rp);
    const ProgRoom* disp = roomById(p.stories[0], "dispensary");
    ASSERT_NE(disp, nullptr) << "apothecary typology didn't produce its dispensary";

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const int floorY = sh.floorTopByStory.empty() ? 12 : sh.floorTopByStory[0];
    const auto placements = FurniturePlacer::furnish(p.stories[0], glm::ivec3(0, 0, 0), floorY);

    const auto counter = firstOfType(placements, "counter");
    ASSERT_FALSE(counter.type.empty()) << "no dispensing counter was placed";
    const bool inDisp = counter.worldPos.x >= disp->rect.x && counter.worldPos.x < disp->rect.x + disp->rect.w &&
                        counter.worldPos.z >= disp->rect.z && counter.worldPos.z < disp->rect.z + disp->rect.d;
    EXPECT_TRUE(inDisp) << "dispensing counter is not in the dispensary room";
}

TEST(ApothecaryTypologyTest, CharacterWalksDispensaryToStoreroom) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("apothecary");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = apothecaryProgram(rp);
    const ProgRoom* disp = roomById(p.stories[0], "dispensary");
    const ProgRoom* store = roomById(p.stories[0], "storeroom");
    ASSERT_NE(disp, nullptr); ASSERT_NE(store, nullptr);

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_TRUE(walkBetween(sh, *disp, *store, p.footprintW, p.footprintD))
        << "character could NOT walk dispensary -> storeroom — the apothecary interior is not passable";
}

TEST(ApothecaryTypologyTest, SealedApothecaryBlocksDispensaryToStoreroom) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("apothecary");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = apothecaryProgram(rp);
    auto& portals = p.stories[0].portals;
    std::vector<ProgPortal> exteriorOnly;
    for (const auto& po : portals)
        if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
    portals = exteriorOnly;

    const ProgRoom* disp = roomById(p.stories[0], "dispensary");
    const ProgRoom* store = roomById(p.stories[0], "storeroom");
    ASSERT_NE(disp, nullptr); ASSERT_NE(store, nullptr);

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_FALSE(walkBetween(sh, *disp, *store, p.footprintW, p.footprintD))
        << "character crossed SOLID partitions — the apothecary traversal proof has no teeth";
}
