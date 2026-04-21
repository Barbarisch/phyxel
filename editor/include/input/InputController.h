#pragma once

#include <memory>

namespace Phyxel {

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

    /// Seconds since last voxel size mode change (used by HUD to show pop-up label).
    float getModeChangeTimer() const { return m_modeChangeTimer; }

private:
    Input::InputManager* m_inputManager;
    VoxelInteractionSystem* m_interactionSystem;
    Application* m_app;
    DebugFlags m_debugFlags;
    
    // Preview state
    bool m_showTreePreview = false;

    // Timer for mode-change pop-up label (counts down from 1.2 s to 0)
    float m_modeChangeTimer = 0.0f;

    void setupKeyboardBindings();
    void setupMouseBindings();
};

}
