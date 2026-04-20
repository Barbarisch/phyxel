#include "physics/VoxelDynamicsWorld.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Physics {

VoxelDynamicsWorld::VoxelDynamicsWorld() = default;

// ---- Terrain ----

void VoxelDynamicsWorld::registerGrid(VoxelOccupancyGrid* grid) {
    if (!grid) return;
    auto it = std::find(m_grids.begin(), m_grids.end(), grid);
    if (it == m_grids.end())
        m_grids.push_back(grid);
}

void VoxelDynamicsWorld::unregisterGrid(VoxelOccupancyGrid* grid) {
    m_grids.erase(std::remove(m_grids.begin(), m_grids.end(), grid), m_grids.end());
}

// ---- Body management ----

VoxelRigidBody* VoxelDynamicsWorld::createBody(const std::vector<LocalBox>& boxes,
                                                const glm::vec3& worldPos,
                                                const glm::quat& orientation,
                                                float restitution, float friction,
                                                float linearDamp, float angularDamp) {
    auto body = std::make_unique<VoxelRigidBody>(m_nextId++);
    for (const auto& b : boxes)
        body->addLocalBox(b);
    body->finalizeShape();

    body->position      = worldPos;
    body->orientation   = glm::normalize(orientation);
    body->restitution   = restitution;
    body->friction      = friction;
    body->linearDamping = linearDamp;
    body->angularDamping= angularDamp;

    VoxelRigidBody* raw = body.get();
    m_bodies.push_back(std::move(body));
    return raw;
}

VoxelRigidBody* VoxelDynamicsWorld::createVoxelBody(const glm::vec3& worldPos,
                                                      const glm::vec3& halfExtents,
                                                      float mass,
                                                      float restitution,
                                                      float friction) {
    LocalBox box;
    box.offset      = glm::vec3(0.0f);
    box.halfExtents = halfExtents;
    box.mass        = mass;
    return createBody({box}, worldPos, glm::quat(1,0,0,0), restitution, friction);
}

void VoxelDynamicsWorld::removeBody(VoxelRigidBody* body) {
    if (!body) return;
    auto it = std::find_if(m_bodies.begin(), m_bodies.end(),
                           [body](const auto& u) { return u.get() == body; });
    if (it != m_bodies.end())
        m_bodies.erase(it);
}

void VoxelDynamicsWorld::removeAllBodies() {
    m_bodies.clear();
    m_contacts.clear();
}

size_t VoxelDynamicsWorld::getActiveCount() const {
    size_t count = 0;
    for (const auto& b : m_bodies)
        if (!b->isAsleep) ++count;
    return count;
}

VoxelRigidBody* VoxelDynamicsWorld::getBodyById(uint32_t id) const {
    for (const auto& b : m_bodies)
        if (b->id == id) return b.get();
    return nullptr;
}

// ---- Simulation ----

void VoxelDynamicsWorld::stepSimulation(float deltaTime, int maxSubsteps, float fixedStep) {
    m_accumulator += deltaTime;
    int steps = 0;
    while (m_accumulator >= fixedStep && steps < maxSubsteps) {
        substep(fixedStep);
        m_accumulator -= fixedStep;
        ++steps;
    }
    // Drain accumulator if we hit the substep cap (prevents spiral of death)
    if (steps == maxSubsteps)
        m_accumulator = 0.0f;
}

void VoxelDynamicsWorld::substep(float dt) {
    // 1. Rebuild world-space inertia tensors and apply gravity to awake bodies
    integrateVelocities(dt);

    // 2. Detect contacts
    m_contacts.clear();
    generateContacts();

    // 3. Prepare and solve
    VoxelContactSolver::prepareContacts(m_contacts, dt);
    VoxelContactSolver::solveContacts(m_contacts);

    // 4. Integrate positions
    integratePositions(dt);

    // 5. Update sleep timers
    updateSleepState(dt);

    // 6. Remove dead / fallen bodies
    cleanupDead();
}

void VoxelDynamicsWorld::integrateVelocities(float dt) {
    for (auto& body : m_bodies) {
        if (body->isDead || body->isAsleep || body->invMass == 0.0f) continue;
        body->updateInertiaTensorWorld();

        // Gravity
        body->linearVelocity += m_gravity * dt;

        // Damping (exponential decay approximation)
        float ld = std::pow(1.0f - body->linearDamping,  dt);
        float ad = std::pow(1.0f - body->angularDamping, dt);
        body->linearVelocity  *= ld;
        body->angularVelocity *= ad;
    }
}

void VoxelDynamicsWorld::integratePositions(float dt) {
    for (auto& body : m_bodies) {
        if (body->isDead || body->isAsleep || body->invMass == 0.0f) continue;

        body->position += body->linearVelocity * dt;

        // Integrate orientation via angular velocity
        float omegaLen = glm::length(body->angularVelocity);
        if (omegaLen > 1e-6f) {
            float angle = omegaLen * dt;
            glm::vec3 axis = body->angularVelocity / omegaLen;
            glm::quat dq   = glm::angleAxis(angle, axis);
            body->orientation = glm::normalize(dq * body->orientation);
        }
    }
}

void VoxelDynamicsWorld::generateContacts() {
    // ---- Body vs terrain ----
    for (auto& body : m_bodies) {
        if (body->isDead || body->isAsleep || body->invMass == 0.0f) continue;

        glm::vec3 bMin, bMax;
        body->getWorldAABB(bMin, bMax);

        // Expand slightly for contact generation stability
        const float expand = 0.01f;
        bMin -= glm::vec3(expand);
        bMax += glm::vec3(expand);

        for (VoxelOccupancyGrid* grid : m_grids) {
            std::vector<OccupiedBox> terrainBoxes;
            grid->queryAABB(bMin, bMax, terrainBoxes);

            for (const OccupiedBox& tb : terrainBoxes) {
                for (size_t bi = 0; bi < body->getLocalBoxes().size(); ++bi) {
                    VoxelContactSolver::generateOBBvsAABB(body.get(), bi, tb, m_contacts);
                }
            }
        }
    }

    // ---- Body vs kinematic obstacles (character segment boxes) ----
    // Sleeping bodies are included — a resting voxel is still solid.
    // Any contact wakes the body so the solver can apply the separating impulse.
    for (auto& body : m_bodies) {
        if (body->isDead || body->invMass == 0.0f) continue;

        glm::vec3 bMin, bMax;
        body->getWorldAABB(bMin, bMax);

        for (const KinematicObstacle& obs : m_kinematicObstacles) {
            glm::vec3 oMin = obs.center - obs.halfExtents;
            glm::vec3 oMax = obs.center + obs.halfExtents;
            if (bMax.x < oMin.x || bMin.x > oMax.x ||
                bMax.y < oMin.y || bMin.y > oMax.y ||
                bMax.z < oMin.z || bMin.z > oMax.z) continue;

            // Wake sleeping body so it responds to the impulse this substep
            if (body->isAsleep) {
                body->isAsleep   = false;
                body->sleepTimer = 0.0f;
            }

            OccupiedBox box{obs.center, obs.halfExtents};
            size_t before = m_contacts.size();
            for (size_t bi = 0; bi < body->getLocalBoxes().size(); ++bi)
                VoxelContactSolver::generateOBBvsAABB(body.get(), bi, box, m_contacts);

            // Stamp character velocity onto the new contacts so the solver
            // produces speed-proportional push impulses
            for (size_t k = before; k < m_contacts.size(); ++k)
                m_contacts[k].obstacleVelocity = obs.velocity;
        }
    }

    // ---- Body vs body (all awake pairs) ----
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        VoxelRigidBody* A = m_bodies[i].get();
        if (A->isDead || A->isAsleep) continue;

        glm::vec3 aMin, aMax;
        A->getWorldAABB(aMin, aMax);

        for (size_t j = i + 1; j < m_bodies.size(); ++j) {
            VoxelRigidBody* B = m_bodies[j].get();
            if (B->isDead || B->isAsleep) continue;

            glm::vec3 bMin, bMax;
            B->getWorldAABB(bMin, bMax);

            // Broadphase AABB rejection
            if (aMax.x < bMin.x || aMin.x > bMax.x ||
                aMax.y < bMin.y || aMin.y > bMax.y ||
                aMax.z < bMin.z || aMin.z > bMax.z) continue;

            for (size_t bi = 0; bi < A->getLocalBoxes().size(); ++bi) {
                for (size_t bj = 0; bj < B->getLocalBoxes().size(); ++bj) {
                    VoxelContactSolver::generateOBBvsOBB(A, bi, B, bj, m_contacts);
                }
            }
        }
    }
}

void VoxelDynamicsWorld::updateSleepState(float dt) {
    for (auto& body : m_bodies) {
        if (body->invMass == 0.0f) continue;

        float vSq  = glm::dot(body->linearVelocity, body->linearVelocity);
        float wSq  = glm::dot(body->angularVelocity, body->angularVelocity);
        bool  slow = vSq < VoxelRigidBody::SLEEP_VELOCITY_SQ
                  && wSq < VoxelRigidBody::SLEEP_ANGULAR_SQ;

        if (slow) {
            body->sleepTimer += dt;
            if (body->sleepTimer >= VoxelRigidBody::SLEEP_TIME && !body->isAsleep) {
                body->isAsleep         = true;
                body->linearVelocity   = glm::vec3(0.0f);
                body->angularVelocity  = glm::vec3(0.0f);
            }
        } else {
            body->sleepTimer = 0.0f;
            body->isAsleep   = false;
        }
    }
}

float VoxelDynamicsWorld::findGroundY(const glm::vec3& feetPos, float halfWidth, float maxSearchDown) const {
    glm::vec3 queryMin(feetPos.x - halfWidth, feetPos.y - maxSearchDown, feetPos.z - halfWidth);
    glm::vec3 queryMax(feetPos.x + halfWidth, feetPos.y,                  feetPos.z + halfWidth);

    float best = -std::numeric_limits<float>::max();
    bool  found = false;
    for (auto* grid : m_grids) {
        std::vector<OccupiedBox> boxes;
        grid->queryAABB(queryMin, queryMax, boxes);
        for (const auto& b : boxes) {
            float surfY = b.center.y + b.halfExtents.y;
            if (surfY > best) { best = surfY; found = true; }
        }
    }
    return found ? best : -std::numeric_limits<float>::max();
}

bool VoxelDynamicsWorld::overlapsTerrain(const glm::vec3& center, const glm::vec3& halfExtents) const {
    glm::vec3 queryMin = center - halfExtents;
    glm::vec3 queryMax = center + halfExtents;
    for (auto* grid : m_grids) {
        std::vector<OccupiedBox> boxes;
        grid->queryAABB(queryMin, queryMax, boxes);
        if (!boxes.empty()) return true;
    }
    return false;
}

bool VoxelDynamicsWorld::overlapsAnyBody(const glm::vec3& center, const glm::vec3& halfExtents) const {
    glm::vec3 qMin = center - halfExtents;
    glm::vec3 qMax = center + halfExtents;
    for (const auto& body : m_bodies) {
        if (body->isDead || body->invMass == 0.0f) continue;
        glm::vec3 bMin, bMax;
        body->getWorldAABB(bMin, bMax);
        if (qMax.x > bMin.x && qMin.x < bMax.x &&
            qMax.y > bMin.y && qMin.y < bMax.y &&
            qMax.z > bMin.z && qMin.z < bMax.z)
            return true;
    }
    return false;
}

void VoxelDynamicsWorld::setKinematicObstacles(std::vector<KinematicObstacle> obstacles) {
    m_kinematicObstacles = std::move(obstacles);
}

void VoxelDynamicsWorld::cleanupDead() {
    // Mark expired / fallen bodies dead — DynamicObjectManager calls removeBody()
    // before nulling its raw pointer, so we must NOT destroy the unique_ptr here.
    for (auto& body : m_bodies) {
        body->lifetime -= 1.0f / 60.0f;
        if (body->lifetime <= 0.0f || body->position.y < m_fallThreshold)
            body->isDead = true;
    }
}

} // namespace Physics
} // namespace Phyxel
