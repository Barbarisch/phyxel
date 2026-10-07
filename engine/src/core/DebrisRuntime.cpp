#include "core/DebrisRuntime.h"
#include "core/ChunkManager.h"
#include "core/DebrisMoverFeed.h"
#include "core/KinematicVoxelManager.h"
#include "graphics/RenderCoordinator.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>
#include <utility>

namespace Phyxel {

DebrisRuntime::~DebrisRuntime() = default;

bool DebrisRuntime::initialize(Vulkan::VulkanDevice* device, Graphics::RenderCoordinator* renderer,
                               ChunkManager* chunks, Physics::PhysicsWorld* physics, const Config& cfg) {
    m_physics = physics;
    m_gpu.reset();
    m_disabledReason.clear();
    // All debris lives in the GPU solver. Without it breaks, blasts and derez make NO debris, so a
    // missing solver must be LOUD: one ERROR, a reason the host echoes (/api/debug/gpu_physics),
    // and every refused piece counted (DamageSystem::refusedDebrisTotal).
    const char* off = std::getenv("PHYXEL_DISABLE_GPU_DEBRIS");
    if (off && *off && std::string(off) != "0") {
        m_disabledReason = "disabled by PHYXEL_DISABLE_GPU_DEBRIS";
    } else if (!cfg.enabled) {
        m_disabledReason = "disabled by configuration (debris.enabled = false)";
    } else {
        m_gpu = std::make_unique<GpuParticlePhysics>();
    }
    if (m_gpu && device && m_gpu->initialize(device, "")) {
        // RenderCoordinator: compute dispatch, the debris draw and the shared static occupancy.
        if (renderer) renderer->setGpuParticlePhysics(m_gpu.get());
        // ChunkManager: the debris light sampler.
        if (chunks) chunks->setGpuParticlePhysics(m_gpu.get());
        LOG_INFO("DebrisRuntime", "GpuParticlePhysics initialized successfully!");
        return true;
    }
    if (m_disabledReason.empty())
        m_disabledReason = device ? "GpuParticlePhysics::initialize failed" : "no Vulkan device";
    m_gpu.reset();
    LOG_ERROR("DebrisRuntime", "GPU debris DISABLED (" + m_disabledReason +
              "): breaks, blasts and derez will produce NO debris");
    return false;
}

void DebrisRuntime::beginFrame(float dt) {
    if (m_gpu) m_gpu->update(dt);
}

void DebrisRuntime::feedCharacters(const std::vector<Scene::AnimatedVoxelCharacter*>& characters,
                                   const glm::vec3& eye) {
    if (!m_gpu || !m_gpu->isInitialized()) return;
    // Phase 3a: EVERY animated character - the player, spawned entities, NPCs, monsters, fauna -
    // as oriented limb boxes with per-limb velocity, fed after all of them updated this frame. A
    // derezzing character feeds nothing; an update-LOD-deferred one is extrapolated
    // (collectMoverBoxes). Nearest the camera first, so the MAX_KINEMATIC overflow (counted) drops
    // the farthest.
    std::vector<std::pair<float, Scene::AnimatedVoxelCharacter*>> movers;
    std::unordered_set<Scene::AnimatedVoxelCharacter*> seen;
    movers.reserve(characters.size());
    for (auto* c : characters) {
        if (!c || !seen.insert(c).second) continue;
        const glm::vec3 d = c->getPosition() - eye;
        movers.push_back({glm::dot(d, d), c});
    }
    std::sort(movers.begin(), movers.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<Scene::AnimatedVoxelCharacter::MoverBox> limbs;
    for (auto& [d2, c] : movers) c->collectMoverBoxes(limbs);
    std::vector<GpuParticlePhysics::MoverBox> boxes;
    boxes.reserve(limbs.size());
    for (const auto& l : limbs) boxes.push_back({l.center, l.halfExtents, l.rotation, l.velocity});
    m_gpu->setMoverBoxes(std::move(boxes));
}

void DebrisRuntime::feedKinematicObjects(Core::KinematicVoxelManager* kvm, float dt, const glm::vec3& eye) {
    if (!kvm) return;
    // Phase 3b: doors, animated template parts and held items - every KinematicVoxelObject flagged
    // pushesDebris - as oriented sub-boxes with per-box velocity from this frame's transform delta.
    // The same boxes become CPU kinematic obstacles (doors block furniture, even with debris off).
    kvm->syncCollidersToPhysics(dt);
    if (!m_gpu || !m_gpu->isInitialized()) return;
    // Nearest the camera first, trimmed to the slots the limbs left (a settlement has ~100 doors x
    // 8 boxes): the far ones are dropped, counted and logged once.
    const auto& km = kvm->lastMoverBoxes();
    std::vector<std::pair<float, const Core::KinematicVoxelManager::MoverBox*>> order;
    order.reserve(km.size());
    for (const auto& m : km) order.push_back({glm::dot(m.center - eye, m.center - eye), &m});
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const uint32_t budget = m_gpu->objectMoverBudget();
    std::vector<GpuParticlePhysics::MoverBox> boxes;
    boxes.reserve(std::min<size_t>(order.size(), budget));
    for (const auto& [d2, m] : order) {
        if (boxes.size() >= budget) break;
        boxes.push_back({m->center, m->halfExtents, m->rotation, m->velocity});
    }
    if (order.size() > boxes.size() && !m_loggedObjectSkip) {
        m_loggedObjectSkip = true;
        LOG_WARN("GpuParticlePhysics", "debris movers: {} door/part/held-item boxes past the {}-box budget "
                 "were not fed (the farthest from the camera); logged once",
                 order.size() - boxes.size(), DebrisShared::MAX_KINEMATIC);
    }
    m_gpu->setObjectMoverBoxes(std::move(boxes));
}

void DebrisRuntime::feedRigidBodies(const glm::vec3& eye) {
    if (!m_gpu || !m_gpu->isInitialized()) return;
    // Phase 3c: every CPU rigid body (furniture incl. grabbed/thrown, fragments, felled trees, item
    // props) at its post-step pose, compound boxes with point velocities; sleepers too, as
    // supports. One-way: debris does not push them back (Phase 4 impulses do). Nearest the camera
    // first, whole bodies only, in the slots the limbs and objects left; a body that does not fit
    // is counted and logged once.
    auto* vw = m_physics ? m_physics->getVoxelWorld() : nullptr;
    if (vw && m_gpu->getActiveParticleCount() > 0) {
        std::vector<GpuParticlePhysics::MoverBox> boxes;
        const auto fed = DebrisMoverFeed::appendRigidBodies(*vw, eye, m_gpu->bodyMoverBudget(), boxes);
        if (fed.bodiesSkipped > 0 && !m_loggedBodySkip) {
            m_loggedBodySkip = true;
            LOG_WARN("GpuParticlePhysics", "debris movers: {} CPU bodies past the {}-box budget were not fed "
                     "(debris passes through them); logged once", fed.bodiesSkipped, DebrisShared::MAX_KINEMATIC);
        }
        m_gpu->setBodyMoverBoxes(std::move(boxes));
    } else if (m_gpu->bodyMoverCount() > 0) {
        m_gpu->setBodyMoverBoxes({});   // no debris to push: free the slots
    }
}

}  // namespace Phyxel
