#include <gtest/gtest.h>
#include "scene/CharacterSkeleton.h"
#include <glm/glm.hpp>

using namespace Phyxel;
using namespace Phyxel::Scene;

// ============================================================================
// BoneFilterConfig
// ============================================================================

TEST(BoneFilterConfigTest, SkipsFingerBones) {
    BoneFilterConfig filter;
    EXPECT_TRUE(filter.shouldSkip("LeftThumb1"));
    EXPECT_TRUE(filter.shouldSkip("RightIndex3"));
    EXPECT_TRUE(filter.shouldSkip("LeftMiddle2"));
    EXPECT_TRUE(filter.shouldSkip("RightRing1"));
    EXPECT_TRUE(filter.shouldSkip("LeftPinky"));
}

TEST(BoneFilterConfigTest, SkipsSmallBones) {
    BoneFilterConfig filter;
    EXPECT_TRUE(filter.shouldSkip("LeftEye"));
    EXPECT_TRUE(filter.shouldSkip("RightToe"));
    EXPECT_TRUE(filter.shouldSkip("HeadEnd"));
}

TEST(BoneFilterConfigTest, KeepsMajorBones) {
    BoneFilterConfig filter;
    EXPECT_FALSE(filter.shouldSkip("Hips"));
    EXPECT_FALSE(filter.shouldSkip("Spine1"));
    EXPECT_FALSE(filter.shouldSkip("Head"));
    EXPECT_FALSE(filter.shouldSkip("LeftArm"));
    EXPECT_FALSE(filter.shouldSkip("RightLeg"));
    EXPECT_FALSE(filter.shouldSkip("LeftFoot"));
    EXPECT_FALSE(filter.shouldSkip("RightHand"));
}

TEST(BoneFilterConfigTest, CaseInsensitive) {
    BoneFilterConfig filter;
    EXPECT_TRUE(filter.shouldSkip("LEFTTHUMB"));
    EXPECT_TRUE(filter.shouldSkip("rightEYE"));
    EXPECT_FALSE(filter.shouldSkip("SPINE"));
}

// ============================================================================
// Helper: Build a simple humanoid skeleton for testing
// ============================================================================

static CharacterSkeleton makeTestHumanoid() {
    CharacterSkeleton cs;
    auto& skel = cs.skeleton;

    // Build a minimal humanoid skeleton
    skel.addBone("Hips", -1, glm::vec3(0, 0.9f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("Spine", 0, glm::vec3(0, 0.3f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("Head", 1, glm::vec3(0, 0.4f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("LeftArm", 1, glm::vec3(-0.3f, 0.2f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("LeftForearm", 3, glm::vec3(-0.25f, 0, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("LeftHand", 4, glm::vec3(-0.2f, 0, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("RightArm", 1, glm::vec3(0.3f, 0.2f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("RightForearm", 6, glm::vec3(0.25f, 0, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("RightHand", 7, glm::vec3(0.2f, 0, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("LeftLeg", 0, glm::vec3(-0.15f, -0.4f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("LeftShin", 9, glm::vec3(0, -0.4f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("LeftFoot", 10, glm::vec3(0, -0.1f, 0.1f), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("RightLeg", 0, glm::vec3(0.15f, -0.4f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("RightShin", 12, glm::vec3(0, -0.4f, 0), glm::quat(1,0,0,0), glm::vec3(1));
    skel.addBone("RightFoot", 13, glm::vec3(0, -0.1f, 0.1f), glm::quat(1,0,0,0), glm::vec3(1));
    // Add a filtered bone
    skel.addBone("LeftThumb1", 5, glm::vec3(-0.05f, 0, 0), glm::quat(1,0,0,0), glm::vec3(1));

    return cs;
}

// ============================================================================
// Children map
// ============================================================================

TEST(CharacterSkeletonTest, BuildChildrenMap) {
    auto cs = makeTestHumanoid();
    auto children = cs.buildChildrenMap();

    // Hips (id=0) has children: Spine(1), LeftLeg(9), RightLeg(12)
    ASSERT_EQ(children[0].size(), 3u);

    // Spine (id=1) has children: Head(2), LeftArm(3), RightArm(6)
    ASSERT_EQ(children[1].size(), 3u);

    // Head (id=2) has no children
    EXPECT_TRUE(children.find(2) == children.end() || children[2].empty());
}

// ============================================================================
// Physics bone IDs (filtering)
// ============================================================================

TEST(CharacterSkeletonTest, GetPhysicsBoneIdsExcludesFiltered) {
    auto cs = makeTestHumanoid();
    auto ids = cs.getPhysicsBoneIds();

    // Should have 15 bones, minus LeftThumb1 = 14 + LeftHand and RightHand
    // Actually: 16 total bones, LeftThumb1 is filtered → 15 physics bones
    EXPECT_EQ(ids.size(), 15u);

    // LeftThumb1 (id=15) should NOT be in the list
    for (int id : ids) {
        EXPECT_NE(cs.skeleton.bones[id].name, "LeftThumb1");
    }
}

// ============================================================================
// Compute bone sizes
// ============================================================================

TEST(CharacterSkeletonTest, ComputeBoneSizesProducesSizes) {
    auto cs = makeTestHumanoid();
    cs.computeBoneSizes();

    // All non-filtered bones should have sizes
    auto ids = cs.getPhysicsBoneIds();
    for (int id : ids) {
        EXPECT_TRUE(cs.boneSizes.count(id) > 0)
            << "Missing bone size for: " << cs.skeleton.bones[id].name;
        // Size should be positive in all dimensions
        auto size = cs.boneSizes[id];
        EXPECT_GT(size.x, 0.0f);
        EXPECT_GT(size.y, 0.0f);
        EXPECT_GT(size.z, 0.0f);
    }

    // Filtered bones should NOT have sizes
    EXPECT_EQ(cs.boneSizes.count(15), 0u); // LeftThumb1
}

TEST(CharacterSkeletonTest, ComputeBoneMasses) {
    auto cs = makeTestHumanoid();
    cs.computeBoneSizes();

    for (int id : cs.getPhysicsBoneIds()) {
        EXPECT_TRUE(cs.boneMasses.count(id) > 0);
        EXPECT_GT(cs.boneMasses[id], 0.0f);
    }
}

// ============================================================================
// Joint definition generation
// ============================================================================

TEST(CharacterSkeletonTest, GenerateJointDefsCreatesJoints) {
    auto cs = makeTestHumanoid();
    cs.computeBoneSizes();
    cs.generateJointDefs();

    // Root bone (Hips, id=0) should NOT have a joint (no parent)
    EXPECT_EQ(cs.jointDefs.count(0), 0u);

    // Every non-root physics bone should have a joint
    for (int id : cs.getPhysicsBoneIds()) {
        if (cs.skeleton.bones[id].parentId >= 0) {
            EXPECT_TRUE(cs.jointDefs.count(id) > 0)
                << "Missing joint for: " << cs.skeleton.bones[id].name;
        }
    }
}

TEST(CharacterSkeletonTest, JointTypeHeuristics) {
    auto cs = makeTestHumanoid();
    cs.computeBoneSizes();
    cs.generateJointDefs();

    // Head → ConeTwist (neck joint)
    ASSERT_TRUE(cs.jointDefs.count(2));
    EXPECT_EQ(cs.jointDefs[2].type, JointType::ConeTwist);

    // LeftArm (shoulder) → ConeTwist
    ASSERT_TRUE(cs.jointDefs.count(3));
    EXPECT_EQ(cs.jointDefs[3].type, JointType::ConeTwist);

    // LeftForearm (elbow) → Hinge
    ASSERT_TRUE(cs.jointDefs.count(4));
    EXPECT_EQ(cs.jointDefs[4].type, JointType::Hinge);

    // LeftShin (knee) → Hinge
    ASSERT_TRUE(cs.jointDefs.count(10));
    EXPECT_EQ(cs.jointDefs[10].type, JointType::Hinge);

    // LeftLeg (hip) → Hinge
    ASSERT_TRUE(cs.jointDefs.count(9));
    EXPECT_EQ(cs.jointDefs[9].type, JointType::Hinge);

    // LeftFoot (ankle) → Hinge
    ASSERT_TRUE(cs.jointDefs.count(11));
    EXPECT_EQ(cs.jointDefs[11].type, JointType::Hinge);

    // LeftHand (wrist) → Hinge
    ASSERT_TRUE(cs.jointDefs.count(5));
    EXPECT_EQ(cs.jointDefs[5].type, JointType::Hinge);

    // Spine → ConeTwist
    ASSERT_TRUE(cs.jointDefs.count(1));
    EXPECT_EQ(cs.jointDefs[1].type, JointType::ConeTwist);
}

TEST(CharacterSkeletonTest, JointParentChildIds) {
    auto cs = makeTestHumanoid();
    cs.computeBoneSizes();
    cs.generateJointDefs();

    // Spine (id=1) parent should be Hips (id=0)
    EXPECT_EQ(cs.jointDefs[1].parentBoneId, 0);
    EXPECT_EQ(cs.jointDefs[1].childBoneId, 1);

    // Head (id=2) parent should be Spine (id=1)
    EXPECT_EQ(cs.jointDefs[2].parentBoneId, 1);
    EXPECT_EQ(cs.jointDefs[2].childBoneId, 2);
}

TEST(CharacterSkeletonTest, KneeElbowLimitsCorrect) {
    auto cs = makeTestHumanoid();
    cs.computeBoneSizes();
    cs.generateJointDefs();

    // Knee: bends backward (low=-1.5, high=0.0)
    auto& knee = cs.jointDefs[10]; // LeftShin
    EXPECT_LE(knee.hingeLimitLow, 0.0f);
    EXPECT_LE(knee.hingeLimitHigh, 0.0f);

    // Elbow: bends forward (low=0.0, high=1.5)
    auto& elbow = cs.jointDefs[4]; // LeftForearm
    EXPECT_GE(elbow.hingeLimitLow, 0.0f);
    EXPECT_GE(elbow.hingeLimitHigh, 0.0f);
}

// ============================================================================
// Loading from .anim file (if available)
// ============================================================================

TEST(CharacterSkeletonTest, LoadFromAnimFile) {
    CharacterSkeleton cs;
    // character.anim is at the project root and may or may not be accessible
    // from the test working directory
    bool loaded = cs.loadFromAnimFile("character.anim");
    if (!loaded) {
        // Try relative paths that tests might run from
        loaded = cs.loadFromAnimFile("../../character.anim");
    }
    if (!loaded) {
        GTEST_SKIP() << "character.anim not found, skipping file load test";
    }

    EXPECT_GT(cs.skeleton.bones.size(), 0u);
    EXPECT_FALSE(cs.jointDefs.empty());
    EXPECT_FALSE(cs.boneSizes.empty());

    // Verify that joint defs were created for non-root bones
    bool hasConeTwist = false, hasHinge = false;
    for (const auto& [id, joint] : cs.jointDefs) {
        if (joint.type == JointType::ConeTwist) hasConeTwist = true;
        if (joint.type == JointType::Hinge) hasHinge = true;
    }
    EXPECT_TRUE(hasConeTwist);
    EXPECT_TRUE(hasHinge);
}

// ============================================================================
// Motor strength varies by joint
// ============================================================================

TEST(CharacterSkeletonTest, MotorStrengthVariesByJoint) {
    auto cs = makeTestHumanoid();
    cs.computeBoneSizes();
    cs.generateJointDefs();

    // Spine should have higher motor strength than hands
    float spineMotor = cs.jointDefs[1].motorStrength;  // Spine
    float handMotor  = cs.jointDefs[5].motorStrength;   // LeftHand
    EXPECT_GT(spineMotor, handMotor);
}
