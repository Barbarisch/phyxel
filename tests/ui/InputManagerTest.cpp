#include <gtest/gtest.h>
#include "input/InputManager.h"
#include <glm/glm.hpp>

using namespace Phyxel;
using namespace Phyxel::Input;

class InputManagerTest : public ::testing::Test {
protected:
    InputManager inputManager;
};

TEST_F(InputManagerTest, InitialState) {
    // Verify default camera state
    glm::vec3 pos = inputManager.getCameraPosition();
    EXPECT_EQ(pos.x, 50.0f);
    EXPECT_EQ(pos.y, 50.0f);
    EXPECT_EQ(pos.z, 50.0f);

    glm::vec3 front = inputManager.getCameraFront();
    EXPECT_EQ(front.x, 0.0f);
    EXPECT_EQ(front.y, 0.0f);
    EXPECT_EQ(front.z, -1.0f); // Default looking -Z

    EXPECT_FALSE(inputManager.isMouseCaptured());
}

TEST_F(InputManagerTest, CameraPositionControl) {
    glm::vec3 newPos(10.0f, 20.0f, 30.0f);
    inputManager.setCameraPosition(newPos);
    
    glm::vec3 pos = inputManager.getCameraPosition();
    EXPECT_EQ(pos, newPos);
}

TEST_F(InputManagerTest, CameraFrontControl) {
    glm::vec3 newFront(1.0f, 0.0f, 0.0f);
    inputManager.setCameraFront(newFront);
    
    glm::vec3 front = inputManager.getCameraFront();
    EXPECT_EQ(front, newFront);
}

TEST_F(InputManagerTest, YawPitchControl) {
    // Set yaw to 0 degrees (pointing right/positive X)
    // Pitch 0
    inputManager.setYawPitch(0.0f, 0.0f);
    
    glm::vec3 front = inputManager.getCameraFront();
    // Expect roughly (1, 0, 0)
    EXPECT_NEAR(front.x, 1.0f, 0.001f);
    EXPECT_NEAR(front.y, 0.0f, 0.001f);
    EXPECT_NEAR(front.z, 0.0f, 0.001f);
    
    // Set pitch to 90 degrees (pointing up/positive Y)
    // Yaw doesn't matter much at pure pitch 90, but let's keep it 0
    inputManager.setYawPitch(0.0f, 89.0f); // Clamp usually prevents full 90
    
    front = inputManager.getCameraFront();
    EXPECT_NEAR(front.y, 1.0f, 0.02f); // Close to 1
}

TEST_F(InputManagerTest, ActionRegistration) {
    bool actionTriggered = false;
    inputManager.registerAction(GLFW_KEY_SPACE, "TestJump", [&]() {
        actionTriggered = true;
    });
    
    // We can't easily trigger it without mocking GLFW, 
    // but we verified the registration didn't crash.
    SUCCEED();
}

TEST_F(InputManagerTest, ActionMapSeededWithDefaults) {
    // The constructor seeds the action map from GameSettings::defaultKeybindings
    // so gameplay queries work before any settings file is loaded.
    EXPECT_EQ(inputManager.getActionKey("MoveForward"), GLFW_KEY_W);
    EXPECT_EQ(inputManager.getActionKey("MoveBackward"), GLFW_KEY_S);
    EXPECT_EQ(inputManager.getActionKey("MoveLeft"), GLFW_KEY_A);
    EXPECT_EQ(inputManager.getActionKey("MoveRight"), GLFW_KEY_D);
    EXPECT_EQ(inputManager.getActionKey("Jump"), GLFW_KEY_SPACE);
    EXPECT_EQ(inputManager.getActionKey("Sprint"), GLFW_KEY_LEFT_SHIFT);
    EXPECT_EQ(inputManager.getActionModifiers("MoveForward"), 0);
}

TEST_F(InputManagerTest, BindActionOverridesDefault) {
    // Rebind MoveForward from W to the Up arrow — the action map is the single
    // source of truth, so a later query reflects the override.
    inputManager.bindAction("MoveForward", GLFW_KEY_UP, 0);
    EXPECT_EQ(inputManager.getActionKey("MoveForward"), GLFW_KEY_UP);

    // Unbound actions report UNKNOWN (the rebind UI uses this to show "unset").
    EXPECT_EQ(inputManager.getActionKey("NoSuchAction"), GLFW_KEY_UNKNOWN);
    EXPECT_EQ(inputManager.getActionModifiers("NoSuchAction"), 0);
}

TEST_F(InputManagerTest, ClearActionBindings) {
    inputManager.clearActionBindings();
    EXPECT_EQ(inputManager.getActionKey("MoveForward"), GLFW_KEY_UNKNOWN);
    // With no window and no binding, a query is safely false (never crashes).
    EXPECT_FALSE(inputManager.isActionPressed("MoveForward"));
}

TEST_F(InputManagerTest, KeyScanSafeWithoutWindow) {
    // The rebind key-capture helpers must not crash before a window is attached;
    // they report "nothing held" / "no modifiers" until the engine is up.
    EXPECT_EQ(inputManager.scanPressedKey(), GLFW_KEY_UNKNOWN);
    EXPECT_EQ(inputManager.currentModifiers(), 0);
}

TEST_F(InputManagerTest, ScriptingConsoleModeToggle) {
    // Default state should be false
    EXPECT_FALSE(inputManager.isScriptingConsoleMode());
    
    // Enable it
    inputManager.setScriptingConsoleMode(true);
    EXPECT_TRUE(inputManager.isScriptingConsoleMode());
    
    // Disable it
    inputManager.setScriptingConsoleMode(false);
    EXPECT_FALSE(inputManager.isScriptingConsoleMode());
}
