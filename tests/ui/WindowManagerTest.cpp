#include <gtest/gtest.h>
#include "ui/WindowManager.h"

using namespace VulkanCube::UI;

class WindowManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure clean state
    }
    
    void TearDown() override {
        // Ensure clean state
    }
};

TEST_F(WindowManagerTest, InitialValues) {
    WindowManager wm;
    // Default values from header
    EXPECT_EQ(wm.getWidth(), 800);
    EXPECT_EQ(wm.getHeight(), 600);
    EXPECT_EQ(wm.getHandle(), nullptr);
    EXPECT_FALSE(wm.wasResized());
}

TEST_F(WindowManagerTest, SettersBeforeInit) {
    WindowManager wm;
    wm.setSize(1024, 768);
    wm.setTitle("New Title");
    
    EXPECT_EQ(wm.getWidth(), 1024);
    EXPECT_EQ(wm.getHeight(), 768);
}

TEST_F(WindowManagerTest, InitializationHeadless) {
    // Pre-initialize GLFW to set hidden hint
    if (!glfwInit()) {
        GTEST_SKIP() << "Cannot initialize GLFW, skipping window test";
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    
    WindowManager wm;
    bool success = wm.initialize(800, 600, "Headless Test");
    
    if (!success) {
        // Might fail on CI without display
        glfwTerminate();
        GTEST_SKIP() << "Failed to create window (headless), skipping";
    }
    
    EXPECT_TRUE(success);
    EXPECT_NE(wm.getHandle(), nullptr);
    EXPECT_EQ(wm.getWidth(), 800);
    EXPECT_EQ(wm.getHeight(), 600);
    
    // Test resize flag
    // We can't easily trigger a real resize callback without window interaction,
    // but we can verify the callback setter works (compiles and runs)
    bool resized = false;
    wm.setResizeCallback([&](int w, int h) {
        resized = true;
    });
    
    // Manually trigger? No public method to trigger callback.
    // But we can check state.
    
    wm.cleanup();
    // cleanup calls glfwTerminate, so we are done.
}
