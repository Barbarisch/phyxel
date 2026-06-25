#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/FurniturePlacer.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// Fixture semantic identity (step 1 of conversational fine-tuning). For a session
// to honor "move the 2nd bedroom's bed", each placed fixture must remember which
// room/role it is — not just its template name. labelFixtures() derives that from
// the program: room purpose + the room's ordinal among same-purpose rooms.
// ============================================================================

namespace {
ProgStory story(const char* s) { return ProgStory::fromJson(nlohmann::json::parse(s)); }

// Find the label of the first fixture of `type` in the room at same-purpose ordinal `purposeIdx`.
const FixtureLabel* bedIn(const std::vector<FixtureLabel>& labels, const std::string& purpose,
                          int purposeIdx, const std::string& type) {
    for (const auto& L : labels)
        if (L.purpose == purpose && L.purposeIndex == purposeIdx && L.type == type) return &L;
    return nullptr;
}
} // namespace

// THE addressing case: two bedchambers. Each one's bed must be labeled with a DISTINCT
// purposeIndex (0 and 1), so "the 2nd bedroom's bed" resolves unambiguously. (Red on the stub that
// gives every room purposeIndex 0 — both beds collide at index 0; green once the ordinal is real.)
TEST(FixtureLabelTest, TwoBedroomsGetDistinctPurposeOrdinals) {
    const auto s = story(R"json({"height":3,"rooms":[
        {"id":"bc1","rect":[0,0,6,5],"purpose":"bedchamber"},
        {"id":"bc2","rect":[0,5,6,5],"purpose":"bedchamber"}
    ]})json");
    const auto placements = FurniturePlacer::furnish(s, glm::ivec3(0, 0, 0), 10);
    const auto labels = FurniturePlacer::labelFixtures(s, placements);
    ASSERT_EQ(labels.size(), placements.size());

    const FixtureLabel* bed0 = bedIn(labels, "bedchamber", 0, "bed");
    const FixtureLabel* bed1 = bedIn(labels, "bedchamber", 1, "bed");
    ASSERT_NE(bed0, nullptr) << "1st bedroom's bed has no label at purposeIndex 0";
    ASSERT_NE(bed1, nullptr) << "2nd bedroom's bed has no label at purposeIndex 1";
    EXPECT_EQ(bed0->room, "bc1");
    EXPECT_EQ(bed1->room, "bc2") << "the 2nd bedroom's bed must resolve to bc2, not collide with bc1";
}

// Each label carries the room's purpose and matches the placement's room/type 1:1 (order-aligned).
TEST(FixtureLabelTest, LabelsCarryPurposeAndAlignWithPlacements) {
    const auto s = story(R"json({"height":3,"rooms":[
        {"id":"k","rect":[0,0,5,5],"purpose":"kitchen"},
        {"id":"bc","rect":[5,0,5,5],"purpose":"bedchamber"}
    ]})json");
    const auto placements = FurniturePlacer::furnish(s, glm::ivec3(0, 0, 0), 10);
    const auto labels = FurniturePlacer::labelFixtures(s, placements);
    ASSERT_EQ(labels.size(), placements.size());
    for (size_t i = 0; i < placements.size(); ++i) {
        EXPECT_EQ(labels[i].room, placements[i].room);
        EXPECT_EQ(labels[i].type, placements[i].type);
        if (placements[i].room == "k")  EXPECT_EQ(labels[i].purpose, "kitchen");
        if (placements[i].room == "bc") EXPECT_EQ(labels[i].purpose, "bedchamber");
        // single room per purpose here -> every ordinal is 0
        EXPECT_EQ(labels[i].purposeIndex, 0);
    }
}
