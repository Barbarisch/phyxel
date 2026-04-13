#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "core/PlacedObjectManager.h"

namespace Phyxel {
    class ChunkManager;
    namespace Core {
        class EntityRegistry;
        class NPCManager;
    }
}

namespace Phyxel::Editor {

/// Dockable tree-view panel showing all scene objects: entities, NPCs, placed
/// objects, and loaded chunks.  Selecting a row populates the Properties panel.
class WorldOutlinerPanel {
public:
    WorldOutlinerPanel() = default;

    // Wire subsystems
    void setEntityRegistry(Core::EntityRegistry* reg)       { m_entityRegistry = reg; }
    void setNPCManager(Core::NPCManager* mgr)               { m_npcManager = mgr; }
    void setPlacedObjectManager(Core::PlacedObjectManager* p){ m_placedObjects = p; }
    void setChunkManager(ChunkManager* cm)                   { m_chunkManager = cm; }

    /// Render the panel.  @param open  visibility bool (nullptr = always visible)
    void render(bool* open);

    /// Currently-selected item (empty = none). Usable by other panels.
    const std::string& selectedId() const { return m_selectedId; }

private:
    void renderEntitiesSection();
    void renderNPCsSection();
    void renderPlacedObjectsSection();
    void renderPlacedObjectNode(const Core::PlacedObject& obj,
                                const std::vector<Core::PlacedObject>& allObjects);
    void renderChunksSection();

    Core::EntityRegistry*      m_entityRegistry = nullptr;
    Core::NPCManager*          m_npcManager     = nullptr;
    Core::PlacedObjectManager* m_placedObjects   = nullptr;
    ChunkManager*              m_chunkManager    = nullptr;

    std::string m_selectedId;       // currently selected item ID
    std::string m_filterText;       // search filter
    char        m_filterBuf[128]{}; // ImGui input buffer
};

} // namespace Phyxel::Editor
