#include "WorldOutlinerPanel.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>

#include "core/EntityRegistry.h"
#include "core/NPCManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ObjectTemplateManager.h"
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

    // Global Delete key — removes whatever is selected
    if (!m_selectedId.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (m_selectedId.rfind("npc:", 0) == 0 && onRemoveNPC) {
            onRemoveNPC(m_selectedId.substr(4));
            m_selectedId.clear();
        } else if (m_selectedId.rfind("po:", 0) == 0 && onRemovePlacedObject) {
            onRemovePlacedObject(m_selectedId.substr(3));
            m_selectedId.clear();
        } else if (onRemoveEntity) {
            onRemoveEntity(m_selectedId);
            m_selectedId.clear();
        }
    }

    renderEntitiesSection();
    renderNPCsSection();
    renderPlacedObjectsSection();
    renderChunksSection();

    // Popup modals for adding items
    renderAddEntityPopup();
    renderAddNPCPopup();
    renderAddTemplatePopup();

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

    // Header with inline "+" button
    bool headerOpen = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (onSpawnEntity) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20);
        if (ImGui::SmallButton("+###AddEntity"))
            ImGui::OpenPopup("AddEntityPopup");
    }
    if (!headerOpen) return;

    // Group by type tag
    struct TypeGroup { std::string type; std::vector<std::pair<std::string, Scene::Entity*>> items; };
    std::vector<TypeGroup> groups;
    auto addToGroup = [&](const std::string& type, const std::string& id, Scene::Entity* e) {
        for (auto& g : groups) {
            if (g.type == type) { g.items.push_back({id, e}); return; }
        }
        groups.push_back({type, {{id, e}}});
    };

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

    std::string entityToRemove; // deferred removal

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

                // Right-click context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (onRemoveEntity && ImGui::MenuItem("Remove")) {
                        entityToRemove = id;
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::TreePop();
        }
    }

    // Deferred removal (safe — not mid-iteration of registry)
    if (!entityToRemove.empty() && onRemoveEntity) {
        onRemoveEntity(entityToRemove);
        if (m_selectedId == entityToRemove) m_selectedId.clear();
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

    bool headerOpen = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (onSpawnNPC) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20);
        if (ImGui::SmallButton("+###AddNPC"))
            ImGui::OpenPopup("AddNPCPopup");
    }
    if (!headerOpen) return;

    auto names = m_npcManager->getAllNPCNames();
    std::sort(names.begin(), names.end());

    std::string npcToRemove;

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

        if (ImGui::BeginPopupContextItem()) {
            if (onRemoveNPC && ImGui::MenuItem("Remove")) {
                npcToRemove = name;
            }
            ImGui::EndPopup();
        }
    }

    if (!npcToRemove.empty() && onRemoveNPC) {
        onRemoveNPC(npcToRemove);
        if (m_selectedId == "npc:" + npcToRemove) m_selectedId.clear();
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

    bool headerOpen = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (onSpawnTemplate) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20);
        if (ImGui::SmallButton("+###AddTemplate"))
            ImGui::OpenPopup("AddTemplatePopup");
    }
    if (!headerOpen) return;

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

    // Right-click context menu
    std::string poToRemove;
    if (ImGui::BeginPopupContextItem()) {
        if (onRemovePlacedObject && ImGui::MenuItem("Remove")) {
            poToRemove = obj.id;
        }
        ImGui::EndPopup();
    }

    if (nodeOpen && hasChildren) {
        for (auto& other : allObjects) {
            if (other.parentId == obj.id) {
                renderPlacedObjectNode(other, allObjects);
            }
        }
        ImGui::TreePop();
    }

    // Deferred removal
    if (!poToRemove.empty() && onRemovePlacedObject) {
        onRemovePlacedObject(poToRemove);
        if (m_selectedId == "po:" + poToRemove) m_selectedId.clear();
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

// ---------------------------------------------------------------------------
// Add Entity popup
// ---------------------------------------------------------------------------

void WorldOutlinerPanel::renderAddEntityPopup() {
    if (!ImGui::BeginPopup("AddEntityPopup")) return;

    ImGui::Text("Spawn Entity");
    ImGui::Separator();

    const char* types[] = {"animated", "physics", "spider"};
    ImGui::Combo("Type", &m_addEntityType, types, IM_ARRAYSIZE(types));
    ImGui::DragFloat3("Position", m_addEntityPos, 0.5f);

    if (ImGui::Button("Spawn", ImVec2(120, 0))) {
        if (onSpawnEntity) {
            std::string newId = onSpawnEntity(
                types[m_addEntityType],
                glm::vec3(m_addEntityPos[0], m_addEntityPos[1], m_addEntityPos[2]));
            if (!newId.empty()) m_selectedId = newId;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Add NPC popup
// ---------------------------------------------------------------------------

void WorldOutlinerPanel::renderAddNPCPopup() {
    if (!ImGui::BeginPopup("AddNPCPopup")) return;

    ImGui::Text("Spawn NPC");
    ImGui::Separator();

    ImGui::InputText("Name", m_addNPCName, sizeof(m_addNPCName));
    ImGui::DragFloat3("Position##npc", m_addNPCPos, 0.5f);

    const char* behaviors[] = {"idle", "patrol", "wander"};
    ImGui::Combo("Behavior", &m_addNPCBehavior, behaviors, IM_ARRAYSIZE(behaviors));

    bool nameEmpty = (m_addNPCName[0] == '\0');
    if (nameEmpty) ImGui::BeginDisabled();
    if (ImGui::Button("Spawn##npc", ImVec2(120, 0))) {
        if (onSpawnNPC) {
            onSpawnNPC(m_addNPCName,
                       glm::vec3(m_addNPCPos[0], m_addNPCPos[1], m_addNPCPos[2]),
                       behaviors[m_addNPCBehavior]);
            m_selectedId = "npc:" + std::string(m_addNPCName);
            m_addNPCName[0] = '\0'; // reset
        }
        ImGui::CloseCurrentPopup();
    }
    if (nameEmpty) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel##npc", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Add Template popup
// ---------------------------------------------------------------------------

void WorldOutlinerPanel::renderAddTemplatePopup() {
    if (!ImGui::BeginPopup("AddTemplatePopup")) return;

    ImGui::Text("Place Template");
    ImGui::Separator();

    // Build template name list from ObjectTemplateManager
    static std::vector<std::string> templateNames;
    static std::vector<const char*> templateNamesCStr;
    if (m_templateManager) {
        templateNames = m_templateManager->getTemplateNames();
        std::sort(templateNames.begin(), templateNames.end());
        templateNamesCStr.resize(templateNames.size());
        for (size_t i = 0; i < templateNames.size(); i++)
            templateNamesCStr[i] = templateNames[i].c_str();
    }

    if (templateNamesCStr.empty()) {
        ImGui::TextDisabled("No templates available");
    } else {
        if (m_addTemplateIdx >= (int)templateNamesCStr.size())
            m_addTemplateIdx = 0;
        ImGui::Combo("Template", &m_addTemplateIdx, templateNamesCStr.data(), (int)templateNamesCStr.size());
    }

    ImGui::DragFloat3("Position##tmpl", m_addTemplatePos, 0.5f);
    ImGui::SliderInt("Rotation", &m_addTemplateRot, 0, 270);
    // Snap to 90-degree increments
    m_addTemplateRot = (m_addTemplateRot / 90) * 90;

    bool canPlace = !templateNamesCStr.empty() && onSpawnTemplate;
    if (!canPlace) ImGui::BeginDisabled();
    if (ImGui::Button("Place", ImVec2(120, 0))) {
        if (onSpawnTemplate && !templateNames.empty()) {
            std::string objId = onSpawnTemplate(
                templateNames[m_addTemplateIdx],
                glm::ivec3((int)m_addTemplatePos[0], (int)m_addTemplatePos[1], (int)m_addTemplatePos[2]),
                m_addTemplateRot);
            if (!objId.empty()) m_selectedId = "po:" + objId;
        }
        ImGui::CloseCurrentPopup();
    }
    if (!canPlace) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel##tmpl", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

} // namespace Phyxel::Editor
