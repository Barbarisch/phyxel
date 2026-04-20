#include "physics/VoxelDynamicsWorld.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Phyxel {
namespace Physics {

// ---- Parallel helper --------------------------------------------------------
// Splits [0, count) into m_threadCount chunks.
// Calls func(begin, end) on each chunk — chunk 0 runs on the calling thread,
// the rest run as std::async tasks so MSVC's concrt thread pool is reused.
namespace {
template<typename F>
void parallelRange(size_t count, int threadCount, F&& func) {
    if (count == 0) return;
    size_t tc = static_cast<size_t>(std::max(1, threadCount));
    if (tc == 1 || count <= tc) {
        func(0, count);
        return;
    }
    size_t chunk = (count + tc - 1) / tc;
    std::vector<std::future<void>> futures;
    futures.reserve(tc - 1);
    for (size_t t = 1; t < tc; ++t) {
        size_t b = t * chunk;
        size_t e = std::min(b + chunk, count);
        if (b >= count) break;
        futures.push_back(std::async(std::launch::async, [b, e, &func] { func(b, e); }));
    }
    func(0, std::min(chunk, count));  // main thread handles chunk 0
    for (auto& f : futures) f.get();
}
} // namespace

// ---- Construction -----------------------------------------------------------

VoxelDynamicsWorld::VoxelDynamicsWorld() {
    unsigned int hw = std::thread::hardware_concurrency();
    m_threadCount = hw > 0 ? static_cast<int>(hw) : 4;
}

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

// ---- Body management --------------------------------------------------------

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

// ---- Simulation -------------------------------------------------------------

void VoxelDynamicsWorld::stepSimulation(float deltaTime, int maxSubsteps, float fixedStep) {
    m_accumulator += deltaTime;
    int steps = 0;
    while (m_accumulator >= fixedStep && steps < maxSubsteps) {
        substep(fixedStep);
        m_accumulator -= fixedStep;
        ++steps;
    }
    if (steps == maxSubsteps)
        m_accumulator = 0.0f;
}

void VoxelDynamicsWorld::substep(float dt) {
    integrateVelocities(dt);

    m_contacts.clear();
    generateContacts();

    // Parallel prepareContacts — each contact is independent
    parallelRange(m_contacts.size(), m_threadCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i)
            VoxelContactSolver::prepareContact(m_contacts[i], dt);
    });

    VoxelContactSolver::solveContacts(m_contacts);  // sequential PGS — cannot parallelize

    integratePositions(dt);
    updateSleepState(dt);
    cleanupDead();
}

// ---- Parallel physics phases ------------------------------------------------

void VoxelDynamicsWorld::integrateVelocities(float dt) {
    size_t n = m_bodies.size();
    parallelRange(n, m_threadCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            auto& body = m_bodies[i];
            if (body->isDead || body->isAsleep || body->invMass == 0.0f) continue;
            body->updateInertiaTensorWorld();
            body->linearVelocity += m_gravity * dt;
            float ld = std::pow(1.0f - body->linearDamping,  dt);
            float ad = std::pow(1.0f - body->angularDamping, dt);
            body->linearVelocity  *= ld;
            body->angularVelocity *= ad;
        }
    });
}

void VoxelDynamicsWorld::integratePositions(float dt) {
    size_t n = m_bodies.size();
    parallelRange(n, m_threadCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            auto& body = m_bodies[i];
            if (body->isDead || body->isAsleep || body->invMass == 0.0f) continue;
            body->position += body->linearVelocity * dt;
            float omegaLen = glm::length(body->angularVelocity);
            if (omegaLen > 1e-6f) {
                float angle = omegaLen * dt;
                glm::vec3 axis = body->angularVelocity / omegaLen;
                glm::quat dq   = glm::angleAxis(angle, axis);
                body->orientation = glm::normalize(dq * body->orientation);
            }
        }
    });
}

void VoxelDynamicsWorld::generateContacts() {
    size_t n  = m_bodies.size();
    int    tc = std::max(1, m_threadCount);

    // Build awake-body list and cache each AABB once — shared by terrain and body-body phases.
    struct AwakeBody { VoxelRigidBody* body; glm::vec3 mn, mx; };
    std::vector<AwakeBody> awake;
    awake.reserve(n);
    for (auto& b : m_bodies) {
        if (b->isDead || b->isAsleep || b->invMass == 0.0f) continue;
        AwakeBody ab;
        ab.body = b.get();
        b->getWorldAABB(ab.mn, ab.mx);
        awake.push_back(ab);
    }
    size_t na = awake.size();

    // ---- Body vs terrain (parallel, per-thread contact buffers) ----
    if (!m_grids.empty() && na > 0) {
        std::vector<std::vector<ContactPoint>> threadBufs(tc);
        const auto& grids = m_grids;
        const float expand = 0.01f;

        parallelRange(na, tc, [&](size_t b, size_t e) {
            size_t chunk = (na + static_cast<size_t>(tc) - 1) / static_cast<size_t>(tc);
            size_t slot  = (chunk > 0) ? (b / chunk) : 0;
            slot = std::min(slot, static_cast<size_t>(tc) - 1);
            auto& buf = threadBufs[slot];

            for (size_t i = b; i < e; ++i) {
                const AwakeBody& ab = awake[i];
                glm::vec3 bMin = ab.mn - glm::vec3(expand);
                glm::vec3 bMax = ab.mx + glm::vec3(expand);

                for (VoxelOccupancyGrid* grid : grids) {
                    std::vector<OccupiedBox> terrainBoxes;
                    grid->queryAABB(bMin, bMax, terrainBoxes);
                    for (const OccupiedBox& tb : terrainBoxes)
                        for (size_t bi = 0; bi < ab.body->getLocalBoxes().size(); ++bi)
                            VoxelContactSolver::generateOBBvsAABB(ab.body, bi, tb, buf);
                }
            }
        });

        for (auto& buf : threadBufs)
            m_contacts.insert(m_contacts.end(), buf.begin(), buf.end());
    }

    // ---- Body vs kinematic obstacles (sequential — writes isAsleep) ----
    if (!m_kinematicObstacles.empty()) {
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

                if (body->isAsleep) {
                    body->isAsleep   = false;
                    body->sleepTimer = 0.0f;
                }

                OccupiedBox box{obs.center, obs.halfExtents};
                size_t before = m_contacts.size();
                for (size_t bi = 0; bi < body->getLocalBoxes().size(); ++bi)
                    VoxelContactSolver::generateOBBvsAABB(body.get(), bi, box, m_contacts);
                for (size_t k = before; k < m_contacts.size(); ++k)
                    m_contacts[k].obstacleVelocity = obs.velocity;
            }
        }
    }

    // ---- Body vs body: spatial hash broadphase ----
    // Buckets bodies into 3-D cells of CELL_SIZE. Only pairs sharing a cell are
    // tested, reducing average complexity from O(N²) to O(N) for sparse scenes.
    if (na < 2) return;
    {
        static constexpr float CELL_SIZE = 2.0f;
        static constexpr float INV_CELL  = 1.0f / CELL_SIZE;

        // Pack 3 signed cell coords into a uint64 key (21 bits each, offset to positive).
        auto cellKey = [](int x, int y, int z) -> uint64_t {
            uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x + (1 << 20)) & 0x1FFFFF);
            uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(y + (1 << 20)) & 0x1FFFFF);
            uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(z + (1 << 20)) & 0x1FFFFF);
            return ux | (uy << 21) | (uz << 42);
        };

        // Insert each body into every cell its AABB overlaps (usually 1–8 cells).
        std::unordered_map<uint64_t, std::vector<uint32_t>> spatialHash;
        spatialHash.reserve(na * 4);
        for (uint32_t i = 0; i < static_cast<uint32_t>(na); ++i) {
            const AwakeBody& ab = awake[i];
            int x0 = static_cast<int>(std::floor(ab.mn.x * INV_CELL));
            int y0 = static_cast<int>(std::floor(ab.mn.y * INV_CELL));
            int z0 = static_cast<int>(std::floor(ab.mn.z * INV_CELL));
            int x1 = static_cast<int>(std::floor(ab.mx.x * INV_CELL));
            int y1 = static_cast<int>(std::floor(ab.mx.y * INV_CELL));
            int z1 = static_cast<int>(std::floor(ab.mx.z * INV_CELL));
            for (int cx = x0; cx <= x1; ++cx)
            for (int cy = y0; cy <= y1; ++cy)
            for (int cz = z0; cz <= z1; ++cz)
                spatialHash[cellKey(cx, cy, cz)].push_back(i);
        }

        // Test each unique pair that shares at least one cell.
        std::unordered_set<uint64_t> testedPairs;
        testedPairs.reserve(na * 4);

        for (auto& [key, indices] : spatialHash) {
            size_t nc = indices.size();
            if (nc < 2) continue;
            for (size_t a = 0; a < nc; ++a) {
                for (size_t b = a + 1; b < nc; ++b) {
                    uint32_t ia = indices[a], ib = indices[b];
                    if (ia > ib) std::swap(ia, ib);
                    uint64_t pairKey = (static_cast<uint64_t>(ia) << 32) | static_cast<uint64_t>(ib);
                    if (!testedPairs.insert(pairKey).second) continue;

                    const AwakeBody& A = awake[ia];
                    const AwakeBody& B = awake[ib];
                    // AABB overlap (quick reject using cached extents)
                    if (A.mx.x < B.mn.x || A.mn.x > B.mx.x ||
                        A.mx.y < B.mn.y || A.mn.y > B.mx.y ||
                        A.mx.z < B.mn.z || A.mn.z > B.mx.z) continue;

                    for (size_t bi = 0; bi < A.body->getLocalBoxes().size(); ++bi)
                        for (size_t bj = 0; bj < B.body->getLocalBoxes().size(); ++bj)
                            VoxelContactSolver::generateOBBvsOBB(A.body, bi, B.body, bj, m_contacts);
                }
            }
        }
    }
}

void VoxelDynamicsWorld::updateSleepState(float dt) {
    size_t n = m_bodies.size();
    parallelRange(n, m_threadCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            auto& body = m_bodies[i];
            if (body->invMass == 0.0f) continue;
            float vSq  = glm::dot(body->linearVelocity,  body->linearVelocity);
            float wSq  = glm::dot(body->angularVelocity, body->angularVelocity);
            bool  slow = vSq < VoxelRigidBody::SLEEP_VELOCITY_SQ
                      && wSq < VoxelRigidBody::SLEEP_ANGULAR_SQ;
            if (slow) {
                body->sleepTimer += dt;
                if (body->sleepTimer >= VoxelRigidBody::SLEEP_TIME && !body->isAsleep) {
                    body->isAsleep        = true;
                    body->linearVelocity  = glm::vec3(0.0f);
                    body->angularVelocity = glm::vec3(0.0f);
                }
            } else {
                body->sleepTimer = 0.0f;
                body->isAsleep   = false;
            }
        }
    });
}

// ---- Queries ----------------------------------------------------------------

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
    size_t n = m_bodies.size();
    parallelRange(n, m_threadCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            auto& body = m_bodies[i];
            body->lifetime -= 1.0f / 60.0f;
            if (body->lifetime <= 0.0f || body->position.y < m_fallThreshold)
                body->isDead = true;
        }
    });
}

} // namespace Physics
} // namespace Phyxel
