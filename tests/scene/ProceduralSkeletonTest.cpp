#include <gtest/gtest.h>
#include "scene/ProceduralSkeleton.h"
#include <glm/glm.hpp>
#include <set>
#include <algorithm>

using namespace Phyxel;
using namespace Phyxel::Scene;

// ============================================================================
// HumanoidParams tests
// ============================================================================

TEST(HumanoidParamsTest, DefaultValues) {
    HumanoidParams params;
    EXPECT_FLOAT_EQ(params.height, 1.8f);
    EXPECT_FLOAT_EQ(params.bulk, 1.0f);
    EXPECT_GT(params.headRatio, 0.0f);
    EXPECT_GT(params.torsoRatio, 0.0f);
    EXPECT_GT(params.legRatio, 0.0f);
    EXPECT_GT(params.armRatio, 0.0f);
}

TEST(HumanoidParamsTest, ProportionsSumToLessThanOne) {
    HumanoidParams params;
    float sum = params.headRatio + params.torsoRatio + params.legRatio;
    EXPECT_LE(sum, 1.0f);
}

// ============================================================================
// Humanoid Skeleton tests
// ============================================================================

TEST(ProceduralSkeletonTest, HumanoidDefaultBoneCount) {
    auto cs = ProceduralSkeleton::humanoid();
    // 19 bones: Hips, Spine, Chest, Neck, Head, 2x(Shoulder,Arm,ForeArm,Hand), 2x(UpLeg,Leg,Foot)
    EXPECT_EQ(cs.skeleton.bones.size(), 19u);
}

TEST(ProceduralSkeletonTest, HumanoidHasRequiredBones) {
    auto cs = ProceduralSkeleton::humanoid();
    const auto& boneMap = cs.skeleton.boneMap;

    std::vector<std::string> required = {
        "Hips", "Spine", "Chest", "Neck", "Head",
        "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
        "RightShoulder", "RightArm", "RightForeArm", "RightHand",
        "LeftUpLeg", "LeftLeg", "LeftFoot",
        "RightUpLeg", "RightLeg", "RightFoot"
    };

    for (const auto& name : required) {
        EXPECT_NE(boneMap.find(name), boneMap.end()) << "Missing bone: " << name;
    }
}

TEST(ProceduralSkeletonTest, HumanoidRootIsHips) {
    auto cs = ProceduralSkeleton::humanoid();
    ASSERT_FALSE(cs.skeleton.bones.empty());
    EXPECT_EQ(cs.skeleton.bones[0].name, "Hips");
    EXPECT_EQ(cs.skeleton.bones[0].parentId, -1);
}

TEST(ProceduralSkeletonTest, HumanoidAllBonesHaveParent) {
    auto cs = ProceduralSkeleton::humanoid();
    for (size_t i = 1; i < cs.skeleton.bones.size(); ++i) {
        EXPECT_GE(cs.skeleton.bones[i].parentId, 0)
            << "Bone " << cs.skeleton.bones[i].name << " has no parent";
    }
}

TEST(ProceduralSkeletonTest, HumanoidHasVoxelShapes) {
    auto cs = ProceduralSkeleton::humanoid();
    EXPECT_FALSE(cs.voxelModel.shapes.empty());
    EXPECT_GE(cs.voxelModel.shapes.size(), cs.skeleton.bones.size());
}

TEST(ProceduralSkeletonTest, HumanoidShapesHavePositiveSize) {
    auto cs = ProceduralSkeleton::humanoid();
    for (const auto& shape : cs.voxelModel.shapes) {
        EXPECT_GT(shape.size.x, 0.0f) << "Shape for bone " << shape.boneId;
        EXPECT_GT(shape.size.y, 0.0f) << "Shape for bone " << shape.boneId;
        EXPECT_GT(shape.size.z, 0.0f) << "Shape for bone " << shape.boneId;
    }
}

TEST(ProceduralSkeletonTest, HumanoidJointDefsGenerated) {
    auto cs = ProceduralSkeleton::humanoid();
    EXPECT_FALSE(cs.jointDefs.empty());
    // All bones except root should have a joint
    EXPECT_GE(cs.jointDefs.size(), cs.skeleton.bones.size() - 1);
}

TEST(ProceduralSkeletonTest, HumanoidBoneSizesComputed) {
    auto cs = ProceduralSkeleton::humanoid();
    EXPECT_FALSE(cs.boneSizes.empty());
}

TEST(ProceduralSkeletonTest, HumanoidCustomHeight) {
    HumanoidParams params;
    params.height = 3.0f;
    auto cs = ProceduralSkeleton::humanoid(params);

    // Hips should be at legRatio * height
    float expectedHipY = 3.0f * params.legRatio;
    EXPECT_NEAR(cs.skeleton.bones[0].localPosition.y, expectedHipY, 0.01f);
}

TEST(ProceduralSkeletonTest, HumanoidSymmetry) {
    auto cs = ProceduralSkeleton::humanoid();
    auto& bones = cs.skeleton.bones;
    auto& boneMap = cs.skeleton.boneMap;

    // Left and right upper legs should be mirrored on X
    int leftLeg = boneMap.at("LeftUpLeg");
    int rightLeg = boneMap.at("RightUpLeg");
    EXPECT_NEAR(bones[leftLeg].localPosition.x, -bones[rightLeg].localPosition.x, 0.001f);
    EXPECT_NEAR(bones[leftLeg].localPosition.y, bones[rightLeg].localPosition.y, 0.001f);
}

TEST(ProceduralSkeletonTest, HumanoidBulkScalesWidths) {
    HumanoidParams thin;
    thin.bulk = 0.5f;
    HumanoidParams wide;
    wide.bulk = 2.0f;

    auto csThin = ProceduralSkeleton::humanoid(thin);
    auto csWide = ProceduralSkeleton::humanoid(wide);

    // Chest shape should be wider for bulk=2 than bulk=0.5
    auto findChestShapeWidth = [](const CharacterSkeleton& cs) -> float {
        for (const auto& shape : cs.voxelModel.shapes) {
            if (shape.boneId == cs.skeleton.boneMap.at("Chest"))
                return shape.size.x;
        }
        return 0.0f;
    };

    EXPECT_GT(findChestShapeWidth(csWide), findChestShapeWidth(csThin));
}

// ============================================================================
// Quadruped Skeleton tests
// ============================================================================

TEST(ProceduralSkeletonTest, QuadrupedDefaultBoneCount) {
    auto cs = ProceduralSkeleton::quadruped();
    // 14 named + optional tail = 14 or 15
    EXPECT_GE(cs.skeleton.bones.size(), 13u);
    EXPECT_LE(cs.skeleton.bones.size(), 20u);
}

TEST(ProceduralSkeletonTest, QuadrupedHasRequiredBones) {
    auto cs = ProceduralSkeleton::quadruped();
    const auto& boneMap = cs.skeleton.boneMap;

    std::vector<std::string> required = {
        "Hips", "Spine", "Chest", "Neck", "Head",
        "FrontLeftUpLeg", "FrontLeftLeg",
        "FrontRightUpLeg", "FrontRightLeg",
        "RearLeftUpLeg", "RearLeftLeg",
        "RearRightUpLeg", "RearRightLeg"
    };

    for (const auto& name : required) {
        EXPECT_NE(boneMap.find(name), boneMap.end()) << "Missing bone: " << name;
    }
}

TEST(ProceduralSkeletonTest, QuadrupedHasTailByDefault) {
    auto cs = ProceduralSkeleton::quadruped();
    EXPECT_NE(cs.skeleton.boneMap.find("Tail"), cs.skeleton.boneMap.end());
}

TEST(ProceduralSkeletonTest, QuadrupedNoTailWhenZeroLength) {
    QuadrupedParams params;
    params.tailLength = 0.0f;
    auto cs = ProceduralSkeleton::quadruped(params);
    EXPECT_EQ(cs.skeleton.boneMap.find("Tail"), cs.skeleton.boneMap.end());
}

TEST(ProceduralSkeletonTest, QuadrupedJointsGenerated) {
    auto cs = ProceduralSkeleton::quadruped();
    EXPECT_FALSE(cs.jointDefs.empty());
}

TEST(ProceduralSkeletonTest, QuadrupedShapesPresent) {
    auto cs = ProceduralSkeleton::quadruped();
    EXPECT_FALSE(cs.voxelModel.shapes.empty());
}

// ============================================================================
// Arachnid Skeleton tests
// ============================================================================

TEST(ProceduralSkeletonTest, ArachnidDefaultLegCount) {
    auto cs = ProceduralSkeleton::arachnid();
    // Body + 8 legs × 2 segments = 17 bones
    EXPECT_EQ(cs.skeleton.bones.size(), 17u);
}

TEST(ProceduralSkeletonTest, ArachnidCustomLegCount) {
    ArachnidParams params;
    params.legCount = 6;
    auto cs = ProceduralSkeleton::arachnid(params);
    // Body + 6 legs × 2 segments = 13
    EXPECT_EQ(cs.skeleton.bones.size(), 13u);
}

TEST(ProceduralSkeletonTest, ArachnidOddLegCountRoundedUp) {
    ArachnidParams params;
    params.legCount = 5; // should become 6
    auto cs = ProceduralSkeleton::arachnid(params);
    // Body + 6 legs × 2 = 13
    EXPECT_EQ(cs.skeleton.bones.size(), 13u);
}

TEST(ProceduralSkeletonTest, ArachnidMinLegCount) {
    ArachnidParams params;
    params.legCount = 2; // clamped to 4
    auto cs = ProceduralSkeleton::arachnid(params);
    // Body + 4 legs × 2 = 9
    EXPECT_EQ(cs.skeleton.bones.size(), 9u);
}

TEST(ProceduralSkeletonTest, ArachnidRootIsBody) {
    auto cs = ProceduralSkeleton::arachnid();
    EXPECT_EQ(cs.skeleton.bones[0].name, "Body");
    EXPECT_EQ(cs.skeleton.bones[0].parentId, -1);
}

TEST(ProceduralSkeletonTest, ArachnidShapesPresent) {
    auto cs = ProceduralSkeleton::arachnid();
    EXPECT_FALSE(cs.voxelModel.shapes.empty());
}

TEST(ProceduralSkeletonTest, ArachnidJointsGenerated) {
    auto cs = ProceduralSkeleton::arachnid();
    EXPECT_FALSE(cs.jointDefs.empty());
}

// ============================================================================
// Walk Cycle tests
// ============================================================================

TEST(ProceduralSkeletonTest, HumanoidWalkCycleValid) {
    auto cs = ProceduralSkeleton::humanoid();
    auto clip = ProceduralSkeleton::humanoidWalkCycle(cs);

    EXPECT_EQ(clip.name, "walk");
    EXPECT_GT(clip.duration, 0.0f);
    EXPECT_GT(clip.speed, 0.0f);
    EXPECT_FALSE(clip.channels.empty());
}

TEST(ProceduralSkeletonTest, HumanoidWalkCycleCustomSpeed) {
    auto cs = ProceduralSkeleton::humanoid();
    auto clip = ProceduralSkeleton::humanoidWalkCycle(cs, 5.0f);
    EXPECT_FLOAT_EQ(clip.speed, 5.0f);
}

TEST(ProceduralSkeletonTest, HumanoidWalkCycleHasLegChannels) {
    auto cs = ProceduralSkeleton::humanoid();
    auto clip = ProceduralSkeleton::humanoidWalkCycle(cs);

    std::set<int> animatedBones;
    for (const auto& ch : clip.channels) {
        animatedBones.insert(ch.boneId);
    }

    // Should animate at least the upper legs
    EXPECT_NE(animatedBones.find(cs.skeleton.boneMap.at("LeftUpLeg")), animatedBones.end());
    EXPECT_NE(animatedBones.find(cs.skeleton.boneMap.at("RightUpLeg")), animatedBones.end());
}

TEST(ProceduralSkeletonTest, HumanoidIdleCycleValid) {
    auto cs = ProceduralSkeleton::humanoid();
    auto clip = ProceduralSkeleton::humanoidIdleCycle(cs);

    EXPECT_EQ(clip.name, "idle");
    EXPECT_GT(clip.duration, 0.0f);
    EXPECT_FLOAT_EQ(clip.speed, 0.0f);
    EXPECT_FALSE(clip.channels.empty());
}

TEST(ProceduralSkeletonTest, QuadrupedWalkCycleValid) {
    auto cs = ProceduralSkeleton::quadruped();
    auto clip = ProceduralSkeleton::quadrupedWalkCycle(cs);

    EXPECT_EQ(clip.name, "walk");
    EXPECT_GT(clip.duration, 0.0f);
    EXPECT_FALSE(clip.channels.empty());
}

TEST(ProceduralSkeletonTest, ArachnidWalkCycleValid) {
    auto cs = ProceduralSkeleton::arachnid();
    auto clip = ProceduralSkeleton::arachnidWalkCycle(cs);

    EXPECT_EQ(clip.name, "walk");
    EXPECT_GT(clip.duration, 0.0f);
    EXPECT_FALSE(clip.channels.empty());
}

// ============================================================================
// Integration: procedural skeleton works with CharacterSkeleton API
// ============================================================================

TEST(ProceduralSkeletonTest, HumanoidPhysicsBoneIdsValid) {
    auto cs = ProceduralSkeleton::humanoid();
    auto physBones = cs.getPhysicsBoneIds();
    EXPECT_FALSE(physBones.empty());

    // All IDs should be valid bone indices
    for (int id : physBones) {
        EXPECT_GE(id, 0);
        EXPECT_LT(id, static_cast<int>(cs.skeleton.bones.size()));
    }
}

TEST(ProceduralSkeletonTest, HumanoidChildrenMapValid) {
    auto cs = ProceduralSkeleton::humanoid();
    auto children = cs.buildChildrenMap();

    // Hips (bone 0) should have children
    EXPECT_FALSE(children[0].empty());

    // All children should reference valid parents
    for (auto& [parentId, childIds] : children) {
        for (int childId : childIds) {
            EXPECT_EQ(cs.skeleton.bones[childId].parentId, parentId);
        }
    }
}

TEST(ProceduralSkeletonTest, AllBoneNamesUnique) {
    auto cs = ProceduralSkeleton::humanoid();
    std::set<std::string> names;
    for (const auto& bone : cs.skeleton.bones) {
        EXPECT_TRUE(names.insert(bone.name).second) << "Duplicate bone: " << bone.name;
    }
}
