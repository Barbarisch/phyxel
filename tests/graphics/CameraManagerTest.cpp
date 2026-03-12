#include <gtest/gtest.h>
#include "graphics/CameraManager.h"
#include <glm/glm.hpp>
#include <cmath>

using namespace Phyxel::Graphics;

// ============================================================================
// CameraSlot CRUD Tests
// ============================================================================

class CameraManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        camera = std::make_unique<Camera>(glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), -90.0f, 0.0f);
        mgr = std::make_unique<CameraManager>(camera.get());
    }

    std::unique_ptr<Camera> camera;
    std::unique_ptr<CameraManager> mgr;
};

TEST_F(CameraManagerTest, CreateSlot_ReturnsValidIndex) {
    int idx = mgr->createSlot("test", glm::vec3(10, 20, 30), -90.0f, 0.0f);
    EXPECT_GE(idx, 0);
    EXPECT_EQ(mgr->slotCount(), 1u);
}

TEST_F(CameraManagerTest, CreateSlot_DuplicateNameFails) {
    mgr->createSlot("cam1", glm::vec3(0, 0, 0));
    int idx2 = mgr->createSlot("cam1", glm::vec3(1, 1, 1));
    EXPECT_EQ(idx2, -1);
    EXPECT_EQ(mgr->slotCount(), 1u);
}

TEST_F(CameraManagerTest, GetSlot_Found) {
    mgr->createSlot("cam1", glm::vec3(5, 10, 15), -45.0f, -30.0f);
    const CameraSlot* slot = mgr->getSlot("cam1");
    ASSERT_NE(slot, nullptr);
    EXPECT_FLOAT_EQ(slot->position.x, 5.0f);
    EXPECT_FLOAT_EQ(slot->yaw, -45.0f);
}

TEST_F(CameraManagerTest, GetSlot_NotFound) {
    EXPECT_EQ(mgr->getSlot("nonexistent"), nullptr);
}

TEST_F(CameraManagerTest, RemoveSlot) {
    mgr->createSlot("cam1", glm::vec3(0, 0, 0));
    EXPECT_TRUE(mgr->removeSlot("cam1"));
    EXPECT_EQ(mgr->slotCount(), 0u);
    EXPECT_EQ(mgr->getSlot("cam1"), nullptr);
}

TEST_F(CameraManagerTest, RemoveSlot_NonexistentFails) {
    EXPECT_FALSE(mgr->removeSlot("nope"));
}

TEST_F(CameraManagerTest, UpdateSlot) {
    mgr->createSlot("cam1", glm::vec3(0, 0, 0));
    CameraSlot updated;
    updated.position = glm::vec3(99, 88, 77);
    updated.yaw = 45.0f;
    updated.pitch = -15.0f;
    EXPECT_TRUE(mgr->updateSlot("cam1", updated));

    const CameraSlot* slot = mgr->getSlot("cam1");
    ASSERT_NE(slot, nullptr);
    EXPECT_FLOAT_EQ(slot->position.x, 99.0f);
    EXPECT_FLOAT_EQ(slot->yaw, 45.0f);
    EXPECT_EQ(slot->name, "cam1"); // Name preserved
}

// ============================================================================
// Active Slot Tests
// ============================================================================

TEST_F(CameraManagerTest, SetActiveSlot_SnapsCamera) {
    mgr->createSlot("cam1", glm::vec3(100, 200, 300), -45.0f, -30.0f);
    EXPECT_TRUE(mgr->setActiveSlot("cam1"));
    EXPECT_EQ(mgr->getActiveSlotName(), "cam1");

    EXPECT_FLOAT_EQ(camera->getPosition().x, 100.0f);
    EXPECT_FLOAT_EQ(camera->getPosition().y, 200.0f);
    EXPECT_FLOAT_EQ(camera->getYaw(), -45.0f);
    EXPECT_FLOAT_EQ(camera->getPitch(), -30.0f);
}

TEST_F(CameraManagerTest, SetActiveSlot_NonexistentFails) {
    EXPECT_FALSE(mgr->setActiveSlot("nope"));
}

TEST_F(CameraManagerTest, CycleSlot_Wraps) {
    mgr->createSlot("cam1", glm::vec3(1, 0, 0));
    mgr->createSlot("cam2", glm::vec3(2, 0, 0));
    mgr->createSlot("cam3", glm::vec3(3, 0, 0));

    mgr->setActiveSlot("cam1");
    EXPECT_TRUE(mgr->cycleSlot());
    EXPECT_EQ(mgr->getActiveSlotName(), "cam2");

    EXPECT_TRUE(mgr->cycleSlot());
    EXPECT_EQ(mgr->getActiveSlotName(), "cam3");

    EXPECT_TRUE(mgr->cycleSlot());
    EXPECT_EQ(mgr->getActiveSlotName(), "cam1"); // Wrap around
}

TEST_F(CameraManagerTest, CyclePrevSlot_Wraps) {
    mgr->createSlot("cam1", glm::vec3(1, 0, 0));
    mgr->createSlot("cam2", glm::vec3(2, 0, 0));
    mgr->createSlot("cam3", glm::vec3(3, 0, 0));

    mgr->setActiveSlot("cam1");
    EXPECT_TRUE(mgr->cyclePrevSlot());
    EXPECT_EQ(mgr->getActiveSlotName(), "cam3"); // Wrap backward
}

TEST_F(CameraManagerTest, CycleSlot_EmptyReturnsFalse) {
    EXPECT_FALSE(mgr->cycleSlot());
}

// ============================================================================
// Transition Tests
// ============================================================================

TEST_F(CameraManagerTest, TransitionToSlot_ProgressesOverTime) {
    mgr->createSlot("from", glm::vec3(0, 0, 0), -90.0f, 0.0f);
    mgr->createSlot("to", glm::vec3(100, 0, 0), -90.0f, 0.0f);
    mgr->setActiveSlot("from");

    EXPECT_TRUE(mgr->transitionToSlot("to", 1.0f));

    // At t=0, should still be near from
    mgr->update(0.0f);
    EXPECT_NEAR(camera->getPosition().x, 0.0f, 1.0f);

    // At t=0.5, should be partway
    mgr->update(0.5f);
    float midX = camera->getPosition().x;
    EXPECT_GT(midX, 0.0f);
    EXPECT_LT(midX, 100.0f);

    // At t=1.0, should be at destination
    mgr->update(0.5f);
    EXPECT_NEAR(camera->getPosition().x, 100.0f, 0.01f);
}

// ============================================================================
// CameraTransition Unit Tests
// ============================================================================

TEST(CameraTransitionTest, LinearEase) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraTransition transition;

    CameraSlot from;
    from.position = glm::vec3(0, 0, 0);
    from.yaw = 0.0f;
    from.pitch = 0.0f;

    CameraSlot to;
    to.position = glm::vec3(100, 0, 0);
    to.yaw = 0.0f;
    to.pitch = 0.0f;

    transition.start(from, to, 1.0f, CameraTransition::EaseType::Linear);
    EXPECT_TRUE(transition.isActive());

    transition.update(0.5f, cam);
    EXPECT_NEAR(cam.getPosition().x, 50.0f, 0.5f);
    EXPECT_TRUE(transition.isActive());

    transition.update(0.5f, cam);
    EXPECT_NEAR(cam.getPosition().x, 100.0f, 0.01f);
    EXPECT_FALSE(transition.isActive());
}

TEST(CameraTransitionTest, YawWrapping) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraTransition transition;

    CameraSlot from;
    from.position = glm::vec3(0, 0, 0);
    from.yaw = 170.0f;
    from.pitch = 0.0f;

    CameraSlot to;
    to.position = glm::vec3(0, 0, 0);
    to.yaw = -170.0f; // Should wrap short way (20 degrees), not long way (340)
    to.pitch = 0.0f;

    transition.start(from, to, 1.0f, CameraTransition::EaseType::Linear);
    transition.update(0.5f, cam);

    // Midpoint should be ~180 (wrapping through 180), not crossing 0
    float midYaw = cam.getYaw();
    EXPECT_NEAR(std::abs(midYaw), 180.0f, 1.0f);
}

// ============================================================================
// CameraPath Tests
// ============================================================================

TEST(CameraPathTest, NeedsTwoWaypoints) {
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, -90, 0, 0});
    path.play();
    EXPECT_FALSE(path.isPlaying()); // Not enough waypoints
}

TEST(CameraPathTest, PlayAndProgress) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, -90.0f, 0.0f, 0.0f});
    path.addWaypoint({{100, 0, 0}, -90.0f, 0.0f, 0.0f});
    path.addWaypoint({{100, 100, 0}, -90.0f, 0.0f, 0.0f});
    path.play();
    EXPECT_TRUE(path.isPlaying());

    // Progress through first segment
    for (int i = 0; i < 20; i++) {
        path.update(0.1f, cam);
    }

    // Should now be past the first waypoint
    EXPECT_FALSE(path.isFinished());
}

TEST(CameraPathTest, FinishesAtEnd) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, 0, 0, 0});
    path.addWaypoint({{10, 0, 0}, 0, 0, 0});
    path.play();

    // Run through the entire path
    for (int i = 0; i < 30; i++) {
        path.update(0.1f, cam);
    }

    EXPECT_TRUE(path.isFinished());
    EXPECT_FALSE(path.isPlaying());
}

TEST(CameraPathTest, LoopingDoesNotFinish) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, 0, 0, 0});
    path.addWaypoint({{10, 0, 0}, 0, 0, 0});
    path.setLooping(true);
    path.play();

    // Run through several loops
    for (int i = 0; i < 100; i++) {
        path.update(0.1f, cam);
    }

    EXPECT_FALSE(path.isFinished());
    EXPECT_TRUE(path.isPlaying());
}

TEST(CameraPathTest, StopResetsState) {
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, 0, 0, 0});
    path.addWaypoint({{10, 0, 0}, 0, 0, 0});
    path.play();
    path.stop();
    EXPECT_FALSE(path.isPlaying());
    EXPECT_FALSE(path.isFinished());
}

TEST(CameraPathTest, ClearWaypoints) {
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, 0, 0, 0});
    path.addWaypoint({{10, 0, 0}, 0, 0, 0});
    EXPECT_EQ(path.waypointCount(), 2u);
    path.clearWaypoints();
    EXPECT_EQ(path.waypointCount(), 0u);
}

// ============================================================================
// Entity Follow Tests
// ============================================================================

TEST_F(CameraManagerTest, FollowEntity_WithLookup) {
    mgr->createSlot("follow_cam", glm::vec3(0, 0, 0), -90.0f, 0.0f, CameraMode::ThirdPerson);
    mgr->setActiveSlot("follow_cam");

    glm::vec3 entityPos(50.0f, 10.0f, 50.0f);
    mgr->setEntityPositionLookup([&](const std::string& id) -> std::optional<glm::vec3> {
        if (id == "npc_1") return entityPos;
        return std::nullopt;
    });

    EXPECT_TRUE(mgr->followEntity("follow_cam", "npc_1", 5.0f, 2.0f));

    mgr->update(0.016f);

    // Camera should have moved toward entity position
    // (exact position depends on camera front vector, but it shouldn't be at origin anymore)
    float dist = glm::length(camera->getPosition() - glm::vec3(0, 0, 0));
    EXPECT_GT(dist, 1.0f);
}

TEST_F(CameraManagerTest, UnfollowEntity) {
    mgr->createSlot("cam1", glm::vec3(0, 0, 0));
    mgr->followEntity("cam1", "npc_1");
    EXPECT_TRUE(mgr->unfollowEntity("cam1"));

    const CameraSlot* slot = mgr->getSlot("cam1");
    ASSERT_NE(slot, nullptr);
    EXPECT_TRUE(slot->followEntityId.empty());
}

// ============================================================================
// CaptureCurrentState Tests
// ============================================================================

TEST_F(CameraManagerTest, CaptureCurrentState) {
    camera->setPosition(glm::vec3(42, 84, 126));
    camera->setYaw(-45.0f);
    camera->setPitch(-15.0f);
    camera->setMode(CameraMode::Free);

    CameraSlot captured = mgr->captureCurrentState("snapshot");
    EXPECT_EQ(captured.name, "snapshot");
    EXPECT_FLOAT_EQ(captured.position.x, 42.0f);
    EXPECT_FLOAT_EQ(captured.yaw, -45.0f);
    EXPECT_FLOAT_EQ(captured.pitch, -15.0f);
    EXPECT_EQ(captured.mode, CameraMode::Free);
}
