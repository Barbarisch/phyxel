#include "core/KinematicVoxelManager.h"
#include "core/Types.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <climits>
#include <cfloat>
#include <unordered_set>

namespace Phyxel {
namespace Core {

// ============================================================================
// Construction / destruction
// ============================================================================

KinematicVoxelManager::KinematicVoxelManager(Physics::PhysicsWorld* physicsWorld)
    : m_physicsWorld(physicsWorld)
{}

KinematicVoxelManager::~KinematicVoxelManager() {
    clear();
}

// ============================================================================
// Public interface
// ============================================================================

std::string KinematicVoxelManager::add(const std::string& idHint,
                                        std::vector<KinematicVoxel> voxels,
                                        const glm::mat4& initialTransform,
                                        const std::string& placedObjectId,
                                        bool skipCollider)
{
    std::string id = generateId(idHint);

    KinematicVoxelObject obj;
    obj.id             = id;
    obj.placedObjectId = placedObjectId;
    obj.voxels         = std::move(voxels);
    obj.faces          = buildFaces(obj.voxels);
    obj.currentTransform = initialTransform;

    if (m_physicsWorld && !obj.voxels.empty() && !skipCollider) {
        glm::vec3 halfExtents = computeHalfExtents(obj.voxels);
        createCollider(obj, halfExtents);
    }

    LOG_INFO_FMT("KinematicVoxelManager", "Added '" << id << "': "
                 << obj.voxels.size() << " voxels, "
                 << obj.faces.size() << " faces"
                 << (obj.collider ? ", collider created" : ", no physics world"));

    m_objects[id] = std::move(obj);
    m_bufferDirty = true;
    return id;
}

void KinematicVoxelManager::remove(const std::string& id) {
    auto it = m_objects.find(id);
    if (it == m_objects.end()) return;

    destroyCollider(it->second);
    m_objects.erase(it);
    m_bufferDirty = true;
    LOG_INFO_FMT("KinematicVoxelManager", "Removed '" << id << "'");
}

void KinematicVoxelManager::setTransform(const std::string& id, const glm::mat4& transform) {
    auto it = m_objects.find(id);
    if (it != m_objects.end()) {
        it->second.currentTransform = transform;
    }
}

glm::mat4 KinematicVoxelManager::getTransform(const std::string& id) const {
    auto it = m_objects.find(id);
    return (it != m_objects.end()) ? it->second.currentTransform : glm::mat4(1.0f);
}

void KinematicVoxelManager::syncCollidersToPhysics() {
    for (auto& [id, obj] : m_objects) {
        if (!obj.collider || !obj.motionState) continue;

        btTransform t = toBulletTransform(obj.currentTransform);
        obj.motionState->setWorldTransform(t);
        obj.collider->setWorldTransform(t);
        obj.collider->activate(true);
    }
}

void KinematicVoxelManager::clear() {
    for (auto& [id, obj] : m_objects) {
        destroyCollider(obj);
    }
    m_objects.clear();
    m_bufferDirty = true;
}

// ============================================================================
// Private helpers
// ============================================================================

std::vector<KinematicFaceData> KinematicVoxelManager::buildFaces(
    const std::vector<KinematicVoxel>& voxels)
{
    // Build spatial lookup for adjacency culling.
    // Key: quantized position (scaled to integer grid), Value: true if occupied.
    // This culls internal faces between touching voxels of the same scale.
    auto quantize = [](const glm::vec3& pos, float scale) -> glm::ivec3 {
        // Round to nearest grid position at this scale
        return glm::ivec3(glm::round(pos / scale));
    };

    // Group voxels by scale for per-scale adjacency
    float commonScale = voxels.empty() ? 1.0f : voxels[0].scale.x;
    bool uniformScale = true;
    for (const auto& v : voxels) {
        if (v.scale.x != commonScale) { uniformScale = false; break; }
    }

    // Build occupied set (works best when all voxels have same scale)
    std::unordered_set<int64_t> occupied;
    auto posKey = [](glm::ivec3 p) -> int64_t {
        return (int64_t(p.x) & 0xFFFFF) | ((int64_t(p.y) & 0xFFFFF) << 20) | ((int64_t(p.z) & 0xFFFFF) << 40);
    };

    if (uniformScale) {
        for (const auto& v : voxels) {
            occupied.insert(posKey(quantize(v.localPos, commonScale)));
        }
    }

    // Face direction offsets: +Z, -Z, +X, -X, +Y, -Y
    static const glm::ivec3 faceDir[6] = {
        {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}
    };

    std::vector<KinematicFaceData> faces;
    faces.reserve(voxels.size() * 3); // expect ~half culled

    for (const auto& v : voxels) {
        glm::ivec3 qpos = uniformScale ? quantize(v.localPos, commonScale) : glm::ivec3(0);

        for (uint32_t faceId = 0; faceId < 6; ++faceId) {
            // Cull face if neighbor exists in that direction
            if (uniformScale) {
                glm::ivec3 neighbor = qpos + faceDir[faceId];
                if (occupied.count(posKey(neighbor))) continue;
            }

            KinematicFaceData f{};
            f.localPosition = v.localPos;
            f.scale         = v.scale;
            f.textureIndex  = TextureConstants::getTextureIndexForMaterial(v.materialName, (int)faceId);
            f.faceId        = faceId;
            faces.push_back(f);
        }
    }
    return faces;
}

glm::vec3 KinematicVoxelManager::computeHalfExtents(const std::vector<KinematicVoxel>& voxels) {
    if (voxels.empty()) return glm::vec3(0.5f);

    glm::vec3 mn( FLT_MAX);
    glm::vec3 mx(-FLT_MAX);

    for (const auto& v : voxels) {
        glm::vec3 h = v.scale * 0.5f;
        mn = glm::min(mn, v.localPos - h);
        mx = glm::max(mx, v.localPos + h);
    }
    return (mx - mn) * 0.5f;
}

void KinematicVoxelManager::createCollider(KinematicVoxelObject& obj,
                                            const glm::vec3& halfExtents) {
    if (!m_physicsWorld) return;

    auto* world = m_physicsWorld->getWorld();
    if (!world) return;

    obj.colliderShape = new btBoxShape(btVector3(halfExtents.x, halfExtents.y, halfExtents.z));

    btTransform startTransform = toBulletTransform(obj.currentTransform);
    obj.motionState = new btDefaultMotionState(startTransform);

    btRigidBody::btRigidBodyConstructionInfo rbInfo(
        0.0f,                   // mass = 0 → static by default
        obj.motionState,
        obj.colliderShape,
        btVector3(0, 0, 0)      // zero inertia for static/kinematic
    );
    obj.collider = new btRigidBody(rbInfo);

    // Mark as kinematic so Bullet moves it when we update the world transform
    obj.collider->setCollisionFlags(
        obj.collider->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
    obj.collider->setActivationState(DISABLE_DEACTIVATION);

    world->addRigidBody(obj.collider);
}

void KinematicVoxelManager::destroyCollider(KinematicVoxelObject& obj) {
    if (!obj.collider) return;

    if (m_physicsWorld && m_physicsWorld->getWorld()) {
        m_physicsWorld->getWorld()->removeRigidBody(obj.collider);
    }

    delete obj.collider;
    delete obj.colliderShape;
    delete obj.motionState;

    obj.collider      = nullptr;
    obj.colliderShape = nullptr;
    obj.motionState   = nullptr;
}

btTransform KinematicVoxelManager::toBulletTransform(const glm::mat4& m) {
    btTransform t;
    t.setFromOpenGLMatrix(glm::value_ptr(m));
    return t;
}

std::string KinematicVoxelManager::generateId(const std::string& hint) {
    int& counter = m_idCounters[hint];
    ++counter;
    return hint + "_" + std::to_string(counter);
}

} // namespace Core
} // namespace Phyxel
