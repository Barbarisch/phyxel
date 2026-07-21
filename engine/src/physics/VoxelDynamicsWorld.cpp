#include "physics/VoxelDynamicsWorld.h"
#include "utils/Logger.h"
#include <algorithm>
#include <chrono>
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
    // U1a: dedup via the chunk-coord map — O(1), so re-registering on an air→content
    // transition is cheap (it happens on the edit/rebuild path, not just once at load).
    const glm::ivec3 o = grid->getChunkOrigin();
    auto [it, inserted] = m_gridByChunk.try_emplace(chunkKey(o.x >> 5, o.y >> 5, o.z >> 5), grid);
    if (inserted) {
        m_grids.push_back(grid);
        LOG_DEBUG_FMT("VoxelDynamicsWorld", "registerGrid into world=" << static_cast<const void*>(this)
                      << " grid=" << static_cast<const void*>(grid) << " total=" << m_grids.size());
    } else if (it->second != grid) {
        // Two grids claim one chunk coord — the 1-grid-per-chunk invariant broke. Keep the
        // incumbent (don't silently swap the collision surface) and flag it.
        LOG_WARN("VoxelDynamicsWorld", "registerGrid: chunk-coord collision, keeping incumbent grid");
    }
}

void VoxelDynamicsWorld::unregisterGrid(VoxelOccupancyGrid* grid) {
    if (!grid) return;
    m_grids.erase(std::remove(m_grids.begin(), m_grids.end(), grid), m_grids.end());
    const glm::ivec3 o = grid->getChunkOrigin();
    auto it = m_gridByChunk.find(chunkKey(o.x >> 5, o.y >> 5, o.z >> 5));
    if (it != m_gridByChunk.end() && it->second == grid)   // erase only if it's ours
        m_gridByChunk.erase(it);
}

void VoxelDynamicsWorld::gatherGridsOverlapping(const glm::vec3& mn, const glm::vec3& mx,
                                                std::vector<VoxelOccupancyGrid*>& out) const {
    out.clear();
    if (m_gridByChunk.empty()) return;
    // Chunk-coord span the AABB touches. floorDiv by 32 (grids live at multiples of 32);
    // shifting a negative int is fine here because we want arithmetic floor, so use a
    // branch to floor toward -inf.
    auto floorDiv32 = [](float v) -> int {
        return static_cast<int>(std::floor(v / 32.0f));
    };
    const int x0 = floorDiv32(mn.x), x1 = floorDiv32(mx.x);
    const int y0 = floorDiv32(mn.y), y1 = floorDiv32(mx.y);
    const int z0 = floorDiv32(mn.z), z1 = floorDiv32(mx.z);
    for (int cx = x0; cx <= x1; ++cx)
    for (int cy = y0; cy <= y1; ++cy)
    for (int cz = z0; cz <= z1; ++cz) {
        auto it = m_gridByChunk.find(chunkKey(cx, cy, cz));
        if (it != m_gridByChunk.end()) out.push_back(it->second);
    }
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
            // Anti-tunneling (docs/DestructionSystemV2.md §15.6 A): this solver integrates
            // plain Euler and generates terrain contacts only from a body's current AABB —
            // there is NO CCD. A body moving > ~1 voxel/substep therefore skips clean through
            // a 1-voxel-thick surface (a floating platform, a thin roof) into the void and
            // free-falls to the fall threshold. Cap the per-substep displacement to < the
            // terrain cell size so the discrete contact check can't be jumped. Speed above
            // this is unphysical for CPU rigid bodies (furniture, fells, fracture chunks).
            constexpr float kMaxStepVoxels = 0.9f;
            const float     maxSpeed = kMaxStepVoxels / std::max(dt, 1e-4f);
            const float     sp = glm::length(body->linearVelocity);
            if (sp > maxSpeed) body->linearVelocity *= (maxSpeed / sp);
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
    using Clock = std::chrono::high_resolution_clock;
    const auto tGen0 = Clock::now();   // §15.5 broadphase profiling

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

    // §15.5: the terrain broadphase visits Σ(boxes) × gridCount grids per substep — cost that
    // scales with WORLD SIZE, not the falling object. Count the calls arithmetically (rather than
    // atomically inside the parallel hot loop, which would distort the timing beside it).
    size_t awakeBoxes = 0;
    for (const AwakeBody& ab : awake) awakeBoxes += ab.body->getLocalBoxes().size();
    const auto tTerrain0 = Clock::now();

    // ---- Body vs terrain (parallel, per-thread contact buffers) ----
    // Terrain is queried PER COLLISION BOX, not per body: a multi-box body's
    // whole-body AABB spans the entire object (a fallen tree ≈ 10x15x10 m →
    // ~1500 occupied voxels), and pairing every one of those with every body
    // box was terrainBoxes × bodyBoxes narrow-phase tests per substep
    // (~225k for a 150-box fell — the "FPS ≈ 0 while the tree falls" hang;
    // 870 ms/frame measured at 46 boxes). Each box only overlaps a handful of
    // voxels, so per-box queries are O(boxes × ~4) instead.
    std::vector<uint64_t> threadCalls(tc, 0);   // §15.5: actual queryAABB calls (U1a proof)
    if (!m_gridByChunk.empty() && na > 0) {
        std::vector<std::vector<ContactPoint>> threadBufs(tc);
        const float expand = 0.01f;

        parallelRange(na, tc, [&](size_t b, size_t e) {
            size_t chunk = (na + static_cast<size_t>(tc) - 1) / static_cast<size_t>(tc);
            size_t slot  = (chunk > 0) ? (b / chunk) : 0;
            slot = std::min(slot, static_cast<size_t>(tc) - 1);
            auto& buf = threadBufs[slot];
            std::vector<OccupiedBox> terrainBoxes;         // scratch, reused across boxes
            std::vector<VoxelOccupancyGrid*> localGrids;   // grids overlapping THIS body (U1a)

            for (size_t i = b; i < e; ++i) {
                const AwakeBody& ab = awake[i];
                // U1a: gather the few grids under this body's AABB ONCE, reuse across its
                // boxes — a fragment's boxes are all near each other, so this replaces the
                // full m_grids scan (O(worldSize)) with O(chunksTheBodyTouches). Widen by a
                // margin (> the per-box `expand`) so a box whose face sits on a chunk border
                // and expands into the neighbour still sees that neighbour's grid.
                gatherGridsOverlapping(ab.mn - glm::vec3(0.5f), ab.mx + glm::vec3(0.5f), localGrids);
                if (localGrids.empty()) continue;

                const glm::mat3 rot = glm::mat3_cast(ab.body->orientation);
                const glm::mat3 absRot(glm::abs(rot[0]), glm::abs(rot[1]), glm::abs(rot[2]));
                const auto& localBoxes = ab.body->getLocalBoxes();

                for (size_t bi = 0; bi < localBoxes.size(); ++bi) {
                    // Conservative world AABB of THIS box only.
                    const glm::vec3 c  = ab.body->position + rot * localBoxes[bi].offset;
                    const glm::vec3 he = absRot * localBoxes[bi].halfExtents + glm::vec3(expand);
                    for (VoxelOccupancyGrid* grid : localGrids) {
                        terrainBoxes.clear();
                        grid->queryAABB(c - he, c + he, terrainBoxes);
                        ++threadCalls[slot];
                        for (const OccupiedBox& tb : terrainBoxes)
                            VoxelContactSolver::generateOBBvsAABB(ab.body, bi, tb, buf);
                    }
                }
            }
        });

        for (auto& buf : threadBufs)
            m_contacts.insert(m_contacts.end(), buf.begin(), buf.end());
    }

    // §15.5: snapshot the terrain-broadphase cost (written at function end below).
    const double terrainMs =
        std::chrono::duration<double, std::milli>(Clock::now() - tTerrain0).count();

    // ---- Body vs kinematic obstacles (sequential — writes isAsleep) ----
    // Flatten every owner's boxes into the scratch buffer so dynamic bodies are deflected by the
    // UNION of all characters' segment boxes (not just the last one to call setKinematicObstacles).
    m_kinematicObstacles.clear();
    for (const auto& kv : m_obstaclesByOwner)
        m_kinematicObstacles.insert(m_kinematicObstacles.end(), kv.second.begin(), kv.second.end());
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

                OccupiedBox box{obs.center, obs.halfExtents};
                size_t before = m_contacts.size();
                for (size_t bi = 0; bi < body->getLocalBoxes().size(); ++bi)
                    VoxelContactSolver::generateOBBvsAABB(body.get(), bi, box, m_contacts);

                // Wake ONLY on a real generated contact from a MOVING obstacle.
                // The old rule woke (and reset the sleep timer of) any body whose
                // AABB merely overlapped an obstacle — a character standing near a
                // felled tree held its huge fragment awake forever, and the awake
                // body's per-frame contact generation ate ~870 ms/frame.
                const bool touched = m_contacts.size() > before;
                const bool obsMoving = glm::dot(obs.velocity, obs.velocity) > 1e-4f;
                if (touched && body->isAsleep && obsMoving) {
                    body->isAsleep   = false;
                    body->sleepTimer = 0.0f;
                }
                if (body->isAsleep) {
                    m_contacts.resize(before);   // sleeping body: discard, nothing to solve
                    continue;
                }
                for (size_t k = before; k < m_contacts.size(); ++k)
                    m_contacts[k].obstacleVelocity = obs.velocity;
            }
        }
    }

    // §15.5: record the substep snapshot (both this early-out and the natural end below).
    // queryAABBCalls is now the ACTUAL count from the indexed path (U1a) — post-fix it is
    // ~boxes × (chunks the body touches), NOT boxes × gridCount. gridCount stays reported as
    // context so the regression test can assert calls do NOT scale with it.
    uint64_t actualCalls = 0;
    for (uint64_t c : threadCalls) actualCalls += c;
    auto recordStats = [&]() {
        m_broadphaseStats.terrainBroadphaseMs = terrainMs;
        m_broadphaseStats.generateContactsMs  =
            std::chrono::duration<double, std::milli>(Clock::now() - tGen0).count();
        m_broadphaseStats.queryAABBCalls    = actualCalls;
        m_broadphaseStats.gridCount         = m_grids.size();
        m_broadphaseStats.awakeBodies       = na;
        m_broadphaseStats.awakeBoxes        = awakeBoxes;
        m_broadphaseStats.contactsGenerated = m_contacts.size();
    };

    // ---- Body vs body: spatial hash broadphase ----
    // Buckets bodies into 3-D cells of CELL_SIZE. Only pairs sharing a cell are
    // tested, reducing average complexity from O(N²) to O(N) for sparse scenes.
    if (na < 2) { recordStats(); return; }
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

    recordStats();   // §15.5 broadphase profiling
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
            // Position fallback: still in place for SLEEP_POS_TIME → sleep, even
            // when solver jitter keeps the velocity test from ever passing.
            if (!body->isAsleep) {
                const glm::vec3 d = body->position - body->sleepRefPos;
                if (glm::dot(d, d) < VoxelRigidBody::SLEEP_POS_EPS * VoxelRigidBody::SLEEP_POS_EPS) {
                    body->sleepPosTimer += dt;
                    if (body->sleepPosTimer >= VoxelRigidBody::SLEEP_POS_TIME) {
                        body->isAsleep        = true;
                        body->linearVelocity  = glm::vec3(0.0f);
                        body->angularVelocity = glm::vec3(0.0f);
                    }
                } else {
                    body->sleepRefPos   = body->position;
                    body->sleepPosTimer = 0.0f;
                }
            }
        }
    });
}

// ---- Queries ----------------------------------------------------------------

float VoxelDynamicsWorld::findGroundY(const glm::vec3& feetPos, float halfWidth, float maxSearchDown) const {
    // Subtract a tiny epsilon so the Y upper bound is exclusive: a voxel whose
    // bottom face sits exactly at feetPos.y (i.e. a wall beside the character)
    // is not treated as ground.  Without this, floor(feetPos.y) includes the
    // cell starting AT feetPos.y, causing upward snap ghosts when the character
    // stands adjacent to a wall whose base equals the current surface height.
    glm::vec3 queryMin(feetPos.x - halfWidth, feetPos.y - maxSearchDown, feetPos.z - halfWidth);
    glm::vec3 queryMax(feetPos.x + halfWidth, feetPos.y - 1e-4f,         feetPos.z + halfWidth);

    float best = -std::numeric_limits<float>::max();
    bool  found = false;
    std::vector<VoxelOccupancyGrid*> grids;                 // U1a: only chunks under the column
    gatherGridsOverlapping(queryMin, queryMax, grids);
    for (auto* grid : grids) {
        std::vector<OccupiedBox> boxes;
        grid->queryAABB(queryMin, queryMax, boxes);
        for (const auto& b : boxes) {
            float surfY = b.center.y + b.halfExtents.y;
            if (surfY > best) { best = surfY; found = true; }
        }
    }
    return found ? best : -std::numeric_limits<float>::max();
}

float VoxelDynamicsWorld::groundHeight(const glm::vec3& feetPos, float halfWidth, float maxSearchDown) const {
    // Static terrain answer first, then raise it if a dynamic body supports higher.
    float best = findGroundY(feetPos, halfWidth, maxSearchDown);

    const float yLo  = feetPos.y - maxSearchDown;
    const float yHi  = feetPos.y - 1e-4f;            // matches findGroundY's exclusive top
    const float xMin = feetPos.x - halfWidth, xMax = feetPos.x + halfWidth;
    const float zMin = feetPos.z - halfWidth, zMax = feetPos.z + halfWidth;

    // Dynamic rigid bodies ONLY (furniture, debris). NOT kinematic obstacles —
    // those are character segment boxes; including them lets a character detect
    // its own body as ground. The character is kinematic (never in m_bodies), so
    // this can never self-detect.
    //
    // Per ORIENTED box, via exact down-rays: neither the whole-body AABB roof
    // (crown-height phantom over a fallen tree) nor per-box conservative AABBs
    // (upright-merged slabs inflate into multi-meter platforms once the body
    // rotates — live: characters levitating 3.7m over a fallen birch) give the
    // real standing surface. Five vertical rays through the character column
    // (center + corners) against each OBB in its local frame.
    const glm::vec2 rayXZ[5] = {
        {feetPos.x, feetPos.z},
        {xMin, zMin}, {xMin, zMax}, {xMax, zMin}, {xMax, zMax}
    };
    for (const auto& body : m_bodies) {
        if (body->isDead) continue;
        glm::vec3 bMin, bMax;
        body->getWorldAABB(bMin, bMax);
        if (bMax.x < xMin || bMin.x > xMax) continue;       // XZ column overlap (broad reject)
        if (bMax.z < zMin || bMin.z > zMax) continue;
        if (bMin.y >= yHi || bMax.y <= yLo) continue;       // straddles the band below the feet
        const glm::mat3 rot    = glm::mat3_cast(body->orientation);
        const glm::mat3 rotInv = glm::transpose(rot);
        for (const auto& lb : body->getLocalBoxes()) {
            const glm::vec3 c = body->position + rot * lb.offset;
            const glm::vec3& he = lb.halfExtents;
            for (const auto& xz : rayXZ) {
                // Ray from the top of the band straight down, in OBB local space.
                const glm::vec3 o = rotInv * (glm::vec3(xz.x, yHi, xz.y) - c);
                const glm::vec3 d = rotInv * glm::vec3(0.0f, -1.0f, 0.0f);
                float t0 = 0.0f, t1 = yHi - yLo;    // search band length
                bool hit = true;
                for (int a = 0; a < 3 && hit; ++a) {
                    if (std::fabs(d[a]) < 1e-8f) {
                        if (o[a] < -he[a] || o[a] > he[a]) hit = false;
                        continue;
                    }
                    float ta = (-he[a] - o[a]) / d[a];
                    float tb = ( he[a] - o[a]) / d[a];
                    if (ta > tb) std::swap(ta, tb);
                    t0 = std::max(t0, ta);
                    t1 = std::min(t1, tb);
                    if (t0 > t1) hit = false;
                }
                if (!hit) continue;
                const float top = yHi - t0;          // world y where the ray enters the box
                if (top > best && top > yLo) best = top;
            }
        }
    }
    return best;
}

bool VoxelDynamicsWorld::overlapsTerrain(const glm::vec3& center, const glm::vec3& halfExtents) const {
    glm::vec3 queryMin = center - halfExtents;
    glm::vec3 queryMax = center + halfExtents;
    std::vector<VoxelOccupancyGrid*> grids;                 // U1a: only overlapping chunks
    gatherGridsOverlapping(queryMin, queryMax, grids);
    for (auto* grid : grids) {
        std::vector<OccupiedBox> boxes;
        grid->queryAABB(queryMin, queryMax, boxes);
        if (!boxes.empty()) return true;
    }
    return false;
}

bool VoxelDynamicsWorld::overlapsAnyBody(const glm::vec3& center, const glm::vec3& halfExtents) const {
    glm::vec3 qMin = center - halfExtents;
    glm::vec3 qMax = center + halfExtents;
    std::vector<ContactPoint> scratch;
    for (const auto& body : m_bodies) {
        if (body->isDead || body->invMass == 0.0f) continue;
        glm::vec3 bMin, bMax;
        body->getWorldAABB(bMin, bMax);
        // Whole-body AABB is only the BROAD reject. Blocking on it stops the
        // character at a multi-box body's invisible envelope — a fallen tree's
        // AABB spans trunk + branches, and the player was stopped ~2m from the
        // visible wood in open air (live). And per-box CONSERVATIVE AABBs are
        // not enough either: greedy-merged slabs on a ROTATED (fallen) body
        // inflate into multi-meter phantom platforms (live: an invisible 6x3m
        // floor 5m up, measured via /api/debug/body_boxes — he 3.68x1.46x3.69
        // from an upright-merged crown slab). Use the solver's exact
        // OBB-vs-AABB test per box.
        if (qMax.x <= bMin.x || qMin.x >= bMax.x ||
            qMax.y <= bMin.y || qMin.y >= bMax.y ||
            qMax.z <= bMin.z || qMin.z >= bMax.z)
            continue;
        const glm::mat3 rot = glm::mat3_cast(body->orientation);
        const glm::mat3 absRot(glm::abs(rot[0]), glm::abs(rot[1]), glm::abs(rot[2]));
        const OccupiedBox query{center, halfExtents};
        for (size_t bi = 0; bi < body->getLocalBoxes().size(); ++bi) {
            const auto& lb = body->getLocalBoxes()[bi];
            const glm::vec3 c  = body->position + rot * lb.offset;
            const glm::vec3 he = absRot * lb.halfExtents;
            // cheap conservative pre-reject, then the exact oriented test
            if (qMax.x <= c.x - he.x || qMin.x >= c.x + he.x ||
                qMax.y <= c.y - he.y || qMin.y >= c.y + he.y ||
                qMax.z <= c.z - he.z || qMin.z >= c.z + he.z)
                continue;
            scratch.clear();
            if (VoxelContactSolver::generateOBBvsAABB(body.get(), bi, query, scratch) > 0)
                return true;
        }
    }
    return false;
}

void VoxelDynamicsWorld::setKinematicObstacles(const void* owner, std::vector<KinematicObstacle> obstacles) {
    if (obstacles.empty()) m_obstaclesByOwner.erase(owner);
    else                   m_obstaclesByOwner[owner] = std::move(obstacles);
}

void VoxelDynamicsWorld::removeKinematicObstacles(const void* owner) {
    m_obstaclesByOwner.erase(owner);
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
