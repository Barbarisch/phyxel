#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Physics {

PhysicsWorld::PhysicsWorld() = default;
PhysicsWorld::~PhysicsWorld() { cleanup(); }

bool PhysicsWorld::initialize() {
    m_voxelWorld = std::make_unique<VoxelDynamicsWorld>();
    m_voxelWorld->setGravity(glm::vec3(0.0f, -9.81f, 0.0f));
    m_voxelWorld->setFallThreshold(m_fallThreshold);
    LOG_INFO("Physics", "PhysicsWorld initialized (VoxelDynamicsWorld)");
    return true;
}

void PhysicsWorld::cleanup() {
    m_voxelWorld.reset();
}

void PhysicsWorld::stepSimulation(float deltaTime, int /*maxSubSteps*/, float /*fixedTimeStep*/) {
    if (m_voxelWorld) m_voxelWorld->stepSimulation(deltaTime);
}

void PhysicsWorld::reset() {
    if (m_voxelWorld) m_voxelWorld->removeAllBodies();
}

void PhysicsWorld::setGravity(const glm::vec3& gravity) {
    if (m_voxelWorld) m_voxelWorld->setGravity(gravity);
}

glm::vec3 PhysicsWorld::getGravity() const {
    if (m_voxelWorld) return m_voxelWorld->getGravity();
    return glm::vec3(0.0f, -9.81f, 0.0f);
}

} // namespace Physics
} // namespace Phyxel
