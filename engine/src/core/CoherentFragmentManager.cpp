#include "core/CoherentFragmentManager.h"
#include "core/CoherentFragmentService.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <limits>

namespace Phyxel {
namespace Core {

namespace {
glm::mat4 bodyToTransform(const Physics::VoxelRigidBody* b) {
    return glm::translate(glm::mat4(1.0f), b->position) * glm::mat4_cast(b->orientation);
}
}

uint32_t CoherentFragmentManager::spawn(
    const std::string& idHint,
    std::vector<KinematicVoxel> voxels,
    const glm::mat4& objectTransform,
    const glm::vec3& initialLinVel,
    const glm::vec3& initialAngVel,
    const std::function<float(const KinematicVoxel&)>& voxelMass)
{
    if (!ready()) return 0;

    auto pf = CoherentFragmentService::physicalize(
        m_world, m_kinematic, idHint, std::move(voxels), objectTransform,
        initialLinVel, initialAngVel, voxelMass, /*finalizeTotalMass*/ nullptr);
    if (!pf.ok()) return 0;

    // Persistent: never auto-expire (the 2026-07-14 "permanent settled object" decision).
    pf.body->lifetime = std::numeric_limits<float>::max();

    m_frags.push_back({ pf.body->id, pf.kineticObjId });
    return pf.body->id;
}

uint32_t CoherentFragmentManager::spawn(
    const std::string& idHint,
    std::vector<KinematicVoxel> renderVoxels,
    const std::vector<KinematicVoxel>& collisionVoxels,
    const glm::mat4& objectTransform,
    const glm::vec3& initialLinVel,
    const glm::vec3& initialAngVel,
    const std::function<float(const KinematicVoxel&)>& voxelMass)
{
    if (!ready()) return 0;

    auto pf = CoherentFragmentService::physicalize(
        m_world, m_kinematic, idHint, std::move(renderVoxels), collisionVoxels,
        objectTransform, initialLinVel, initialAngVel, voxelMass,
        /*finalizeTotalMass*/ nullptr);
    if (!pf.ok()) return 0;

    // Persistent: never auto-expire (the 2026-07-14 "permanent settled object" decision).
    pf.body->lifetime = std::numeric_limits<float>::max();

    m_frags.push_back({ pf.body->id, pf.kineticObjId });
    return pf.body->id;
}

void CoherentFragmentManager::update(float /*dt*/) {
    if (!m_world || !m_kinematic) return;

    // Sync live bodies; reap fragments whose body the world has removed.
    m_frags.erase(
        std::remove_if(m_frags.begin(), m_frags.end(), [&](const Frag& f) {
            Physics::VoxelRigidBody* b = m_world->getBodyById(f.bodyId);
            if (!b) {
                m_kinematic->remove(f.kinId);   // body gone -> drop its render
                return true;
            }
            m_kinematic->setTransform(f.kinId, bodyToTransform(b));
            return false;
        }),
        m_frags.end());
}

void CoherentFragmentManager::clear() {
    for (const auto& f : m_frags) {
        if (m_world) {
            if (Physics::VoxelRigidBody* b = m_world->getBodyById(f.bodyId)) {
                m_world->removeBody(b);
            }
        }
        if (m_kinematic) m_kinematic->remove(f.kinId);
    }
    m_frags.clear();
}

} // namespace Core
} // namespace Phyxel
