#include "core/DynamicFurnitureManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "core/GpuParticlePhysics.h"
#include "physics/PhysicsWorld.h"
#include "physics/Material.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"
#include "core/ChunkManager.h"
#include "utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <limits>
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
        if (m_kinematic && !obj.kineticObjId.empty())
            m_kinematic->remove(obj.kineticObjId);
        cleanupVoxelBody(obj);
    }
    m_active.clear();
}

void DynamicFurnitureManager::deactivateAll() {
    std::vector<std::string> ids;
    ids.reserve(m_active.size());
    for (const auto& [id, _] : m_active) ids.push_back(id);
    for (const auto& id : ids) deactivate(id, false);
}

// ============================================================================
// Activation — static voxels → VoxelRigidBody
// ============================================================================

bool DynamicFurnitureManager::activate(const std::string& placedObjectId,
                                        const glm::vec3& impulse,
                                        const glm::vec3& /*contactPoint*/)
{
    if (!m_placedObjects || !m_templateManager || !m_kinematic || !m_physicsWorld) {
        LOG_ERROR("DynamicFurniture", "activate() called with missing dependencies");
        return false;
    }

    auto* vw = m_physicsWorld->getVoxelWorld();
    if (!vw) {
        LOG_ERROR("DynamicFurniture", "activate() — VoxelDynamicsWorld not available");
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

    if (static_cast<int>(m_active.size()) >= MAX_DYNAMIC_FURNITURE)
        evictOldest();

    // 1. Build voxel list from template
    auto voxels = buildVoxelsFromTemplate(placed->templateName);
    if (voxels.empty()) {
        LOG_WARN_FMT("DynamicFurniture", "Template '" << placed->templateName << "' yielded no voxels");
        return false;
    }

    // 2. Greedy merge voxels into larger boxes for collision efficiency
    Physics::MaterialManager matMgr;
    auto mergedBoxes = mergeVoxelsGreedy(voxels, matMgr);

    LOG_INFO_FMT("DynamicFurniture", "Greedy merge: " << voxels.size() << " voxels -> "
                 << mergedBoxes.size() << " boxes");

    // 3. Compute raw total mass and center of mass from merged boxes
    float rawMass = 0.0f;
    glm::vec3 localCOM(0.0f);
    for (const auto& mb : mergedBoxes) {
        rawMass  += mb.mass;
        localCOM += mb.center * mb.mass;
    }
    if (rawMass > 0.0f) localCOM /= rawMass;

    // Scale to realistic furniture range (1–20 kg)
    float desiredMass = std::clamp(rawMass * 0.05f, 1.0f, 20.0f);
    desiredMass = std::max(desiredMass, 5.0f);
    if (massOverride > 0.0f) desiredMass = massOverride;
    if (placed->metadata.contains("mass_override")) {
        float ov = placed->metadata["mass_override"].get<float>();
        if (ov > 0.0f) desiredMass = ov;
    }
    float massScale = (rawMass > 0.0f) ? desiredMass / rawMass : 1.0f;

    // 4. Build LocalBox list centered around COM, with scaled masses
    std::vector<Physics::LocalBox> localBoxes;
    localBoxes.reserve(mergedBoxes.size());
    for (const auto& mb : mergedBoxes) {
        Physics::LocalBox lb;
        lb.offset      = mb.center - localCOM;
        lb.halfExtents = mb.halfExtents;
        lb.mass        = mb.mass * massScale;
        localBoxes.push_back(lb);
    }

    // 5. Compute initial world position of COM
    glm::vec3 rotatedCOM = localCOM;
    glm::quat orientation(1, 0, 0, 0);
    if (placed->rotation != 0) {
        float angle = glm::radians(static_cast<float>(placed->rotation));
        glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
        rotatedCOM  = glm::vec3(rotMat * glm::vec4(localCOM, 1.0f));
        orientation = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    glm::vec3 worldCOMPos = glm::vec3(placed->position) + rotatedCOM;

    // 6. Remove static voxels from chunks and rebuild chunk collision
    m_placedObjects->clearVoxelsOnly(placedObjectId);
    if (m_chunkManager) {
        glm::ivec3 cMin = ChunkManager::worldToChunkCoord(placed->boundingMin);
        glm::ivec3 cMax = ChunkManager::worldToChunkCoord(placed->boundingMax);
        for (int cx = cMin.x; cx <= cMax.x; ++cx)
            for (int cy = cMin.y; cy <= cMax.y; ++cy)
                for (int cz = cMin.z; cz <= cMax.z; ++cz) {
                    Chunk* chunk = m_chunkManager->getChunkAtCoord(glm::ivec3(cx, cy, cz));
                    if (chunk) chunk->forcePhysicsRebuild();
                }
    }

    // 7. Create VoxelRigidBody
    DynamicFurnitureObject obj;
    obj.placedObjectId   = placedObjectId;
    obj.templateName     = placed->templateName;
    obj.originalPosition = placed->position;
    obj.originalRotation = placed->rotation;
    obj.totalMass        = desiredMass;

    obj.voxelBody = vw->createBody(localBoxes, worldCOMPos, orientation,
                                   /*restitution=*/0.2f, /*friction=*/0.6f,
                                   /*linDamp=*/0.4f, /*angDamp=*/0.5f);

    // 8. Apply initial impulse
    if (glm::length2(impulse) > 0.0f)
        obj.voxelBody->applyCentralImpulse(impulse);

    // Furniture bodies must never expire — disable the lifetime countdown so
    // cleanupDead() doesn't mark them isDead after 30 s, which would make them pass-through.
    obj.voxelBody->lifetime = std::numeric_limits<float>::max();

    // Seed prevLinearVelocity so the first checkFractureContacts call sees zero delta-v
    // rather than the full activation impulse (which would falsely trigger fracture).
    obj.prevLinearVelocity = obj.voxelBody->linearVelocity;

    // 9. Build initial world transform for rendering
    obj.currentTransform = glm::translate(glm::mat4(1.0f), worldCOMPos)
                         * glm::mat4_cast(orientation);

    // 10. Shift voxel positions by -COM for rendering alignment
    for (auto& v : voxels) v.localPos -= localCOM;

    // 11. Register with KinematicVoxelManager
    obj.kineticObjId = m_kinematic->add("dynfurn", std::move(voxels), obj.currentTransform,
                                        placedObjectId, true);

    auto* mutablePlaced = const_cast<PlacedObject*>(m_placedObjects->get(placedObjectId));
    if (mutablePlaced) mutablePlaced->metadata["dynamic_furniture"] = true;

    LOG_INFO_FMT("DynamicFurniture", "Activated '" << placedObjectId
                 << "' (template=" << obj.templateName
                 << ", mass=" << obj.totalMass
                 << ", boxes=" << localBoxes.size()
                 << ", worldPos=(" << worldCOMPos.x << "," << worldCOMPos.y << "," << worldCOMPos.z << "))");

    m_active[placedObjectId] = std::move(obj);
    return true;
}

// ============================================================================
// Deactivation — VoxelRigidBody → static voxels
// ============================================================================

bool DynamicFurnitureManager::deactivate(const std::string& placedObjectId, bool restoreOriginal) {
    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return false;

    DynamicFurnitureObject& obj = it->second;

    if (m_kinematic && !obj.kineticObjId.empty())
        m_kinematic->remove(obj.kineticObjId);

    glm::ivec3 placePos = obj.originalPosition;
    int placeRot = obj.originalRotation;

    if (!restoreOriginal && obj.voxelBody) {
        glm::vec3 pos = obj.voxelBody->position;
        placePos = glm::ivec3(glm::floor(pos));

        // Quantize Y-axis rotation to nearest 90 degrees
        glm::mat3 R = glm::toMat3(obj.voxelBody->orientation);
        float yaw = atan2(R[2][0], R[0][0]);
        int snapped = static_cast<int>(round(glm::degrees(yaw) / 90.0f)) * 90;
        placeRot = ((snapped % 360) + 360) % 360;
    }

    cleanupVoxelBody(obj);

    if (m_placedObjects && m_templateManager) {
        auto* mutablePlaced = const_cast<PlacedObject*>(m_placedObjects->get(placedObjectId));
        if (mutablePlaced) mutablePlaced->metadata.erase("dynamic_furniture");

        if (placePos != obj.originalPosition || placeRot != obj.originalRotation) {
            m_placedObjects->remove(placedObjectId);
            m_placedObjects->placeTemplate(obj.templateName, placePos, placeRot);
        } else {
            m_templateManager->spawnTemplate(obj.templateName, glm::vec3(placePos), true, placeRot);
        }

        // Rebuild occupancy grid so the re-staticized voxels block character movement again.
        if (m_chunkManager) {
            const VoxelTemplate* tmpl = m_templateManager->getTemplate(obj.templateName);
            glm::ivec3 cMin = ChunkManager::worldToChunkCoord(placePos);
            glm::ivec3 cMax = cMin;
            if (tmpl) {
                for (const auto& c : tmpl->cubes) {
                    glm::ivec3 cc = ChunkManager::worldToChunkCoord(placePos + c.relativePos);
                    cMin = glm::min(cMin, cc);
                    cMax = glm::max(cMax, cc);
                }
                for (const auto& s : tmpl->subcubes) {
                    glm::ivec3 cc = ChunkManager::worldToChunkCoord(placePos + s.parentRelativePos);
                    cMin = glm::min(cMin, cc);
                    cMax = glm::max(cMax, cc);
                }
            }
            for (int cx = cMin.x; cx <= cMax.x; ++cx)
                for (int cy = cMin.y; cy <= cMax.y; ++cy)
                    for (int cz = cMin.z; cz <= cMax.z; ++cz) {
                        Chunk* chunk = m_chunkManager->getChunkAtCoord(glm::ivec3(cx, cy, cz));
                        if (chunk) chunk->forcePhysicsRebuild();
                    }
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

void DynamicFurnitureManager::update(float dt) {
    checkFractureContacts(dt);

    for (auto& [id, obj] : m_active) {
        if (!obj.voxelBody || obj.isGrabbed) continue;

        // Update rendering transform from physics state
        obj.currentTransform = glm::translate(glm::mat4(1.0f), obj.voxelBody->position)
                             * glm::mat4_cast(obj.voxelBody->orientation);

        if (m_kinematic && !obj.kineticObjId.empty())
            m_kinematic->setTransform(obj.kineticObjId, obj.currentTransform);

        if (!obj.voxelBody->isAsleep)
            obj.sleepTimer = 0.0f;

        // Record velocity for next-frame delta-v fracture detection
        obj.prevLinearVelocity = obj.voxelBody->linearVelocity;
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
    it->second.totalMass = mass;
    LOG_WARN_FMT("DynamicFurniture", "setObjectMass: mass set to " << mass
                 << " but VoxelRigidBody inertia not updated (recreate body to apply)");
}

glm::mat4 DynamicFurnitureManager::getTransform(const std::string& placedObjectId) const {
    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return glm::mat4(1.0f);
    return it->second.currentTransform;
}

// ============================================================================
// Internal helpers
// ============================================================================

void DynamicFurnitureManager::cleanupVoxelBody(DynamicFurnitureObject& obj) {
    if (!obj.voxelBody) return;
    auto* vw = m_physicsWorld ? m_physicsWorld->getVoxelWorld() : nullptr;
    if (vw) vw->removeBody(obj.voxelBody);
    obj.voxelBody = nullptr;
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
// Delta-v based fracture detection
// ============================================================================

void DynamicFurnitureManager::checkFractureContacts(float dt) {
    if (dt <= 0.0f) return;

    struct ShatterRequest { std::string objectId; float force; glm::vec3 contactPoint; };
    std::vector<ShatterRequest> requests;

    for (const auto& [id, obj] : m_active) {
        if (!obj.voxelBody || obj.isGrabbed) continue;

        glm::vec3 deltaV = obj.voxelBody->linearVelocity - obj.prevLinearVelocity;
        float dv = glm::length(deltaV);
        if (dv < 4.0f) continue;

        // Use fixed physics timestep so force estimate is frame-rate independent.
        constexpr float kPhysicsDt = 1.0f / 60.0f;
        float estimatedForce = obj.totalMass * dv / kPhysicsDt;
        requests.push_back({id, estimatedForce, obj.voxelBody->position});
    }

    for (const auto& req : requests)
        shatter(req.objectId, req.force, req.contactPoint);
}

// ============================================================================
// Greedy box merge
// ============================================================================

std::vector<DynamicFurnitureManager::MergedBox> DynamicFurnitureManager::mergeVoxelsGreedy(
    const std::vector<KinematicVoxel>& voxels,
    const Physics::MaterialManager& matMgr)
{
    if (voxels.empty()) return {};

    float minScale = 1.0f;
    for (const auto& v : voxels)
        minScale = std::min(minScale, v.scale.x);

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
            const auto& mat = matMgr.getMaterial(v.materialName);
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
        glm::ivec3 gmin = glm::clamp(glm::ivec3(glm::floor((vmin - boundsMin) * invCell)),
                                     glm::ivec3(0), gridSize - glm::ivec3(1));
        glm::ivec3 gmax = glm::clamp(glm::ivec3(glm::floor((vmax - boundsMin) * invCell - glm::vec3(0.001f))),
                                     glm::ivec3(0), gridSize - glm::ivec3(1));

        const auto& mat = matMgr.getMaterial(v.materialName);
        constexpr float PIECE_MASS = 0.05f;
        int cellCount = (gmax.x-gmin.x+1)*(gmax.y-gmin.y+1)*(gmax.z-gmin.z+1);
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
                    for (int zz = z; zz <= zEnd; ++zz)
                        if (grid[gridIdx(x, yEnd+1, zz)] != 1) { canExpandY = false; break; }
                    if (canExpandY) ++yEnd;
                }

                int xEnd = x;
                bool canExpandX = true;
                while (canExpandX && xEnd + 1 < gx) {
                    for (int yy = y; yy <= yEnd && canExpandX; ++yy)
                        for (int zz = z; zz <= zEnd; ++zz)
                            if (grid[gridIdx(xEnd+1, yy, zz)] != 1) { canExpandX = false; break; }
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
                glm::vec3 boxMax = boundsMin + glm::vec3(xEnd+1, yEnd+1, zEnd+1) * cellSize;
                result.push_back({(boxMin + boxMax) * 0.5f, (boxMax - boxMin) * 0.5f, boxMass});
            }
        }
    }

    return result;
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
        if (glm::length(voxels[i].localPos - contactPoint) <= fractureRadius)
            severed[i] = true;
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
    for (size_t i = 0; i < n; ++i)
        if (!severed[i])
            spatialMap[hashPos(voxels[i].localPos)].push_back(i);

    std::vector<int> component(n, -1);
    int numComponents = 0;

    for (size_t i = 0; i < n; ++i) {
        if (severed[i] || component[i] >= 0) continue;
        std::queue<size_t> q;
        q.push(i);
        component[i] = numComponents;
        while (!q.empty()) {
            size_t cur = q.front(); q.pop();
            const auto& pos = voxels[cur].localPos;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        auto it = spatialMap.find(hashPos(pos + glm::vec3(dx, dy, dz) * cellSize));
                        if (it == spatialMap.end()) continue;
                        for (size_t j : it->second) {
                            if (component[j] >= 0) continue;
                            float adjDist = (voxels[cur].scale.x + voxels[j].scale.x) * 0.5f + ADJ_EPSILON;
                            if (glm::length(voxels[j].localPos - pos) <= adjDist) {
                                component[j] = numComponents;
                                q.push(j);
                            }
                        }
                    }
        }
        ++numComponents;
    }

    std::vector<std::vector<KinematicVoxel>> result(numComponents);
    for (size_t i = 0; i < n; ++i)
        if (component[i] >= 0)
            result[component[i]].push_back(voxels[i]);
    for (size_t i = 0; i < n; ++i)
        if (severed[i])
            result.push_back({voxels[i]});

    return result;
}

int DynamicFurnitureManager::shatter(const std::string& placedObjectId,
                                      float contactForce,
                                      const glm::vec3& contactPoint)
{
    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return 0;

    auto& obj = it->second;

    Physics::MaterialManager matMgr;
    const auto& kinObjects = m_kinematic->getObjects();
    auto kinIt = kinObjects.find(obj.kineticObjId);
    if (kinIt == kinObjects.end()) return 0;

    const auto& voxels = kinIt->second.voxels;
    if (voxels.size() < 2) return 0;

    float avgBondStrength = 0.0f;
    for (const auto& v : voxels)
        avgBondStrength += matMgr.getMaterial(v.materialName).bondStrength;
    avgBondStrength /= static_cast<float>(voxels.size());

    if (contactForce < avgBondStrength * BREAK_FORCE_SCALAR) return 0;

    float fractureRadius = 0.5f + (contactForce / (avgBondStrength * BREAK_FORCE_SCALAR)) * 0.5f;

    glm::mat4 invTransform = glm::inverse(obj.currentTransform);
    glm::vec3 localContact = glm::vec3(invTransform * glm::vec4(contactPoint, 1.0f));

    auto fragments = findConnectedComponents(voxels, localContact, fractureRadius);
    if (fragments.size() <= 1) return 0;

    glm::vec3 linearVel(0.0f), angularVel(0.0f);
    if (obj.voxelBody) {
        linearVel  = obj.voxelBody->linearVelocity;
        angularVel = obj.voxelBody->angularVelocity;
    }

    cleanupVoxelBody(obj);
    m_kinematic->remove(obj.kineticObjId);

    std::string origId = placedObjectId;
    glm::mat4 origTransform = obj.currentTransform;
    m_active.erase(it);

    auto* vw = m_physicsWorld ? m_physicsWorld->getVoxelWorld() : nullptr;
    int fragmentCount = 0;

    for (size_t fi = 0; fi < fragments.size(); ++fi) {
        auto& fragVoxels = fragments[fi];
        if (fragVoxels.empty()) continue;

        if (static_cast<int>(fragVoxels.size()) >= MIN_FRAGMENT_VOXELS && vw) {
            if (m_active.size() >= MAX_DYNAMIC_FURNITURE) evictOldest();

            std::string fragId = origId + "_frag" + std::to_string(fi);

            // Build merged boxes for fragment
            auto fragMerged = mergeVoxelsGreedy(fragVoxels, matMgr);
            float fragRawMass = 0.0f;
            glm::vec3 fragCOM(0.0f);
            for (const auto& mb : fragMerged) { fragRawMass += mb.mass; fragCOM += mb.center * mb.mass; }
            if (fragRawMass > 0.0f) fragCOM /= fragRawMass;

            float fragMass = std::max(fragRawMass * 0.05f, 0.5f);
            float fragScale = (fragRawMass > 0.0f) ? fragMass / fragRawMass : 1.0f;

            std::vector<Physics::LocalBox> fragBoxes;
            fragBoxes.reserve(fragMerged.size());
            for (const auto& mb : fragMerged) {
                Physics::LocalBox lb;
                lb.offset      = mb.center - fragCOM;
                lb.halfExtents = mb.halfExtents;
                lb.mass        = mb.mass * fragScale;
                fragBoxes.push_back(lb);
            }

            glm::vec3 fragWorldCOM = glm::vec3(origTransform * glm::vec4(fragCOM, 1.0f));
            glm::quat fragOri = glm::quat_cast(glm::mat3(origTransform));

            DynamicFurnitureObject fragObj;
            fragObj.placedObjectId   = fragId;
            fragObj.voxelBody = vw->createBody(fragBoxes, fragWorldCOM, fragOri,
                                               0.2f, 0.6f, 0.4f, 0.5f);
            fragObj.totalMass = fragMass;

            glm::vec3 fragCenter(0.0f);
            for (const auto& v : fragVoxels) fragCenter += v.localPos;
            fragCenter /= static_cast<float>(fragVoxels.size());
            glm::vec3 scatter = glm::normalize(fragCenter - localContact) * 2.0f;

            fragObj.voxelBody->linearVelocity  = linearVel + scatter + glm::vec3(0.0f, 1.0f, 0.0f);
            fragObj.voxelBody->angularVelocity = angularVel;
            fragObj.voxelBody->wake();

            fragObj.currentTransform = glm::translate(glm::mat4(1.0f), fragWorldCOM)
                                     * glm::mat4_cast(fragOri);

            for (auto& v : fragVoxels) v.localPos -= fragCOM;

            fragObj.kineticObjId = m_kinematic->add(fragId, std::move(fragVoxels),
                                                     fragObj.currentTransform, fragId, true);
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
// Grab system
// ============================================================================

bool DynamicFurnitureManager::grab(const std::string& placedObjectId) {
    if (isGrabbing()) return false;

    auto it = m_active.find(placedObjectId);
    if (it == m_active.end()) return false;

    auto& obj = it->second;
    if (!obj.voxelBody) return false;

    obj.voxelBody->linearVelocity  = glm::vec3(0.0f);
    obj.voxelBody->angularVelocity = glm::vec3(0.0f);
    obj.voxelBody->wake();
    obj.isGrabbed = true;
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
        if (obj.voxelBody) obj.voxelBody->wake();
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
        if (obj.voxelBody) {
            obj.voxelBody->applyCentralImpulse(impulse);
            obj.voxelBody->wake();
        }
        obj.isGrabbed = false;
        LOG_INFO_FMT("DynamicFurniture", "Threw '" << m_grabbedObjectId
                     << "' with impulse (" << impulse.x << "," << impulse.y << "," << impulse.z << ")");
    }
    m_grabbedObjectId.clear();
}

void DynamicFurnitureManager::updateGrabPosition(const glm::vec3& cameraPos,
                                                   const glm::vec3& cameraFront) {
    if (m_grabbedObjectId.empty()) return;

    auto it = m_active.find(m_grabbedObjectId);
    if (it == m_active.end()) { m_grabbedObjectId.clear(); return; }

    auto& obj = it->second;
    if (!obj.voxelBody) return;

    glm::vec3 targetPos = cameraPos + cameraFront * GRAB_HOLD_DISTANCE;

    obj.voxelBody->position        = targetPos;
    obj.voxelBody->orientation     = glm::quat(1, 0, 0, 0);
    obj.voxelBody->linearVelocity  = glm::vec3(0.0f);
    obj.voxelBody->angularVelocity = glm::vec3(0.0f);

    obj.currentTransform = glm::translate(glm::mat4(1.0f), targetPos);

    if (m_kinematic)
        m_kinematic->setTransform(obj.kineticObjId, obj.currentTransform);
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

        glm::vec3 bmin = glm::vec3(obj.boundingMin) - glm::vec3(PLAYER_RADIUS);
        glm::vec3 bmax = glm::vec3(obj.boundingMax) + glm::vec3(PLAYER_RADIUS);

        if (playerPos.x >= bmin.x && playerPos.x <= bmax.x &&
            playerPos.y >= bmin.y && playerPos.y <= bmax.y &&
            playerPos.z >= bmin.z && playerPos.z <= bmax.z)
        {
            glm::vec3 impulse = glm::normalize(playerVelocity) * speed * 2.0f;
            LOG_INFO_FMT("DynamicFurniture", "Sprint collision with '" << id << "'");
            activate(id, impulse, playerPos);
            return;
        }
    }
}

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
        constexpr float subScale = 1.0f / 3.0f;
        KinematicVoxel v;
        v.localPos = glm::vec3(sub.parentRelativePos)
                   + glm::vec3(sub.subcubePos) * subScale
                   + glm::vec3(subScale * 0.5f);
        v.scale        = glm::vec3(subScale);
        v.parentFrac   = glm::vec3(sub.subcubePos) * subScale;
        v.materialName = sub.material;
        voxels.push_back(v);
    }

    for (const auto& micro : tmpl->microcubes) {
        constexpr float subScale   = 1.0f / 3.0f;
        constexpr float microScale = 1.0f / 9.0f;
        KinematicVoxel v;
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

} // namespace Core
} // namespace Phyxel
