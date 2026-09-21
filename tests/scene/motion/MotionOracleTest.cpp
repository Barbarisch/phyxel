#include <gtest/gtest.h>

#include "scene/motion/MotionOracle.h"

#include <limits>

using namespace Phyxel::Scene::Motion;

namespace {
OracleFrame frame(float angle, float footX = 0.0f) {
    OracleFrame value;
    value.localRotations = {glm::angleAxis(angle, glm::vec3(0, 1, 0)), glm::quat(1,0,0,0)};
    value.worldJointPositions = {{0, 1, 0}, {footX, 0, 0}};
    value.plantedJoints = {false, true};
    value.generatedRootVelocity = {1, 0, 0};
    value.capsuleVelocity = {1, 0, 0};
    return value;
}
}

TEST(MotionOracle, KnownSmoothSequenceHasBoundedMetrics) {
    const auto metrics = evaluateMotion({frame(0.0f), frame(0.1f), frame(0.2f)}, 0.1f, {{0,1}});
    ASSERT_TRUE(metrics.valid);
    EXPECT_NEAR(metrics.maxAngularVelocity, 1.0f, 1.0e-4f);
    EXPECT_NEAR(metrics.maxAngularAcceleration, 0.0f, 1.0e-3f);
    EXPECT_FLOAT_EQ(metrics.maxPlantedJointSpeed, 0.0f);
    EXPECT_FLOAT_EQ(metrics.maxRootVelocityError, 0.0f);
}

TEST(MotionOracle, SyntheticPopAndFootSlideFailTheirMetrics) {
    auto bad = frame(2.5f, 0.5f);
    bad.generatedRootVelocity = {3, 0, 0};
    const auto metrics = evaluateMotion({frame(0.0f), bad}, 0.1f, {{0,1}});
    ASSERT_TRUE(metrics.valid);
    EXPECT_GT(metrics.maxPoseDeltaRadians, 2.0f);
    EXPECT_GT(metrics.maxPlantedJointSpeed, 4.0f);
    EXPECT_GT(metrics.maxRootVelocityError, 1.9f);
    EXPECT_GT(metrics.maxChainLengthError, 0.1f);
}

TEST(MotionOracle, RejectsNonFinitePose) {
    auto bad = frame(0.0f);
    bad.localRotations[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(evaluateMotion({frame(0.0f), bad}, 0.1f).valid);
}
