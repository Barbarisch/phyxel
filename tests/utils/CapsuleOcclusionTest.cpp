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

// --- Ravenmere G-147: the camera's own character must not clip into first person -------------
//
// MmoRig already switches to an eye view when the boom is zoomed under 1 u, and the boom is also
// shortened by wall collision. Either way the camera ends up INSIDE the player's body and the
// mesh clips through the near plane. The renderer drops the owner from the MAIN pass (never the
// shadow pass) while that is true, and this is the predicate it uses.
//
// All coordinates here are CAMERA-RELATIVE, which is how RenderCoordinator already works: the
// camera is the origin, the character's feet are at `rel`.

TEST(CapsuleOcclusionTest, AFirstPersonCameraIsInsideItsOwnBody) {
    // Boom fully collapsed: the camera sits at the character's eye height, above its own feet.
    const Phyxel::Utils::BodyCapsule own{glm::vec3(0.0f, -1.4f, 0.0f), 1.8f, 0.45f};
    EXPECT_TRUE(pointInsideCapsule(glm::vec3(0.0f), own)) << "eye view: the camera is in the head";
}

TEST(CapsuleOcclusionTest, AThirdPersonCameraIsOutsideItsOwnBody) {
    // Boom 2.5 u behind at the same height: well clear of the body.
    const Phyxel::Utils::BodyCapsule own{glm::vec3(0.0f, -1.4f, 2.5f), 1.8f, 0.45f};
    EXPECT_FALSE(pointInsideCapsule(glm::vec3(0.0f), own));
    // And a modest zoom, 1.2 u back, is still outside: this must not blank the character during
    // ordinary close third-person play.
    const Phyxel::Utils::BodyCapsule near{glm::vec3(0.0f, -1.4f, 1.2f), 1.8f, 0.45f};
    EXPECT_FALSE(pointInsideCapsule(glm::vec3(0.0f), near));
}

TEST(CapsuleOcclusionTest, AWallPushingTheCameraIntoTheBodyCountsToo) {
    // The boom was 5 u but a wall shortened it to 0.3: the camera is inside the torso even though
    // the player never zoomed in. Same predicate, no special case.
    const Phyxel::Utils::BodyCapsule own{glm::vec3(0.0f, -1.4f, 0.3f), 1.8f, 0.45f};
    EXPECT_TRUE(pointInsideCapsule(glm::vec3(0.0f), own));
}

TEST(CapsuleOcclusionTest, APointAboveOrBelowTheBodyIsOutsideIt) {
    const Phyxel::Utils::BodyCapsule own{glm::vec3(0.0f, -1.4f, 0.0f), 1.8f, 0.45f};
    EXPECT_FALSE(pointInsideCapsule(glm::vec3(0.0f, 1.0f, 0.0f), own))  << "above the head";
    EXPECT_FALSE(pointInsideCapsule(glm::vec3(0.0f, -2.0f, 0.0f), own)) << "below the feet";
}
