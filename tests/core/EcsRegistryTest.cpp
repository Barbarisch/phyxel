#include <gtest/gtest.h>
#include "ecs/Registry.h"

#include <string>
#include <vector>
#include <algorithm>

using Phyxel::Ecs::Registry;
using Phyxel::Ecs::Entity;
using Phyxel::Ecs::NullEntity;

namespace {
struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };
struct Tag { std::string name; };
}

TEST(EcsRegistry, CreateProducesValidDistinctEntities) {
    Registry r;
    Entity a = r.create();
    Entity b = r.create();
    EXPECT_TRUE(r.valid(a));
    EXPECT_TRUE(r.valid(b));
    EXPECT_NE(a, b);
    EXPECT_EQ(r.aliveCount(), 2u);
    EXPECT_FALSE(r.valid(NullEntity));
}

TEST(EcsRegistry, DestroyInvalidatesHandle) {
    Registry r;
    Entity a = r.create();
    r.destroy(a);
    EXPECT_FALSE(r.valid(a));
    EXPECT_EQ(r.aliveCount(), 0u);
    r.destroy(a);  // double destroy is a no-op
    EXPECT_EQ(r.aliveCount(), 0u);
}

TEST(EcsRegistry, StaleHandleDetectedAfterSlotReuse) {
    Registry r;
    Entity a = r.create();
    r.destroy(a);
    Entity b = r.create();          // reuses a's slot
    EXPECT_EQ(a.index, b.index);    // same slot...
    EXPECT_NE(a.generation, b.generation);  // ...different generation
    EXPECT_FALSE(r.valid(a));       // stale handle rejected
    EXPECT_TRUE(r.valid(b));
}

TEST(EcsRegistry, AddGetHasRemove) {
    Registry r;
    Entity e = r.create();
    EXPECT_FALSE(r.has<Position>(e));

    r.add<Position>(e, {1.0f, 2.0f, 3.0f});
    EXPECT_TRUE(r.has<Position>(e));
    ASSERT_NE(r.get<Position>(e), nullptr);
    EXPECT_FLOAT_EQ(r.get<Position>(e)->y, 2.0f);

    r.get<Position>(e)->y = 9.0f;          // mutate in place
    EXPECT_FLOAT_EQ(r.get<Position>(e)->y, 9.0f);

    r.remove<Position>(e);
    EXPECT_FALSE(r.has<Position>(e));
    EXPECT_EQ(r.get<Position>(e), nullptr);
}

TEST(EcsRegistry, AddOverwritesExisting) {
    Registry r;
    Entity e = r.create();
    r.add<Position>(e, {1, 1, 1});
    r.add<Position>(e, {5, 5, 5});
    EXPECT_FLOAT_EQ(r.get<Position>(e)->x, 5.0f);
}

TEST(EcsRegistry, DestroyRemovesAllComponents) {
    Registry r;
    Entity e = r.create();
    r.add<Position>(e, {1, 2, 3});
    r.add<Tag>(e, {"chair"});
    r.destroy(e);
    EXPECT_FALSE(r.valid(e));
    // Reuse the slot; the new entity must not inherit the old components.
    Entity e2 = r.create();
    EXPECT_FALSE(r.has<Position>(e2));
    EXPECT_FALSE(r.has<Tag>(e2));
}

TEST(EcsRegistry, SwapRemoveKeepsOtherComponentsIntact) {
    Registry r;
    std::vector<Entity> es;
    for (int i = 0; i < 5; ++i) {
        Entity e = r.create();
        r.add<Position>(e, {float(i), 0, 0});
        es.push_back(e);
    }
    r.remove<Position>(es[1]);   // middle removal triggers swap with last
    EXPECT_FALSE(r.has<Position>(es[1]));
    for (int i : {0, 2, 3, 4}) {
        ASSERT_TRUE(r.has<Position>(es[i]));
        EXPECT_FLOAT_EQ(r.get<Position>(es[i])->x, float(i));
    }
}

TEST(EcsRegistry, ViewSingleComponent) {
    Registry r;
    for (int i = 0; i < 3; ++i) { Entity e = r.create(); r.add<Position>(e, {float(i), 0, 0}); }
    Entity noPos = r.create(); (void)noPos;  // not in view

    float sum = 0; int count = 0;
    r.view<Position>([&](Entity, Position& p) { sum += p.x; ++count; });
    EXPECT_EQ(count, 3);
    EXPECT_FLOAT_EQ(sum, 0 + 1 + 2);
}

TEST(EcsRegistry, ViewMultiComponentIntersection) {
    Registry r;
    Entity a = r.create(); r.add<Position>(a, {1, 0, 0}); r.add<Velocity>(a, {1, 0, 0});
    Entity b = r.create(); r.add<Position>(b, {2, 0, 0});                          // no Velocity
    Entity c = r.create(); r.add<Position>(c, {3, 0, 0}); r.add<Velocity>(c, {2, 0, 0});

    std::vector<float> seen;
    r.view<Position, Velocity>([&](Entity, Position& p, Velocity&) { seen.push_back(p.x); });
    std::sort(seen.begin(), seen.end());
    ASSERT_EQ(seen.size(), 2u);          // only a and c have both
    EXPECT_FLOAT_EQ(seen[0], 1.0f);
    EXPECT_FLOAT_EQ(seen[1], 3.0f);
}

TEST(EcsRegistry, ViewEmptyWhenComponentNeverUsed) {
    Registry r;
    Entity e = r.create(); r.add<Position>(e, {0, 0, 0});
    int count = 0;
    r.view<Velocity>([&](Entity, Velocity&) { ++count; });  // no Velocity pool exists
    EXPECT_EQ(count, 0);
}
