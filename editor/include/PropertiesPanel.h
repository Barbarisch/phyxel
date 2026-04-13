#pragma once

#include <string>
#include <glm/glm.hpp>

namespace Phyxel {
    class VoxelInteractionSystem;
    namespace Core {
        class EntityRegistry;
        class PlacedObjectManager;
    }
    namespace Scene { class Entity; }
}

namespace Phyxel::Editor {

/// A dockable ImGui panel that inspects the currently hovered voxel, placed object,
/// and nearest entity. Updates live each frame based on the crosshair/raycast state.
class PropertiesPanel {
public:
    PropertiesPanel() = default;

    /// Wire up the systems this panel reads from.
    void setVoxelInteraction(VoxelInteractionSystem* vis) { m_voxelInteraction = vis; }
    void setEntityRegistry(Core::EntityRegistry* reg) { m_entityRegistry = reg; }
    void setPlacedObjectManager(Core::PlacedObjectManager* pom) { m_placedObjects = pom; }
    void setCameraState(const glm::vec3& pos, const glm::vec3& front) { m_camPos = pos; m_camFront = front; }

    /// Render the panel. Call each frame inside the ImGui render block.
    /// @param open  Visibility bool (nullptr = always visible)
    void render(bool* open = nullptr);

private:
    void renderVoxelSection();
    void renderPlacedObjectSection(const glm::ivec3& worldPos);
    void renderEntitySection();

    VoxelInteractionSystem* m_voxelInteraction = nullptr;
    Core::EntityRegistry* m_entityRegistry = nullptr;
    Core::PlacedObjectManager* m_placedObjects = nullptr;
    glm::vec3 m_camPos{0.0f};
    glm::vec3 m_camFront{0.0f, 0.0f, 1.0f};
};

} // namespace Phyxel::Editor
