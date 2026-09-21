#include <gtest/gtest.h>

#include "scene/motion/MotionSource.h"

#include <limits>

using namespace Phyxel::Scene::Motion;

namespace {
LocalPoseFrame validFrame() {
    LocalPoseFrame frame;
    frame.timeSeconds = 1.25;
    frame.rootTranslation = {1.0f, 2.0f, 3.0f};
    frame.jointNames = {"hips"};
    frame.localRotations = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    return frame;
}
}

TEST(MotionSource, RejectsMalformedAndNonFiniteFrames) {
    LocalPoseFrame frame = validFrame();
    EXPECT_TRUE(frame.structurallyValid());

    frame.localRotations.clear();
    EXPECT_FALSE(frame.structurallyValid());
    frame = validFrame();
    frame.localRotations[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(frame.structurallyValid());
    frame = validFrame();
    frame.rootTranslation.z = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(frame.structurallyValid());
}

TEST(MotionSource, InvalidProviderPreservesExactClipPose) {
    LocalPoseFrame clip = validFrame();
    clip.provenance = MotionProvenance::Clip;
    LocalPoseFrame invalid = validFrame();
    invalid.localRotations.clear();

    const LocalPoseFrame selected = providerOrFallback(&invalid, clip);
    EXPECT_EQ(selected.provenance, MotionProvenance::Fallback);
    EXPECT_EQ(selected.timeSeconds, clip.timeSeconds);
    EXPECT_EQ(selected.rootTranslation, clip.rootTranslation);
    ASSERT_EQ(selected.localRotations.size(), clip.localRotations.size());
    EXPECT_EQ(selected.localRotations[0].w, clip.localRotations[0].w);
    EXPECT_EQ(selected.localRotations[0].x, clip.localRotations[0].x);
    EXPECT_EQ(selected.localRotations[0].y, clip.localRotations[0].y);
    EXPECT_EQ(selected.localRotations[0].z, clip.localRotations[0].z);
}

TEST(MotionSource, StableIntentIsPreservedExactly) {
    DeterministicMotionSource source({"hips"}, {glm::quat(1, 0, 0, 0)});
    MotionIntent intent;
    intent.movementDirection = {0.25f, 0.0f, 0.75f};
    intent.facingDirection = {-1.0f, 0.0f, 0.0f};
    intent.targetSpeed = 3.5f;
    intent.hasWorldTarget = true;
    intent.worldTarget = {4.0f, 5.0f, 6.0f};
    intent.targetHeadingRadians = 1.2f;
    intent.styleKey = "walk";
    intent.seed = UINT64_C(0x123456789abcdef0);

    source.submitIntent(intent);
    EXPECT_TRUE(source.lastIntent() == intent);
    LocalPoseFrame frame;
    EXPECT_TRUE(source.sample(0.5, frame));
    EXPECT_EQ(frame.provenance, MotionProvenance::Test);
}

TEST(MotionSource, UnavailableSourceReportsClipFallback) {
    DeterministicMotionSource source({"hips"}, {glm::quat(1, 0, 0, 0)});
    source.setAvailable(false);
    LocalPoseFrame frame;
    EXPECT_FALSE(source.sample(0.0, frame));
    const MotionSourceStatus status = source.status();
    EXPECT_EQ(status.state, MotionSourceState::Degraded);
    EXPECT_EQ(status.effectiveProvider, "clips");
    EXPECT_FALSE(status.error.empty());
}

