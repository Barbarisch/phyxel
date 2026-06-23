#include <gtest/gtest.h>

#include <fstream>

#include "core/RoomProgram.h"

using namespace Phyxel::Core;

TEST(RoomProgramTest, LoadsTypologyWithBaysAndSources) {
    auto j = nlohmann::json::parse(R"({
        "programs": {
            "hall_house": {
                "description": "open hall flanked by service + solar",
                "width_min": 6, "width_max": 8, "bay_length": 4, "bays": 4, "proportion_max": 3.0,
                "rooms": [
                    { "id": "service", "purpose": "service", "bays": 1 },
                    { "id": "hall", "purpose": "hall", "bays": 2 },
                    { "id": "solar", "purpose": "solar", "bays": 1 }
                ],
                "source": "Wikipedia Hall house",
                "sources": { "bays": "four-bay cruck, open hall = middle 2 bays" }
            }
        }
    })");
    RoomProgramRegistry reg;
    ASSERT_TRUE(reg.loadFromJson(j));
    const RoomProgram* hh = reg.get("hall_house");
    ASSERT_NE(hh, nullptr);
    EXPECT_EQ(hh->bays, 4);
    EXPECT_DOUBLE_EQ(hh->bayLength, 4.0);
    EXPECT_DOUBLE_EQ(hh->widthMax, 8.0);
    ASSERT_EQ(hh->rooms.size(), 3u);
    EXPECT_EQ(hh->rooms[1].id, "hall");
    EXPECT_DOUBLE_EQ(hh->rooms[1].bays, 2.0);   // open hall = 2 middle bays
    EXPECT_TRUE(hh->hasSource("bays"));
    EXPECT_FALSE(hh->source.empty());
}

TEST(RoomProgramTest, DefaultTypologyForFunctionIsConservative) {
    EXPECT_EQ(RoomProgramRegistry::defaultTypologyForFunction("house"), "hall_house");
    EXPECT_EQ(RoomProgramRegistry::defaultTypologyForFunction("cottage"), "croft");
    EXPECT_EQ(RoomProgramRegistry::defaultTypologyForFunction("manor"), "manor_hall");
    // non-dwelling functions get no typology -> the room gate is skipped, not guessed
    EXPECT_EQ(RoomProgramRegistry::defaultTypologyForFunction("church"), "");
    EXPECT_EQ(RoomProgramRegistry::defaultTypologyForFunction("tower"), "");
}

// The shipped canon must be present, grounded (every typology cites a source),
// and bay-driven (medieval frame), NOT modern area tables.
TEST(RoomProgramTest, ShippedCanonIsGroundedAndBayDriven) {
    const char* candidates[] = {
        "resources/room_program.json",
        "../resources/room_program.json",
        "../../resources/room_program.json",
        "../../../resources/room_program.json",
    };
    RoomProgramRegistry reg;
    bool found = false;
    for (const char* p : candidates) {
        std::ifstream f(p);
        if (f.good()) { found = reg.loadFromFile(p); break; }
    }
    if (!found) GTEST_SKIP() << "resources/room_program.json not reachable from CWD";

    for (const char* id : {"croft", "longhouse", "hall_house", "manor_hall"}) {
        const RoomProgram* p = reg.get(id);
        ASSERT_NE(p, nullptr) << "missing typology: " << id;
        EXPECT_GT(p->bayLength, 0.0) << id << " has no bay length (not bay-driven)";
        EXPECT_GT(p->bays, 0) << id << " has no bays";
        EXPECT_FALSE(p->source.empty() && p->sources.empty()) << id << " is UNSOURCED";
    }
    // Sanity: the hall house's open hall is the middle two bays.
    const RoomProgram* hh = reg.get("hall_house");
    bool hallIsTwoBays = false;
    for (const auto& r : hh->rooms) if (r.purpose == "hall" && r.bays == 2.0) hallIsTwoBays = true;
    EXPECT_TRUE(hallIsTwoBays);
}
