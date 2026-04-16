#include "core/DynamicFurnitureManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "physics/PhysicsWorld.h"
#include "physics/Material.h"
#include "core/ChunkManager.h"
#include "utils/Logger.h"

#include <btBulletDynamicsCommon.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

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

btCompoundShape* DynamicFurnitureManager::buildCompoundShape(
    const std::vector<KinematicVoxel>& voxels,
    std::vector<btBoxShape*>& childShapes,
    float& totalMass) const
{
    if (voxels.empty()) return nullptr;

    auto* compound = new btCompoundShape(true); // true = enable dynamic AABB tree
    childShapes.clear();
    childShapes.reserve(voxels.size());
    totalMass = 0.0f;

    Physics::MaterialManager matMgr;

    for (const auto& voxel : voxels) {
        // Half-extents from scale (scale=1 means a 1x1x1 cube → half-extent 0.5)
        glm::vec3 halfExtents = voxel.scale * 0.5f;
        auto* box = new btBoxShape(btVector3(halfExtents.x, halfExtents.y, halfExtents.z));
        childShapes.push_back(box);

        // Position child at voxel's local position
        btTransform childXform;
        childXform.setIdentity();
        childXform.setOrigin(btVector3(voxel.localPos.x, voxel.localPos.y, voxel.localPos.z));
        compound->addChildShape(childXform, box);

        // Accumulate mass
        const auto& mat = matMgr.getMaterial(voxel.materialName);
        // Scale mass by volume (scale^3 relative to full cube)
        float volume = voxel.scale.x * voxel.scale.y * voxel.scale.z;
        totalMass += mat.mass * volume;
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

} // namespace Core
} // namespace Phyxel
