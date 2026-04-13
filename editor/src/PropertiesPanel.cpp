#include "PropertiesPanel.h"
#include "scene/VoxelInteractionSystem.h"
#include "scene/CubeLocation.h"
#include "core/EntityRegistry.h"
#include "core/PlacedObjectManager.h"
#include "core/Chunk.h"
#include "core/HealthComponent.h"
#include "scene/Entity.h"
#include "scene/NPCEntity.h"
#include "ai/NeedsSystem.h"
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace Phyxel::Editor {

static const char* faceName(int face) {
    switch (face) {
        case 0: return "-X (West)";
        case 1: return "+X (East)";
        case 2: return "-Y (Bottom)";
        case 3: return "+Y (Top)";
        case 4: return "-Z (South)";
        case 5: return "+Z (North)";
        default: return "Unknown";
    }
}

void PropertiesPanel::render(bool* open) {
    if (!ImGui::Begin("Properties", open)) {
        ImGui::End();
        return;
    }

    renderVoxelSection();
    renderEntitySection();

    ImGui::End();
}

// ============================================================================
// Voxel Section
// ============================================================================

void PropertiesPanel::renderVoxelSection() {
    if (!m_voxelInteraction) {
        ImGui::TextDisabled("No voxel system");
        return;
    }

    if (!m_voxelInteraction->hasHoveredCube()) {
        if (ImGui::CollapsingHeader("Voxel", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("No voxel hovered");
        }
        return;
    }

    const auto& loc = m_voxelInteraction->getCurrentHoveredLocation();

    if (ImGui::CollapsingHeader("Voxel", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Determine subdivision level
        const char* level = "Cube";
        if (loc.isMicrocube) level = "Microcube";
        else if (loc.isSubcube) level = "Subcube";
        ImGui::Text("Type: %s", level);

        // World position
        ImGui::Text("World: %d, %d, %d", loc.worldPos.x, loc.worldPos.y, loc.worldPos.z);

        // Local position within chunk
        ImGui::Text("Local: %d, %d, %d", loc.localPos.x, loc.localPos.y, loc.localPos.z);

        // Subcube/microcube coordinates
        if (loc.isSubcube) {
            ImGui::Text("Sub: %d, %d, %d", loc.subcubePos.x, loc.subcubePos.y, loc.subcubePos.z);
        }
        if (loc.isMicrocube) {
            ImGui::Text("Sub: %d, %d, %d", loc.subcubePos.x, loc.subcubePos.y, loc.subcubePos.z);
            ImGui::Text("Micro: %d, %d, %d", loc.microcubePos.x, loc.microcubePos.y, loc.microcubePos.z);
        }

        // Hit face
        if (loc.hitFace >= 0) {
            ImGui::Text("Face: %s", faceName(loc.hitFace));
        }

        // Material — read from chunk data
        if (loc.chunk) {
            if (loc.isMicrocube) {
                const Microcube* micro = loc.chunk->getMicrocubeAt(loc.localPos, loc.subcubePos, loc.microcubePos);
                if (micro) {
                    ImGui::Text("Material: %s", micro->getMaterialName().c_str());
                }
            } else if (loc.isSubcube) {
                const Subcube* sub = loc.chunk->getSubcubeAt(loc.localPos, loc.subcubePos);
                if (sub) {
                    ImGui::Text("Material: %s", sub->getMaterialName().c_str());
                }
            } else {
                const Cube* cube = loc.chunk->getCubeAt(loc.localPos);
                if (cube) {
                    ImGui::Text("Material: %s", cube->getMaterialName().c_str());
                }
            }
        }

        // Hit point (exact intersection)
        ImGui::Text("Hit: %.2f, %.2f, %.2f", loc.hitPoint.x, loc.hitPoint.y, loc.hitPoint.z);

        // Placed object ownership
        renderPlacedObjectSection(loc.worldPos);
    }
}

// ============================================================================
// Placed Object Section
// ============================================================================

void PropertiesPanel::renderPlacedObjectSection(const glm::ivec3& worldPos) {
    if (!m_placedObjects) return;

    auto objectIds = m_placedObjects->getAt(worldPos);
    if (objectIds.empty()) return;

    ImGui::Separator();
    ImGui::Text("Placed Object");
    ImGui::Indent();

    for (const auto& objId : objectIds) {
        const Core::PlacedObject* obj = m_placedObjects->get(objId);
        if (!obj) continue;

        ImGui::Text("ID: %s", obj->id.c_str());
        ImGui::Text("Template: %s", obj->templateName.c_str());
        ImGui::Text("Category: %s", obj->category.c_str());
        ImGui::Text("Origin: %d, %d, %d", obj->position.x, obj->position.y, obj->position.z);
        ImGui::Text("Rotation: %d\xC2\xB0", obj->rotation); // degree symbol in UTF-8
        ImGui::Text("AABB: (%d,%d,%d) - (%d,%d,%d)",
            obj->boundingMin.x, obj->boundingMin.y, obj->boundingMin.z,
            obj->boundingMax.x, obj->boundingMax.y, obj->boundingMax.z);

        if (!obj->parentId.empty()) {
            ImGui::Text("Parent: %s", obj->parentId.c_str());
        }

        // Interaction points count
        if (!obj->interactionPoints.empty()) {
            ImGui::Text("Interactions: %d", static_cast<int>(obj->interactionPoints.size()));
        }

        // Metadata keys
        if (!obj->metadata.empty()) {
            if (ImGui::TreeNode("Metadata")) {
                for (auto it = obj->metadata.begin(); it != obj->metadata.end(); ++it) {
                    ImGui::Text("%s: %s", it.key().c_str(), it.value().dump().c_str());
                }
                ImGui::TreePop();
            }
        }
    }

    ImGui::Unindent();
}

// ============================================================================
// Entity Section
// ============================================================================

void PropertiesPanel::renderEntitySection() {
    if (!m_entityRegistry) return;

    if (!ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen)) return;

    // Find nearest entity to the camera look direction
    // Use a generous radius centered on the point ~10 units ahead of camera
    constexpr float searchRadius = 15.0f;
    glm::vec3 searchCenter = m_camPos + m_camFront * 10.0f;
    auto nearby = m_entityRegistry->getEntitiesNear(searchCenter, searchRadius);

    if (nearby.empty()) {
        ImGui::TextDisabled("No entities nearby");
        return;
    }

    // Pick the entity closest to the camera look ray
    Scene::Entity* bestEntity = nullptr;
    std::string bestId;
    float bestScore = std::numeric_limits<float>::max();

    for (const auto& [id, entity] : nearby) {
        if (!entity) continue;
        glm::vec3 toEntity = entity->getPosition() - m_camPos;
        float dist = glm::length(toEntity);
        if (dist < 0.01f) continue;

        // Score = perpendicular distance from entity to camera ray
        glm::vec3 dir = toEntity / dist;
        float dot = glm::dot(dir, m_camFront);
        if (dot < 0.0f) continue; // behind camera

        float perpDist = dist * std::sqrt(1.0f - dot * dot);
        // Weight by distance too so closer entities win ties
        float score = perpDist + dist * 0.1f;
        if (score < bestScore) {
            bestScore = score;
            bestEntity = entity;
            bestId = id;
        }
    }

    if (!bestEntity) {
        ImGui::TextDisabled("No entities in view");
        return;
    }

    // Basic entity info
    ImGui::Text("ID: %s", bestId.c_str());

    // Position
    auto pos = bestEntity->getPosition();
    ImGui::Text("Position: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);

    // Distance from camera
    float dist = glm::length(pos - m_camPos);
    ImGui::Text("Distance: %.1f", dist);

    // Rotation (as euler angles)
    auto rot = bestEntity->getRotation();
    glm::vec3 euler = glm::degrees(glm::eulerAngles(rot));
    ImGui::Text("Rotation: %.0f, %.0f, %.0f", euler.x, euler.y, euler.z);

    // Scale
    auto scale = bestEntity->getScale();
    if (scale != glm::vec3(1.0f)) {
        ImGui::Text("Scale: %.2f, %.2f, %.2f", scale.x, scale.y, scale.z);
    }

    // Health
    auto* health = bestEntity->getHealthComponent();
    if (health) {
        float hp = health->getHealth();
        float maxHp = health->getMaxHealth();
        float pct = health->getHealthPercent();
        ImGui::Separator();
        ImGui::Text("Health: %.0f / %.0f", hp, maxHp);

        // Color-coded health bar
        ImVec4 barColor;
        if (pct > 0.6f) barColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        else if (pct > 0.3f) barColor = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
        else barColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(pct, ImVec2(-1, 0), "");
        ImGui::PopStyleColor();

        if (!health->isAlive()) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "DEAD");
        }
        if (health->isInvulnerable()) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "Invulnerable");
        }
    }

    // NPC-specific info
    auto* npc = dynamic_cast<Scene::NPCEntity*>(bestEntity);
    if (npc) {
        ImGui::Separator();
        ImGui::Text("NPC: %s", npc->getName().c_str());

        if (npc->getBehavior()) {
            ImGui::Text("Behavior: %s", npc->getBehavior()->getBehaviorName().c_str());
        }

        ImGui::Text("Interact Radius: %.1f", npc->getInteractionRadius());

        // Needs summary
        const auto& needs = npc->getNeeds();
        if (ImGui::TreeNode("Needs")) {
            for (const auto& [type, need] : needs.getAllNeeds()) {
                ImGui::Text("%s: %.0f%%", AI::needTypeToString(type).c_str(), need.value);
            }
            ImGui::TreePop();
        }
    }
}

} // namespace Phyxel::Editor
