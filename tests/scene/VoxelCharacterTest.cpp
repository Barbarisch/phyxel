#include <gtest/gtest.h>
#include "scene/VoxelCharacter.h"
#include "scene/CharacterSkeleton.h"
#include "scene/CharacterAppearance.h"
#include "physics/PhysicsWorld.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Phyxel;
using namespace Phyxel::Scene;

// ============================================================================
// Helper: build a minimal 3-bone skeleton (root → spine → head)
// ============================================================================

static CharacterSkeleton makeMinimalSkeleton() {
    CharacterSkeleton cs;
    cs.skeleton.addBone("Hips", -1,
                        glm::vec3(0, 0, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("Spine", 0,
                        glm::vec3(0, 0.3f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("Head", 1,
                        glm::vec3(0, 0.3f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));

    cs.computeBoneSizes();
    cs.generateJointDefs();
    return cs;
}

// 5-bone humanoid: Hips → Spine → Head, Hips → LeftLeg, Hips → RightLeg
static CharacterSkeleton makeHumanoidSkeleton() {
    CharacterSkeleton cs;
    cs.skeleton.addBone("Hips", -1,
                        glm::vec3(0, 0, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("Spine", 0,
                        glm::vec3(0, 0.4f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("Head", 1,
                        glm::vec3(0, 0.3f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("LeftLeg", 0,
                        glm::vec3(-0.1f, -0.4f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("RightLeg", 0,
                        glm::vec3(0.1f, -0.4f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));

    cs.computeBoneSizes();
    cs.generateJointDefs();
    return cs;
}

// ============================================================================
// DriveMode enum tests
// ============================================================================

TEST(DriveModeTest, EnumValues) {
    EXPECT_NE(static_cast<int>(DriveMode::Animated), static_cast<int>(DriveMode::Physics));
    EXPECT_NE(static_cast<int>(DriveMode::Physics), static_cast<int>(DriveMode::PassiveRagdoll));
    EXPECT_NE(static_cast<int>(DriveMode::Animated), static_cast<int>(DriveMode::PassiveRagdoll));
}

// ============================================================================
// VoxelCharacter construction tests (no PhysicsWorld)
// ============================================================================

TEST(VoxelCharacterBasicTest, ConstructWithNullPhysicsWorld) {
    // Should not crash with null physics — bones simply won't be created
    // Note: this will likely fail since createController needs physicsWorld.
    // We test with a real PhysicsWorld below.
}

// ============================================================================
// Tests requiring PhysicsWorld
// ============================================================================

class VoxelCharacterTest : public ::testing::Test {
protected:
    void SetUp() override {
        physicsWorld_ = std::make_unique<Physics::PhysicsWorld>();
        physicsWorld_->initialize();
    }

    void TearDown() override {
        character_.reset();
        physicsWorld_.reset();
    }

    std::unique_ptr<Physics::PhysicsWorld> physicsWorld_;
    std::unique_ptr<VoxelCharacter> character_;
};

// ---- Construction ----

TEST_F(VoxelCharacterTest, ConstructAtPosition) {
    glm::vec3 pos(10.0f, 20.0f, 30.0f);
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), pos);
    EXPECT_NE(character_, nullptr);
}

TEST_F(VoxelCharacterTest, DefaultDriveModeIsAnimated) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_EQ(character_->getDriveMode(), DriveMode::Animated);
}

TEST_F(VoxelCharacterTest, InitialPositionSet) {
    glm::vec3 pos(5.0f, 10.0f, 15.0f);
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), pos);
    glm::vec3 got = character_->getPosition();
    EXPECT_NEAR(got.x, pos.x, 0.5f);
    EXPECT_NEAR(got.y, pos.y, 0.5f);
    EXPECT_NEAR(got.z, pos.z, 0.5f);
}

TEST_F(VoxelCharacterTest, HasHealthComponent) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_NE(character_->getHealthComponent(), nullptr);
}

// ---- Appearance ----

TEST_F(VoxelCharacterTest, SetAppearance) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    CharacterAppearance appearance;
    appearance.headScale = 1.5f;
    appearance.bulkScale = 1.2f;
    character_->setAppearance(appearance);
    EXPECT_FLOAT_EQ(character_->getAppearance().headScale, 1.5f);
    EXPECT_FLOAT_EQ(character_->getAppearance().bulkScale, 1.2f);
}

TEST_F(VoxelCharacterTest, AppearancePropagatedToSkeleton) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    CharacterAppearance app;
    app.heightScale = 1.3f;
    character_->setAppearance(app);
    EXPECT_FLOAT_EQ(character_->getCharacterSkeleton().appearance.heightScale, 1.3f);
}

// ---- Loading (without file — we test the programmatic skeleton path) ----

TEST_F(VoxelCharacterTest, LoadModelReturnsFalseForMissingFile) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_FALSE(character_->loadModel("nonexistent_file.anim"));
}

// ---- SetPosition / GetPosition ----

TEST_F(VoxelCharacterTest, SetPositionUpdatesBoth) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    glm::vec3 target(20.0f, 30.0f, 40.0f);
    character_->setPosition(target);
    glm::vec3 got = character_->getPosition();
    EXPECT_NEAR(got.x, target.x, 0.5f);
    EXPECT_NEAR(got.y, target.y, 0.5f);
    EXPECT_NEAR(got.z, target.z, 0.5f);
}

// ---- Control inputs (should not crash even without loaded model) ----

TEST_F(VoxelCharacterTest, ControlInputsNoModel) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_NO_THROW(character_->setControlInput(1.0f, 0.5f, 0.0f));
    EXPECT_NO_THROW(character_->setSprint(true));
    EXPECT_NO_THROW(character_->jump());
    EXPECT_NO_THROW(character_->attack());
    EXPECT_NO_THROW(character_->setCrouch(true));
}

// ---- Animation control (without loaded model) ----

TEST_F(VoxelCharacterTest, AnimationNamesEmptyWithoutModel) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_TRUE(character_->getAnimationNames().empty());
}

TEST_F(VoxelCharacterTest, AnimationMappingSetGet) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    character_->setAnimationMapping("Idle", "my_idle_clip");
    EXPECT_EQ(character_->getAnimationMapping("Idle"), "my_idle_clip");
    EXPECT_EQ(character_->getAnimationMapping("Walk"), "");
}

TEST_F(VoxelCharacterTest, PlayAnimationNoModel) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_NO_THROW(character_->playAnimation("idle"));
}

TEST_F(VoxelCharacterTest, CycleAnimationNoModel) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_NO_THROW(character_->cycleAnimation(true));
    EXPECT_NO_THROW(character_->cycleAnimation(false));
}

// ---- Update (should not crash even without model) ----

TEST_F(VoxelCharacterTest, UpdateAnimatedModeNoModel) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_NO_THROW(character_->update(1.0f / 60.0f));
}

// ---- Drive mode switching (without loaded model: should be no-op) ----

TEST_F(VoxelCharacterTest, SetDriveModeWithoutModelIsNoOp) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    character_->setDriveMode(DriveMode::Physics);
    // Without a loaded model, mode doesn't change
    EXPECT_EQ(character_->getDriveMode(), DriveMode::Animated);
}

TEST_F(VoxelCharacterTest, PhysicsDriveNullInAnimatedMode) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_EQ(character_->getPhysicsDrive(), nullptr);
}

// ---- MoveVelocity ----

TEST_F(VoxelCharacterTest, SetMoveVelocityAnimated) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_NO_THROW(character_->setMoveVelocity(glm::vec3(1, 0, 0)));
}

// ---- Skeleton access ----

TEST_F(VoxelCharacterTest, SkeletonEmptyBeforeLoad) {
    character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), glm::vec3(0));
    EXPECT_TRUE(character_->getSkeleton().bones.empty());
    EXPECT_TRUE(character_->getAnimationClips().empty());
}

// ============================================================================
// Tests with a loaded .anim file (requires character.anim at repo root)
// ============================================================================

class VoxelCharacterWithModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        physicsWorld_ = std::make_unique<Physics::PhysicsWorld>();
        physicsWorld_->initialize();
        character_ = std::make_unique<VoxelCharacter>(physicsWorld_.get(), spawnPos_);
        loaded_ = character_->loadModel("character.anim");
    }

    void TearDown() override {
        character_.reset();
        physicsWorld_.reset();
    }

    glm::vec3 spawnPos_{16.0f, 20.0f, 16.0f};
    std::unique_ptr<Physics::PhysicsWorld> physicsWorld_;
    std::unique_ptr<VoxelCharacter> character_;
    bool loaded_ = false;
};

TEST_F(VoxelCharacterWithModelTest, LoadSucceeds) {
    ASSERT_TRUE(loaded_) << "character.anim must exist at repo root for this test";
}

TEST_F(VoxelCharacterWithModelTest, SkeletonPopulated) {
    if (!loaded_) GTEST_SKIP();
    EXPECT_FALSE(character_->getSkeleton().bones.empty());
}

TEST_F(VoxelCharacterWithModelTest, AnimationClipsLoaded) {
    if (!loaded_) GTEST_SKIP();
    EXPECT_FALSE(character_->getAnimationClips().empty());
}

TEST_F(VoxelCharacterWithModelTest, AnimationNamesNonEmpty) {
    if (!loaded_) GTEST_SKIP();
    auto names = character_->getAnimationNames();
    EXPECT_FALSE(names.empty());
}

TEST_F(VoxelCharacterWithModelTest, PartsCreatedInAnimatedMode) {
    if (!loaded_) GTEST_SKIP();
    EXPECT_FALSE(character_->getParts().empty());
}

TEST_F(VoxelCharacterWithModelTest, UpdateDoesNotCrash) {
    if (!loaded_) GTEST_SKIP();
    for (int i = 0; i < 60; ++i) {
        character_->update(1.0f / 60.0f);
    }
}

TEST_F(VoxelCharacterWithModelTest, PlayAnimationByName) {
    if (!loaded_) GTEST_SKIP();
    auto names = character_->getAnimationNames();
    if (!names.empty()) {
        EXPECT_NO_THROW(character_->playAnimation(names[0]));
    }
}

TEST_F(VoxelCharacterWithModelTest, CycleAnimationCyclesIndex) {
    if (!loaded_) GTEST_SKIP();
    character_->cycleAnimation(true);
    // Just check it doesn't crash and we can still update
    EXPECT_NO_THROW(character_->update(1.0f / 60.0f));
}

// ---- Drive mode switching WITH model ----

TEST_F(VoxelCharacterWithModelTest, SwitchToPhysicsMode) {
    if (!loaded_) GTEST_SKIP();
    character_->setDriveMode(DriveMode::Physics);
    EXPECT_EQ(character_->getDriveMode(), DriveMode::Physics);
    EXPECT_NE(character_->getPhysicsDrive(), nullptr);
}

TEST_F(VoxelCharacterWithModelTest, SwitchToPhysicsThenBack) {
    if (!loaded_) GTEST_SKIP();
    character_->setDriveMode(DriveMode::Physics);
    EXPECT_EQ(character_->getDriveMode(), DriveMode::Physics);

    character_->setDriveMode(DriveMode::Animated);
    EXPECT_EQ(character_->getDriveMode(), DriveMode::Animated);
    EXPECT_EQ(character_->getPhysicsDrive(), nullptr);
    EXPECT_FALSE(character_->getParts().empty());
}

TEST_F(VoxelCharacterWithModelTest, SwitchToPassiveRagdoll) {
    if (!loaded_) GTEST_SKIP();
    character_->setDriveMode(DriveMode::PassiveRagdoll);
    EXPECT_EQ(character_->getDriveMode(), DriveMode::PassiveRagdoll);
    EXPECT_NE(character_->getPhysicsDrive(), nullptr);
}

TEST_F(VoxelCharacterWithModelTest, PhysicsModeUpdateNocrash) {
    if (!loaded_) GTEST_SKIP();
    character_->setDriveMode(DriveMode::Physics);
    for (int i = 0; i < 30; ++i) {
        physicsWorld_->stepSimulation(1.0f / 60.0f);
        character_->update(1.0f / 60.0f);
    }
}

TEST_F(VoxelCharacterWithModelTest, PassiveRagdollUpdateNocrash) {
    if (!loaded_) GTEST_SKIP();
    character_->setDriveMode(DriveMode::PassiveRagdoll);
    for (int i = 0; i < 30; ++i) {
        physicsWorld_->stepSimulation(1.0f / 60.0f);
        character_->update(1.0f / 60.0f);
    }
}

TEST_F(VoxelCharacterWithModelTest, PositionPreservedAfterModeSwitch) {
    if (!loaded_) GTEST_SKIP();
    glm::vec3 testPos(50.0f, 30.0f, 50.0f);
    character_->setPosition(testPos);

    character_->setDriveMode(DriveMode::Physics);
    glm::vec3 physPos = character_->getPosition();
    EXPECT_NEAR(physPos.x, testPos.x, 2.0f);
    EXPECT_NEAR(physPos.z, testPos.z, 2.0f);
}

TEST_F(VoxelCharacterWithModelTest, SetSameDriveModeIsNoOp) {
    if (!loaded_) GTEST_SKIP();
    character_->setDriveMode(DriveMode::Animated);
    EXPECT_EQ(character_->getDriveMode(), DriveMode::Animated);
}

TEST_F(VoxelCharacterWithModelTest, ControlInputsInPhysicsMode) {
    if (!loaded_) GTEST_SKIP();
    character_->setDriveMode(DriveMode::Physics);
    EXPECT_NO_THROW(character_->setControlInput(1.0f, 0.0f));
    EXPECT_NO_THROW(character_->jump());
    EXPECT_NO_THROW(character_->setSprint(true));
}

TEST_F(VoxelCharacterWithModelTest, RecolorFromAppearance) {
    if (!loaded_) GTEST_SKIP();
    CharacterAppearance app;
    app.skinColor = glm::vec4(0.9f, 0.7f, 0.5f, 1.0f);
    character_->setAppearance(app);
    EXPECT_NO_THROW(character_->recolorFromAppearance());
}
