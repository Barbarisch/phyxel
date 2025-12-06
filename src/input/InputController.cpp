#include "input/InputController.h"
#include "input/InputManager.h"
#include "scene/VoxelInteractionSystem.h"
#include "Application.h"
#include "utils/Logger.h"
#include <GLFW/glfw3.h>

namespace VulkanCube {

InputController::InputController(Input::InputManager* inputManager, 
                               VoxelInteractionSystem* interactionSystem,
                               Application* app)
    : m_inputManager(inputManager)
    , m_interactionSystem(interactionSystem)
    , m_app(app)
{
}

void InputController::initializeBindings() {
    setupKeyboardBindings();
    setupMouseBindings();
    LOG_INFO("InputController", "Input bindings initialized");
}

void InputController::setupKeyboardBindings() {
    // ESC - Exit application
    m_inputManager->registerAction(GLFW_KEY_ESCAPE, "Exit", [this]() {
        LOG_INFO("InputController", "ESC pressed - requesting shutdown");
        // We need to access window manager to close. 
        // Application::cleanup() handles shutdown, but we need to signal it.
        // Application::run() loop checks windowManager->shouldClose().
        // So we need to set window should close.
        // Application doesn't expose windowManager directly but has setWindowSize etc.
        // But we can use glfwSetWindowShouldClose if we have the window handle.
        // Or we can add a method to Application to request shutdown.
        // For now, let's assume we can access it via Application or just use the same logic as before if possible.
        // The original code used: glfwSetWindowShouldClose(windowManager->getHandle(), true);
        // Application has windowManager as private.
        // I should add `Application::quit()` method.
        m_app->quit(); 
    });
    
    // F1 - Toggle performance overlay
    m_inputManager->registerAction(GLFW_KEY_F1, "Toggle Performance Overlay", [this]() {
        m_app->togglePerformanceOverlay();
    });
    
    // F2 - Save world (placeholder)
    m_inputManager->registerAction(GLFW_KEY_F2, "Save World", []() {
        LOG_INFO("InputController", "World save functionality not yet implemented in refactored code");
    });
    
    // F3 - Toggle force debug visualization
    m_inputManager->registerAction(GLFW_KEY_F3, "Toggle Force Debug", [this]() {
        m_debugFlags.showForceSystemDebug = !m_debugFlags.showForceSystemDebug;
        LOG_INFO("InputController", std::string("Force debug visualization: ") + 
                 (m_debugFlags.showForceSystemDebug ? "ENABLED" : "DISABLED"));
    });
    
    // F4 - Toggle debug rendering mode
    m_inputManager->registerAction(GLFW_KEY_F4, "Toggle Debug Rendering", [this]() {
        m_app->toggleDebugRendering();
    });
    
    // Ctrl + F4 - Cycle debug visualization mode
    m_inputManager->registerActionWithModifier(GLFW_KEY_F4, GLFW_MOD_CONTROL, "Cycle Debug Mode", [this]() {
        m_app->cycleDebugVisualizationMode();
    });
    
    // F5 - Toggle raycast visualization
    m_inputManager->registerAction(GLFW_KEY_F5, "Toggle Raycast Visualization", [this]() {
        m_app->toggleRaycastVisualization();
    });
    
    // G - Spawn dynamic subcube (placeholder)
    m_inputManager->registerAction(GLFW_KEY_G, "Spawn Dynamic Subcube", [this]() {
        glm::vec3 spawnPos = m_inputManager->getCameraPosition() + m_inputManager->getCameraFront() * 5.0f;
        LOG_INFO_FMT("InputController", "Dynamic spawn at " << spawnPos.x 
                     << ", " << spawnPos.y << ", " << spawnPos.z << " not yet implemented");
    });
    
    // C - Place cube
    m_inputManager->registerAction(GLFW_KEY_C, "Place Cube", [this]() {
        LOG_INFO("InputController", "C key pressed - attempting to place cube");
        m_interactionSystem->placeVoxelAtHover();
    });
    
    // Shift + C - Place subcube
    m_inputManager->registerActionWithModifier(GLFW_KEY_C, GLFW_MOD_SHIFT, "Place Subcube", [this]() {
        LOG_INFO("InputController", "Shift+C pressed - attempting to place subcube");
        m_interactionSystem->placeSubcubeAtHover();
    });
    
    // Ctrl + C - Place microcube
    m_inputManager->registerActionWithModifier(GLFW_KEY_C, GLFW_MOD_CONTROL, "Place Microcube", [this]() {
        LOG_INFO("InputController", "Ctrl+C pressed - attempting to place microcube");
        m_interactionSystem->placeMicrocubeAtHover();
    });
    
    // O - Toggle breaking forces
    m_inputManager->registerAction(GLFW_KEY_O, "Toggle Breaking Forces", [this]() {
        m_debugFlags.disableBreakingForces = !m_debugFlags.disableBreakingForces;
        LOG_INFO("InputController", std::string("Breaking forces: ") + 
                 (m_debugFlags.disableBreakingForces ? "DISABLED" : "ENABLED"));
    });
}

void InputController::setupMouseBindings() {
    // Left click - Break cube/subcube/microcube
    m_inputManager->registerMouseAction(GLFW_MOUSE_BUTTON_LEFT, 0, "Break Voxel", [this]() {
        // Check if we're hovering over a microcube, subcube, or regular cube
        if (m_interactionSystem->hasHoveredCube()) {
            const auto& loc = m_interactionSystem->getCurrentHoveredLocation();
            if (loc.isMicrocube) {
                // Break microcube
                m_interactionSystem->breakHoveredMicrocube();
            } else if (loc.isSubcube) {
                // Break subcube with physics
                m_interactionSystem->breakHoveredSubcube();
            } else {
                // Break regular cube into dynamic cube with physics
                m_interactionSystem->breakHoveredCube(m_inputManager->getCameraPosition());
            }
        }
    });
    
    // Ctrl + Left click - Subdivide cube
    m_inputManager->registerMouseAction(GLFW_MOUSE_BUTTON_LEFT, GLFW_MOD_CONTROL, "Subdivide Cube", [this]() {
        m_interactionSystem->subdivideHoveredCube();
    });
    
    // Alt + Left click - Subdivide subcube into microcubes
    m_inputManager->registerMouseAction(GLFW_MOUSE_BUTTON_LEFT, GLFW_MOD_ALT, "Subdivide Subcube", [this]() {
        m_interactionSystem->subdivideHoveredSubcube();
    });
    
    // Middle click - Subdivide cube
    m_inputManager->registerMouseAction(GLFW_MOUSE_BUTTON_MIDDLE, 0, "Subdivide Cube (Middle)", [this]() {
        m_interactionSystem->subdivideHoveredCube();
    });
}

}
