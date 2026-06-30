#include "core/ItemPropManager.h"

#include "core/ItemEffectSystem.h"
#include "core/ItemRegistry.h"
#include "core/KinematicVoxelManager.h"
#include "core/MaterialRegistry.h"
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ChunkManager.h"
#include "utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <limits>

namespace fs = std::filesystem;

namespace Phyxel {
namespace Core {

namespace {
// Resolve a template's authored # surface: into the render-side descriptor:
// the surface texture name → atlas layer (via the material texture cache), and
// the axis char → 0/1/2. Inactive when the template has no surface texture.
KinematicSurface surfaceFromTemplate(const VoxelTemplate& tmpl) {
    KinematicSurface surf;
    if (!tmpl.surface.active()) return surf;
    int axis = (tmpl.surface.axis == 'x') ? 0 : (tmpl.surface.axis == 'z') ? 2 : 1;
    // Sample the surface material on its dominant (positive-axis) face. A surface
    // material carries one albedo, so any face index resolves to the same layer.
    static const int axisPosFace[3] = {2 /*+X*/, 4 /*+Y*/, 0 /*+Z*/};
    surf.active = true;
    surf.axis = static_cast<uint8_t>(axis);
    surf.textureIndex = MaterialRegistry::instance().getTextureIndex(tmpl.surface.texture,
                                                                     axisPosFace[axis]);
    return surf;
}
}  // namespace

std::vector<KinematicVoxel> ItemPropManager::voxelsFromTemplate(const VoxelTemplate& tmpl) {
    std::vector<KinematicVoxel> voxels;
    voxels.reserve(tmpl.cubes.size() + tmpl.subcubes.size() + tmpl.microcubes.size());

    for (const auto& cube : tmpl.cubes) {
        KinematicVoxel v;
        v.localPos     = glm::vec3(cube.relativePos) + glm::vec3(0.5f);
        v.scale        = glm::vec3(1.0f);
        v.parentFrac   = glm::vec3(0.0f);
        v.materialName = cube.material;
        voxels.push_back(v);
    }
    for (const auto& sub : tmpl.subcubes) {
        KinematicVoxel v;
        constexpr float subScale = 1.0f / 3.0f;
        v.localPos = glm::vec3(sub.parentRelativePos)
                   + glm::vec3(sub.subcubePos) * subScale
                   + glm::vec3(subScale * 0.5f);
        v.scale        = glm::vec3(subScale);
        v.parentFrac   = glm::vec3(sub.subcubePos) * subScale;
        v.materialName = sub.material;
        voxels.push_back(v);
    }
    for (const auto& micro : tmpl.microcubes) {
        KinematicVoxel v;
        constexpr float subScale   = 1.0f / 3.0f;
        constexpr float microScale = 1.0f / 9.0f;
        v.localPos = glm::vec3(micro.parentRelativePos)
                   + glm::vec3(micro.subcubePos) * subScale
                   + glm::vec3(micro.microcubePos) * microScale
                   + glm::vec3(microScale * 0.5f);
        v.scale        = glm::vec3(microScale);
        v.parentFrac   = glm::vec3(micro.subcubePos) * subScale
                       + glm::vec3(micro.microcubePos) * microScale;
        v.materialName = micro.material;
        voxels.push_back(v);
    }
    return voxels;
}

const VoxelTemplate* ItemPropManager::resolveItemTemplate(const std::string& templateFile) const {
    if (!m_templates || templateFile.empty()) return nullptr;

    const std::string stem = fs::path(templateFile).stem().string();
    if (const auto* t = m_templates->getTemplate(stem)) return t;

    // On-demand load: the startup scan only covers resources/templates/ root.
    std::string path = "resources/templates/" + templateFile;
    if (fs::path(path).extension().empty()) path += ".voxel";
    if (fs::exists(path) && m_templates->loadTemplate(path))
        return m_templates->getTemplate(stem);

    LOG_WARN("ItemPropManager", "Template '{}' not found (tried '{}')", templateFile, path);
    return nullptr;
}

std::string ItemPropManager::spawnProp(const std::string& itemId, const glm::vec3& position,
                                       float yawDeg, bool snapToGround) {
    if (!m_placed || !m_kinematic) return "";

    const auto* def = ItemRegistry::instance().getItem(itemId);
    if (!def) {
        LOG_WARN("ItemPropManager", "spawnProp: unknown item '{}'", itemId);
        return "";
    }
    if (!def->holdable || def->templateFile.empty()) {
        LOG_WARN("ItemPropManager", "spawnProp: item '{}' is not holdable / has no template", itemId);
        return "";
    }
    const VoxelTemplate* tmpl = resolveItemTemplate(def->templateFile);
    if (!tmpl) return "";

    auto voxels = voxelsFromTemplate(*tmpl);
    if (voxels.empty()) {
        LOG_WARN("ItemPropManager", "spawnProp: template '{}' has no voxels", def->templateFile);
        return "";
    }

    // World props use the item's held scale: item templates are authored at
    // full-cube resolution, so unscaled props tower over the character.
    const float propScale = def->held.scale > 0.0f ? def->held.scale : 1.0f;

    // Local-space bounds of the voxel set (scaled).
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const auto& v : voxels) {
        lo = glm::min(lo, (v.localPos - v.scale * 0.5f) * propScale);
        hi = glm::max(hi, (v.localPos + v.scale * 0.5f) * propScale);
    }

    glm::vec3 pos = position;
    if (snapToGround && m_chunks) {
        int x = (int)std::floor(pos.x), z = (int)std::floor(pos.z);
        int y = (int)std::floor(pos.y);
        for (int i = 0; i < 32; ++i, --y) {
            if (m_chunks->hasVoxelAt(glm::ivec3(x, y, z))) { pos.y = float(y + 1); break; }
        }
    }
    // Rest the template's bottom on the ground plane.
    pos.y -= lo.y;

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
    transform = glm::rotate(transform, glm::radians(yawDeg), glm::vec3(0, 1, 0));
    transform = glm::scale(transform, glm::vec3(propScale));

    // Conservative world AABB: transform the 8 local corners.
    glm::vec3 wlo(std::numeric_limits<float>::max()), whi(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
        glm::vec3 w = glm::vec3(transform * glm::vec4(c, 1.0f));
        wlo = glm::min(wlo, w);
        whi = glm::max(whi, w);
    }

    std::string kinId = m_kinematic->add("itemprop_" + itemId, std::move(voxels), transform,
                                         "", false, surfaceFromTemplate(*tmpl));

    std::string placedId = m_placed->registerItemProp(
        itemId, tmpl->name,
        glm::ivec3(glm::floor(pos)), (int)yawDeg,
        glm::ivec3(glm::floor(wlo)), glm::ivec3(glm::ceil(whi)),
        def->name);
    if (placedId.empty()) {
        m_kinematic->remove(kinId);
        return "";
    }

    m_props[placedId] = {placedId, itemId, kinId};
    registerPropEffects(m_props[placedId]);
    LOG_INFO("ItemPropManager", "Spawned item prop '{}' as '{}' at ({}, {}, {})",
             itemId, placedId, pos.x, pos.y, pos.z);
    return placedId;
}

void ItemPropManager::registerPropEffects(const Prop& prop) {
    if (!m_effects || !m_kinematic) return;
    const auto* def = ItemRegistry::instance().getItem(prop.itemId);
    if (!def || def->effects.empty()) return;
    KinematicVoxelManager* kin = m_kinematic;
    const std::string kinId = prop.kinId;
    m_effects->registerInstance(prop.placedObjectId, def, /*held=*/false,
                                [kin, kinId]() { return kin->getTransform(kinId); });
}

void ItemPropManager::onPlacedObjectRemoved(const std::string& placedObjectId) {
    auto it = m_props.find(placedObjectId);
    if (it == m_props.end()) return;
    if (m_effects) m_effects->unregisterInstance(placedObjectId);
    if (m_kinematic) m_kinematic->remove(it->second.kinId);
    m_props.erase(it);
}

bool ItemPropManager::removeProp(const std::string& placedObjectId) {
    if (!m_props.count(placedObjectId)) return false;
    onPlacedObjectRemoved(placedObjectId);          // kinematic teardown (idempotent)
    if (m_placed) m_placed->remove(placedObjectId); // registry entry (no voxel clear for items)
    return true;
}

std::string ItemPropManager::pickupProp(const std::string& placedObjectId) {
    auto it = m_props.find(placedObjectId);
    if (it == m_props.end()) return "";
    std::string itemId = it->second.itemId;
    removeProp(placedObjectId);
    return itemId;
}

void ItemPropManager::rebuildFromPlacedObjects() {
    if (!m_placed || !m_kinematic) return;

    // Prune props whose placed object vanished (e.g. snapshot restore).
    for (auto it = m_props.begin(); it != m_props.end();) {
        if (!m_placed->get(it->first)) {
            m_kinematic->remove(it->second.kinId);
            it = m_props.erase(it);
        } else {
            ++it;
        }
    }

    int rebuilt = 0;
    for (const auto& obj : m_placed->list()) {
        if (obj.category != "item" || m_props.count(obj.id)) continue;
        std::string itemId = obj.metadata.value("itemId", std::string());
        const auto* def = itemId.empty() ? nullptr : ItemRegistry::instance().getItem(itemId);
        const VoxelTemplate* tmpl = def ? resolveItemTemplate(def->templateFile) : nullptr;
        if (!tmpl) {
            LOG_WARN("ItemPropManager", "Cannot rebuild item prop '{}' (item '{}')", obj.id, itemId);
            continue;
        }
        auto voxels = voxelsFromTemplate(*tmpl);

        const float propScale = def->held.scale > 0.0f ? def->held.scale : 1.0f;
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(obj.position));
        transform = glm::rotate(transform, glm::radians((float)obj.rotation), glm::vec3(0, 1, 0));
        transform = glm::scale(transform, glm::vec3(propScale));

        std::string kinId = m_kinematic->add("itemprop_" + itemId, std::move(voxels), transform,
                                             "", false, surfaceFromTemplate(*tmpl));
        m_props[obj.id] = {obj.id, itemId, kinId};
        registerPropEffects(m_props[obj.id]);
        ++rebuilt;
    }
    if (rebuilt > 0)
        LOG_INFO("ItemPropManager", "Rebuilt {} item props from placed objects", rebuilt);
}

} // namespace Core
} // namespace Phyxel
