#pragma once

#include <string>
#include <functional>
#include <glm/glm.hpp>
#include "core/SceneManager.h"

namespace Phyxel {
    class VoxelInteractionSystem;
    class ObjectTemplateManager;
    namespace Core {
        class EntityRegistry;
        class PlacedObjectManager;
        class NPCManager;
        class DynamicFurnitureManager;
    }
    namespace Scene {
        class Entity;
        class AnimatedVoxelCharacter;
        class NPCEntity;
    }
}

namespace Phyxel::Editor {

/// Selection types for the Properties panel.
enum class SelectionType { None, Entity, NPC, PlacedObject, Scene };

/// A dockable ImGui panel that shows an inspector for whatever is selected in
/// the World Outliner.  When nothing is selected it falls back to the
/// crosshair-based voxel/entity inspector.
class PropertiesPanel {
public:
    PropertiesPanel() = default;

    // --- Subsystem wiring ---
    void setVoxelInteraction(VoxelInteractionSystem* vis) { m_voxelInteraction = vis; }
    void setEntityRegistry(Core::EntityRegistry* reg)     { m_entityRegistry = reg; }
    void setPlacedObjectManager(Core::PlacedObjectManager* pom) { m_placedObjects = pom; }
    void setNPCManager(Core::NPCManager* mgr)             { m_npcManager = mgr; }
    void setObjectTemplateManager(ObjectTemplateManager* otm) { m_templateManager = otm; }
    void setDynamicFurnitureManager(Core::DynamicFurnitureManager* dfm) { m_furnitureManager = dfm; }
    void setSceneManager(Core::SceneManager* sm)                        { m_sceneManager = sm; }
    void setCameraState(const glm::vec3& pos, const glm::vec3& front) { m_camPos = pos; m_camFront = front; }

    // --- Scene property mutation callbacks ---
    // Called when a scene field is edited. field is one of: "name", "worldDatabase",
    // "transitionStyle", "onEnterScript", "onExitScript".
    std::function<void(const std::string& id, const std::string& field,
                       const std::string& value)> onScenePropertyChanged;
    // Called when "Save Manifest" is pressed.
    std::function<void()> onSaveManifest;
    // Called when "Switch to Scene" is pressed from the inspector.
    std::function<bool(const std::string& id)> onSwitchScene;

    // --- Selection (set each frame by Application from WorldOutliner) ---
    void setSelection(SelectionType type, const std::string& id) { m_selType = type; m_selId = id; }
    void clearSelection() { m_selType = SelectionType::None; m_selId.clear(); }

    /// Render the panel.  @param open  Visibility bool (nullptr = always visible)
    void render(bool* open = nullptr);

private:
    // Selection-driven inspectors
    void renderEntityInspector(const std::string& id);
    void renderAnimatedCharInspector(Scene::AnimatedVoxelCharacter* ch);
    void renderNPCInspector(const std::string& name);
    void renderPlacedObjectInspector(const std::string& id);
    void renderSceneInspector(const std::string& id);

    // Fallback crosshair inspectors
    void renderVoxelSection();
    void renderPlacedObjectSection(const glm::ivec3& worldPos);
    void renderNearestEntitySection();

    VoxelInteractionSystem*    m_voxelInteraction = nullptr;
    Core::EntityRegistry*      m_entityRegistry   = nullptr;
    Core::PlacedObjectManager* m_placedObjects     = nullptr;
    Core::NPCManager*          m_npcManager        = nullptr;
    ObjectTemplateManager* m_templateManager  = nullptr;
    Core::DynamicFurnitureManager* m_furnitureManager = nullptr;
    Core::SceneManager*        m_sceneManager     = nullptr;
    glm::vec3 m_camPos{0.0f};
    glm::vec3 m_camFront{0.0f, 0.0f, 1.0f};

    SelectionType m_selType = SelectionType::None;
    std::string   m_selId;
};

} // namespace Phyxel::Editor
