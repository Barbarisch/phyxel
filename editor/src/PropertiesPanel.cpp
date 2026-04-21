#include "PropertiesPanel.h"
#include "core/MaterialRegistry.h"
#include "scene/VoxelInteractionSystem.h"
#include "scene/CubeLocation.h"
#include "core/EntityRegistry.h"
#include "core/PlacedObjectManager.h"
#include "core/NPCManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "physics/Material.h"
#include "core/Chunk.h"
#include "core/HealthComponent.h"
#include "scene/Entity.h"
#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "ai/NeedsSystem.h"
#include "core/DynamicFurnitureManager.h"
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <algorithm>

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

// ============================================================================
// Main render
// ============================================================================

void PropertiesPanel::render(bool* open) {
    if (!ImGui::Begin("Properties", open)) {
        ImGui::End();
        return;
    }

    // If something is selected in the World Outliner, show its inspector
    if (m_selType != SelectionType::None && !m_selId.empty()) {
        switch (m_selType) {
            case SelectionType::Entity:
                renderEntityInspector(m_selId);
                break;
            case SelectionType::NPC:
                renderNPCInspector(m_selId);
                break;
            case SelectionType::PlacedObject:
                renderPlacedObjectInspector(m_selId);
                break;
            default:
                break;
        }
    } else {
        // Fallback: crosshair-based inspection
        renderVoxelSection();
        renderNearestEntitySection();
    }

    ImGui::End();
}

// ============================================================================
// Selection-driven: Entity inspector
// ============================================================================

void PropertiesPanel::renderEntityInspector(const std::string& id) {
    if (!m_entityRegistry) { ImGui::TextDisabled("No entity registry"); return; }

    Scene::Entity* entity = m_entityRegistry->getEntity(id);
    if (!entity) { ImGui::TextDisabled("Entity not found: %s", id.c_str()); return; }

    auto j = m_entityRegistry->entityToJson(id);
    std::string type = j.value("type", "unknown");

    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", id.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", type.c_str());
    ImGui::Separator();

    // Position
    auto pos = entity->getPosition();
    ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);

    // Rotation
    auto rot = entity->getRotation();
    glm::vec3 euler = glm::degrees(glm::eulerAngles(rot));
    ImGui::Text("Rotation: %.0f, %.0f, %.0f", euler.x, euler.y, euler.z);

    // Scale
    auto scale = entity->getScale();
    if (scale != glm::vec3(1.0f)) {
        ImGui::Text("Scale: %.2f, %.2f, %.2f", scale.x, scale.y, scale.z);
    }

    // Health
    auto* health = entity->getHealthComponent();
    if (health) {
        ImGui::Separator();
        float hp = health->getHealth();
        float maxHp = health->getMaxHealth();
        float pct = health->getHealthPercent();
        ImGui::Text("Health: %.0f / %.0f", hp, maxHp);

        ImVec4 barColor;
        if (pct > 0.6f) barColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        else if (pct > 0.3f) barColor = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
        else barColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(pct, ImVec2(-1, 0), "");
        ImGui::PopStyleColor();

        if (!health->isAlive()) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "DEAD");
        if (health->isInvulnerable()) ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "Invulnerable");
    }

    // If it's an AnimatedVoxelCharacter, show the full animated inspector
    auto* animChar = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity);
    if (animChar) {
        ImGui::Separator();
        renderAnimatedCharInspector(animChar);
    }
}

// ============================================================================
// Animated character inspector (moved from Application::renderAnimatedCharPanel)
// ============================================================================

void PropertiesPanel::renderAnimatedCharInspector(Scene::AnimatedVoxelCharacter* ch) {
    // --- State & Motion ---
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "State: %s",
        ch->stateToString(ch->getAnimationState()).c_str());
    ImGui::Text("Yaw: %.1f deg", glm::degrees(ch->getYaw()));

    glm::vec3 vel = ch->getControllerVelocity();
    float speed = glm::length(glm::vec2(vel.x, vel.z));
    ImGui::Text("Speed: %.2f m/s  (vel: %.1f, %.1f, %.1f)", speed, vel.x, vel.y, vel.z);
    ImGui::Text("Archetype: %s", ch->getArchetype().c_str());

    // --- Animation ---
    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Clip: %s", ch->getCurrentClipName().c_str());
        float progress = ch->getAnimationProgress();
        float duration = ch->getAnimationDuration();
        ImGui::Text("Progress: %.0f%%  (%.2f / %.2f s)", progress * 100.0f,
                     progress * duration, duration);
        ImGui::ProgressBar(progress, ImVec2(-1, 0));

        float bd = ch->getBlendDuration();
        if (ImGui::SliderFloat("Blend duration (s)", &bd, 0.0f, 1.0f, "%.2f"))
            ch->setBlendDuration(bd);

        float stepH = ch->getMaxStepHeight();
        if (ImGui::SliderFloat("Step-up height (m)", &stepH, 0.0f, 1.5f, "%.2f"))
            ch->setMaxStepHeight(stepH);

        // Available clips
        const auto& clips = ch->getAnimationClips();
        char clipsLabel[64];
        snprintf(clipsLabel, sizeof(clipsLabel), "Clips (%d)", (int)clips.size());
        if (ImGui::TreeNode(clipsLabel)) {
            for (const auto& clip : clips) {
                ImGui::BulletText("%s  dur=%.2fs spd=%.1f",
                    clip.name.c_str(), clip.duration, clip.speed);
            }
            ImGui::TreePop();
        }
    }

    // --- Voxel Model Stats ---
    if (ImGui::CollapsingHeader("Voxel Model", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& model = ch->getVoxelModel();
        ImGui::Text("Bone shapes: %d", (int)model.shapes.size());

        int totalVoxels = 0;
        for (const auto& shape : model.shapes) {
            int vx = std::max(1, (int)std::round(shape.size.x));
            int vy = std::max(1, (int)std::round(shape.size.y));
            int vz = std::max(1, (int)std::round(shape.size.z));
            totalVoxels += vx * vy * vz;
        }
        ImGui::Text("Approx total voxels: %d", totalVoxels);

        if (ImGui::TreeNode("Shape details")) {
            const auto& skel = ch->getSkeleton();
            for (const auto& shape : model.shapes) {
                const char* name = (shape.boneId >= 0 && shape.boneId < (int)skel.bones.size())
                    ? skel.bones[shape.boneId].name.c_str() : "?";
                ImGui::Text("  [%2d] %s  size(%.1f,%.1f,%.1f) off(%.1f,%.1f,%.1f)",
                    shape.boneId, name,
                    shape.size.x, shape.size.y, shape.size.z,
                    shape.offset.x, shape.offset.y, shape.offset.z);
            }
            ImGui::TreePop();
        }
    }

    // --- Skeleton ---
    if (ImGui::CollapsingHeader("Skeleton")) {
        const auto& skel = ch->getSkeleton();
        ImGui::Text("Bones: %d", (int)skel.bones.size());

        for (const auto& bone : skel.bones) {
            const char* parentName = (bone.parentId >= 0 && bone.parentId < (int)skel.bones.size())
                ? skel.bones[bone.parentId].name.c_str() : "(root)";

            char label[128];
            snprintf(label, sizeof(label), "[%2d] %s", bone.id, bone.name.c_str());
            bool open = ImGui::TreeNode(label);
            if (open) {
                ImGui::Text("Parent: [%d] %s", bone.parentId, parentName);
                glm::vec3 lp = bone.localPosition;
                ImGui::Text("Local pos: %.3f, %.3f, %.3f", lp.x, lp.y, lp.z);
                glm::vec3 gp = glm::vec3(bone.globalTransform[3]);
                ImGui::Text("Model pos: %.3f, %.3f, %.3f", gp.x, gp.y, gp.z);
                ImGui::TreePop();
            }
        }
    }

    // --- Attachments ---
    if (ch->hasAttachments()) {
        if (ImGui::CollapsingHeader("Attachments")) {
            ImGui::Text("Has active bone attachments");
        }
    }

    // --- Physics ---
    if (ImGui::CollapsingHeader("Physics")) {
        ImGui::Text("Controller half-height: %.3f m", ch->getControllerHalfHeight());
        ImGui::Text("Controller half-width: %.3f m", ch->getControllerHalfWidth());
        ImGui::Text("Sitting: %s", ch->isSitting() ? "Yes" : "No");
    }
}

// ============================================================================
// Selection-driven: NPC inspector
// ============================================================================

void PropertiesPanel::renderNPCInspector(const std::string& name) {
    if (!m_npcManager) { ImGui::TextDisabled("No NPC manager"); return; }

    auto* npc = m_npcManager->getNPC(name);
    if (!npc) { ImGui::TextDisabled("NPC not found: %s", name.c_str()); return; }

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "NPC: %s", name.c_str());
    ImGui::Separator();

    // Position
    auto pos = npc->getPosition();
    ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);

    // Behavior
    if (npc->getBehavior()) {
        ImGui::Text("Behavior: %s", npc->getBehavior()->getBehaviorName().c_str());
    }

    ImGui::Text("Interact Radius: %.1f", npc->getInteractionRadius());

    // Health
    auto* health = npc->getHealthComponent();
    if (health) {
        ImGui::Separator();
        float hp = health->getHealth();
        float maxHp = health->getMaxHealth();
        float pct = health->getHealthPercent();
        ImGui::Text("Health: %.0f / %.0f", hp, maxHp);

        ImVec4 barColor;
        if (pct > 0.6f) barColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        else if (pct > 0.3f) barColor = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
        else barColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(pct, ImVec2(-1, 0), "");
        ImGui::PopStyleColor();

        if (!health->isAlive()) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "DEAD");
    }

    // Needs
    const auto& needs = npc->getNeeds();
    if (ImGui::CollapsingHeader("Needs", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& [type, need] : needs.getAllNeeds()) {
            ImGui::Text("%s: %.0f%%", AI::needTypeToString(type).c_str(), need.value);
        }
    }

    // If the NPC has an animated character, show its inspector too
    auto* animChar = npc->getAnimatedCharacter();
    if (animChar) {
        ImGui::Separator();
        renderAnimatedCharInspector(animChar);
    }
}

// ============================================================================
// Selection-driven: Placed Object inspector
// ============================================================================

void PropertiesPanel::renderPlacedObjectInspector(const std::string& id) {
    if (!m_placedObjects) { ImGui::TextDisabled("No placed object manager"); return; }

    const Core::PlacedObject* obj = m_placedObjects->get(id);
    if (!obj) { ImGui::TextDisabled("Object not found: %s", id.c_str()); return; }

    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.7f, 1.0f), "%s", obj->id.c_str());
    ImGui::Separator();

    ImGui::Text("Template: %s", obj->templateName.c_str());
    ImGui::Text("Category: %s", obj->category.c_str());
    ImGui::Text("Origin: %d, %d, %d", obj->position.x, obj->position.y, obj->position.z);
    ImGui::Text("Rotation: %d\xC2\xB0", obj->rotation);
    ImGui::Text("AABB: (%d,%d,%d) - (%d,%d,%d)",
        obj->boundingMin.x, obj->boundingMin.y, obj->boundingMin.z,
        obj->boundingMax.x, obj->boundingMax.y, obj->boundingMax.z);

    // Weight and materials (from template voxels)
    if (m_templateManager && obj->category == "template") {
        const auto* tmpl = m_templateManager->getTemplate(obj->templateName);
        if (tmpl) {
            auto& reg = Phyxel::Core::MaterialRegistry::instance();
            std::unordered_map<std::string, int> materialCounts;
            float totalWeight = 0.0f;
            int totalVoxels = 0;
            constexpr float PIECE_MASS = 0.05f;  // Same as DynamicFurnitureManager

            for (const auto& c : tmpl->cubes) {
                materialCounts[c.material]++;
                totalWeight += reg.getPhysics(c.material).mass * PIECE_MASS;
                totalVoxels++;
            }
            for (const auto& s : tmpl->subcubes) {
                materialCounts[s.material]++;
                totalWeight += reg.getPhysics(s.material).mass * PIECE_MASS;
                totalVoxels++;
            }
            for (const auto& m : tmpl->microcubes) {
                materialCounts[m.material]++;
                totalWeight += reg.getPhysics(m.material).mass * PIECE_MASS;
                totalVoxels++;
            }

            ImGui::Separator();
            ImGui::Text("Weight: %.1f kg  (%d pieces)", totalWeight, totalVoxels);

            if (ImGui::CollapsingHeader("Materials")) {
                for (const auto& [matName, count] : materialCounts) {
                    const auto& mat = reg.getPhysics(matName);
                    ImGui::BulletText("%s: %d (mass=%.1f, bond=%.2f)",
                        matName.c_str(), count, mat.mass, mat.bondStrength);
                }
            }
        }
    }

    // Dynamic furniture state & weight control
    if (m_furnitureManager) {
        ImGui::Separator();
        bool isDynamic = m_furnitureManager->isActive(id);
        if (isDynamic) {
            const auto& activeObjs = m_furnitureManager->getActiveObjects();
            auto it = activeObjs.find(id);
            if (it != activeObjs.end()) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "State: DYNAMIC");
                glm::vec3 pos = glm::vec3(it->second.currentTransform[3]);
                ImGui::Text("Position: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);

                // Editable weight slider (live updates Bullet rigid body)
                float mass = it->second.totalMass;
                if (ImGui::SliderFloat("Weight (kg)", &mass, 0.1f, 100.0f, "%.1f")) {
                    m_furnitureManager->setObjectMass(id, mass);
                }

                if (it->second.isGrabbed)
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "GRABBED");
            }
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "State: Static");

            // Weight override slider for static objects (takes effect on activation)
            auto* mutableObj = const_cast<Core::PlacedObject*>(obj);
            float massOverride = 0.0f;
            if (mutableObj->metadata.contains("mass_override")) {
                massOverride = mutableObj->metadata["mass_override"].get<float>();
            }
            if (massOverride < 0.1f) massOverride = 5.0f;  // Default minimum mass
            if (ImGui::SliderFloat("Weight (kg)", &massOverride, 0.1f, 100.0f, "%.1f")) {
                mutableObj->metadata["mass_override"] = massOverride;
            }
            ImGui::TextDisabled("(Applied when object becomes dynamic)");
        }

        // Reset button — always visible
        ImGui::Spacing();
        if (ImGui::Button("Reset to Original Position", ImVec2(-1, 0))) {
            if (isDynamic) {
                m_furnitureManager->deactivate(id, true);
            } else if (m_placedObjects && m_templateManager) {
                // Static but possibly moved — re-place voxels at original registry position
                m_templateManager->spawnTemplate(obj->templateName,
                    glm::vec3(obj->position), true, obj->rotation);
            }
        }
    }

    if (!obj->parentId.empty()) {
        ImGui::Text("Parent: %s", obj->parentId.c_str());
    }

    // Interaction points
    if (!obj->interactionPoints.empty()) {
        if (ImGui::CollapsingHeader("Interaction Points")) {
            for (size_t i = 0; i < obj->interactionPoints.size(); ++i) {
                const auto& ip = obj->interactionPoints[i];
                ImGui::Text("[%d] type=%s pos=(%.1f,%.1f,%.1f)", (int)i,
                    ip.type.c_str(), ip.worldPos.x, ip.worldPos.y, ip.worldPos.z);
            }
        }
    }

    // Metadata
    if (!obj->metadata.empty()) {
        if (ImGui::CollapsingHeader("Metadata")) {
            for (auto it = obj->metadata.begin(); it != obj->metadata.end(); ++it) {
                ImGui::Text("%s: %s", it.key().c_str(), it.value().dump().c_str());
            }
        }
    }
}

// ============================================================================
// Fallback: Voxel Section (crosshair-based)
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
        const char* level = "Cube";
        if (loc.isMicrocube) level = "Microcube";
        else if (loc.isSubcube) level = "Subcube";
        ImGui::Text("Type: %s", level);

        ImGui::Text("World: %d, %d, %d", loc.worldPos.x, loc.worldPos.y, loc.worldPos.z);
        ImGui::Text("Local: %d, %d, %d", loc.localPos.x, loc.localPos.y, loc.localPos.z);

        if (loc.isSubcube) {
            ImGui::Text("Sub: %d, %d, %d", loc.subcubePos.x, loc.subcubePos.y, loc.subcubePos.z);
        }
        if (loc.isMicrocube) {
            ImGui::Text("Sub: %d, %d, %d", loc.subcubePos.x, loc.subcubePos.y, loc.subcubePos.z);
            ImGui::Text("Micro: %d, %d, %d", loc.microcubePos.x, loc.microcubePos.y, loc.microcubePos.z);
        }

        if (loc.hitFace >= 0) {
            ImGui::Text("Face: %s", faceName(loc.hitFace));
        }

        if (loc.chunk) {
            if (loc.isMicrocube) {
                const Microcube* micro = loc.chunk->getMicrocubeAt(loc.localPos, loc.subcubePos, loc.microcubePos);
                if (micro) ImGui::Text("Material: %s", micro->getMaterialName().c_str());
            } else if (loc.isSubcube) {
                const Subcube* sub = loc.chunk->getSubcubeAt(loc.localPos, loc.subcubePos);
                if (sub) ImGui::Text("Material: %s", sub->getMaterialName().c_str());
            } else {
                const Cube* cube = loc.chunk->getCubeAt(loc.localPos);
                if (cube) ImGui::Text("Material: %s", cube->getMaterialName().c_str());
            }
        }

        ImGui::Text("Hit: %.2f, %.2f, %.2f", loc.hitPoint.x, loc.hitPoint.y, loc.hitPoint.z);

        renderPlacedObjectSection(loc.worldPos);
    }
}

// ============================================================================
// Fallback: Placed Object inline section (crosshair-based)
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
        ImGui::Text("Rotation: %d\xC2\xB0", obj->rotation);
        ImGui::Text("AABB: (%d,%d,%d) - (%d,%d,%d)",
            obj->boundingMin.x, obj->boundingMin.y, obj->boundingMin.z,
            obj->boundingMax.x, obj->boundingMax.y, obj->boundingMax.z);

        if (!obj->parentId.empty()) {
            ImGui::Text("Parent: %s", obj->parentId.c_str());
        }

        if (!obj->interactionPoints.empty()) {
            ImGui::Text("Interactions: %d", static_cast<int>(obj->interactionPoints.size()));
        }

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
// Fallback: Nearest entity section (crosshair-based)
// ============================================================================

void PropertiesPanel::renderNearestEntitySection() {
    if (!m_entityRegistry) return;

    if (!ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen)) return;

    constexpr float searchRadius = 15.0f;
    glm::vec3 searchCenter = m_camPos + m_camFront * 10.0f;
    auto nearby = m_entityRegistry->getEntitiesNear(searchCenter, searchRadius);

    if (nearby.empty()) {
        ImGui::TextDisabled("No entities nearby");
        return;
    }

    Scene::Entity* bestEntity = nullptr;
    std::string bestId;
    float bestScore = std::numeric_limits<float>::max();

    for (const auto& [id, entity] : nearby) {
        if (!entity) continue;
        glm::vec3 toEntity = entity->getPosition() - m_camPos;
        float dist = glm::length(toEntity);
        if (dist < 0.01f) continue;

        glm::vec3 dir = toEntity / dist;
        float dot = glm::dot(dir, m_camFront);
        if (dot < 0.0f) continue;

        float perpDist = dist * std::sqrt(1.0f - dot * dot);
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

    ImGui::Text("ID: %s", bestId.c_str());

    auto pos = bestEntity->getPosition();
    ImGui::Text("Position: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);

    float dist = glm::length(pos - m_camPos);
    ImGui::Text("Distance: %.1f", dist);

    auto rot = bestEntity->getRotation();
    glm::vec3 euler = glm::degrees(glm::eulerAngles(rot));
    ImGui::Text("Rotation: %.0f, %.0f, %.0f", euler.x, euler.y, euler.z);

    auto scale = bestEntity->getScale();
    if (scale != glm::vec3(1.0f)) {
        ImGui::Text("Scale: %.2f, %.2f, %.2f", scale.x, scale.y, scale.z);
    }

    auto* health = bestEntity->getHealthComponent();
    if (health) {
        float hp = health->getHealth();
        float maxHp = health->getMaxHealth();
        float pct = health->getHealthPercent();
        ImGui::Separator();
        ImGui::Text("Health: %.0f / %.0f", hp, maxHp);

        ImVec4 barColor;
        if (pct > 0.6f) barColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        else if (pct > 0.3f) barColor = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
        else barColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(pct, ImVec2(-1, 0), "");
        ImGui::PopStyleColor();

        if (!health->isAlive()) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "DEAD");
        if (health->isInvulnerable()) ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "Invulnerable");
    }

    auto* npc = dynamic_cast<Scene::NPCEntity*>(bestEntity);
    if (npc) {
        ImGui::Separator();
        ImGui::Text("NPC: %s", npc->getName().c_str());
        if (npc->getBehavior()) {
            ImGui::Text("Behavior: %s", npc->getBehavior()->getBehaviorName().c_str());
        }
        ImGui::Text("Interact Radius: %.1f", npc->getInteractionRadius());

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
