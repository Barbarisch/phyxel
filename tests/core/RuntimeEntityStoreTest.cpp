#include <gtest/gtest.h>

#include "core/RuntimeEntityStore.h"
#include "core/Uuid.h"

#include <sqlite3.h>
#include <string>

using namespace Phyxel::Core;

namespace {

// Fresh in-memory DB per test (no on-disk world needed).
struct RuntimeEntityStoreTest : public ::testing::Test {
    sqlite3* db = nullptr;
    void SetUp() override { ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK); }
    void TearDown() override { sqlite3_close(db); }
};

TEST_F(RuntimeEntityStoreTest, RoundTripPreservesUuidAndRecipe) {
    RuntimeEntity a{Uuid::generate(), "entity_1", "animated", "resources/animated_characters/humanoid.anim",
                    glm::vec3(12.0f, 34.0f, -5.0f)};
    RuntimeEntity b{Uuid::generate(), "entity_2", "animated", "resources/animated_characters/wolf.anim",
                    glm::vec3(-1.5f, 20.0f, 7.25f)};

    ASSERT_TRUE(RuntimeEntityStore::saveToDb(db, {a, b}));
    auto loaded = RuntimeEntityStore::loadFromDb(db);
    ASSERT_EQ(loaded.size(), 2u);

    // Find by uuid (row order is not guaranteed).
    auto find = [&](const std::string& uuid) -> const RuntimeEntity* {
        for (const auto& e : loaded) if (e.uuid == uuid) return &e;
        return nullptr;
    };
    const RuntimeEntity* la = find(a.uuid);
    ASSERT_NE(la, nullptr) << "entity uuid did not survive the DB round-trip";
    EXPECT_EQ(la->id, "entity_1");
    EXPECT_EQ(la->type, "animated");
    EXPECT_EQ(la->animFile, a.animFile);
    EXPECT_FLOAT_EQ(la->position.x, 12.0f);
    EXPECT_FLOAT_EQ(la->position.y, 34.0f);
    EXPECT_FLOAT_EQ(la->position.z, -5.0f);
    ASSERT_NE(find(b.uuid), nullptr);
}

TEST_F(RuntimeEntityStoreTest, SaveReplacesPreviousSet) {
    RuntimeEntity a{Uuid::generate(), "e1", "animated", "a.anim", glm::vec3(0)};
    RuntimeEntity b{Uuid::generate(), "e2", "animated", "b.anim", glm::vec3(0)};
    ASSERT_TRUE(RuntimeEntityStore::saveToDb(db, {a, b}));
    ASSERT_EQ(RuntimeEntityStore::loadFromDb(db).size(), 2u);

    // A subsequent save with only `a` must drop `b` (a removed entity mustn't linger).
    ASSERT_TRUE(RuntimeEntityStore::saveToDb(db, {a}));
    auto loaded = RuntimeEntityStore::loadFromDb(db);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].uuid, a.uuid);
}

TEST_F(RuntimeEntityStoreTest, EmptyAndNullAreSafe) {
    EXPECT_TRUE(RuntimeEntityStore::saveToDb(db, {}));
    EXPECT_TRUE(RuntimeEntityStore::loadFromDb(db).empty());
    EXPECT_FALSE(RuntimeEntityStore::saveToDb(nullptr, {}));
    EXPECT_TRUE(RuntimeEntityStore::loadFromDb(nullptr).empty());
}

}  // namespace
