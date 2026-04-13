#include "WorldOutlinerPanel.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>

#include "core/EntityRegistry.h"
#include "core/NPCManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ChunkManager.h"
#include "scene/Entity.h"
#include "scene/NPCEntity.h"
#include "scene/NPCBehavior.h"

namespace Phyxel::Editor {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static bool passesFilter(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    // Case-insensitive substring match
    std::string lowerText = text;
    std::string lowerFilter = filter;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
    return lowerText.find(lowerFilter) != std::string::npos;
}

// ---------------------------------------------------------------------------
// main render
// ---------------------------------------------------------------------------

void WorldOutlinerPanel::render(bool* open) {
    if (!ImGui::Begin("World Outliner", open)) {
        ImGui::End();
        return;
    }

    // Search filter
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter...", m_filterBuf, sizeof(m_filterBuf));
    m_filterText = m_filterBuf;
    ImGui::Separator();

    renderEntitiesSection();
    renderNPCsSection();
    renderPlacedObjectsSection();
    renderChunksSection();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

void WorldOutlinerPanel::renderEntitiesSection() {
    if (!m_entityRegistry) return;

    auto ids = m_entityRegistry->getAllIds();

    char label[128];
    snprintf(label, sizeof(label), "Entities (%zu)###Entities", ids.size());

    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Group by type tag
    struct TypeGroup { std::string type; std::vector<std::pair<std::string, Scene::Entity*>> items; };
    std::vector<TypeGroup> groups;
    auto addToGroup = [&](const std::string& type, const std::string& id, Scene::Entity* e) {
        for (auto& g : groups) {
            if (g.type == type) { g.items.push_back({id, e}); return; }
        }
        groups.push_back({type, {{id, e}}});
    };

    // Use toJson to get types, but also need Entity* for position display
    // More efficient: iterate via forEach + entityToJson for type
    // Best: use getEntitiesByType for known types, but we don't know all types
    // Simplest: iterate all, get json per entity for the type tag
    for (auto& id : ids) {
        auto j = m_entityRegistry->entityToJson(id);
        std::string type = j.value("type", "unknown");
        Scene::Entity* entity = m_entityRegistry->getEntity(id);
        addToGroup(type, id, entity);
    }

    // Sort groups by type name
    std::sort(groups.begin(), groups.end(), [](const TypeGroup& a, const TypeGroup& b) {
        return a.type < b.type;
    });

    for (auto& group : groups) {
        char groupLabel[128];
        snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)###ent_%s",
                 group.type.c_str(), group.items.size(), group.type.c_str());

        if (ImGui::TreeNode(groupLabel)) {
            for (auto& [id, entity] : group.items) {
                if (!passesFilter(id, m_filterText)) continue;

                bool selected = (m_selectedId == id);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (selected) flags |= ImGuiTreeNodeFlags_Selected;

                // Show position inline
                if (entity) {
                    auto pos = entity->getPosition();
                    char itemLabel[256];
                    snprintf(itemLabel, sizeof(itemLabel), "%s  (%.0f, %.0f, %.0f)###%s",
                             id.c_str(), pos.x, pos.y, pos.z, id.c_str());
                    ImGui::TreeNodeEx(itemLabel, flags);
                } else {
                    ImGui::TreeNodeEx(id.c_str(), flags);
                }

                if (ImGui::IsItemClicked())
                    m_selectedId = id;
            }
            ImGui::TreePop();
        }
    }
}

// ---------------------------------------------------------------------------
// NPCs
// ---------------------------------------------------------------------------

void WorldOutlinerPanel::renderNPCsSection() {
    if (!m_npcManager) return;

    size_t count = m_npcManager->getNPCCount();
    char label[128];
    snprintf(label, sizeof(label), "NPCs (%zu)###NPCs", count);

    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
        return;

    auto names = m_npcManager->getAllNPCNames();
    std::sort(names.begin(), names.end());

    for (auto& name : names) {
        if (!passesFilter(name, m_filterText)) continue;

        auto* npc = m_npcManager->getNPC(name);
        if (!npc) continue;

        bool selected = (m_selectedId == "npc:" + name);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;

        auto pos = npc->getPosition();
        const char* behavior = npc->getBehavior() ? npc->getBehavior()->getBehaviorName().c_str() : "none";

        char itemLabel[256];
        snprintf(itemLabel, sizeof(itemLabel), "%s  [%s]  (%.0f, %.0f, %.0f)###npc_%s",
                 name.c_str(), behavior, pos.x, pos.y, pos.z, name.c_str());
        ImGui::TreeNodeEx(itemLabel, flags);

        if (ImGui::IsItemClicked())
            m_selectedId = "npc:" + name;
    }
}

// ---------------------------------------------------------------------------
// Placed Objects
// ---------------------------------------------------------------------------

void WorldOutlinerPanel::renderPlacedObjectsSection() {
    if (!m_placedObjects) return;

    auto objects = m_placedObjects->list();
    char label[128];
    snprintf(label, sizeof(label), "Placed Objects (%zu)###PlacedObjects", objects.size());

    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Group by category (template vs structure)
    struct CatGroup { std::string category; std::vector<const Core::PlacedObject*> items; };
    std::vector<CatGroup> groups;
    for (auto& obj : objects) {
        if (obj.parentId.empty()) { // Only root objects at top level
            bool found = false;
            for (auto& g : groups) {
                if (g.category == obj.category) { g.items.push_back(&obj); found = true; break; }
            }
            if (!found) groups.push_back({obj.category, {&obj}});
        }
    }

    for (auto& group : groups) {
        char groupLabel[128];
        snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)###po_%s",
                 group.category.c_str(), group.items.size(), group.category.c_str());

        if (ImGui::TreeNode(groupLabel)) {
            for (auto* obj : group.items) {
                renderPlacedObjectNode(*obj, objects);
            }
            ImGui::TreePop();
        }
    }
}

void WorldOutlinerPanel::renderPlacedObjectNode(const Core::PlacedObject& obj,
                                                  const std::vector<Core::PlacedObject>& allObjects) {
    if (!passesFilter(obj.id, m_filterText) && !passesFilter(obj.templateName, m_filterText))
        return;

    // Check if this object has children
    bool hasChildren = false;
    for (auto& other : allObjects) {
        if (other.parentId == obj.id) { hasChildren = true; break; }
    }

    bool selected = (m_selectedId == "po:" + obj.id);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    char itemLabel[256];
    auto& p = obj.position;
    snprintf(itemLabel, sizeof(itemLabel), "%s  [%s]  (%d, %d, %d)###po_%s",
             obj.id.c_str(), obj.templateName.c_str(), p.x, p.y, p.z, obj.id.c_str());

    bool nodeOpen = ImGui::TreeNodeEx(itemLabel, flags);

    if (ImGui::IsItemClicked())
        m_selectedId = "po:" + obj.id;

    if (nodeOpen && hasChildren) {
        for (auto& other : allObjects) {
            if (other.parentId == obj.id) {
                renderPlacedObjectNode(other, allObjects);
            }
        }
        ImGui::TreePop();
    }
}

// ---------------------------------------------------------------------------
// Chunks
// ---------------------------------------------------------------------------

void WorldOutlinerPanel::renderChunksSection() {
    if (!m_chunkManager) return;

    size_t chunkCount = m_chunkManager->chunkMap.size();
    char label[128];
    snprintf(label, sizeof(label), "Chunks (%zu)###Chunks", chunkCount);

    if (!ImGui::CollapsingHeader(label))
        return;

    // Sort chunks by origin for stable display
    struct ChunkInfo { glm::ivec3 origin; size_t cubes; size_t subcubes; size_t microcubes; };
    std::vector<ChunkInfo> infos;
    infos.reserve(chunkCount);
    for (auto& [coord, chunk] : m_chunkManager->chunkMap) {
        if (!chunk) continue;
        infos.push_back({
            chunk->getWorldOrigin(),
            chunk->getCubeCount(),
            chunk->getStaticSubcubeCount(),
            chunk->getStaticMicrocubeCount()
        });
    }
    std::sort(infos.begin(), infos.end(), [](const ChunkInfo& a, const ChunkInfo& b) {
        if (a.origin.x != b.origin.x) return a.origin.x < b.origin.x;
        if (a.origin.y != b.origin.y) return a.origin.y < b.origin.y;
        return a.origin.z < b.origin.z;
    });

    for (auto& info : infos) {
        char chunkLabel[256];
        snprintf(chunkLabel, sizeof(chunkLabel), "(%d, %d, %d)  cubes:%zu sub:%zu micro:%zu",
                 info.origin.x, info.origin.y, info.origin.z,
                 info.cubes, info.subcubes, info.microcubes);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        ImGui::TreeNodeEx(chunkLabel, flags);
    }
}

} // namespace Phyxel::Editor
