#pragma once

#include <string>

namespace Phyxel {
    namespace Graphics { class CameraManager; class Camera; }
    namespace Core { class EntityRegistry; }
    namespace Input { class InputManager; }
}

namespace Phyxel::Editor {

/// A dockable ImGui panel for managing camera mode, slots, and entity follow.
class CameraPanel {
public:
    CameraPanel() = default;

    // --- Subsystem wiring ---
    void setCameraManager(Graphics::CameraManager* cm) { m_cameraManager = cm; }
    void setCamera(Graphics::Camera* cam) { m_camera = cam; }
    void setEntityRegistry(Core::EntityRegistry* reg) { m_entityRegistry = reg; }
    void setInputManager(Input::InputManager* im) { m_inputManager = im; }

    /// Render the panel.  @param open  Visibility bool (nullptr = always visible)
    void render(bool* open = nullptr);

private:
    void renderCurrentState();
    void renderModeSection();
    void renderSlotList();
    void renderFollowSection();

    Graphics::CameraManager* m_cameraManager = nullptr;
    Graphics::Camera* m_camera = nullptr;
    Core::EntityRegistry* m_entityRegistry = nullptr;
    Input::InputManager* m_inputManager = nullptr;

    char m_newSlotName[64] = "bookmark";
    float m_followDistance = 5.0f;
    float m_followHeight = 1.5f;
};

} // namespace Phyxel::Editor
