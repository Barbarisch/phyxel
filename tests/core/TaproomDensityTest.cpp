// Taproom furnishing DENSITY — "lots of tables and chairs", measured.
//
// The generated Prancing Pony taproom came out with ONE table, ZERO benches and
// four seats total. The recipe asks for far more than that (tavern_table and
// bench both carry per_area 15, stool per_area 12), so something between the
// recipe and the placed output is eating the reps.
//
// This runs the REAL generated tavern layout through the REAL placer with the
// REAL measured footprints and counts what comes out — no screenshots, no
// guessing at the mechanism. A public drinking room is the one interior where
// seat count IS the feature.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "core/BuildingProgram.h"
#include "core/FurnitureCatalog.h"
#include "core/FurniturePlacer.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

namespace fs = std::filesystem;
using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

bool loadCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}

// Footprints the way the forge derives them when no template manager is wired:
// from each asset's committed .metrics.json overall extent. Headless, but the
// SAME numbers the live build packs against.
std::map<std::string, Footprint> footprintsFromMetrics() {
    std::map<std::string, Footprint> out;
    for (const auto& type : FurnitureCatalog::mappedTypes()) {
        const std::string tmpl = FurnitureCatalog::templateFor(type);
        if (tmpl.empty()) continue;
        const fs::path p = fs::path("resources/templates") / (tmpl + ".metrics.json");
        if (!fs::exists(p)) continue;
        try {
            std::ifstream in(p);
            const nlohmann::json m = nlohmann::json::parse(in);
            if (!m.contains("overall_max") || !m["overall_max"].is_array() ||
                m["overall_max"].size() < 3)
                continue;
            const double ex = m["overall_max"][0].get<double>();
            const double ey = m["overall_max"][1].get<double>();
            const double ez = m["overall_max"][2].get<double>();
            Footprint fp = footprintFromExtents(ex, ez);
            // The TRUE micro extents matter: spanCellsOf reserves the placed
            // SPILL, not just the cube footprint, and `fits(span)` is what
            // actually rejects a second table. A rig that leaves microW/microD
            // at 0 sees no spill and cheerfully packs furniture the live build
            // cannot fit — it measured 4 tables while the tavern shipped 1.
            fp.microW = std::max(0, (int)std::lround(ex * 9.0) - 1);
            fp.microD = std::max(0, (int)std::lround(ez * 9.0) - 1);
            fp.microH = std::max(1, (int)std::lround(std::ceil(ey * 9.0)));
            out[type] = fp;
        } catch (...) {
        }
    }
    return out;
}

struct Furnished {
    std::vector<FurniturePlacement> placements;
    std::vector<UnplacedFixture> unplaced;
    int roomCells = 0;
    int stairRects = 0;
    int count(const std::string& type) const {
        int n = 0;
        for (const auto& p : placements) if (p.type == type) ++n;
        return n;
    }
};

// Build the real tavern ground floor and furnish it.
Furnished furnishTavernGroundFloor(int w, int d, unsigned seed = 99u) {
    RoomProgramRegistry reg;
    EXPECT_TRUE(loadCanon(reg));
    const RoomProgram* tavern = reg.get("tavern");
    EXPECT_NE(tavern, nullptr);

    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({w, d});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram p = BuildingProgram::fromJson(j);
    EXPECT_TRUE(autofillRoomLayout(p, seed, tavern));

    Furnished f;
    for (const auto& r : p.stories[0].rooms)
        if (r.purpose == "taproom") f.roomCells = r.rect.w * r.rect.d;
    // FAITHFUL RIG: the live build realizes the shell and furnishes via
    // furnishFromPlan, which derives wall thickness and the reserved stair wells
    // from the REALIZED AssemblyPlan. Calling the bare `furnish` with guessed
    // side-channel numbers gave 4 tables while the shipped tavern got 1 — the rig
    // has to be the real path or it measures a building that does not exist.
    StyleProfileRegistry styles;
    EXPECT_TRUE(styles.loadFromFile("resources/structure_styles.json"));
    const StyleProfile* sp = styles.get(p.style);
    EXPECT_NE(sp, nullptr) << "style '" << p.style << "' not found";
    auto shell = StructureRealizer::realizeShell(p, sp ? *sp : StyleProfile{});
    EXPECT_TRUE(shell.ok) << shell.error;
    f.stairRects = (int)FurniturePlacer::planStairRects(shell.plan, 0).size();
    f.placements = FurniturePlacer::furnishFromPlan(
        p.stories[0], 0, glm::ivec3(0, 0, 0), /*floorY=*/1, shell.plan,
        footprintsFromMetrics(), &f.unplaced, tavern->wealthTier);
    return f;
}

}  // namespace

// The measurement itself — reported even when it passes, so the number is on the
// record rather than inferred from a screenshot.
TEST(TaproomDensity, TheRealTaproomIsFurnishedForACrowd) {
    if (!fs::exists("resources/furnishing_recipes.json"))
        GTEST_SKIP() << "repo-root CWD required";
    FurniturePlacer::clearRecipes();
    ASSERT_TRUE(FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json"));

    const Furnished f = furnishTavernGroundFloor(7, 20);
    const int tables = f.count("tavern_table");
    const int benches = f.count("bench");
    const int stools = f.count("stool");
    const int barStools = f.count("bar_stool");
    const int seats = benches * 2 + stools + barStools;   // a bench seats ~2

    std::string got = "taproom " + std::to_string(f.roomCells) + " cells: tables=" +
                      std::to_string(tables) + " benches=" + std::to_string(benches) +
                      " stools=" + std::to_string(stools) + " bar_stools=" +
                      std::to_string(barStools) + " => seats=" + std::to_string(seats);
    for (const auto& u : f.unplaced) got += "\n  unplaced: " + u.type + " in " + u.room;
    std::cout << "[ MEASURED ] " << got << " (stair rects reserved: " << f.stairRects
              << ", kegs=" << f.count("keg") << ")\n";

    // The recipe asks for one table per 15 floor cells. A taproom big enough for
    // two must get two — a single showpiece table is the defect this pins.
    EXPECT_GE(tables, 2) << "the taproom seats a crowd, not one table. " << got;
    EXPECT_GE(benches, 1) << "a medieval taproom's default seat is the bench. " << got;
    EXPECT_GE(seats, 6) << "'lots of tables and chairs' means a room you can drink in. " << got;
}

// Density must SCALE: doubling the room must not leave the furniture count flat.
// (This is the invariant a per_area field exists to provide.)
TEST(TaproomDensity, SeatCountScalesWithRoomSize) {
    if (!fs::exists("resources/furnishing_recipes.json"))
        GTEST_SKIP() << "repo-root CWD required";
    FurniturePlacer::clearRecipes();
    ASSERT_TRUE(FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json"));

    const Furnished small = furnishTavernGroundFloor(7, 14);
    const Furnished big   = furnishTavernGroundFloor(7, 28);
    ASSERT_GT(big.roomCells, small.roomCells) << "the big taproom is not bigger";

    auto seatsOf = [](const Furnished& f) {
        return f.count("bench") * 2 + f.count("stool") + f.count("bar_stool");
    };
    EXPECT_GT(big.count("tavern_table"), small.count("tavern_table"))
        << "per_area is inert: " << small.roomCells << " cells -> "
        << small.count("tavern_table") << " tables, " << big.roomCells << " cells -> "
        << big.count("tavern_table") << " tables";
    EXPECT_GT(seatsOf(big), seatsOf(small))
        << "seating does not scale with the room";
    FurniturePlacer::clearRecipes();
}

// SEED SWEEP: the taproom must be furnished for EVERY layout the autofill can
// roll, not just the one the rig happened to pick. The shipped tavern got ZERO
// tables while a seed-99 rig measured four — a layout-dependent failure a
// single-seed test cannot see. Doors move with the seed, and a rank of tables
// that lands on a doorway cell places nothing at all.
TEST(TaproomDensity, EverySeedGetsATaproomYouCanSitIn) {
    if (!fs::exists("resources/furnishing_recipes.json"))
        GTEST_SKIP() << "repo-root CWD required";
    FurniturePlacer::clearRecipes();
    ASSERT_TRUE(FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json"));

    int worstTables = 99, worstSeats = 99;
    unsigned worstSeed = 0;
    std::string report;
    for (unsigned seed : {1u, 7u, 42u, 99u, 123u, 777u, 2026u, 31337u}) {
        const Furnished f = furnishTavernGroundFloor(7, 20, seed);
        const int tables = f.count("tavern_table");
        const int seats = f.count("bench") * 2 + f.count("stool") + f.count("bar_stool");
        report += "\n  seed " + std::to_string(seed) + ": tables=" + std::to_string(tables) +
                  " seats=" + std::to_string(seats);
        if (tables < worstTables) { worstTables = tables; worstSeed = seed; }
        worstSeats = std::min(worstSeats, seats);
    }
    std::cout << "[ MEASURED ] seed sweep:" << report << "\n";
    EXPECT_GE(worstTables, 2) << "seed " << worstSeed << " furnished a taproom with "
                              << worstTables << " table(s):" << report;
    EXPECT_GE(worstSeats, 6) << "some layout leaves nowhere to sit:" << report;
    FurniturePlacer::clearRecipes();
}
