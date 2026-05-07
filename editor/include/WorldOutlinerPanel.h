#pragma once

#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include "core/PlacedObjectManager.h"
#include "core/SceneManager.h"

namespace Phyxel {
    class ChunkManager;
    class ObjectTemplateManager;
    namespace Core {
        class EntityRegistry;
        class NPCManager;
    }
}

namespace Phyxel::Editor {

/// Dockable tree-view panel showing all scene objects: entities, NPCs, placed
/// objects, and loaded chunks.  Selecting a row populates the Properties panel.
/// Supports add/remove via right-click context menus and toolbar buttons.
class WorldOutlinerPanel {
public:
    WorldOutlinerPanel() = default;

    // Wire subsystems (read-only access for display)
    void setEntityRegistry(Core::EntityRegistry* reg)        { m_entityRegistry = reg; }
    void setNPCManager(Core::NPCManager* mgr)                { m_npcManager = mgr; }
    void setPlacedObjectManager(Core::PlacedObjectManager* p){ m_placedObjects = p; }
    void setChunkManager(ChunkManager* cm)                    { m_chunkManager = cm; }
    void setObjectTemplateManager(ObjectTemplateManager* tm)  { m_templateManager = tm; }
    void setSceneManager(Core::SceneManager* sm)               { m_sceneManager = sm; }

    // --- Scene mutation callbacks (set by Application) ---
    // Switch to a scene by ID. Returns true on success.
    std::function<bool(const std::string& id)> onSwitchScene;
    // Create a new blank scene. Returns the new scene ID or empty on failure.
    std::function<std::string(const std::string& name, const std::string& dbPath, int sceneType)> onCreateScene;
    // Delete a scene by ID. Returns true on success.
    std::function<bool(const std::string& id)> onDeleteScene;
    // Save the scene manifest to game.json.
    std::function<void()> onSaveManifest;
    // Convert a single-DB session to a multi-scene project.
    std::function<void()> onConvertToMultiScene;

    // --- Mutation callbacks (set by Application) ---
    // Remove entity by ID. Returns true on success.
    std::function<bool(const std::string& id)> onRemoveEntity;
    // Spawn entity: type ("animated"/"physics"/"spider"), position. Returns new ID or empty.
    std::function<std::string(const std::string& type, const glm::vec3& pos)> onSpawnEntity;
    // Remove NPC by name.
    std::function<bool(const std::string& name)> onRemoveNPC;
    // Spawn NPC: name, position, behavior. Returns true on success.
    std::function<bool(const std::string& name, const glm::vec3& pos, const std::string& behavior)> onSpawnNPC;
    // Remove placed object by ID.
    std::function<bool(const std::string& id)> onRemovePlacedObject;
    // Delete a chunk by chunk coordinate (chunkOrigin / 32).
    std::function<void(const glm::ivec3& chunkCoord)> onDeleteChunk;
    // Spawn template: templateName, position, rotation. Returns object ID or empty.
    std::function<std::string(const std::string& name, const glm::ivec3& pos, int rotation)> onSpawnTemplate;

    /// Render the panel.  @param open  visibility bool (nullptr = always visible)
    void render(bool* open);

    /// Currently-selected item (empty = none). Usable by other panels.
    const std::string& selectedId() const { return m_selectedId; }

private:
    void renderScenesSection();
    void renderEntitiesSection();
    void renderNPCsSection();
    void renderPlacedObjectsSection();
    void renderPlacedObjectNode(const Core::PlacedObject& obj,
                                const std::vector<Core::PlacedObject>& allObjects);
    void renderChunksSection();

    void renderAddEntityPopup();
    void renderAddNPCPopup();
    void renderAddTemplatePopup();
    void renderCreateScenePopup();

    Core::EntityRegistry*      m_entityRegistry  = nullptr;
    Core::NPCManager*          m_npcManager      = nullptr;
    Core::PlacedObjectManager* m_placedObjects    = nullptr;
    ChunkManager*              m_chunkManager     = nullptr;
    ObjectTemplateManager*     m_templateManager  = nullptr;
    Core::SceneManager*        m_sceneManager     = nullptr;

    std::string m_selectedId;       // currently selected item ID
    std::string m_filterText;       // search filter
    char        m_filterBuf[128]{}; // ImGui input buffer

    // Add-entity popup state
    int   m_addEntityType = 0;      // 0=animated, 1=physics, 2=spider
    float m_addEntityPos[3]{0, 20, 0};

    // Add-NPC popup state
    char  m_addNPCName[64]{};
    float m_addNPCPos[3]{0, 20, 0};
    int   m_addNPCBehavior = 0;     // 0=idle, 1=patrol, 2=wander

    // Add-template popup state
    int   m_addTemplateIdx = 0;
    float m_addTemplatePos[3]{0, 16, 0};
    int   m_addTemplateRot = 0;

    // Create-scene popup state
    char  m_createSceneName[128]{};
    char  m_createSceneDb[256]{};
    bool  m_createSceneNameTouched = false; // true once user edits name
    int   m_createSceneType        = 0;    // 0=World, 1=Menu, 2=Cutscene
};

} // namespace Phyxel::Editor
