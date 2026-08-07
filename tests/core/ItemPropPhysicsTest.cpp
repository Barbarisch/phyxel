// Item props as real physics bodies — L2 validation.
//
// Contract (2026-08-06, follow-up to the fine-voxel item class):
//  * spawnProp with a wired VoxelDynamicsWorld creates a DYNAMIC compound body
//    (merged boxes = collision shape, material-weighted mass, lifetime pinned
//    to FLT_MAX so cleanupDead's 30 s timer can't kill a lying item).
//  * update() syncs body pose -> kinematic render transform AND the PlacedObject
//    pose (bbox + pickup interaction point) so [E] Take follows a tumbling item.
//  * When the body sleeps (island rest) for kRestRetireSeconds, the body is
//    RETIRED (removed from the physics world); the prop stays as a plain
//    kinematic object at its settled pose — the item-class analogue of
//    furniture's re-staticize, minus chunk baking (items never bake).
//  * A body that vanishes externally (void fall / cleanupDead) retires the
//    prop gracefully at its last synced pose.
//  * spawnProp accepts an initial velocity (the drop-toss path).
//  * STATIC-FIRST (2026-08-07): spawns are settled by default; dynamic is
//    opt-in (drop/throw/hit) and capped; walk-through bump-revive removed.
//
// All assertions run on real engine objects (VoxelDynamicsWorld,
// KinematicVoxelManager, PlacedObjectManager, ObjectTemplateManager) — no
// Vulkan needed; the kinematic manager is CPU-side.

#include <gtest/gtest.h>

#include "core/ItemPropManager.h"
#include "core/ItemRegistry.h"
#include "core/ItemDefinition.h"
#include "core/KinematicVoxelManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"

#include <glm/glm.hpp>
#include <cfloat>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

// A small fine-grid item template (a 2x12x2-cell rod with a 6x4x2 head).
const char* kTestTemplateBody =
    "# grid: 27\n"
    "# category: item\n";

std::string buildRodBody() {
    std::string body = kTestTemplateBody;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 2; ++x)
            for (int z = 0; z < 2; ++z)
                body += "V " + std::to_string(x) + " " + std::to_string(y) + " "
                      + std::to_string(z) + " Wood\n";
    for (int y = 12; y < 16; ++y)
        for (int x = 0; x < 6; ++x)
            for (int z = 0; z < 2; ++z)
                body += "V " + std::to_string(x) + " " + std::to_string(y) + " "
                      + std::to_string(z) + " Metal\n";
    return body;
}

// Test harness: real managers, no chunks (props spawn in mid-air).
struct Rig {
    ObjectTemplateManager templates{nullptr, nullptr};
    KinematicVoxelManager kinematic;
    PlacedObjectManager placed{nullptr, &templates, nullptr};
    Physics::VoxelDynamicsWorld world;
    ItemPropManager props;

    explicit Rig(const std::string& itemId) {
        auto path = fs::temp_directory_path() / (itemId + ".voxel");
        std::ofstream f(path);
        f << buildRodBody();
        f.close();
        EXPECT_TRUE(templates.loadTemplate(path.string()));

        ItemDefinition def;
        def.id = itemId;
        def.name = "Test Rod";
        def.templateFile = itemId;          // resolves via getTemplate(stem)
        def.holdable = true;
        def.held.scale = 1.0f;
        ItemRegistry::instance().registerItem(def);

        props.setDependencies(&placed, &templates, &kinematic, nullptr);
        props.setDynamicsWorld(&world);
    }

    Physics::VoxelRigidBody* body(const std::string& placedId) {
        const auto* p = props.get(placedId);
        return p ? world.getBodyById(p->bodyId) : nullptr;
    }
};

}  // namespace

TEST(ItemPropPhysics, SpawnCreatesDynamicCompoundBody) {
    Rig rig("physrod_a");
    auto id = rig.props.spawnProp("physrod_a", {5.0f, 10.0f, 5.0f}, 0.0f,
                                  /*snapToGround=*/false, "", glm::vec3(0.0f),
                                  /*dynamic=*/true);
    ASSERT_FALSE(id.empty());
    const auto* p = rig.props.get(id);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->dynamic);

    auto* body = rig.body(id);
    ASSERT_NE(body, nullptr) << "no rigid body created for the item prop";
    EXPECT_GT(body->getLocalBoxes().size(), 1u) << "compound expected (rod + head)";
    EXPECT_GT(body->getTotalMass(), 0.0f);
    EXPECT_EQ(body->lifetime, FLT_MAX) << "item bodies must opt out of cleanupDead";
    EXPECT_GT(body->invMass, 0.0f) << "body must be dynamic, not frozen";
}

TEST(ItemPropPhysics, SpawnAppliesInitialVelocity) {
    Rig rig("physrod_b");
    const glm::vec3 vel(2.0f, 1.5f, -0.5f);
    auto id = rig.props.spawnProp("physrod_b", {5.0f, 10.0f, 5.0f}, 0.0f,
                                  false, "", vel, /*dynamic=*/true);
    ASSERT_FALSE(id.empty());
    auto* body = rig.body(id);
    ASSERT_NE(body, nullptr);
    EXPECT_NEAR(body->linearVelocity.x, vel.x, 1e-4f);
    EXPECT_NEAR(body->linearVelocity.y, vel.y, 1e-4f);
    EXPECT_NEAR(body->linearVelocity.z, vel.z, 1e-4f);
}

TEST(ItemPropPhysics, UpdateSyncsRenderAndPlacedPose) {
    Rig rig("physrod_c");
    auto id = rig.props.spawnProp("physrod_c", {5.0f, 10.0f, 5.0f}, 0.0f, false,
                                  "", glm::vec3(0.0f), /*dynamic=*/true);
    ASSERT_FALSE(id.empty());
    auto* body = rig.body(id);
    ASSERT_NE(body, nullptr);

    // Teleport the body and update: render transform + placed pose must follow.
    body->position += glm::vec3(3.0f, -2.0f, 1.0f);
    rig.props.update(0.016f);

    const auto* p = rig.props.get(id);
    const auto& obj = rig.kinematic.getObjects().at(p->kinId);
    const glm::vec3 renderPos(obj.currentTransform[3]);
    EXPECT_NEAR(renderPos.x, body->position.x, 1e-4f);
    EXPECT_NEAR(renderPos.y, body->position.y, 1e-4f);
    EXPECT_NEAR(renderPos.z, body->position.z, 1e-4f);

    // Placed-object bbox re-centered near the body; pickup point at bbox center.
    const auto* placedObj = rig.placed.get(id);
    ASSERT_NE(placedObj, nullptr);
    const glm::vec3 bboxCenter =
        (glm::vec3(placedObj->boundingMin) + glm::vec3(placedObj->boundingMax)) * 0.5f;
    EXPECT_LT(glm::distance(bboxCenter, body->position), 1.5f)
        << "placed bbox did not follow the body";
    ASSERT_FALSE(placedObj->interactionPoints.empty());
    EXPECT_LT(glm::distance(placedObj->interactionPoints[0].worldPos, bboxCenter), 1.0f)
        << "[E] Take pickup point did not follow";
}

TEST(ItemPropPhysics, SleepRetiresBodyKeepsProp) {
    Rig rig("physrod_d");
    auto id = rig.props.spawnProp("physrod_d", {5.0f, 10.0f, 5.0f}, 0.0f, false,
                                  "", glm::vec3(0.0f), /*dynamic=*/true);
    auto* body = rig.body(id);
    ASSERT_NE(body, nullptr);
    const uint32_t bodyId = body->id;

    // Simulate island rest (the world normally sets this).
    body->isAsleep = true;
    body->linearVelocity = body->angularVelocity = glm::vec3(0.0f);
    const glm::vec3 settled = body->position;

    for (int i = 0; i < 5; ++i)
        rig.props.update(ItemPropManager::kRestRetireSeconds * 0.35f);

    EXPECT_EQ(rig.world.getBodyById(bodyId), nullptr) << "body not retired after rest";
    const auto* p = rig.props.get(id);
    ASSERT_NE(p, nullptr) << "prop must survive retirement";
    EXPECT_FALSE(p->dynamic);
    // Render transform stays at the settled pose.
    const auto& obj = rig.kinematic.getObjects().at(p->kinId);
    const glm::vec3 renderPos(obj.currentTransform[3]);
    EXPECT_LT(glm::distance(renderPos, settled), 1e-3f);
    // Pickup still works: the placed object still exists with a pickup point.
    const auto* placedObj = rig.placed.get(id);
    ASSERT_NE(placedObj, nullptr);
    ASSERT_FALSE(placedObj->interactionPoints.empty());
}

TEST(ItemPropPhysics, VanishedBodyRetiresGracefully) {
    Rig rig("physrod_e");
    auto id = rig.props.spawnProp("physrod_e", {5.0f, 10.0f, 5.0f}, 0.0f, false,
                                  "", glm::vec3(0.0f), /*dynamic=*/true);
    auto* body = rig.body(id);
    ASSERT_NE(body, nullptr);

    // Simulate cleanupDead / void-fall: the world removes the body externally.
    rig.world.removeBody(body);
    rig.props.update(0.016f);   // must not crash, must retire in place

    const auto* p = rig.props.get(id);
    ASSERT_NE(p, nullptr);
    EXPECT_FALSE(p->dynamic);
    EXPECT_EQ(rig.props.count(), 1u);
}

TEST(ItemPropPhysics, TipAssistTopplesUprightSleeperInsteadOfFreezing) {
    // Velocity-threshold sleep freezes a slow inverted-pendulum topple: an
    // elongated item asleep while still upright must be WOKEN with a topple
    // nudge (not retired leaning on nothing). After kTipAssistMax nudges the
    // pose is accepted (legitimately propped against geometry).
    Rig rig("physrod_i");
    auto id = rig.props.spawnProp("physrod_i", {5.0f, 10.0f, 5.0f}, 0.0f, false,
                                  "", glm::vec3(0.0f), /*dynamic=*/true);
    auto* body = rig.body(id);
    ASSERT_NE(body, nullptr);

    // Force the pathological state: standing upright, island-asleep.
    body->orientation = glm::quat(1, 0, 0, 0);
    body->isAsleep = true;
    body->linearVelocity = body->angularVelocity = glm::vec3(0.0f);

    rig.props.update(0.016f);
    EXPECT_FALSE(body->isAsleep) << "tip assist must wake the upright sleeper";
    EXPECT_GT(glm::length(body->angularVelocity), 0.1f) << "no topple nudge applied";
    EXPECT_TRUE(rig.props.get(id)->dynamic) << "must not retire while upright";

    // Exhaust the assist budget without the pose changing (simulates a
    // genuinely propped item): retire is then allowed.
    for (int i = 0; i < ItemPropManager::kTipAssistMax + 1; ++i) {
        body->isAsleep = true;
        body->linearVelocity = body->angularVelocity = glm::vec3(0.0f);
        rig.props.update(ItemPropManager::kRestRetireSeconds + 0.1f);
    }
    EXPECT_FALSE(rig.props.get(id)->dynamic)
        << "assist budget exhausted: propped pose must be accepted and retired";
}

TEST(ItemPropPhysics, PickupWhileDynamicRemovesBody) {
    // (Added with its fix in the same change — the leak was found by review,
    // not by a red run: a picked-up prop's body has lifetime=FLT_MAX and would
    // otherwise simulate invisibly forever.)
    Rig rig("physrod_h");
    auto id = rig.props.spawnProp("physrod_h", {5.0f, 10.0f, 5.0f}, 0.0f, false,
                                  "", glm::vec3(0.0f), /*dynamic=*/true);
    auto* body = rig.body(id);
    ASSERT_NE(body, nullptr);
    const uint32_t bodyId = body->id;

    std::string itemId = rig.props.pickupProp(id);
    EXPECT_EQ(itemId, "physrod_h");
    EXPECT_EQ(rig.world.getBodyById(bodyId), nullptr)
        << "rigid body leaked after pickup";
    EXPECT_EQ(rig.props.count(), 0u);
}

TEST(ItemPropPhysics, PathQualifiedResolutionDefeatsStemShadowing) {
    // The 2026-08-06 shadowing bug (red state demonstrated LIVE, not in a
    // failing run of this test: legacy root-level BlockSmith templates named
    // lantern/goblet/candlestick/plate/torch silently substituted for the
    // items/ subdirectory remodels — the "giant blocky lantern"). Contract:
    // resolveItemTemplate resolves by the RELATIVE PATH key, so a template
    // registered under the bare stem never shadows "items/<stem>".
    ObjectTemplateManager mgr(nullptr, nullptr);
    KinematicVoxelManager kvm;
    PlacedObjectManager placed(nullptr, &mgr, nullptr);
    ItemPropManager props;
    props.setDependencies(&placed, &mgr, &kvm, nullptr);

    // Legacy root-style template registered under the bare stem.
    auto legacyPath = fs::temp_directory_path() / "_shadow_probe.voxel";
    { std::ofstream f(legacyPath); f << "C 0 0 0 Stone\n"; }
    ASSERT_TRUE(mgr.loadTemplate(legacyPath.string()));
    ASSERT_NE(mgr.getTemplate("_shadow_probe"), nullptr);

    // The item's REAL template lives under resources/templates/items/.
    const fs::path itemDir = fs::path("resources/templates/items");
    ASSERT_TRUE(fs::exists(itemDir)) << "test expects repo-root CWD";
    const fs::path itemPath = itemDir / "_shadow_probe.voxel";
    { std::ofstream f(itemPath); f << "# grid: 27\nV 0 0 0 Wood\nV 0 1 0 Wood\n"; }

    const auto* resolved = props.resolveItemTemplate("items/_shadow_probe.voxel");
    fs::remove(itemPath);   // clean up before asserting
    ASSERT_NE(resolved, nullptr);
    EXPECT_TRUE(resolved->isFineGrid())
        << "stem-registered legacy template shadowed the item's real model";
    EXPECT_EQ(resolved->fineVoxels.size(), 2u);
    // And the legacy entry is untouched under its own key.
    EXPECT_NE(mgr.getTemplate("_shadow_probe"), nullptr);
    EXPECT_FALSE(mgr.getTemplate("_shadow_probe")->isFineGrid());
}

// ---------------------------------------------------------------------------
// STATIC-FIRST movability (2026-08-07 plan): items spawn settled with NO body;
// physics is opt-in (drop/throw/hit). Cost is bounded by a concurrent-dynamic
// cap. Walk-through bump-revive is REMOVED (explicit hit revives instead).
// ---------------------------------------------------------------------------

TEST(ItemPropPhysics, DefaultSpawnIsStaticNoBody) {
    Rig rig("staticrod_a");
    auto id = rig.props.spawnProp("staticrod_a", {5.0f, 10.0f, 5.0f}, 0.0f, false);
    ASSERT_FALSE(id.empty());
    const auto* p = rig.props.get(id);
    ASSERT_NE(p, nullptr);
    EXPECT_FALSE(p->dynamic) << "default spawn must be settled (static-first)";
    EXPECT_EQ(p->bodyId, 0u);
    EXPECT_EQ(rig.world.getBodyCount(), 0u) << "no physics body for a placed item";
}

TEST(ItemPropPhysics, ExplicitDynamicSpawnCreatesBody) {
    Rig rig("staticrod_b");
    auto id = rig.props.spawnProp("staticrod_b", {5.0f, 10.0f, 5.0f}, 0.0f,
                                  false, "", glm::vec3(1.0f, 0.5f, 0.0f),
                                  /*dynamic=*/true);
    ASSERT_FALSE(id.empty());
    EXPECT_TRUE(rig.props.get(id)->dynamic);
    EXPECT_NE(rig.body(id), nullptr);
}

TEST(ItemPropPhysics, WalkThroughDoesNotRevive) {
    Rig rig("staticrod_c");
    auto id = rig.props.spawnProp("staticrod_c", {5.0f, 10.0f, 5.0f}, 0.0f, false);
    ASSERT_FALSE(rig.props.get(id)->dynamic);
    const glm::vec3 at(5.0f, 10.0f, 5.0f);
    // A fast player walking straight through the settled prop: NO revive.
    for (int i = 0; i < 10; ++i)
        rig.props.update(0.016f, at, glm::vec3(3.0f, 0.0f, 0.0f));
    EXPECT_FALSE(rig.props.get(id)->dynamic)
        << "walk-through bump-revive must be removed (static-first)";
}

TEST(ItemPropPhysics, HitRevivesSettledProp) {
    Rig rig("staticrod_d");
    auto id = rig.props.spawnProp("staticrod_d", {5.0f, 10.0f, 5.0f}, 0.0f, false);
    ASSERT_FALSE(rig.props.get(id)->dynamic);
    // Explicit hit (attack) physicalizes with an impulse.
    EXPECT_TRUE(rig.props.hitProp(id, glm::vec3(2.0f, 1.0f, 0.0f)));
    const auto* p = rig.props.get(id);
    EXPECT_TRUE(p->dynamic) << "explicit hit must revive";
    auto* body = rig.body(id);
    ASSERT_NE(body, nullptr);
    EXPECT_GT(glm::length(body->linearVelocity), 0.5f);
}

TEST(ItemPropPhysics, ConcurrentDynamicCapEvictsOldest) {
    Rig rig("staticrod_e");
    std::vector<std::string> ids;
    for (int i = 0; i < ItemPropManager::kMaxDynamicItems + 1; ++i) {
        auto id = rig.props.spawnProp("staticrod_e",
                                      {5.0f + i * 2.0f, 10.0f, 5.0f}, 0.0f,
                                      false, "", glm::vec3(0.0f),
                                      /*dynamic=*/true);
        ASSERT_FALSE(id.empty());
        ids.push_back(id);
    }
    // The cap holds: at most kMaxDynamicItems live bodies; the OLDEST dynamic
    // prop was retired (frozen at its pose), not destroyed.
    size_t dynamicCount = 0;
    for (const auto& id : ids)
        if (rig.props.get(id)->dynamic) ++dynamicCount;
    EXPECT_LE(dynamicCount, size_t(ItemPropManager::kMaxDynamicItems));
    EXPECT_FALSE(rig.props.get(ids.front())->dynamic) << "oldest not evicted";
    ASSERT_NE(rig.props.get(ids.front()), nullptr);
    EXPECT_LE(rig.world.getBodyCount(), size_t(ItemPropManager::kMaxDynamicItems));
}

TEST(ItemPropPhysics, ShippedItemColliderWithinBudget) {
    // Coarse-collider contract: the COLLISION compound is decoupled from the
    // render boxes and capped at kMaxColliderBoxes (the M x N narrowphase term
    // is quadratic in this number; the longsword's render mesh is ~37 boxes).
    // Uses the REAL shipped longsword template; skips if not run from repo root.
    if (!fs::exists("resources/templates/weapons/sword_long.voxel"))
        GTEST_SKIP() << "repo-root CWD required";
    ObjectTemplateManager mgr(nullptr, nullptr);
    KinematicVoxelManager kvm;
    PlacedObjectManager placed(nullptr, &mgr, nullptr);
    Physics::VoxelDynamicsWorld world;
    ItemPropManager props;
    props.setDependencies(&placed, &mgr, &kvm, nullptr);
    props.setDynamicsWorld(&world);

    ItemDefinition def;
    def.id = "budget_sword";
    def.name = "Budget Sword";
    def.templateFile = "weapons/sword_long.voxel";
    def.holdable = true;
    def.held.scale = 1.0f;
    ItemRegistry::instance().registerItem(def);

    auto id = props.spawnProp("budget_sword", {5.0f, 10.0f, 5.0f}, 0.0f, false,
                              "", glm::vec3(0.0f), /*dynamic=*/true);
    ASSERT_FALSE(id.empty());
    const auto* p = props.get(id);
    ASSERT_NE(p, nullptr);
    EXPECT_LE(p->localBoxes.size(), size_t(ItemPropManager::kMaxColliderBoxes))
        << "collider must be a coarse compound, not the render mesh";
    // The RENDER geometry keeps full detail.
    const auto& obj = kvm.getObjects().at(p->kinId);
    EXPECT_GT(obj.voxels.size(), p->localBoxes.size());
}

TEST(ItemPropPhysics, NoDynamicsWorldFallsBackToStatic) {
    // CONTROL: without a wired dynamics world the legacy static path is
    // byte-identical — prop exists, no body, update() is a no-op.
    Rig rig("physrod_g");
    rig.props.setDynamicsWorld(nullptr);
    auto id = rig.props.spawnProp("physrod_g", {5.0f, 10.0f, 5.0f}, 0.0f, false);
    ASSERT_FALSE(id.empty());
    const auto* p = rig.props.get(id);
    EXPECT_FALSE(p->dynamic);
    EXPECT_EQ(p->bodyId, 0u);
    rig.props.update(0.016f);  // must not crash
    EXPECT_EQ(rig.props.count(), 1u);
}
