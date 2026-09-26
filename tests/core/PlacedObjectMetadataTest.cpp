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

// The MICRO pose must survive save/load. Before this (2026-09-10) toJson dropped
// microAnchor/placedAtMicro, so every fixture loaded from a world DB lost the pose
// it was placed at: removal fell back to clearing whole cubes, and the loader's
// transition-marker intact check (own cells at the recorded pose) could never run -
// a hatch buried in the tavern floor read "already present" off the floor slab.
TEST(PlacedObjectMetadataTest, MicroPoseRoundTripsThroughJson) {
    Phyxel::Core::PlacedObject o;
    o.id = "trapdoor_1"; o.templateName = "trapdoor"; o.category = "template";
    o.position = {-27, 17, 9}; o.rotation = 90;
    o.placedAtMicro = true; o.microAnchor = {-243, 156, 81};
    const auto back = Phyxel::Core::PlacedObject::fromJson(o.toJson());
    EXPECT_TRUE(back.placedAtMicro);
    EXPECT_EQ(back.microAnchor, glm::ivec3(-243, 156, 81));
    EXPECT_EQ(back.rotation, 90);
    // A record from an older world (no micro fields) loads as cube-placed, not garbage.
    nlohmann::json legacy = o.toJson(); legacy.erase("placed_at_micro"); legacy.erase("micro_anchor");
    const auto old = Phyxel::Core::PlacedObject::fromJson(legacy);
    EXPECT_FALSE(old.placedAtMicro);
}

// ============================================================================
// Structure-light persistence (2026-09-26). LightManager lights are memory-only, so a
// saved settlement came back with every lamp and hearth UNLIT: CityBench C-25 registered
// 67 fixture lights at build and 0 after a relaunch (PerfProgram 2026-09 section 16.1
// fingerprint). The build now records each light on its structure's placed object and
// world load re-registers them via StructureBuildService::restoreLights. This pins the
// whole persisted path: record -> saveToDb -> loadFromDb (a NEW manager, as a relaunch)
// -> restore, with the exact parameters, plus the refusal/malformed accounting.
// ============================================================================

#include <sqlite3.h>
#include <cmath>
#include "core/StructureBuildService.h"

namespace {
struct CapturedLight { glm::vec3 p, c; float i, r; };
}

TEST(StructureLightPersistenceTest, LightsSurviveSaveAndLoadWithExactParameters) {
    using Phyxel::Core::StructureBuildService;
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    const std::vector<CapturedLight> built = {
        {{10.5f, 20.25f, -30.5f}, {1.0f, 0.62f, 0.3f}, 2.5f, 8.0f},     // hearth
        {{-4.0f, 17.75f, 6.5f}, {1.0f, 0.8f, 0.55f}, 1.2f, 5.5f}};      // sconce
    {
        PlacedObjectManager pom(nullptr, nullptr, nullptr);
        const std::string sid = pom.registerStructure("tavern", {0, 16, 0}, 0, {0, 16, 0}, {12, 24, 10});
        ASSERT_FALSE(sid.empty());
        nlohmann::json recs = nlohmann::json::array();
        recs.push_back(StructureBuildService::lightRecord(built[0].p, built[0].c, built[0].i, built[0].r, "hearth", 3));
        recs.push_back(StructureBuildService::lightRecord(built[1].p, built[1].c, built[1].i, built[1].r, "wall_sconce", 4));
        ASSERT_TRUE(pom.setMetadata(sid, StructureBuildService::kLightsKey, recs));
        ASSERT_TRUE(pom.saveToDb(db));
    }

    PlacedObjectManager reloaded(nullptr, nullptr, nullptr);   // a relaunch: fresh registry
    ASSERT_TRUE(reloaded.loadFromDb(db));
    std::vector<CapturedLight> got;
    const auto res = StructureBuildService::restoreLights(
        reloaded, [&](const glm::vec3& p, const glm::vec3& c, float i, float r) {
            got.push_back({p, c, i, r});
            return static_cast<int>(got.size()) - 1;
        });
    sqlite3_close(db);

    EXPECT_EQ(res.restored, 2) << "every recorded light must come back after a reload";
    EXPECT_EQ(res.refused, 0);
    EXPECT_EQ(res.malformed, 0);
    ASSERT_EQ(got.size(), built.size());
    // The saving session's ids (3, 4) are stale after a reload: restore writes back the NEW
    // ids so that removing the structure later removes the right lights.
    const auto* so = reloaded.get("tavern_1");
    ASSERT_NE(so, nullptr);
    EXPECT_EQ(StructureBuildService::recordedLightIds(*so), (std::vector<int>{0, 1}));
    for (size_t k = 0; k < built.size(); ++k) {
        EXPECT_EQ(got[k].p, built[k].p) << "light " << k << " moved";
        EXPECT_EQ(got[k].c, built[k].c) << "light " << k << " changed colour";
        EXPECT_FLOAT_EQ(got[k].i, built[k].i);
        EXPECT_FLOAT_EQ(got[k].r, built[k].r);
    }
}

// Accounting teeth: a capacity refusal is counted as refused (not restored), and a record
// with a missing or non-finite field is skipped as malformed, never guessed. An object
// with no lights key contributes nothing.
TEST(StructureLightPersistenceTest, RefusedAndMalformedRecordsAreCountedNotGuessed) {
    using Phyxel::Core::StructureBuildService;
    PlacedObjectManager pom(nullptr, nullptr, nullptr);
    const std::string a = pom.registerStructure("house", {0, 16, 0}, 0, {0, 16, 0}, {6, 22, 6});
    const std::string b = pom.registerStructure("shed", {20, 16, 0}, 0, {20, 16, 0}, {24, 20, 4});
    ASSERT_FALSE(a.empty());
    ASSERT_FALSE(b.empty());
    nlohmann::json recs = nlohmann::json::array();
    recs.push_back(StructureBuildService::lightRecord({1, 17, 1}, {1, 1, 1}, 1.0f, 4.0f, "candle", 0));
    recs.push_back(StructureBuildService::lightRecord({2, 17, 2}, {1, 1, 1}, 1.0f, 4.0f, "candle", 1));
    nlohmann::json noPos = StructureBuildService::lightRecord({3, 17, 3}, {1, 1, 1}, 1.0f, 4.0f, "x", 2);
    noPos.erase("p");
    recs.push_back(noPos);
    nlohmann::json nanRadius = StructureBuildService::lightRecord({4, 17, 4}, {1, 1, 1}, 1.0f, 4.0f, "x", 5);
    nanRadius["r"] = std::nan("");
    recs.push_back(nanRadius);
    ASSERT_TRUE(pom.setMetadata(a, StructureBuildService::kLightsKey, recs));

    int calls = 0;
    const auto res = StructureBuildService::restoreLights(
        pom, [&](const glm::vec3&, const glm::vec3&, float, float) {
            return (calls++ == 0) ? 7 : -1;   // the second valid light hits capacity
        });
    EXPECT_EQ(res.restored, 1);
    EXPECT_EQ(res.refused, 1);
    EXPECT_EQ(res.malformed, 2);
    EXPECT_EQ(calls, 2) << "malformed records must never reach addPointLight";
    // Only the one light actually registered keeps an id: refused and malformed records are
    // rewritten to -1, never left holding a stale id that teardown would wrongly remove.
    EXPECT_EQ(StructureBuildService::recordedLightIds(*pom.get(a)), (std::vector<int>{7}));
}

// Teardown: removing a structure hands the pre-remove callback the object with its light
// records, so the host can remove those lights. CityBench C-25 left 7 lights burning over
// empty ground when a later overlapping build replaced a house (build 67 fixture lights,
// only 60 attached to a surviving building).
TEST(StructureLightPersistenceTest, RemovingAStructureExposesItsLightIdsForTeardown) {
    using Phyxel::Core::StructureBuildService;
    PlacedObjectManager pom(nullptr, nullptr, nullptr);
    const std::string sid = pom.registerStructure("house", {0, 16, 0}, 0, {0, 16, 0}, {6, 22, 6});
    nlohmann::json recs = nlohmann::json::array();
    recs.push_back(StructureBuildService::lightRecord({1, 17, 1}, {1, 1, 1}, 1.0f, 4.0f, "wall_lantern", 11));
    recs.push_back(StructureBuildService::lightRecord({5, 17, 5}, {1, 1, 1}, 1.0f, 4.0f, "wall_lantern", 12));
    ASSERT_TRUE(pom.setMetadata(sid, StructureBuildService::kLightsKey, recs));

    std::vector<int> tornDown;
    pom.setPreRemoveCallback([&](const std::string&, const PlacedObject& obj) {
        for (int id : StructureBuildService::recordedLightIds(obj)) tornDown.push_back(id);
    });
    ASSERT_TRUE(pom.remove(sid));
    EXPECT_EQ(tornDown, (std::vector<int>{11, 12}));
}
