#include "core/DynamicFurnitureManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "core/GpuParticlePhysics.h"
#include "physics/PhysicsWorld.h"
#include "physics/Material.h"
#include "core/ChunkManager.h"
#include "utils/Logger.h"

#include <btBulletDynamicsCommon.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
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
        // Remove from kinematic renderer
        if (m_kinematic && !obj.kineticObjId.empty()) {
            m_kinematic->remove(obj.kineticObjId);
        }
        cleanupPhysics(obj);
    }
    m_active.clear();
}

void DynamicFurnitureManager::deactivateAll() {
    // Collect IDs first to avoid modifying map during iteration
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

    if (m_active.count(placedObjectId)) {
        LOG_WARN_FMT("DynamicFurniture", "Object '" << placedObjectId << "' is already active");
        return false;
    }

    const PlacedObject* placed = m_placedObjects->get(placedObjectId);
    if (!placed) {
        LOG_ERROR_FMT("DynamicFurniture", "PlacedObject '" << placedObjectId << "' not found");
        return false;
    }

    // Enforce cap — evict oldest if at limit
    if (static_cast<int>(m_active.size()) >= MAX_DYNAMIC_FURNITURE) {
        evictOldest();
    }

    // 1. Build voxel list from template
    auto voxels = buildVoxelsFromTemplate(placed->templateName);
    if (voxels.empty()) {
        LOG_WARN_FMT("DynamicFurniture", "Template '" << placed->templateName << "' yielded no voxels");
        return false;
    }

    // 2. Build compound collision shape
    DynamicFurnitureObject obj;
    obj.placedObjectId   = placedObjectId;
    obj.templateName     = placed->templateName;
    obj.originalPosition = placed->position;
    obj.originalRotation = placed->rotation;

    obj.compoundShape = buildCompoundShape(voxels, obj.childShapes, obj.totalMass);
    if (!obj.compoundShape) {
        LOG_ERROR_FMT("DynamicFurniture", "Failed to build compound shape for '" << placedObjectId << "'");
        return false;
    }

    // 3. Compute initial transform from placed object position + rotation
    glm::vec3 worldPos = glm::vec3(placed->position) + glm::vec3(0.5f); // Center of origin cube
    glm::mat4 initTransform = glm::translate(glm::mat4(1.0f), worldPos);
    if (placed->rotation != 0) {
        initTransform = glm::rotate(initTransform,
                                     glm::radians(static_cast<float>(placed->rotation)),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
    }
    obj.currentTransform = initTransform;

    // 4. Remove static voxels from chunks
    m_placedObjects->clearVoxelsOnly(placedObjectId);

    // 5. Create dynamic Bullet rigid body
    obj.rigidBody = createDynamicBody(obj.compoundShape, obj.totalMass,
                                       initTransform, obj.motionState);
    if (!obj.rigidBody) {
        LOG_ERROR_FMT("DynamicFurniture", "Failed to create rigid body for '" << placedObjectId << "'");
        // Restore voxels since we already cleared them
        // (PlacedObjectManager doesn't have a "re-place" so we'd need to call spawnTemplate again)
        return false;
    }

    // 6. Apply impulse at contact point
    if (glm::length(impulse) > 0.001f) {
        btVector3 btImpulse(impulse.x, impulse.y, impulse.z);
        // Compute local contact point relative to body center
        glm::vec3 localContact = contactPoint - glm::vec3(initTransform[3]);
        btVector3 btLocalContact(localContact.x, localContact.y, localContact.z);
        obj.rigidBody->applyImpulse(btImpulse, btLocalContact);
    }

    // 7. Register with KinematicVoxelManager for rendering
    obj.kineticObjId = m_kinematic->add("dynfurn", std::move(voxels), initTransform, placedObjectId);

    // 8. Mark PlacedObject metadata
    auto* mutablePlaced = const_cast<PlacedObject*>(m_placedObjects->get(placedObjectId));
    if (mutablePlaced) {
        mutablePlaced->metadata["dynamic_furniture"] = true;
    }

    LOG_INFO_FMT("DynamicFurniture", "Activated '" << placedObjectId
                 << "' (template=" << obj.templateName
                 << ", mass=" << obj.totalMass
                 << ", voxels=" << obj.childShapes.size() << ")");

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

    // Remove from kinematic renderer
    if (m_kinematic && !obj.kineticObjId.empty()) {
        m_kinematic->remove(obj.kineticObjId);
    }

    // Determine placement position
    glm::ivec3 placePos = obj.originalPosition;
    int placeRot = obj.originalRotation;

    if (!restoreOriginal && obj.rigidBody) {
        // Use current physics position, quantize to integer grid
        glm::mat4 currentXform = readBulletTransform(obj.rigidBody);
        glm::vec3 pos = glm::vec3(currentXform[3]);
        placePos = glm::ivec3(glm::floor(pos));

        // Quantize rotation to nearest 90 degrees
        // Extract Y-axis rotation from the transform matrix
        float yaw = atan2(currentXform[0][2], currentXform[0][0]);
        float yawDeg = glm::degrees(yaw);
        // Snap to nearest 90
        int snapped = static_cast<int>(round(yawDeg / 90.0f)) * 90;
        placeRot = ((snapped % 360) + 360) % 360;
    }

    // Clean up Bullet physics
    cleanupPhysics(obj);

    // Re-place template at new position
    if (m_placedObjects && m_templateManager) {
        // Remove the old registry entry
        // (We need to place a fresh one at the new position)
        auto* mutablePlaced = const_cast<PlacedObject*>(m_placedObjects->get(placedObjectId));
        if (mutablePlaced) {
            mutablePlaced->metadata.erase("dynamic_furniture");
        }

        // If position changed, we need to remove old and re-place
        if (placePos != obj.originalPosition || placeRot != obj.originalRotation) {
            m_placedObjects->remove(placedObjectId);
            m_placedObjects->placeTemplate(obj.templateName, placePos, placeRot);
        } else {
            // Same position — just re-spawn the voxels via template manager
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

void DynamicFurnitureManager::update(float dt) {
    // Check for fracture-triggering contacts
    checkFractureContacts();

    std::vector<std::string> toRestaticize;

    for (auto& [id, obj] : m_active) {
        if (!obj.rigidBody || obj.isGrabbed) continue;

        // Sync transform from Bullet
        obj.currentTransform = readBulletTransform(obj.rigidBody);

        // Update rendering
        if (m_kinematic && !obj.kineticObjId.empty()) {
            m_kinematic->setTransform(obj.kineticObjId, obj.currentTransform);
        }

        // Check for sleep → re-staticize
        if (!obj.rigidBody->isActive()) {
            obj.sleepTimer += dt;
            if (obj.sleepTimer >= SLEEP_RESTATICIZE_TIME) {
                obj.markedForRestaticize = true;
                toRestaticize.push_back(id);
            }
        } else {
            obj.sleepTimer = 0.0f;
        }
    }

    // Deactivate objects that have come to rest
    for (const auto& id : toRestaticize) {
        deactivate(id, false);
    }
}

// ============================================================================
// Queries
// ============================================================================

bool DynamicFurnitureManager::isActive(const std::string& placedObjectId) const {
    return m_active.count(placedObjectId) > 0;
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

    // Full cubes
    for (const auto& cube : tmpl->cubes) {
        KinematicVoxel v;
        v.localPos     = glm::vec3(cube.relativePos) + glm::vec3(0.5f);
        v.scale        = glm::vec3(1.0f);
        v.materialName = cube.material;
        voxels.push_back(v);
    }

    // Subcubes (1/3 scale)
    for (const auto& sub : tmpl->subcubes) {
        KinematicVoxel v;
        constexpr float subScale = 1.0f / 3.0f;
        v.localPos = glm::vec3(sub.parentRelativePos)
                   + glm::vec3(sub.subcubePos) * subScale
                   + glm::vec3(subScale * 0.5f);
        v.scale        = glm::vec3(subScale);
        v.materialName = sub.material;
        voxels.push_back(v);
    }

    // Microcubes (1/9 scale)
    for (const auto& micro : tmpl->microcubes) {
        KinematicVoxel v;
        constexpr float subScale   = 1.0f / 3.0f;
        constexpr float microScale = 1.0f / 9.0f;
        v.localPos = glm::vec3(micro.parentRelativePos)
                   + glm::vec3(micro.subcubePos) * subScale
                   + glm::vec3(micro.microcubePos) * microScale
                   + glm::vec3(microScale * 0.5f);
        v.scale        = glm::vec3(microScale);
        v.materialName = micro.material;
        voxels.push_back(v);
    }

    return voxels;
}

// ============================================================================
// Greedy box merge — reduces compound child shape count
// ============================================================================

std::vector<DynamicFurnitureManager::MergedBox> DynamicFurnitureManager::mergeVoxelsGreedy(
    const std::vector<KinematicVoxel>& voxels,
    const Physics::MaterialManager& matMgr)
{
    if (voxels.empty()) return {};

    // Find the finest resolution among all voxels (smallest scale component)
    float minScale = 1.0f;
    for (const auto& v : voxels) {
        minScale = std::min(minScale, v.scale.x);
    }

    // Quantize all voxels to a grid at minScale resolution.
    // Each voxel may occupy multiple grid cells (a full cube = 9x9x9 microcube cells).
    // Grid coordinates are integer indices; we find the AABB of all voxels first.
    const float cellSize = minScale;
    const float invCell  = 1.0f / cellSize;

    // Compute grid AABB
    glm::vec3 boundsMin(1e9f), boundsMax(-1e9f);
    for (const auto& v : voxels) {
        glm::vec3 vmin = v.localPos - v.scale * 0.5f;
        glm::vec3 vmax = v.localPos + v.scale * 0.5f;
        boundsMin = glm::min(boundsMin, vmin);
        boundsMax = glm::max(boundsMax, vmax);
    }

    glm::ivec3 gridSize = glm::ivec3(glm::ceil((boundsMax - boundsMin) * invCell)) + glm::ivec3(1);

    // Cap grid size to avoid excessive memory for large objects
    constexpr int MAX_GRID_DIM = 64;
    if (gridSize.x > MAX_GRID_DIM || gridSize.y > MAX_GRID_DIM || gridSize.z > MAX_GRID_DIM) {
        // Fall back: no merge, return one box per voxel
        std::vector<MergedBox> result;
        result.reserve(voxels.size());
        for (const auto& v : voxels) {
            const auto& mat = matMgr.getMaterial(v.materialName);
            float volume = v.scale.x * v.scale.y * v.scale.z;
            result.push_back({v.localPos, v.scale * 0.5f, mat.mass * volume});
        }
        return result;
    }

    // 3D occupancy grid: 0 = empty, 1 = occupied (not yet merged), 2 = merged
    const int gx = gridSize.x, gy = gridSize.y, gz = gridSize.z;
    std::vector<uint8_t> grid(gx * gy * gz, 0);
    // Mass per cell (accumulated from voxels)
    std::vector<float> cellMass(gx * gy * gz, 0.0f);

    auto gridIdx = [&](int x, int y, int z) -> int {
        return z + y * gz + x * (gy * gz);
    };

    // Rasterize voxels into grid
    for (const auto& v : voxels) {
        glm::vec3 vmin = v.localPos - v.scale * 0.5f;
        glm::vec3 vmax = v.localPos + v.scale * 0.5f;
        glm::ivec3 gmin = glm::ivec3(glm::floor((vmin - boundsMin) * invCell));
        glm::ivec3 gmax = glm::ivec3(glm::floor((vmax - boundsMin) * invCell - glm::vec3(0.001f)));
        gmin = glm::clamp(gmin, glm::ivec3(0), gridSize - glm::ivec3(1));
        gmax = glm::clamp(gmax, glm::ivec3(0), gridSize - glm::ivec3(1));

        const auto& mat = matMgr.getMaterial(v.materialName);
        float totalVolume = v.scale.x * v.scale.y * v.scale.z;
        int cellCount = (gmax.x - gmin.x + 1) * (gmax.y - gmin.y + 1) * (gmax.z - gmin.z + 1);
        float massPerCell = (cellCount > 0) ? (mat.mass * totalVolume) / cellCount : 0.0f;

        for (int x = gmin.x; x <= gmax.x; ++x)
            for (int y = gmin.y; y <= gmax.y; ++y)
                for (int z = gmin.z; z <= gmax.z; ++z) {
                    int idx = gridIdx(x, y, z);
                    grid[idx] = 1;
                    cellMass[idx] += massPerCell;
                }
    }

    // Greedy sweep: iterate cells, for each unmerged cell expand as far as possible
    // along Z, then Y, then X to form the largest box.
    std::vector<MergedBox> result;

    for (int x = 0; x < gx; ++x) {
        for (int y = 0; y < gy; ++y) {
            for (int z = 0; z < gz; ++z) {
                if (grid[gridIdx(x, y, z)] != 1) continue;

                // Expand along Z
                int zEnd = z;
                while (zEnd + 1 < gz && grid[gridIdx(x, y, zEnd + 1)] == 1) ++zEnd;

                // Expand along Y (all Z cells in range must be occupied)
                int yEnd = y;
                bool canExpandY = true;
                while (canExpandY && yEnd + 1 < gy) {
                    for (int zz = z; zz <= zEnd; ++zz) {
                        if (grid[gridIdx(x, yEnd + 1, zz)] != 1) { canExpandY = false; break; }
                    }
                    if (canExpandY) ++yEnd;
                }

                // Expand along X (all Y×Z cells in range must be occupied)
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

                // Mark all cells in the merged region
                float boxMass = 0.0f;
                for (int xx = x; xx <= xEnd; ++xx)
                    for (int yy = y; yy <= yEnd; ++yy)
                        for (int zz = z; zz <= zEnd; ++zz) {
                            int idx = gridIdx(xx, yy, zz);
                            grid[idx] = 2; // merged
                            boxMass += cellMass[idx];
                        }

                // Compute world-space box
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

btCompoundShape* DynamicFurnitureManager::buildCompoundShape(
    const std::vector<KinematicVoxel>& voxels,
    std::vector<btBoxShape*>& childShapes,
    float& totalMass) const
{
    if (voxels.empty()) return nullptr;

    Physics::MaterialManager matMgr;

    // Greedy merge: collapse adjacent voxels into larger boxes
    auto mergedBoxes = mergeVoxelsGreedy(voxels, matMgr);

    LOG_INFO("DynamicFurniture", "Greedy merge: %zu voxels → %zu boxes (%.1fx reduction)",
             voxels.size(), mergedBoxes.size(),
             mergedBoxes.empty() ? 1.0f : static_cast<float>(voxels.size()) / mergedBoxes.size());

    auto* compound = new btCompoundShape(true); // true = enable dynamic AABB tree
    childShapes.clear();
    childShapes.reserve(mergedBoxes.size());
    totalMass = 0.0f;

    for (const auto& mb : mergedBoxes) {
        auto* box = new btBoxShape(btVector3(mb.halfExtents.x, mb.halfExtents.y, mb.halfExtents.z));
        childShapes.push_back(box);

        btTransform childXform;
        childXform.setIdentity();
        childXform.setOrigin(btVector3(mb.center.x, mb.center.y, mb.center.z));
        compound->addChildShape(childXform, box);

        totalMass += mb.mass;
    }

    return compound;
}

btRigidBody* DynamicFurnitureManager::createDynamicBody(
    btCompoundShape* shape, float mass,
    const glm::mat4& transform,
    btDefaultMotionState*& outMotionState) const
{
    if (!m_physicsWorld || !shape) return nullptr;

    // Convert glm to Bullet transform
    btTransform btXform;
    btXform.setFromOpenGLMatrix(glm::value_ptr(transform));

    outMotionState = new btDefaultMotionState(btXform);

    btVector3 localInertia(0, 0, 0);
    shape->calculateLocalInertia(mass, localInertia);

    btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, outMotionState, shape, localInertia);
    rbInfo.m_friction    = 0.6f;
    rbInfo.m_restitution = 0.2f;
    rbInfo.m_linearDamping  = 0.15f;
    rbInfo.m_angularDamping = 0.25f;

    auto* body = new btRigidBody(rbInfo);
    body->setActivationState(ACTIVE_TAG);

    // Enable CCD for fast-moving furniture (prevents tunneling)
    float radius = 0.5f; // Approximate
    body->setCcdMotionThreshold(radius);
    body->setCcdSweptSphereRadius(radius * 0.5f);

    m_physicsWorld->getWorld()->addRigidBody(body);
    return body;
}

glm::mat4 DynamicFurnitureManager::readBulletTransform(btRigidBody* body) {
    btTransform btXform;
    body->getMotionState()->getWorldTransform(btXform);
    float m[16];
    btXform.getOpenGLMatrix(m);
    return glm::make_mat4(m);
}

void DynamicFurnitureManager::evictOldest() {
    // Find the object with the highest sleep timer (most likely to be "done")
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

void DynamicFurnitureManager::cleanupPhysics(DynamicFurnitureObject& obj) {
    if (obj.rigidBody) {
        if (m_physicsWorld && m_physicsWorld->getWorld()) {
            m_physicsWorld->getWorld()->removeRigidBody(obj.rigidBody);
        }
        delete obj.rigidBody;
        obj.rigidBody = nullptr;
    }

    if (obj.motionState) {
        delete obj.motionState;
        obj.motionState = nullptr;
    }

    // Delete child shapes before compound
    for (auto* child : obj.childShapes) {
        delete child;
    }
    obj.childShapes.clear();

    if (obj.compoundShape) {
        delete obj.compoundShape;
        obj.compoundShape = nullptr;
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

    // Mark voxels near the contact point as "severed" (removed bonds)
    std::vector<bool> severed(n, false);
    for (size_t i = 0; i < n; ++i) {
        float dist = glm::length(voxels[i].localPos - contactPoint);
        if (dist <= fractureRadius) {
            severed[i] = true;
        }
    }

    // Build adjacency: two voxels are adjacent if their centers are within
    // touching distance (sum of half-scales + small epsilon) and neither is severed.
    // For efficiency, use spatial hashing.
    constexpr float ADJ_EPSILON = 0.05f;

    // Spatial hash: bucket by quantized position
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

    // BFS to find connected components among non-severed voxels
    std::vector<int> component(n, -1);
    int numComponents = 0;

    for (size_t i = 0; i < n; ++i) {
        if (severed[i] || component[i] >= 0) continue;

        // BFS from voxel i
        std::queue<size_t> q;
        q.push(i);
        component[i] = numComponents;

        while (!q.empty()) {
            size_t cur = q.front();
            q.pop();

            const auto& pos = voxels[cur].localPos;
            float touchDist = voxels[cur].scale.x * 0.5f + maxScale * 0.5f + ADJ_EPSILON;

            // Check neighboring spatial cells
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

    // Collect components + severed voxels as individual fragments
    std::vector<std::vector<KinematicVoxel>> result(numComponents);
    for (size_t i = 0; i < n; ++i) {
        if (component[i] >= 0) {
            result[component[i]].push_back(voxels[i]);
        }
    }

    // Each severed voxel becomes its own 1-voxel fragment
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

    // Check bond strength — use average of all voxel materials
    Physics::MaterialManager matMgr;
    const auto& kinObjects = m_kinematic->getObjects();
    auto kinIt = kinObjects.find(obj.kineticObjId);
    if (kinIt == kinObjects.end()) return 0;

    const auto& voxels = kinIt->second.voxels;
    if (voxels.size() < 2) return 0;  // Can't shatter a single voxel

    // Average bond strength
    float avgBondStrength = 0.0f;
    for (const auto& v : voxels) {
        avgBondStrength += matMgr.getMaterial(v.materialName).bondStrength;
    }
    avgBondStrength /= static_cast<float>(voxels.size());

    // Check if force exceeds threshold
    if (contactForce < avgBondStrength * BREAK_FORCE_SCALAR) {
        return 0;
    }

    LOG_INFO("DynamicFurniture", "Shattering '%s': force=%.1f, bondStrength=%.2f, threshold=%.1f",
             placedObjectId.c_str(), contactForce, avgBondStrength,
             avgBondStrength * BREAK_FORCE_SCALAR);

    // Compute fracture radius based on force vs bond strength
    float fractureRadius = 0.5f + (contactForce / (avgBondStrength * BREAK_FORCE_SCALAR)) * 0.5f;

    // Transform contact point to local space
    glm::mat4 invTransform = glm::inverse(obj.currentTransform);
    glm::vec3 localContact = glm::vec3(invTransform * glm::vec4(contactPoint, 1.0f));

    // Find connected components after severing bonds near contact
    auto fragments = findConnectedComponents(voxels, localContact, fractureRadius);

    if (fragments.size() <= 1) {
        LOG_DEBUG("DynamicFurniture", "Shatter produced %zu fragments, not enough to split",
                  fragments.size());
        return 0;
    }

    // Get velocity from current rigid body for fragment inheritance
    glm::vec3 linearVel(0.0f), angularVel(0.0f);
    if (obj.rigidBody) {
        const auto& lv = obj.rigidBody->getLinearVelocity();
        const auto& av = obj.rigidBody->getAngularVelocity();
        linearVel = glm::vec3(lv.x(), lv.y(), lv.z());
        angularVel = glm::vec3(av.x(), av.y(), av.z());
    }

    // Remove the original object (physics + rendering)
    cleanupPhysics(obj);
    m_kinematic->remove(obj.kineticObjId);

    // Save IDs before erasing
    std::string origId = placedObjectId;
    glm::mat4 origTransform = obj.currentTransform;
    m_active.erase(it);

    int fragmentCount = 0;

    for (size_t fi = 0; fi < fragments.size(); ++fi) {
        auto& fragVoxels = fragments[fi];
        if (fragVoxels.empty()) continue;

        if (static_cast<int>(fragVoxels.size()) >= MIN_FRAGMENT_VOXELS) {
            // Large fragment → new compound rigid body
            if (m_active.size() >= MAX_DYNAMIC_FURNITURE) {
                evictOldest();
            }

            std::string fragId = origId + "_frag" + std::to_string(fi);

            DynamicFurnitureObject fragObj;
            fragObj.placedObjectId = fragId;
            fragObj.templateName = "";  // Fragment, not a template

            // Build compound shape from fragment voxels
            float fragMass = 0.0f;
            fragObj.compoundShape = buildCompoundShape(fragVoxels, fragObj.childShapes, fragMass);
            fragObj.totalMass = fragMass;

            if (!fragObj.compoundShape) continue;

            // Create dynamic body at the original transform
            fragObj.rigidBody = createDynamicBody(fragObj.compoundShape, fragMass,
                                                   origTransform, fragObj.motionState);
            if (!fragObj.rigidBody) {
                cleanupPhysics(fragObj);
                continue;
            }

            // Inherit velocity + some scatter
            glm::vec3 fragCenter(0.0f);
            for (const auto& v : fragVoxels) fragCenter += v.localPos;
            fragCenter /= static_cast<float>(fragVoxels.size());
            glm::vec3 scatter = glm::normalize(fragCenter - localContact) * 2.0f;

            fragObj.rigidBody->setLinearVelocity(btVector3(
                linearVel.x + scatter.x, linearVel.y + scatter.y + 1.0f, linearVel.z + scatter.z));
            fragObj.rigidBody->setAngularVelocity(btVector3(
                angularVel.x, angularVel.y, angularVel.z));
            fragObj.rigidBody->activate(true);

            fragObj.currentTransform = origTransform;

            // Register for rendering
            fragObj.kineticObjId = m_kinematic->add(fragId, std::move(fragVoxels), origTransform, fragId);

            m_active[fragId] = std::move(fragObj);
            ++fragmentCount;

        } else if (m_gpuParticles) {
            // Small fragment → GPU particle debris
            for (const auto& v : fragVoxels) {
                glm::vec3 worldPos = glm::vec3(origTransform * glm::vec4(v.localPos, 1.0f));
                glm::vec3 scatter = glm::normalize(v.localPos - localContact) * 3.0f;

                Phyxel::GpuParticlePhysics::SpawnParams sp;
                sp.position = worldPos;
                sp.velocity = linearVel + scatter + glm::vec3(0.0f, 2.0f, 0.0f);
                sp.scale = v.scale;
                sp.materialName = v.materialName;
                sp.lifetime = 5.0f;

                m_gpuParticles->queueSpawn(sp);
            }
            ++fragmentCount;
        }
    }

    LOG_INFO("DynamicFurniture", "Shatter complete: %zu voxels → %zu fragments → %d created",
             voxels.size(), fragments.size(), fragmentCount);

    return fragmentCount;
}

void DynamicFurnitureManager::checkFractureContacts() {
    if (!m_physicsWorld || !m_physicsWorld->getWorld()) return;

    auto* dispatcher = m_physicsWorld->getWorld()->getDispatcher();
    int numManifolds = dispatcher->getNumManifolds();

    // Collect shatter requests (can't modify m_active while iterating)
    struct ShatterRequest {
        std::string objectId;
        float force;
        glm::vec3 contactPoint;
    };
    std::vector<ShatterRequest> requests;

    for (int i = 0; i < numManifolds; ++i) {
        auto* manifold = dispatcher->getManifoldByIndexInternal(i);
        if (manifold->getNumContacts() == 0) continue;

        const auto* body0 = static_cast<const btRigidBody*>(manifold->getBody0());
        const auto* body1 = static_cast<const btRigidBody*>(manifold->getBody1());

        // Check if either body belongs to an active furniture object
        for (auto& [objId, obj] : m_active) {
            if (obj.rigidBody != body0 && obj.rigidBody != body1) continue;

            // Find the strongest contact impulse
            float maxImpulse = 0.0f;
            btVector3 contactPt(0, 0, 0);

            for (int j = 0; j < manifold->getNumContacts(); ++j) {
                const auto& pt = manifold->getContactPoint(j);
                float impulse = pt.getAppliedImpulse();
                if (impulse > maxImpulse) {
                    maxImpulse = impulse;
                    contactPt = (obj.rigidBody == body0) ? pt.getPositionWorldOnA()
                                                          : pt.getPositionWorldOnB();
                }
            }

            if (maxImpulse > 0.0f) {
                requests.push_back({objId, maxImpulse,
                                     glm::vec3(contactPt.x(), contactPt.y(), contactPt.z())});
            }
            break;
        }
    }

    // Process shatter requests
    for (const auto& req : requests) {
        shatter(req.objectId, req.force, req.contactPoint);
    }
}

} // namespace Core
} // namespace Phyxel
