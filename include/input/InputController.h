#pragma once

#include <memory>

namespace VulkanCube {

namespace Input {
    class InputManager;
}
class VoxelInteractionSystem;
class Application;

struct DebugFlags {
    bool hoverDetection = false;
    bool cameraMovement = false;
    bool performanceStats = false;
    bool chunkOperations = false;
    bool cubeOperations = false;
    bool disableBreakingForces = false;
    bool showForceSystemDebug = false;
    float manualForceValue = 500.0f;
};

class InputController {
public:
    InputController(Input::InputManager* inputManager, 
                   VoxelInteractionSystem* interactionSystem,
                   Application* app);
    ~InputController() = default;

    void initializeBindings();
    
    // Called every frame to handle continuous input/logic
    void update(float deltaTime);

    DebugFlags& getDebugFlags() { return m_debugFlags; }

private:
    Input::InputManager* m_inputManager;
    VoxelInteractionSystem* m_interactionSystem;
    Application* m_app;
    DebugFlags m_debugFlags;
    
    // Preview state
    bool m_showTreePreview = false;

    void setupKeyboardBindings();
    void setupMouseBindings();
};

}
