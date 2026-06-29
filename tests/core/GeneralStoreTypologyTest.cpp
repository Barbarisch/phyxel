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
// GENERAL STORE / MERCHANT'S SHOP — a non-residential commercial typology after the tavern + smithy.
// A town needs shops. This proves a general store is a real, grounded, navigable structure AND that its
// defining relationship — a street-facing SALESROOM (sales counter + shelved goods) connected to a rear
// STOREROOM (stock) — holds on the realized output, not just "a box with a counter".
//   (data)  the shipped canon carries a `general_store` typology, grounded (burgage frontage), 2-bay
//           (salesroom + storeroom), single-story, sourced;
//   (wiring) the salesroom recipe places a COUNTER + SHELVING (back_bar); the assets resolve to templates;
//   (L3)     a character-box WALKS the salesroom <-> storeroom on the realized voxels (sealed = teeth).
// Grounding: medieval urban-shop / burgage-plot record (RuralHistoria; burgageplots.info) + documented
// burgage frontage 5.5-17 m (PSAS, journals.socantscot.org). See room_program.json `general_store`.
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

// Autofill the general_store typology onto an 8x6 single-story footprint (2 bays x ~4 m, 6 m wide —
// the narrow end of the documented burgage frontage range).
BuildingProgram storeProgram(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "store"; p.style = "timber_cottage"; p.footprintW = 8; p.footprintD = 6;
    p.substructure = "slab"; p.typology = "general_store";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 13u, rp);
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

// The shipped canon must carry a grounded `general_store` typology: a salesroom + a storeroom,
// bay-driven, single-story, with a source.
TEST(GeneralStoreTypologyTest, ShippedCanonHasGroundedGeneralStore) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";

    const RoomProgram* g = reg.get("general_store");
    ASSERT_NE(g, nullptr) << "no 'general_store' typology in the shipped canon";
    EXPECT_GT(g->bayLength, 0.0) << "general_store not bay-driven";
    EXPECT_GT(g->bays, 0);
    EXPECT_FALSE(g->source.empty() && g->sources.empty()) << "general_store is UNSOURCED";

    bool hasSales = false, hasStore = false;
    for (const auto& r : g->rooms) {
        if (r.purpose == "salesroom") hasSales = true;
        if (r.purpose == "service")   hasStore = true;   // the rear storeroom
    }
    EXPECT_TRUE(hasSales) << "general_store has no salesroom (the shopfront)";
    EXPECT_TRUE(hasStore) << "general_store has no storeroom/service end";
}

// The salesroom recipe places the DEFINING fixtures (a sales counter + shelving), and they resolve to
// real, loadable templates (no silent drop).
TEST(GeneralStoreTypologyTest, SalesRecipeHasCounterAndShelvingAndAssetsResolve) {
    const auto req = FurniturePlacer::requiredFurniture("salesroom");
    bool wantsCounter = false, wantsShelf = false;
    for (const auto& t : req) { if (t == "counter") wantsCounter = true; if (t == "back_bar") wantsShelf = true; }
    EXPECT_TRUE(wantsCounter) << "the salesroom recipe has no sales counter";
    EXPECT_TRUE(wantsShelf) << "the salesroom recipe has no shelving (back_bar)";

    auto fileExists = [](const std::string& name) {
        for (const char* d : {"resources/templates/", "../resources/templates/",
                              "../../resources/templates/", "../../../resources/templates/"}) {
            std::ifstream f(std::string(d) + name + ".voxel");
            if (f.good()) return true;
        }
        return false;
    };
    for (const char* type : {"counter", "back_bar", "barrel", "chest"}) {
        ASSERT_FALSE(FurnitureCatalog::templateFor(type).empty()) << type << " unmapped in the catalog";
        EXPECT_TRUE(fileExists(FurnitureCatalog::templateFor(type))) << type << " template missing on disk";
    }
}

// F: the sales counter actually lands in the SALESROOM (the shopfront), and stock (a barrel) lands in
// the rear storeroom — the program's customer/stock split holds on the placed furniture.
TEST(GeneralStoreTypologyTest, CounterInSalesroomStockInStoreroom) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("general_store");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = storeProgram(rp);
    const ProgRoom* sales = roomById(p.stories[0], "salesroom");
    const ProgRoom* store = roomById(p.stories[0], "storeroom");
    ASSERT_NE(sales, nullptr); ASSERT_NE(store, nullptr) << "general_store typology didn't produce its rooms";

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const int floorY = sh.floorTopByStory.empty() ? 12 : sh.floorTopByStory[0];
    const auto placements = FurniturePlacer::furnish(p.stories[0], glm::ivec3(0, 0, 0), floorY);

    const auto counter = firstOfType(placements, "counter");
    ASSERT_FALSE(counter.type.empty()) << "no sales counter was placed";
    auto inRoom = [](const FurniturePlacement& f, const ProgRoom& r) {
        return f.worldPos.x >= r.rect.x && f.worldPos.x < r.rect.x + r.rect.w &&
               f.worldPos.z >= r.rect.z && f.worldPos.z < r.rect.z + r.rect.d;
    };
    EXPECT_TRUE(inRoom(counter, *sales))
        << "sales counter at (" << counter.worldPos.x << "," << counter.worldPos.z
        << ") is not in the salesroom rect [" << sales->rect.x << "," << sales->rect.z << " "
        << sales->rect.w << "x" << sales->rect.d << "]";
}

// L3: a character-box must WALK the salesroom <-> storeroom on the realized voxels.
TEST(GeneralStoreTypologyTest, CharacterWalksSalesroomToStoreroom) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("general_store");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = storeProgram(rp);
    const ProgRoom* sales = roomById(p.stories[0], "salesroom");
    const ProgRoom* store = roomById(p.stories[0], "storeroom");
    ASSERT_NE(sales, nullptr); ASSERT_NE(store, nullptr);

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_TRUE(walkBetween(sh, *sales, *store, p.footprintW, p.footprintD))
        << "character could NOT walk salesroom -> storeroom — the shop interior is not passable";
}

// TEETH: seal the interior (exterior door only). The walk must now FAIL — the positive relies on a
// carved interior door, not a probe wandering through walls.
TEST(GeneralStoreTypologyTest, SealedStoreBlocksSalesroomToStoreroom) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("general_store");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = storeProgram(rp);
    auto& portals = p.stories[0].portals;
    std::vector<ProgPortal> exteriorOnly;
    for (const auto& po : portals)
        if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
    portals = exteriorOnly;

    const ProgRoom* sales = roomById(p.stories[0], "salesroom");
    const ProgRoom* store = roomById(p.stories[0], "storeroom");
    ASSERT_NE(sales, nullptr); ASSERT_NE(store, nullptr);

    auto sh = StructureRealizer::realizeShell(p, shopStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_FALSE(walkBetween(sh, *sales, *store, p.footprintW, p.footprintD))
        << "character crossed SOLID partitions — the general_store traversal proof has no teeth";
}
