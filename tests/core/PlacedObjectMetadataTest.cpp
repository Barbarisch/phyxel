#include <gtest/gtest.h>

#include "core/PlacedObjectManager.h"

using namespace Phyxel::Core;

// ============================================================================
// Fixture-semantics persistence (step 1). A placed fixture's semantic identity is
// stored in PlacedObject::metadata["fixture"]. saveToDb() persists by dumping
// toJson(); loadFromDb() restores via fromJson() — so the metadata surviving an
// engine restart rests entirely on this serialization round-trip. This test makes
// the "survived a restart" claim a falsifiable assertion instead of a log
// observation (the gap the auditor flagged on the step-1 commit).
// ============================================================================

TEST(PlacedObjectMetadataTest, FixtureLabelSurvivesJsonRoundTrip) {
    PlacedObject obj;
    obj.id = "bed_single_7";
    obj.templateName = "bed_single";
    obj.category = "template";
    obj.position = {45, 19, 47};
    obj.rotation = 180;
    obj.metadata["fixture"] = {
        {"structure", "house_21"}, {"room", "bc2"}, {"purpose", "bedchamber"},
        {"purpose_index", 1}, {"type", "bed"}, {"story", 0}
    };

    // saveToDb(): toJson().dump();  loadFromDb(): fromJson() — exercise that exact path.
    const PlacedObject restored = PlacedObject::fromJson(obj.toJson());

    ASSERT_TRUE(restored.metadata.contains("fixture"))
        << "metadata['fixture'] dropped on serialization — it would not survive a restart";
    const auto& fx = restored.metadata["fixture"];
    EXPECT_EQ(fx.value("purpose", ""), "bedchamber");
    EXPECT_EQ(fx.value("purpose_index", -1), 1) << "the ordinal that distinguishes the 2nd bedroom";
    EXPECT_EQ(fx.value("room", ""), "bc2");
    EXPECT_EQ(fx.value("type", ""), "bed");
    EXPECT_EQ(fx.value("structure", ""), "house_21");
    // and the pose round-trips too
    EXPECT_EQ(restored.rotation, 180);
    EXPECT_EQ(restored.position.z, 47);
}

// TEETH: an empty metadata blob round-trips as empty (not spuriously gaining a 'fixture' key) — so
// the positive test above is asserting a real write, not a default that's always present.
TEST(PlacedObjectMetadataTest, NoFixtureKeyWhenUntagged) {
    PlacedObject obj;
    obj.id = "barrel_9";
    obj.templateName = "barrel";
    const PlacedObject restored = PlacedObject::fromJson(obj.toJson());
    EXPECT_FALSE(restored.metadata.contains("fixture"));
}
