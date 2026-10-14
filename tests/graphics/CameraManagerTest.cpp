#include <gtest/gtest.h>
#include "graphics/CameraManager.h"
#include <glm/glm.hpp>
#include <algorithm>
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

// V1 (docs/PerfProgram2026-09.md section 16): the benchmark route must move at a CONSTANT speed, so
// the same route takes the same time and is at the same place at the same progress on every run.
// The default timing is 1 s per segment whatever its length: on a 1 u segment followed by a 10 u one
// the camera moves at ~1 u/s, then ~10 u/s.
TEST(CameraPathTest, ConstantSpeedCoversEqualArcLengthInEqualTime) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, 0, 0, 0});
    path.addWaypoint({{1, 0, 0}, 0, 0, 0});
    path.addWaypoint({{11, 0, 0}, 0, 0, 0});
    path.setConstantSpeed(2.0f);
    path.play();
    glm::vec3 prev = cam.getPosition();
    int steps = 0, offSpeed = 0;
    float worst = 0.0f;
    while (path.isPlaying() && steps < 1000) {
        path.update(0.1f, cam);
        const glm::vec3 p = cam.getPosition();
        const float d = glm::length(p - prev);
        prev = p;
        ++steps;
        if (!path.isPlaying()) break;                 // the last step is partial
        const float err = std::abs(d - 0.2f) / 0.2f;
        worst = std::max(worst, err);
        if (err > 0.02f) ++offSpeed;
    }
    EXPECT_EQ(offSpeed, 0) << "worst relative speed error " << worst;
    EXPECT_TRUE(path.isFinished());
    EXPECT_NEAR(glm::length(cam.getPosition() - glm::vec3(11, 0, 0)), 0.0f, 1e-3f);
    // The constant-speed path is the polyline: 11 u at 2 u/s is 55 steps of 0.1 s.
    EXPECT_NEAR(path.arcLength(), 11.0f, 1e-3f);
    EXPECT_NEAR(static_cast<float>(steps), 55.0f, 1.0f);
}

// Uniform Catmull-Rom through unevenly spaced waypoints walks BACKWARDS (x = 0, 1, 11: the first
// segment dips to x = -0.125). A benchmark route camera must never do that: in constant-speed mode
// every step moves forward along the route.
TEST(CameraPathTest, ConstantSpeedNeverBacktracksOnUnevenWaypoints) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, 0, 0, 0});
    path.addWaypoint({{1, 0, 0}, 0, 0, 0});
    path.addWaypoint({{11, 0, 0}, 0, 0, 0});
    path.setConstantSpeed(0.5f);
    path.play();
    float prevX = 0.0f, minX = 0.0f;
    bool monotonic = true;
    while (path.isPlaying()) {
        path.update(0.05f, cam);
        const float x = cam.getPosition().x;
        if (x < prevX - 1e-5f) monotonic = false;
        minX = std::min(minX, x);
        prevX = x;
    }
    EXPECT_TRUE(monotonic);
    EXPECT_GE(minX, 0.0f);
}

TEST(CameraPathTest, ConstantSpeedProgressMatchesThePoseAtThatProgress) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, 0, 0, 0});
    path.addWaypoint({{5, 0, 5}, 90, 0, 0});
    path.addWaypoint({{10, 3, 0}, 180, -10, 0});
    path.setConstantSpeed(3.0f);
    path.play();
    for (int i = 0; i < 40 && path.isPlaying(); ++i) {
        path.update(1.0f / 60.0f + 0.013f * (i % 3), cam);   // uneven frame times
        glm::vec3 ref; float yaw, pitch;
        ASSERT_TRUE(path.poseAt(path.progress(), ref, yaw, pitch));
        EXPECT_LT(glm::length(cam.getPosition() - ref), 1e-3f);
        EXPECT_NEAR(cam.getYaw(), yaw, 1e-2f);
    }
}

// Control: speed <= 0 keeps the original per-segment timing (the pinned tests above stay as they are).
TEST(CameraPathTest, ZeroSpeedKeepsTheOriginalOneSecondPerSegment) {
    Camera cam(glm::vec3(0, 0, 0));
    CameraPath path;
    path.addWaypoint({{0, 0, 0}, 0, 0, 0});
    path.addWaypoint({{10, 0, 0}, 0, 0, 0});
    path.setConstantSpeed(0.0f);
    path.play();
    for (int i = 0; i < 5; ++i) path.update(0.1f, cam);   // half of the 1 s segment
    EXPECT_NEAR(cam.getPosition().x, 5.0f, 0.6f);          // Catmull-Rom at t = 0.5
    EXPECT_FALSE(path.isFinished());
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
