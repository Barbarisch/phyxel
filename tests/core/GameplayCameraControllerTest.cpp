#include <gtest/gtest.h>

#include "core/GameplayCameraController.h"
#include "graphics/Camera.h"
#include "input/InputManager.h"

using namespace Phyxel;

// Pins the fix for the post-combat "camera under the floor looking up" defect.
//
// Root cause (found 2026-08-19): with an always-on-look scheme (fps), the
// controller set InputManager::mouseCaptured(true) every driving frame but
// NEVER cleared it when driveCharacter went false (turn-based combat, dialogue).
// The host frees the OS cursor for click-targeting — but that is WindowManager
// state; InputManager::mouseCaptured is a separate flag, so every mouse move
// made to click an enemy kept integrating into yaw/pitch until pitch pinned at
// the +89 clamp. Restoring the exploration rig then framed the camera from
// under the floor. The scaffold masked it with a yaw/pitch snapshot/restore;
// the real fix makes capture symmetric: setMouseCaptured(driveCharacter).
//
// All three tests run headless: InputManager has no GLFW window (key/button
// queries return false before touching GLFW/ImGui), handleMouseMove is the
// public entry the real cursor callback uses, and the rig math is pure.

namespace {

// One captured mouse sweep: establish the cursor position, then move it.
// (First move after capture only latches lastX/lastY via firstMouse.)
void sweep(Input::InputManager& input, double x0, double y0, double x1, double y1) {
    input.handleMouseMove(x0, y0);
    input.handleMouseMove(x1, y1);
}

struct Rig {
    Core::GameplayCameraController ctl;
    Input::InputManager input;
    Graphics::Camera cam;
    Rig() {
        EXPECT_TRUE(ctl.setRigByName("third_person"));
        EXPECT_TRUE(ctl.setSchemeByName("fps"));   // wantsAlwaysOnLook
    }
    void frame(bool drive) { ctl.update(1.0f / 60.0f, input, nullptr, cam, true, drive); }
};

} // namespace

TEST(GameplayCameraControllerTest, LookIntegratesWhileDriving) {
    Rig r;
    r.frame(true);
    ASSERT_TRUE(r.input.isMouseCaptured());

    const float before = r.input.getPitch();
    sweep(r.input, 100.0, 400.0, 100.0, 300.0);   // mouse up 100px
    EXPECT_NE(r.input.getPitch(), before) << "sanity: captured look must integrate";
}

TEST(GameplayCameraControllerTest, LookFrozenWhileNotDriving) {
    Rig r;
    r.frame(true);                                 // exploration: capture on
    ASSERT_TRUE(r.input.isMouseCaptured());

    r.frame(false);                                // combat/dialogue: cursor freed
    EXPECT_FALSE(r.input.isMouseCaptured())
        << "capture must release when the controller stops driving";

    const float yawBefore = r.input.getYaw();
    const float pitchBefore = r.input.getPitch();
    // The click-targeting sweeps that scrambled pitch to +89 in the field:
    sweep(r.input, 640.0, 700.0, 640.0, 100.0);
    sweep(r.input, 100.0, 650.0, 1200.0, 80.0);
    EXPECT_FLOAT_EQ(r.input.getPitch(), pitchBefore)
        << "free-cursor mouse movement must NOT integrate into look";
    EXPECT_FLOAT_EQ(r.input.getYaw(), yawBefore);
}

TEST(GameplayCameraControllerTest, CaptureRestoredWhenDrivingResumes) {
    Rig r;
    r.frame(true);
    r.frame(false);
    // Cursor parked somewhere far away during the tactical phase.
    r.input.handleMouseMove(1200.0, 80.0);

    r.frame(true);                                 // combat over
    ASSERT_TRUE(r.input.isMouseCaptured());

    // firstMouse re-latch: the first move after re-capture must only establish
    // the cursor position, not integrate the parked-position jump.
    const float pitchBefore = r.input.getPitch();
    r.input.handleMouseMove(300.0, 600.0);         // big jump from (1200,80)
    EXPECT_FLOAT_EQ(r.input.getPitch(), pitchBefore)
        << "re-capture must not integrate the cursor-park jump";

    // ...and the NEXT move integrates normally again.
    r.input.handleMouseMove(300.0, 500.0);
    EXPECT_NE(r.input.getPitch(), pitchBefore);
}
