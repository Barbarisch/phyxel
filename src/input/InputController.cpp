#include "input/InputController.h"
#include "input/InputManager.h"
#include "scene/VoxelInteractionSystem.h"
#include "Application.h"
#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "graphics/RaycastVisualizer.h"
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

void InputController::update(float deltaTime) {
    // Handle preview logic
    if (m_showTreePreview) {
        // Get visualizer from app (assuming we can access it, or add getter to App)
        // Application::getRaycastVisualizer() doesn't exist yet, need to add it or pass it in.
        // For now, let's assume we can get it.
        // Wait, Application.h has raycastVisualizer as private unique_ptr.
        // I need to add a getter to Application.h first.
        
        auto visualizer = m_app->getRaycastVisualizer();
        if (visualizer) {
            visualizer->clearPreviewBoxes();
            
            if (m_interactionSystem->hasHoveredCube()) {
                const auto& loc = m_interactionSystem->getCurrentHoveredLocation();
                glm::vec3 basePos = glm::vec3(loc.worldPos) + loc.hitNormal;
                glm::ivec3 iBasePos = glm::round(basePos);
                
                const auto* tmpl = m_app->getObjectTemplateManager()->getTemplate("my_model");
                if (tmpl) {
                    glm::vec3 previewColor(0.0f, 1.0f, 0.0f); // Green ghost
                    
                    for (const auto& cube : tmpl->cubes) {
                        glm::vec3 pos = glm::vec3(iBasePos + cube.relativePos);
                        visualizer->addPreviewBox(pos, glm::vec3(1.0f), previewColor);
                    }
                    
                    for (const auto& sub : tmpl->subcubes) {
                        glm::vec3 parentPos = glm::vec3(iBasePos + sub.parentRelativePos);
                        glm::vec3 subOffset = glm::vec3(sub.subcubePos) * (1.0f/3.0f);
                        visualizer->addPreviewBox(parentPos + subOffset, glm::vec3(1.0f/3.0f), previewColor);
                    }
                    
                    for (const auto& micro : tmpl->microcubes) {
                        glm::vec3 parentPos = glm::vec3(iBasePos + micro.parentRelativePos);
                        glm::vec3 subOffset = glm::vec3(micro.subcubePos) * (1.0f/3.0f);
                        glm::vec3 microOffset = glm::vec3(micro.microcubePos) * (1.0f/9.0f);
                        visualizer->addPreviewBox(parentPos + subOffset + microOffset, glm::vec3(1.0f/9.0f), previewColor);
                    }
                }
            }
        }
    } else {
        // Clear preview if disabled
        auto visualizer = m_app->getRaycastVisualizer();
        if (visualizer) {
            visualizer->clearPreviewBoxes();
        }
    }
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

    // F7 - Toggle Profiler
    m_inputManager->registerAction(GLFW_KEY_F7, "Toggle Profiler", [this]() {
        m_app->toggleProfiler();
    });

    // V - Toggle Camera Mode
    m_inputManager->registerAction(GLFW_KEY_V, "Toggle Camera Mode", [this]() {
        m_app->toggleCameraMode();
    });

    // ` (Grave Accent) - Toggle Scripting Console
    m_inputManager->registerAction(GLFW_KEY_GRAVE_ACCENT, "Toggle Scripting Console", [this]() {
        m_app->toggleScriptingConsole();
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

    // F6 - Toggle Lighting Controls
    m_inputManager->registerAction(GLFW_KEY_F6, "Toggle Lighting Controls", [this]() {
        LOG_INFO("InputController", "F6 pressed - Toggling Lighting Controls");
        m_app->toggleLightingControls();
    });

    // Shift + F5 - Cycle raycast target mode
    m_inputManager->registerActionWithModifier(GLFW_KEY_F5, GLFW_MOD_SHIFT, "Cycle Raycast Target Mode", [this]() {
        m_app->cycleRaycastTargetMode();
    });
    
    // G - Spawn dynamic subcube (placeholder)
    m_inputManager->registerAction(GLFW_KEY_G, "Spawn Dynamic Subcube", [this]() {
        glm::vec3 spawnPos = m_inputManager->getCameraPosition() + m_inputManager->getCameraFront() * 5.0f;
        LOG_INFO_FMT("InputController", "Dynamic spawn at " << spawnPos.x 
                     << ", " << spawnPos.y << ", " << spawnPos.z << " not yet implemented");
    });

    // - - Decrease Ambient Light
    m_inputManager->registerAction(GLFW_KEY_MINUS, "Decrease Ambient Light", [this]() {
        m_app->adjustAmbientLight(-0.1f);
    });

    // = - Increase Ambient Light
    m_inputManager->registerAction(GLFW_KEY_EQUAL, "Increase Ambient Light", [this]() {
        m_app->adjustAmbientLight(0.1f);
    });
    
    // C - Place cube
    m_inputManager->registerAction(GLFW_KEY_C, "Place Cube", [this]() {
        LOG_INFO("InputController", "C key pressed - attempting to place cube");
        m_interactionSystem->placeVoxelAtHover();
    });

    // K - Toggle Character Control
    m_inputManager->registerAction(GLFW_KEY_K, "Toggle Character Control", [this]() {
        LOG_INFO("InputController", "K key pressed - toggling character control");
        m_app->toggleCharacterControl();
    });
    
    // Ctrl + C - Place subcube
    m_inputManager->registerActionWithModifier(GLFW_KEY_C, GLFW_MOD_CONTROL, "Place Subcube", [this]() {
        LOG_INFO("InputController", "Ctrl+C pressed - attempting to place subcube");
        m_interactionSystem->placeSubcubeAtHover();
    });
    
    // Alt + C - Place microcube
    m_inputManager->registerActionWithModifier(GLFW_KEY_C, GLFW_MOD_ALT, "Place Microcube", [this]() {
        LOG_INFO("InputController", "Alt+C pressed - attempting to place microcube");
        m_interactionSystem->placeMicrocubeAtHover();
    });

    // P - Toggle Template Preview
    m_inputManager->registerAction(GLFW_KEY_P, "Toggle Template Preview", [this]() {
        m_showTreePreview = !m_showTreePreview;
        LOG_INFO_FMT("InputController", "Template Preview: " << (m_showTreePreview ? "ENABLED" : "DISABLED"));
    });

    // T - Spawn Template (Static)
    m_inputManager->registerAction(GLFW_KEY_T, "Spawn Static Template", [this]() {
        if (m_interactionSystem->hasHoveredCube()) {
            const auto& loc = m_interactionSystem->getCurrentHoveredLocation();
            // Use worldPos + hitNormal to get the adjacent integer coordinate
            glm::vec3 pos = glm::vec3(loc.worldPos) + loc.hitNormal;
            
            LOG_INFO_FMT("InputController", "Spawning static tree at " << pos.x << ", " << pos.y << ", " << pos.z);
            m_app->getObjectTemplateManager()->spawnTemplateSequentially("my_model", pos, true);
        } else {
            // Spawn in front of player if no hover
            glm::vec3 pos = m_inputManager->getCameraPosition() + m_inputManager->getCameraFront() * 5.0f;
            LOG_INFO_FMT("InputController", "Spawning static tree in front of player at " << pos.x << ", " << pos.y << ", " << pos.z);
            m_app->getObjectTemplateManager()->spawnTemplateSequentially("my_model", pos, true);
        }
    });

    // Shift + T - Spawn Template (Dynamic)
    m_inputManager->registerActionWithModifier(GLFW_KEY_T, GLFW_MOD_SHIFT, "Spawn Dynamic Template", [this]() {
        glm::vec3 pos;
        if (m_interactionSystem->hasHoveredCube()) {
            pos = m_interactionSystem->getCurrentHoveredLocation().hitPoint;
            pos.y += 5.0f; // Drop from height
        } else {
            pos = m_inputManager->getCameraPosition() + m_inputManager->getCameraFront() * 5.0f;
        }
        LOG_INFO_FMT("InputController", "Spawning dynamic tree at " << pos.x << ", " << pos.y << ", " << pos.z);
        m_app->getObjectTemplateManager()->spawnTemplate("my_model", pos, false);
    });
    
    // O - Toggle breaking forces
    m_inputManager->registerAction(GLFW_KEY_O, "Toggle Breaking Forces", [this]() {
        m_debugFlags.disableBreakingForces = !m_debugFlags.disableBreakingForces;
        LOG_INFO("InputController", std::string("Breaking forces: ") + 
                 (m_debugFlags.disableBreakingForces ? "DISABLED" : "ENABLED"));
    });

    // [ - Decrease Spawn Speed
    m_inputManager->registerAction(GLFW_KEY_LEFT_BRACKET, "Decrease Spawn Speed", [this]() {
        auto* tmplMgr = m_app->getObjectTemplateManager();
        int currentSpeed = tmplMgr->getSpawnSpeed();
        int newSpeed = std::max(10, currentSpeed - 50);
        tmplMgr->setSpawnSpeed(newSpeed);
        LOG_INFO_FMT("InputController", "Spawn Speed Decreased: " << newSpeed << " voxels/frame");
    });

    // ] - Increase Spawn Speed
    m_inputManager->registerAction(GLFW_KEY_RIGHT_BRACKET, "Increase Spawn Speed", [this]() {
        auto* tmplMgr = m_app->getObjectTemplateManager();
        int currentSpeed = tmplMgr->getSpawnSpeed();
        int newSpeed = std::min(5000, currentSpeed + 50);
        tmplMgr->setSpawnSpeed(newSpeed);
        LOG_INFO_FMT("InputController", "Spawn Speed Increased: " << newSpeed << " voxels/frame");
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
