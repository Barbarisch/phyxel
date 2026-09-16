// World of Warcraft-style third-person controls + camera (user direction 2026-09-15:
// "the default for 3rd person camera and controls should mimic World of Warcraft").
// Pins the researched defaults: A/D turn (strafe with the right button held), Q/E
// strafe, both buttons run, NumLock autorun, Numpad-/ walk toggle, left drag orbits,
// right drag steers, the left button never swings, wheel zoom to first person, wall
// collision on the boom, and third_person resolving to this rig + scheme by default.
#include <gtest/gtest.h>

#include "core/GameplayCameraController.h"
#include "core/GameShell.h"
#include "graphics/Camera.h"
#include "graphics/CameraRig.h"
#include "input/ControlScheme.h"
#include "input/InputManager.h"
#include "physics/PhysicsWorld.h"
#include "scene/AnimatedVoxelCharacter.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <memory>

using namespace Phyxel;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// Hold a key/button for the sample and age it afterwards (injected input reads
// through isActionPressed / isMouseButtonPressed like the physical kind).
struct Pad {
    Input::InputManager input;
    Input::MmoScheme scheme;
    void press(int key)      { input.injectKey(key, 10.0f); }
    // injectKey floors a non-positive hold to 0.1 s, so "release" = a hair of hold, then age it out
    // A fresh injection survives its first aging tick by design, so a release = a hair of
    // hold + TWO ticks (the first consumes the freshness, the second expires it).
    void release(int key)    { input.injectKey(key, 0.0001f); input.tickInjection(0.01f); input.tickInjection(0.01f); }
    void button(int b)       { input.injectMouseButton(b, 10.0f); }
    void unbutton(int b)     { input.injectMouseButton(b, 0.0001f); input.tickInjection(0.01f); input.tickInjection(0.01f); }
    void releaseAll()        { input.tickInjection(20.0f); input.tickInjection(20.0f); }   // every 10 s hold expires
    Input::ControlIntent sample() { return scheme.sample(input, kDt); }
};

} // namespace

TEST(MmoControlsTest, ADTurnTheBodyButStrafeWhileTheRightButtonSteers) {
    Pad p;
    p.press(GLFW_KEY_A);
    auto in = p.sample();
    EXPECT_LT(in.turn, 0.0f) << "A turns left";
    EXPECT_FLOAT_EQ(in.strafe, 0.0f);
    p.button(GLFW_MOUSE_BUTTON_RIGHT);
    in = p.sample();
    EXPECT_FLOAT_EQ(in.turn, 0.0f) << "with RMB held A no longer turns";
    EXPECT_LT(in.strafe, 0.0f) << "...it strafes";
    EXPECT_TRUE(in.steerFacingToYaw) << "the body follows the camera while steering";
    EXPECT_FALSE(in.followWhenMoving);
}

TEST(MmoControlsTest, QEStrafeAndBothButtonsRunForward) {
    Pad p;
    p.press(GLFW_KEY_Q);
    EXPECT_LT(p.sample().strafe, 0.0f);
    p.releaseAll();
    p.press(GLFW_KEY_E);
    EXPECT_GT(p.sample().strafe, 0.0f);
    p.releaseAll();
    EXPECT_FLOAT_EQ(p.sample().forward, 0.0f);
    p.button(GLFW_MOUSE_BUTTON_LEFT);
    EXPECT_FLOAT_EQ(p.sample().forward, 0.0f) << "one button alone does not move";
    p.button(GLFW_MOUSE_BUTTON_RIGHT);
    EXPECT_FLOAT_EQ(p.sample().forward, -Input::MmoScheme::kRun) << "both buttons = run forward";
}

TEST(MmoControlsTest, AutorunLatchesUntilManualForwardOrBackInput) {
    Pad p;
    p.press(GLFW_KEY_NUM_LOCK);
    EXPECT_FLOAT_EQ(p.sample().forward, -Input::MmoScheme::kRun) << "NumLock starts autorun";
    p.releaseAll();
    EXPECT_FLOAT_EQ(p.sample().forward, -Input::MmoScheme::kRun) << "...and it latches with nothing held";
    EXPECT_TRUE(p.scheme.autorun());
    p.press(GLFW_KEY_S);
    p.sample();
    p.releaseAll();
    EXPECT_FALSE(p.scheme.autorun()) << "manual S cancels autorun";
    EXPECT_FLOAT_EQ(p.sample().forward, 0.0f);
    // toggling again while running -> off
    p.press(GLFW_KEY_NUM_LOCK); p.sample(); p.releaseAll(); p.sample();
    EXPECT_TRUE(p.scheme.autorun());
    p.press(GLFW_KEY_NUM_LOCK); p.sample(); p.releaseAll();
    EXPECT_FALSE(p.scheme.autorun()) << "a second press (after release) toggles it off";
}

TEST(MmoControlsTest, WalkToggleDropsForwardIntoTheWalkBand) {
    Pad p;
    p.press(GLFW_KEY_W);
    EXPECT_FLOAT_EQ(p.sample().forward, -Input::MmoScheme::kRun) << "default gait is RUN (WoW)";
    p.press(GLFW_KEY_KP_DIVIDE);
    p.sample();
    p.release(GLFW_KEY_KP_DIVIDE);
    const float walk = p.sample().forward;
    EXPECT_FLOAT_EQ(walk, -Input::MmoScheme::kWalk);
    EXPECT_GT(walk, -0.6f); EXPECT_LT(walk, -0.1f) << "inside AnimatedVoxelCharacter's walk band";
}

TEST(MmoControlsTest, LeftDragOrbitsRightDragSteersAndTheLeftButtonNeverSwings) {
    Pad p;
    p.button(GLFW_MOUSE_BUTTON_LEFT);
    auto in = p.sample();
    EXPECT_FALSE(in.attack); EXPECT_FALSE(in.heavy);
    EXPECT_FALSE(in.steerFacingToYaw) << "left drag: camera only, the body keeps its heading";
    EXPECT_FALSE(in.followWhenMoving)  << "no follow while a button holds the view";
    p.unbutton(GLFW_MOUSE_BUTTON_LEFT);
    in = p.sample();
    EXPECT_TRUE(in.followWhenMoving) << "buttons up: the camera may follow the body";
    EXPECT_TRUE(p.scheme.leftButtonLooks());
    p.press(GLFW_KEY_LEFT_SHIFT); p.button(GLFW_MOUSE_BUTTON_LEFT);
    in = p.sample();
    EXPECT_FALSE(in.attack); EXPECT_FALSE(in.heavy) << "shift+LMB is not a heavy either";
}

TEST(MmoControlsTest, TheControllerCapturesTheMouseWhileEitherButtonIsHeldAndDriving) {
    Core::GameplayCameraController ctl;
    Input::InputManager input;
    Graphics::Camera cam;
    ASSERT_TRUE(ctl.setRigByName("third_person"));
    ASSERT_TRUE(ctl.setSchemeByName("wow"));
    ctl.update(kDt, input, nullptr, cam, true, true);
    EXPECT_FALSE(input.isMouseCaptured()) << "no button: the cursor is free (no always-on look)";
    input.injectMouseButton(GLFW_MOUSE_BUTTON_LEFT, 10.0f);
    ctl.update(kDt, input, nullptr, cam, true, true);
    EXPECT_TRUE(input.isMouseCaptured()) << "left drag captures (orbit)";
    ctl.update(kDt, input, nullptr, cam, true, /*drive=*/false);
    EXPECT_FALSE(input.isMouseCaptured()) << "not driving (dialogue/combat): never captured";
    input.tickInjection(20.0f); input.tickInjection(20.0f);
    input.injectMouseButton(GLFW_MOUSE_BUTTON_RIGHT, 10.0f);
    ctl.update(kDt, input, nullptr, cam, true, true);
    EXPECT_TRUE(input.isMouseCaptured()) << "right drag captures (steer)";
    input.tickInjection(20.0f); input.tickInjection(20.0f);
    ctl.update(kDt, input, nullptr, cam, true, true);
    EXPECT_FALSE(input.isMouseCaptured()) << "release frees the cursor";
}

TEST(MmoControlsTest, TheRigPullsInFrontOfAWallAndGoesFirstPersonAtFullZoomIn) {
    Graphics::MmoRig rig;
    // A wall of cubes at z = 5 (x -10..10, y 0..5). Character at z = 8 looking toward -z
    // means the camera boom points toward +z... use a camera looking +z so the boom
    // (behind) crosses the wall at z = 5 when the target sits at z = 7.
    rig.solidAt = [](const glm::ivec3& c) { return c.z == 5 && c.y >= 0 && c.y <= 5 && c.x >= -10 && c.x <= 10; };
    rig.distance = 8.0f;
    rig.eyeHeight = 1.0f;
    Graphics::Camera cam;
    const glm::vec3 target{0.5f, 0.0f, 7.5f};
    // yaw -90 looks along -z (Camera convention: front = (cos yaw, ., sin yaw)); the boom
    // goes +z... we need the wall BEHIND the camera direction: look along +z (yaw 90),
    // so the camera sits at z = 7.5 - 8 = -0.5, crossing the wall at z = 5.
    rig.update(cam, target, 90.0f, 0.0f, kDt);
    const float boomZ = 7.5f - cam.getPosition().z;
    EXPECT_LT(boomZ, 8.0f) << "the wall at z=5 shortens the boom";
    EXPECT_GT(cam.getPosition().z, 5.0f) << "the camera stays on the character's side of the wall";
    EXPECT_NEAR(cam.getPosition().z, 6.0f + rig.collisionPadding, 0.05f) << "pulled in to the wall face minus padding";
    // No wall in the way (look along -z): the full boom.
    rig.update(cam, target, -90.0f, 0.0f, kDt);
    EXPECT_NEAR(cam.getPosition().z, 7.5f + 8.0f, 1e-3f);
    // Full zoom-in = first person: the eye sits AT the track point.
    rig.distance = Graphics::MmoRig::kFirstPersonBelow;
    rig.update(cam, target, -90.0f, 0.0f, kDt);
    EXPECT_NEAR(glm::length(cam.getPosition() - (target + glm::vec3(0.0f, 1.0f, 0.0f))), 0.0f, 1e-4f);
    EXPECT_LE(rig.distanceMin, 0.0f);
    EXPECT_GE(rig.distanceMax, 20.0f) << "WoW zooms out to ~28 yd; the rig allows a long boom";
}

TEST(MmoControlsTest, ThirdPersonDefaultsToTheMmoRigAndScheme) {
    auto rig = Graphics::makeCameraRig("third_person");
    ASSERT_NE(rig, nullptr);
    EXPECT_NE(dynamic_cast<Graphics::MmoRig*>(rig.get()), nullptr) << "third_person is the WoW rig";
    EXPECT_NE(dynamic_cast<Graphics::ThirdPersonRig*>(Graphics::makeCameraRig("chase").get()), nullptr);
    EXPECT_EQ(dynamic_cast<Graphics::MmoRig*>(Graphics::makeCameraRig("chase").get()), nullptr) << "chase = the plain orbit";
    auto scheme = Input::makeControlScheme("third_person");
    ASSERT_NE(scheme, nullptr);
    EXPECT_NE(dynamic_cast<Input::MmoScheme*>(scheme.get()), nullptr);
    EXPECT_NE(dynamic_cast<Input::TankScheme*>(Input::makeControlScheme("tank").get()), nullptr) << "tank stays reachable";
    // A game.json that authors only camera.mode gets the matching scheme, not fps.
    EXPECT_EQ(Core::GameShell::defaultSchemeForRig("third_person", "fps"), "wow");
    EXPECT_EQ(Core::GameShell::defaultSchemeForRig("first_person", "fps"), "fps");
    EXPECT_EQ(Core::GameShell::defaultSchemeForRig("overhead", "fps"), "fps");
}

TEST(MmoControlsTest, EaseYawTakesTheShortArcAndSnapsWithinAStep) {
    using C = Core::GameplayCameraController;
    EXPECT_FLOAT_EQ(C::easeYawToward(10.0f, 350.0f, 5.0f), 5.0f) << "-20 deg is the short way, not +340";
    EXPECT_FLOAT_EQ(C::easeYawToward(350.0f, 10.0f, 5.0f), 355.0f);
    EXPECT_FLOAT_EQ(C::easeYawToward(0.0f, 3.0f, 5.0f), 3.0f) << "inside one step: snap to target";
    EXPECT_FLOAT_EQ(C::easeYawToward(-170.0f, 170.0f, 10.0f), -180.0f) << "wraps through +-180";
}

// The camera follows the body: turning with A swings the InputManager yaw to sit
// behind the character's new heading (WoW keeps the camera behind you when you
// turn with the keys), and a left-drag orbit offset is walked off when moving.
TEST(MmoControlsTest, TheCameraEasesBackBehindTheBodyWhenItTurnsOrMoves) {
    auto physics = std::make_unique<Physics::PhysicsWorld>();
    Scene::AnimatedVoxelCharacter ch(physics.get(), glm::vec3(0.0f, 20.0f, 0.0f));
    ASSERT_TRUE(ch.loadModel("resources/animated_characters/humanoid.anim"));
    Core::GameplayCameraController ctl;
    Input::InputManager input;
    Graphics::Camera cam;
    ASSERT_TRUE(ctl.setRigByName("third_person"));
    ASSERT_TRUE(ctl.setSchemeByName("wow"));
    // Body faces +z (yaw 0 rad) -> the camera "behind" it looks along +z = camera yaw 90.
    ch.setFacingYaw(0.0f);
    input.setYawPitch(-45.0f, -20.0f);   // camera parked 135 deg off the body's back
    input.injectKey(GLFW_KEY_W, 10.0f);
    for (int i = 0; i < 120; ++i) ctl.update(kDt, input, &ch, cam, /*advance=*/false, true);
    EXPECT_NEAR(input.getYaw(), 90.0f - glm::degrees(ch.getYaw()), 1.0f)
        << "two seconds of walking swings the camera behind the body";
    // Holding the LEFT button (orbit) freezes the follow.
    input.tickInjection(20.0f); input.tickInjection(20.0f);
    input.setYawPitch(-45.0f, -20.0f);
    input.injectKey(GLFW_KEY_W, 10.0f);
    input.injectMouseButton(GLFW_MOUSE_BUTTON_LEFT, 10.0f);
    for (int i = 0; i < 60; ++i) ctl.update(kDt, input, &ch, cam, false, true);
    EXPECT_NEAR(input.getYaw(), -45.0f, 1e-3f) << "an orbit drag holds the camera where the player put it";
    // Right-button steer: the body turns to the camera yaw.
    input.tickInjection(20.0f); input.tickInjection(20.0f);
    input.setYawPitch(0.0f, -20.0f);       // camera looks along +x
    input.injectMouseButton(GLFW_MOUSE_BUTTON_RIGHT, 10.0f);
    ctl.update(kDt, input, &ch, cam, false, true);
    EXPECT_NEAR(glm::degrees(ch.getYaw()), 90.0f, 1e-2f) << "body faces +x (atan2 convention: yaw 90 deg)";
}

// An injected tap must be observable for the whole frame it arrives in, whatever the
// frame rate. RED before: processInput aged a 0.1 s hold by a 0.15 s frame (the town runs
// ~6 FPS on this laptop) BEFORE the scheme sampled it, so a NumLock tap never toggled
// autorun and every sub-frame harness tap was silently lost.
TEST(MmoControlsTest, AnInjectedTapSurvivesTheFrameItArrivesIn) {
    Input::InputManager input;
    input.injectKey(GLFW_KEY_NUM_LOCK, 0.1f);
    input.tickInjection(0.15f);                       // the arrival frame ages it first...
    EXPECT_TRUE(input.isActionPressed("ToggleAutorun")) << "...but it is still seen this frame";
    input.tickInjection(0.15f);
    EXPECT_FALSE(input.isActionPressed("ToggleAutorun")) << "released the frame after";
    input.injectMouseButton(GLFW_MOUSE_BUTTON_RIGHT, 0.1f);
    input.tickInjection(0.15f);
    EXPECT_TRUE(input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT));
    input.tickInjection(0.15f);
    EXPECT_FALSE(input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT));
}

// G-123: in turn-based combat the character is not driven, yet a drag must still orbit
// the tactical camera - the host says when (combat yes, dialogue no). RED before: the
// MMO capture rule required driveCharacter, so no drag ever integrated in combat.
TEST(MmoControlsTest, ADragOrbitsWhileNotDrivingOnlyWhenTheHostAllowsIt) {
    Core::GameplayCameraController ctl;
    Input::InputManager input;
    Graphics::Camera cam;
    ASSERT_TRUE(ctl.setRigByName("tactical"));
    ASSERT_TRUE(ctl.setSchemeByName("wow"));
    input.injectMouseButton(GLFW_MOUSE_BUTTON_RIGHT, 10.0f);
    ctl.update(kDt, input, nullptr, cam, true, /*drive=*/false);
    EXPECT_FALSE(input.isMouseCaptured()) << "default: a drag while not driving is ignored (dialogue)";
    ctl.setLookWhileNotDriving(true);          // the host: combat
    ctl.update(kDt, input, nullptr, cam, true, false);
    EXPECT_TRUE(input.isMouseCaptured()) << "combat: the drag captures and orbits";
    const float yaw0 = input.getYaw();
    input.handleMouseMove(100.0, 300.0);       // latch
    input.handleMouseMove(160.0, 300.0);       // drag right
    EXPECT_NE(input.getYaw(), yaw0) << "the drag turned the view";
    ctl.setLookWhileNotDriving(false);
    ctl.update(kDt, input, nullptr, cam, true, false);
    EXPECT_FALSE(input.isMouseCaptured());
}
