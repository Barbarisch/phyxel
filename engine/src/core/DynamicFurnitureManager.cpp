#include "core/DynamicFurnitureManager.h"
#include "core/MaterialRegistry.h"
#include "core/KinematicVoxelManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "core/GpuParticlePhysics.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"
#include "physics/Material.h"
#include "core/ChunkManager.h"
#include "utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <queue>
#include <cmath>

namespace Phyxel {
namespace Core {

// ============================================================================
// Lifecycle
// ============================================================================

DynamicFurnitureManager::~DynamicFurnitureManager() {
    clear();
}

void DynamicFurnitureManager::clear() {
    for (auto& [id, obj] : m_active) {
        if (m_kinematic && !obj.kineticObjId.empty()) {
            m_kinematic->remove(obj.kineticObjId);
        }
        cleanupPhysics(obj);
    }
    m_active.clear();
}

void DynamicFurnitureManager::deactivateAll() {
    std::vector<std::string> ids;
    ids.reserve(m_active.size());
    for (const auto& [id, _] : m_active) {
        ids.push_back(id);
    }
    for (const auto& id : ids) {
        deactivate(id, false);
    }
}

// ============================================================================
// Helpers
// ============================================================================

glm::mat4 DynamicFurnitureManager::bodyToTransform(const Physics::VoxelRigidBody* body) {
    glm::mat4 rot   = glm::mat4_cast(body->orientation);
    glm::mat4 trans = glm::translate(glm::mat4(1.0f), body->position);
    return trans * rot;
}

void DynamicFurnitureManager::cleanupPhysics(DynamicFurnitureObject& obj) {
    if (obj.rigidBody) {
        if (m_physicsWorld && m_physicsWorld->getVoxelWorld()) {
            m_physicsWorld->getVoxelWorld()->removeBody(obj.rigidBody);
        }
        obj.rigidBody = nullptr;
    }
}

// ============================================================================
// Activation — static voxels → dynamic rigid body
// ============================================================================

bool DynamicFurnitureManager::activate(const std::string& placedObjectId,
                                        const glm::vec3& impulse,
                                        const glm::vec3& contactPoint)
{
    if (!m_placedObjects || !m_templateManager || !m_kinematic || !m_physicsWorld) {
        LOG_ERROR("DynamicFurniture", "activate() called with missing dependencies");
        return false;
    }

    auto* voxelWorld = m_physicsWorld->getVoxelWorld();
    if (!voxelWorld) {
        LOG_ERROR("DynamicFurniture", "No VoxelDynamicsWorld available");
        return false;
    }

    if (m_active.count(placedObjectId)) {
        LOG_WARN_FMT("DynamicFurniture", "Object '" << placedObjectId << "' is already active");
        return false;
    }

    const PlacedObject* placed = m_placedObjects->get(placedObjectId);
    if (!placed) {
        LOG_ERROR_FMT("DynamicFurniture", "PlacedObject '" << placedObjectId << "' not found");
        return false;
    }

    if (static_cast<int>(m_active.size()) >= MAX_DYNAMIC_FURNITURE) {
        evictOldest();
    }

    // 1. Build voxel list from template
    auto voxels = buildVoxelsFromTemplate(placed->templateName);
    if (voxels.empty()) {
        LOG_WARN_FMT("DynamicFurniture", "Template '" << placed->templateName << "' yielded no voxels");
        return false;
    }

    // 2. Greedy merge to get collision boxes and center of mass
    auto mergedBoxes = mergeVoxelsGreedy(voxels);

    LOG_INFO_FMT("DynamicFurniture", "Greedy merge: " << voxels.size() << " voxels -> "
                 << mergedBoxes.size() << " boxes");

    float rawMass = 0.0f;
    glm::vec3 localCOM(0.0f);
    for (const auto& mb : mergedBoxes) {
        rawMass   += mb.mass;
        localCOM  += mb.center * mb.mass;
    }
    if (rawMass > 0.0f) localCOM /= rawMass;

    // 3. Initialise the object record
    DynamicFurnitureObject obj;
    obj.placedObjectId   = placedObjectId;
    obj.templateName     = placed->templateName;
    obj.originalPosition = placed->position;
    obj.originalRotation = placed->rotation;

    // Scale mass to reasonable furniture range
    float defaultMass = std::clamp(rawMass * 0.05f, 1.0f, 20.0f);
    obj.totalMass = defaultMass;

    if (placed->metadata.contains("mass_override")) {
        float overrideMass = placed->metadata["mass_override"].get<float>();
        if (overrideMass > 0.0f) {
            obj.totalMass = overrideMass;
        }
    }
    if (massOverride > 0.0f) {
        obj.totalMass = massOverride;
    }

    // 4. Compute initial world position from placed origin + rotated COM offset
    glm::vec3 rotatedCOM = localCOM;
    glm::quat initOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    if (placed->rotation != 0) {
        float angle = glm::radians(static_cast<float>(placed->rotation));
        initOrientation = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 rot = glm::mat4_cast(initOrientation);
        rotatedCOM = glm::vec3(rot * glm::vec4(localCOM, 1.0f));
    }
    glm::vec3 worldPos = glm::vec3(placed->position) + rotatedCOM;

    // 5. Build LocalBox list for VoxelDynamicsWorld (boxes recentered around COM)
    std::vector<Physics::LocalBox> localBoxes;
    localBoxes.reserve(mergedBoxes.size());
    for (const auto& mb : mergedBoxes) {
        Physics::LocalBox lb;
        lb.offset      = mb.center - localCOM;
        lb.halfExtents = mb.halfExtents;
        lb.mass        = mb.mass * (obj.totalMass / std::max(rawMass, 1e-6f));
        localBoxes.push_back(lb);
    }

    // 6. Remove static voxels from chunks and rebuild physics collision
    m_placedObjects->clearVoxelsOnly(placedObjectId);
    if (m_chunkManager) {
        glm::ivec3 cMin = ChunkManager::worldToChunkCoord(placed->boundingMin);
        glm::ivec3 cMax = ChunkManager::worldToChunkCoord(placed->boundingMax);
        for (int cx = cMin.x; cx <= cMax.x; ++cx) {
            for (int cy = cMin.y; cy <= cMax.y; ++cy) {
                for (int cz = cMin.z; cz <= cMax.z; ++cz) {
                    Chunk* chunk = m_chunkManager->getChunkAtCoord(glm::ivec3(cx, cy, cz));
                    if (chunk) {
                        chunk->updateChunkPhysicsBody();
                    }
                }
            }
        }
    }

    // 7. Create VoxelDynamicsWorld rigid body
    obj.rigidBody = voxelWorld->createBody(localBoxes, worldPos, initOrientation,
                                            0.2f, 0.6f, 0.4f, 0.5f);
    if (!obj.rigidBody) {
        LOG_ERROR_FMT("DynamicFurniture", "Failed to create rigid body for '" << placedObjectId << "'");
        return false;
    }
    obj.rigidBody->wake();

    // Apply initial impulse
    if (glm::dot(impulse, impulse) > 1e-6f) {
        obj.rigidBody->applyCentralImpulse(impulse);
    }

    // 8. Shift voxel positions by -COM so rendering matches the recentered body
    for (auto& v : voxels) {
        v.localPos -= localCOM;
    }

    obj.currentTransform = bodyToTransform(obj.rigidBody);

    // 9. Register with KinematicVoxelManager for rendering
    obj.kineticObjId = m_kinematic->add("dynfurn", std::move(voxels),
                                         obj.currentTransform, placedObjectId, true);

    auto* mutablePlaced = const_cast<PlacedObject*>(m_placedObjects->get(placedObjectId));
    if (mutablePlaced) {
        mutablePlaced->metadata["dynamic_furniture"] = true;
    }

    LOG_INFO_FMT("DynamicFurniture", "Activated '" << placedObjectId
                 << "' (template=" << obj.templateName
                 << ", mass=" << obj.totalMass
                 << ", boxes=" << localBoxes.size()
                 << ", COM=(" << localCOM.x << "," << localCOM.y << "," << localCOM.z << ")"
                 << ", worldPos=(" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")"
                 << ")");

    m_active[placedObjectId] = std::move(obj);
    return true;
}

// ============================================================================
// Deactivation — dynamic rigid body → static voxels
// ============================================================================

bool DynamicFurnitureManager::deactivate(const std::string& placedObjectId, bool restoreOriginal) {
    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return false;

    DynamicFurnitureObject& obj = it->second;

    if (m_kinematic && !obj.kineticObjId.empty()) {
        m_kinematic->remove(obj.kineticObjId);
    }

    glm::ivec3 placePos = obj.originalPosition;
    int placeRot = obj.originalRotation;

    if (!restoreOriginal && obj.rigidBody) {
        glm::mat4 currentXform = bodyToTransform(obj.rigidBody);
        glm::vec3 pos = glm::vec3(currentXform[3]);
        placePos = glm::ivec3(glm::floor(pos));

        float yaw = atan2(currentXform[0][2], currentXform[0][0]);
        float yawDeg = glm::degrees(yaw);
        int snapped = static_cast<int>(round(yawDeg / 90.0f)) * 90;
        placeRot = ((snapped % 360) + 360) % 360;
    }

    cleanupPhysics(obj);

    if (m_placedObjects && m_templateManager) {
        auto* mutablePlaced = const_cast<PlacedObject*>(m_placedObjects->get(placedObjectId));
        if (mutablePlaced) {
            mutablePlaced->metadata.erase("dynamic_furniture");
        }

        if (placePos != obj.originalPosition || placeRot != obj.originalRotation) {
            m_placedObjects->remove(placedObjectId);
            m_placedObjects->placeTemplate(obj.templateName, placePos, placeRot);
        } else {
            m_templateManager->spawnTemplate(obj.templateName, glm::vec3(placePos), true, placeRot);
        }
    }

    LOG_INFO_FMT("DynamicFurniture", "Deactivated '" << placedObjectId
                 << "' at (" << placePos.x << "," << placePos.y << "," << placePos.z
                 << ") rot=" << placeRot);

    m_active.erase(it);
    return true;
}

// ============================================================================
// Per-frame update
// ============================================================================

void DynamicFurnitureManager::update(float /*dt*/) {
    for (auto& [id, obj] : m_active) {
        if (!obj.rigidBody || obj.isGrabbed) continue;

        obj.currentTransform = bodyToTransform(obj.rigidBody);

        if (m_kinematic && !obj.kineticObjId.empty()) {
            m_kinematic->setTransform(obj.kineticObjId, obj.currentTransform);
        }
    }
}

// ============================================================================
// Queries
// ============================================================================

bool DynamicFurnitureManager::isActive(const std::string& placedObjectId) const {
    return m_active.count(placedObjectId) > 0;
}

void DynamicFurnitureManager::setObjectMass(const std::string& placedObjectId, float mass) {
    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return;

    auto& obj = it->second;
    obj.totalMass = mass;

    if (obj.rigidBody && mass > 0.0f) {
        obj.rigidBody->invMass = 1.0f / mass;
        obj.rigidBody->wake();
    }
}

glm::mat4 DynamicFurnitureManager::getTransform(const std::string& placedObjectId) const {
    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return glm::mat4(1.0f);
    return it->second.currentTransform;
}

// ============================================================================
// Internal helpers
// ============================================================================

std::vector<KinematicVoxel> DynamicFurnitureManager::buildVoxelsFromTemplate(
    const std::string& templateName) const
{
    const VoxelTemplate* tmpl = m_templateManager->getTemplate(templateName);
    if (!tmpl) return {};

    std::vector<KinematicVoxel> voxels;
    voxels.reserve(tmpl->cubes.size() + tmpl->subcubes.size() + tmpl->microcubes.size());

    for (const auto& cube : tmpl->cubes) {
        KinematicVoxel v;
        v.localPos     = glm::vec3(cube.relativePos) + glm::vec3(0.5f);
        v.scale        = glm::vec3(1.0f);
        v.parentFrac   = glm::vec3(0.0f);
        v.materialName = cube.material;
        voxels.push_back(v);
    }

    for (const auto& sub : tmpl->subcubes) {
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

    for (const auto& micro : tmpl->microcubes) {
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

// ============================================================================
// Greedy box merge — reduces collision child count
// ============================================================================

std::vector<DynamicFurnitureManager::MergedBox> DynamicFurnitureManager::mergeVoxelsGreedy(
    const std::vector<KinematicVoxel>& voxels)
{
    if (voxels.empty()) return {};
    auto& reg = Phyxel::Core::MaterialRegistry::instance();

    float minScale = 1.0f;
    for (const auto& v : voxels) {
        minScale = std::min(minScale, v.scale.x);
    }

    const float cellSize = minScale;
    const float invCell  = 1.0f / cellSize;

    glm::vec3 boundsMin(1e9f), boundsMax(-1e9f);
    for (const auto& v : voxels) {
        glm::vec3 vmin = v.localPos - v.scale * 0.5f;
        glm::vec3 vmax = v.localPos + v.scale * 0.5f;
        boundsMin = glm::min(boundsMin, vmin);
        boundsMax = glm::max(boundsMax, vmax);
    }

    glm::ivec3 gridSize = glm::ivec3(glm::ceil((boundsMax - boundsMin) * invCell)) + glm::ivec3(1);

    constexpr int MAX_GRID_DIM = 64;
    if (gridSize.x > MAX_GRID_DIM || gridSize.y > MAX_GRID_DIM || gridSize.z > MAX_GRID_DIM) {
        std::vector<MergedBox> result;
        result.reserve(voxels.size());
        for (const auto& v : voxels) {
            const auto& mat = reg.getPhysics(v.materialName);
            float volume = v.scale.x * v.scale.y * v.scale.z;
            result.push_back({v.localPos, v.scale * 0.5f, mat.mass * volume});
        }
        return result;
    }

    const int gx = gridSize.x, gy = gridSize.y, gz = gridSize.z;
    std::vector<uint8_t> grid(gx * gy * gz, 0);
    std::vector<float> cellMass(gx * gy * gz, 0.0f);

    auto gridIdx = [&](int x, int y, int z) -> int {
        return z + y * gz + x * (gy * gz);
    };

    for (const auto& v : voxels) {
        glm::vec3 vmin = v.localPos - v.scale * 0.5f;
        glm::vec3 vmax = v.localPos + v.scale * 0.5f;
        glm::ivec3 gmin = glm::ivec3(glm::floor((vmin - boundsMin) * invCell));
        glm::ivec3 gmax = glm::ivec3(glm::floor((vmax - boundsMin) * invCell - glm::vec3(0.001f)));
        gmin = glm::clamp(gmin, glm::ivec3(0), gridSize - glm::ivec3(1));
        gmax = glm::clamp(gmax, glm::ivec3(0), gridSize - glm::ivec3(1));

        const auto& mat = reg.getPhysics(v.materialName);
        constexpr float PIECE_MASS = 0.05f;
        int cellCount = (gmax.x - gmin.x + 1) * (gmax.y - gmin.y + 1) * (gmax.z - gmin.z + 1);
        float massPerCell = (cellCount > 0) ? (mat.mass * PIECE_MASS) / cellCount : 0.0f;

        for (int x = gmin.x; x <= gmax.x; ++x)
            for (int y = gmin.y; y <= gmax.y; ++y)
                for (int z = gmin.z; z <= gmax.z; ++z) {
                    int idx = gridIdx(x, y, z);
                    grid[idx] = 1;
                    cellMass[idx] += massPerCell;
                }
    }

    std::vector<MergedBox> result;

    for (int x = 0; x < gx; ++x) {
        for (int y = 0; y < gy; ++y) {
            for (int z = 0; z < gz; ++z) {
                if (grid[gridIdx(x, y, z)] != 1) continue;

                int zEnd = z;
                while (zEnd + 1 < gz && grid[gridIdx(x, y, zEnd + 1)] == 1) ++zEnd;

                int yEnd = y;
                bool canExpandY = true;
                while (canExpandY && yEnd + 1 < gy) {
                    for (int zz = z; zz <= zEnd; ++zz) {
                        if (grid[gridIdx(x, yEnd + 1, zz)] != 1) { canExpandY = false; break; }
                    }
                    if (canExpandY) ++yEnd;
                }

                int xEnd = x;
                bool canExpandX = true;
                while (canExpandX && xEnd + 1 < gx) {
                    for (int yy = y; yy <= yEnd; ++yy) {
                        for (int zz = z; zz <= zEnd; ++zz) {
                            if (grid[gridIdx(xEnd + 1, yy, zz)] != 1) { canExpandX = false; break; }
                        }
                        if (!canExpandX) break;
                    }
                    if (canExpandX) ++xEnd;
                }

                float boxMass = 0.0f;
                for (int xx = x; xx <= xEnd; ++xx)
                    for (int yy = y; yy <= yEnd; ++yy)
                        for (int zz = z; zz <= zEnd; ++zz) {
                            int idx = gridIdx(xx, yy, zz);
                            grid[idx] = 2;
                            boxMass += cellMass[idx];
                        }

                glm::vec3 boxMin = boundsMin + glm::vec3(x, y, z) * cellSize;
                glm::vec3 boxMax = boundsMin + glm::vec3(xEnd + 1, yEnd + 1, zEnd + 1) * cellSize;
                glm::vec3 center = (boxMin + boxMax) * 0.5f;
                glm::vec3 halfExtents = (boxMax - boxMin) * 0.5f;

                result.push_back({center, halfExtents, boxMass});
            }
        }
    }

    return result;
}

void DynamicFurnitureManager::evictOldest() {
    std::string bestId;
    float bestSleep = -1.0f;

    for (const auto& [id, obj] : m_active) {
        if (obj.sleepTimer > bestSleep) {
            bestSleep = obj.sleepTimer;
            bestId = id;
        }
    }

    if (!bestId.empty()) {
        LOG_INFO_FMT("DynamicFurniture", "Evicting '" << bestId << "' (sleepTimer=" << bestSleep << ")");
        deactivate(bestId, false);
    }
}

// ============================================================================
// Fracture — connected component splitting
// ============================================================================

std::vector<std::vector<KinematicVoxel>> DynamicFurnitureManager::findConnectedComponents(
    const std::vector<KinematicVoxel>& voxels,
    const glm::vec3& contactPoint,
    float fractureRadius)
{
    if (voxels.empty()) return {};

    const size_t n = voxels.size();

    std::vector<bool> severed(n, false);
    for (size_t i = 0; i < n; ++i) {
        float dist = glm::length(voxels[i].localPos - contactPoint);
        if (dist <= fractureRadius) {
            severed[i] = true;
        }
    }

    constexpr float ADJ_EPSILON = 0.05f;

    float maxScale = 0.0f;
    for (const auto& v : voxels) maxScale = std::max(maxScale, v.scale.x);
    const float cellSize = maxScale + ADJ_EPSILON;
    const float invCell = 1.0f / cellSize;

    auto hashPos = [&](const glm::vec3& p) -> uint64_t {
        int x = static_cast<int>(std::floor(p.x * invCell));
        int y = static_cast<int>(std::floor(p.y * invCell));
        int z = static_cast<int>(std::floor(p.z * invCell));
        return (uint64_t(uint32_t(x)) << 40) ^ (uint64_t(uint32_t(y)) << 20) ^ uint64_t(uint32_t(z));
    };

    std::unordered_map<uint64_t, std::vector<size_t>> spatialMap;
    for (size_t i = 0; i < n; ++i) {
        if (!severed[i]) {
            spatialMap[hashPos(voxels[i].localPos)].push_back(i);
        }
    }

    std::vector<int> component(n, -1);
    int numComponents = 0;

    for (size_t i = 0; i < n; ++i) {
        if (severed[i] || component[i] >= 0) continue;

        std::queue<size_t> q;
        q.push(i);
        component[i] = numComponents;

        while (!q.empty()) {
            size_t cur = q.front();
            q.pop();

            const auto& pos = voxels[cur].localPos;
            float touchDist = voxels[cur].scale.x * 0.5f + maxScale * 0.5f + ADJ_EPSILON;
            (void)touchDist;

            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        glm::vec3 probe = pos + glm::vec3(dx, dy, dz) * cellSize;
                        auto it = spatialMap.find(hashPos(probe));
                        if (it == spatialMap.end()) continue;

                        for (size_t j : it->second) {
                            if (component[j] >= 0) continue;
                            float dist = glm::length(voxels[j].localPos - pos);
                            float adjDist = (voxels[cur].scale.x + voxels[j].scale.x) * 0.5f + ADJ_EPSILON;
                            if (dist <= adjDist) {
                                component[j] = numComponents;
                                q.push(j);
                            }
                        }
                    }
                }
            }
        }
        ++numComponents;
    }

    std::vector<std::vector<KinematicVoxel>> result(numComponents);
    for (size_t i = 0; i < n; ++i) {
        if (component[i] >= 0) {
            result[component[i]].push_back(voxels[i]);
        }
    }

    for (size_t i = 0; i < n; ++i) {
        if (severed[i]) {
            result.push_back({voxels[i]});
        }
    }

    return result;
}

int DynamicFurnitureManager::shatter(const std::string& placedObjectId,
                                      float contactForce,
                                      const glm::vec3& contactPoint)
{
    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return 0;

    auto& obj = it->second;

    auto& reg = Phyxel::Core::MaterialRegistry::instance();
    const auto& kinObjects = m_kinematic->getObjects();
    auto kinIt = kinObjects.find(obj.kineticObjId);
    if (kinIt == kinObjects.end()) return 0;

    const auto& voxels = kinIt->second.voxels;
    if (voxels.size() < 2) return 0;

    float avgBondStrength = 0.0f;
    for (const auto& v : voxels) {
        avgBondStrength += reg.getPhysics(v.materialName).bondStrength;
    }
    avgBondStrength /= static_cast<float>(voxels.size());

    if (contactForce < avgBondStrength * BREAK_FORCE_SCALAR) {
        return 0;
    }

    LOG_INFO_FMT("DynamicFurniture", "Shattering '" << placedObjectId
                 << "': force=" << contactForce << ", bondStrength=" << avgBondStrength
                 << ", threshold=" << avgBondStrength * BREAK_FORCE_SCALAR);

    float fractureRadius = 0.5f + (contactForce / (avgBondStrength * BREAK_FORCE_SCALAR)) * 0.5f;

    glm::mat4 invTransform = glm::inverse(obj.currentTransform);
    glm::vec3 localContact = glm::vec3(invTransform * glm::vec4(contactPoint, 1.0f));

    auto fragments = findConnectedComponents(voxels, localContact, fractureRadius);

    if (fragments.size() <= 1) {
        LOG_DEBUG("DynamicFurniture", "Shatter produced %zu fragments, not enough to split",
                  fragments.size());
        return 0;
    }

    // Inherit velocity from the current rigid body
    glm::vec3 linearVel(0.0f), angularVel(0.0f);
    if (obj.rigidBody) {
        linearVel  = obj.rigidBody->linearVelocity;
        angularVel = obj.rigidBody->angularVelocity;
    }

    cleanupPhysics(obj);
    m_kinematic->remove(obj.kineticObjId);

    std::string origId = placedObjectId;
    glm::mat4 origTransform = obj.currentTransform;
    m_active.erase(it);

    auto* voxelWorld = m_physicsWorld ? m_physicsWorld->getVoxelWorld() : nullptr;
    int fragmentCount = 0;

    for (size_t fi = 0; fi < fragments.size(); ++fi) {
        auto& fragVoxels = fragments[fi];
        if (fragVoxels.empty()) continue;

        if (static_cast<int>(fragVoxels.size()) >= MIN_FRAGMENT_VOXELS && voxelWorld) {
            if (m_active.size() >= MAX_DYNAMIC_FURNITURE) {
                evictOldest();
            }

            std::string fragId = origId + "_frag" + std::to_string(fi);

            // Compute fragment COM and merged boxes
            auto fragBoxes = mergeVoxelsGreedy(fragVoxels);
            float fragRawMass = 0.0f;
            glm::vec3 fragCOM(0.0f);
            for (const auto& mb : fragBoxes) {
                fragRawMass += mb.mass;
                fragCOM     += mb.center * mb.mass;
            }
            if (fragRawMass > 0.0f) fragCOM /= fragRawMass;

            float fragMass = std::clamp(fragRawMass * 0.05f, 0.5f, 10.0f);

            std::vector<Physics::LocalBox> localBoxes;
            localBoxes.reserve(fragBoxes.size());
            for (const auto& mb : fragBoxes) {
                Physics::LocalBox lb;
                lb.offset      = mb.center - fragCOM;
                lb.halfExtents = mb.halfExtents;
                lb.mass        = mb.mass * (fragMass / std::max(fragRawMass, 1e-6f));
                localBoxes.push_back(lb);
            }

            glm::mat4 fragTransform = glm::translate(origTransform, fragCOM);
            glm::vec3 fragWorldPos  = glm::vec3(fragTransform[3]);
            glm::quat fragOrient    = glm::quat_cast(origTransform);

            DynamicFurnitureObject fragObj;
            fragObj.placedObjectId = fragId;
            fragObj.templateName   = "";
            fragObj.totalMass      = fragMass;

            fragObj.rigidBody = voxelWorld->createBody(localBoxes, fragWorldPos, fragOrient,
                                                        0.2f, 0.6f, 0.4f, 0.5f);
            if (!fragObj.rigidBody) continue;

            // Inherit velocity + scatter
            glm::vec3 fragCenter(0.0f);
            for (const auto& v : fragVoxels) fragCenter += v.localPos;
            fragCenter /= static_cast<float>(fragVoxels.size());
            glm::vec3 scatter = glm::normalize(fragCenter - localContact) * 2.0f;

            fragObj.rigidBody->linearVelocity  = linearVel + scatter + glm::vec3(0.0f, 1.0f, 0.0f);
            fragObj.rigidBody->angularVelocity = angularVel;
            fragObj.rigidBody->wake();

            fragObj.currentTransform = fragTransform;

            for (auto& v : fragVoxels) {
                v.localPos -= fragCOM;
            }

            fragObj.kineticObjId = m_kinematic->add(fragId, std::move(fragVoxels),
                                                     fragTransform, fragId, true);

            m_active[fragId] = std::move(fragObj);
            ++fragmentCount;

        } else if (m_gpuParticles) {
            for (const auto& v : fragVoxels) {
                glm::vec3 worldPos = glm::vec3(origTransform * glm::vec4(v.localPos, 1.0f));
                glm::vec3 scatter = glm::normalize(v.localPos - localContact) * 3.0f;

                Phyxel::GpuParticlePhysics::SpawnParams sp;
                sp.position    = worldPos;
                sp.velocity    = linearVel + scatter + glm::vec3(0.0f, 2.0f, 0.0f);
                sp.scale       = v.scale;
                sp.materialName = v.materialName;
                sp.lifetime    = 5.0f;

                m_gpuParticles->queueSpawn(sp);
            }
            ++fragmentCount;
        }
    }

    LOG_INFO_FMT("DynamicFurniture", "Shatter complete: " << voxels.size() << " voxels -> "
                 << fragments.size() << " fragments -> " << fragmentCount << " created");

    return fragmentCount;
}

// ============================================================================
// Grab system — pick up and carry furniture
// ============================================================================

bool DynamicFurnitureManager::grab(const std::string& placedObjectId) {
    if (isGrabbing()) {
        LOG_WARN("DynamicFurniture", "Already grabbing '%s', release first", m_grabbedObjectId.c_str());
        return false;
    }

    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return false;

    auto& obj = it->second;
    if (!obj.rigidBody) return false;

    // Make kinematic: infinite mass so collisions cannot move it
    obj.rigidBody->invMass            = 0.0f;
    obj.rigidBody->linearVelocity     = glm::vec3(0.0f);
    obj.rigidBody->angularVelocity    = glm::vec3(0.0f);

    obj.isGrabbed  = true;
    obj.sleepTimer = 0.0f;
    m_grabbedObjectId = placedObjectId;

    LOG_INFO_FMT("DynamicFurniture", "Grabbed '" << placedObjectId << "'");
    return true;
}

void DynamicFurnitureManager::releaseGrab() {
    if (m_grabbedObjectId.empty()) return;

    auto it = m_active.find(m_grabbedObjectId);
    if (it != m_active.end()) {
        auto& obj = it->second;
        if (obj.rigidBody && obj.totalMass > 0.0f) {
            obj.rigidBody->invMass = 1.0f / obj.totalMass;
            obj.rigidBody->wake();
        }
        obj.isGrabbed = false;
        LOG_INFO_FMT("DynamicFurniture", "Released '" << m_grabbedObjectId << "'");
    }

    m_grabbedObjectId.clear();
}

void DynamicFurnitureManager::throwGrab(const glm::vec3& impulse) {
    if (m_grabbedObjectId.empty()) return;

    auto it = m_active.find(m_grabbedObjectId);
    if (it != m_active.end()) {
        auto& obj = it->second;
        if (obj.rigidBody) {
            if (obj.totalMass > 0.0f) {
                obj.rigidBody->invMass = 1.0f / obj.totalMass;
            }
            obj.rigidBody->applyCentralImpulse(impulse);
            obj.rigidBody->wake();
        }
        obj.isGrabbed = false;
        LOG_INFO_FMT("DynamicFurniture", "Threw '" << m_grabbedObjectId
                     << "' with impulse (" << impulse.x << ", " << impulse.y << ", " << impulse.z << ")");
    }

    m_grabbedObjectId.clear();
}

void DynamicFurnitureManager::updateGrabPosition(const glm::vec3& cameraPos,
                                                   const glm::vec3& cameraFront) {
    if (m_grabbedObjectId.empty()) return;

    auto it = m_active.find(m_grabbedObjectId);
    if (it == m_active.end()) {
        m_grabbedObjectId.clear();
        return;
    }

    auto& obj = it->second;
    if (!obj.rigidBody) return;

    glm::vec3 targetPos = cameraPos + cameraFront * GRAB_HOLD_DISTANCE;

    // Directly set position; zero velocity to prevent drift
    obj.rigidBody->position        = targetPos;
    obj.rigidBody->linearVelocity  = glm::vec3(0.0f);
    obj.rigidBody->angularVelocity = glm::vec3(0.0f);

    obj.currentTransform = glm::translate(glm::mat4(1.0f), targetPos);

    if (m_kinematic) {
        m_kinematic->setTransform(obj.kineticObjId, obj.currentTransform);
    }
}

void DynamicFurnitureManager::checkPlayerFurnitureCollision(
    const glm::vec3& playerPos,
    const glm::vec3& playerVelocity,
    bool isSprinting)
{
    if (!isSprinting || !m_placedObjects) return;

    float speed = glm::length(playerVelocity);
    if (speed < 1.0f) return;

    const float PLAYER_RADIUS = 0.8f;
    for (const auto& [id, obj] : m_placedObjects->getAllObjects()) {
        if (isActive(id)) continue;
        if (obj.category != "template") continue;

        glm::vec3 bmin(obj.boundingMin);
        glm::vec3 bmax(obj.boundingMax);
        bmin -= glm::vec3(PLAYER_RADIUS);
        bmax += glm::vec3(PLAYER_RADIUS);

        if (playerPos.x >= bmin.x && playerPos.x <= bmax.x &&
            playerPos.y >= bmin.y && playerPos.y <= bmax.y &&
            playerPos.z >= bmin.z && playerPos.z <= bmax.z)
        {
            glm::vec3 pushDir   = glm::normalize(playerVelocity);
            float     pushForce = speed * 2.0f;
            glm::vec3 impulse   = pushDir * pushForce;

            LOG_INFO_FMT("DynamicFurniture", "Sprint collision with '" << id
                         << "' — activating with push (" << pushForce << ")");
            activate(id, impulse, playerPos);
            return;
        }
    }
}

} // namespace Core
} // namespace Phyxel
