// Utils::segmentHitsCapsule — the camera->dot occlusion test behind the target
// ring's depth effect (Ravenmere G-138: "rings ... should be obscured by characters
// in the places that the rings pass behind the character").
#include <gtest/gtest.h>
#include "utils/CapsuleOcclusion.h"

using namespace Phyxel::Utils;

namespace {
const BodyCapsule kBody{{10.0f, 17.0f, 10.0f}, 1.8f, 0.3f};   // feet at (10,17,10)
}

TEST(CapsuleOcclusionTest, TheFarHalfOfARingIsBehindTheBodyTheNearHalfIsNot) {
    // Camera south-east and above, looking at the body; ring of radius 0.55 at the feet.
    const glm::vec3 cam{13.0f, 20.0f, 13.0f};
    const glm::vec3 farDot {10.0f - 0.39f, 17.06f, 10.0f - 0.39f};   // on the far side (NW)
    const glm::vec3 nearDot{10.0f + 0.39f, 17.06f, 10.0f + 0.39f};   // on the near side (SE)
    EXPECT_TRUE (segmentHitsCapsule(cam, farDot,  kBody)) << "the far dot's sight line passes through the body";
    EXPECT_FALSE(segmentHitsCapsule(cam, nearDot, kBody)) << "the near dot is in front of the body";
}

TEST(CapsuleOcclusionTest, SideDotsClearTheBody) {
    const glm::vec3 cam{13.0f, 20.0f, 13.0f};
    // Perpendicular to the view direction (the view runs along (-1,-1) in XZ): the (1,-1) axis.
    const glm::vec3 leftDot {10.0f + 0.39f, 17.06f, 10.0f - 0.39f};
    const glm::vec3 rightDot{10.0f - 0.39f, 17.06f, 10.0f + 0.39f};
    EXPECT_FALSE(segmentHitsCapsule(cam, leftDot,  kBody));
    EXPECT_FALSE(segmentHitsCapsule(cam, rightDot, kBody));
}

TEST(CapsuleOcclusionTest, ABodyStandingBetweenCameraAndDotOccludesIt) {
    const BodyCapsule other{{12.0f, 17.0f, 12.0f}, 1.8f, 0.3f};   // another character on the line
    const glm::vec3 cam{14.0f, 18.0f, 14.0f};
    const glm::vec3 dot{10.0f, 17.06f, 10.0f};
    EXPECT_TRUE(segmentHitsCapsule(cam, dot, other));
    const BodyCapsule aside{{12.0f, 17.0f, 13.5f}, 1.8f, 0.3f};   // 1.06 m off the line
    EXPECT_FALSE(segmentHitsCapsule(cam, dot, aside));
}

TEST(CapsuleOcclusionTest, ASegmentAboveTheHeadDoesNotHit) {
    const glm::vec3 a{5.0f, 19.5f, 10.0f}, b{15.0f, 19.5f, 10.0f};   // 2.5 m up, the body is 1.8 tall
    EXPECT_FALSE(segmentHitsCapsule(a, b, kBody));
    const glm::vec3 c{5.0f, 17.9f, 10.0f}, d{15.0f, 17.9f, 10.0f};   // chest height
    EXPECT_TRUE(segmentHitsCapsule(c, d, kBody));
}
