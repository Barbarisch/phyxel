#include "core/NPCManager.h"
#include "core/NavGrid.h"
#include "core/AStarPathfinder.h"
#include "core/ChunkManager.h"
#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include <chrono>
#include "scene/behaviors/IdleBehavior.h"
#include "scene/behaviors/PatrolBehavior.h"
#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "scene/behaviors/ScheduledBehavior.h"
#include "scene/behaviors/StoryDrivenBehavior.h"
#include "scene/behaviors/CombatBehavior.h"
#include "ai/Schedule.h"
#include "core/EntityRegistry.h"
#include "physics/PhysicsWorld.h"
#include "graphics/LightManager.h"
#include "graphics/DayNightCycle.h"
#include "graphics/AnimationSystem.h"
#include "utils/Logger.h"
#include <limits>

namespace Phyxel {
namespace Core {

NPCManager::~NPCManager() = default;

namespace {
/// Shared by both spawn paths. Returns false when the caller must NOT spawn.
/// Mutates `position` to the resolved one when the request was embedded.
bool applySpawnGate(const SolidAABBFn& solid, bool allowEmbedded, const std::string& name,
                    glm::vec3& position) {
    if (allowEmbedded || !solid) return true;   // escape hatch, or no world to check against
    const SpawnResult sr = resolveSpawn(solid, position);
    if (!sr.ok()) {
        LOG_ERROR("NPCManager", "Refusing to spawn NPC '{}': {}", name, sr.reason);
        return false;
    }
    if (sr.outcome == SpawnOutcome::Relocated) {
        LOG_WARN("NPCManager", "NPC '{}' spawn adjusted: {}", name, sr.reason);
        position = sr.position;
    }
    return true;
}

/// SECOND PASS, with the body that actually exists. applySpawnGate necessarily runs
/// BEFORE construction, so it can only assume the humanoid default -- but a species'
/// real capsule is resolved by resizeController() during construction, from the loaded
/// skeleton. For fauna (BodyPlan clamps half-width to [0.12, 0.60]) that default is
/// wrong by up to 2.4x, so a gap a humanoid fits can leave a wolf or dragon embedded in
/// the walls either side. Re-check here and reposition; refuse only if the real body
/// cannot be placed at all. No-op for anything close to humanoid.
bool verifySpawnForRealBody(const SolidAABBFn& solid, bool allowEmbedded,
                            const std::string& name, Scene::NPCEntity* npc) {
    if (allowEmbedded || !solid || !npc) return true;
    auto* ch = npc->getAnimatedCharacter();
    if (!ch) return true;
    CharacterBounds body;
    body.halfWidth = ch->getControllerHalfWidth();
    body.height = ch->getControllerHalfHeight() * 2.0f;
    const glm::vec3 at = npc->getPosition();
    const SpawnResult sr = verifyPlacedBody(solid, at, body);
    if (sr.outcome == SpawnOutcome::Clear) return true;   // the common case: nothing to do
    if (!sr.ok()) {
        LOG_ERROR("NPCManager", "Refusing to spawn '{}': its REAL body ({}m half-width, {}m "
                  "tall) is inside static geometry and no clear position was found",
                  name, body.halfWidth, body.height);
        return false;
    }
    LOG_WARN("NPCManager", "'{}' re-placed for its real body ({}m half-width): {}",
             name, body.halfWidth, sr.reason);
    npc->setPosition(sr.position);
    return true;
}

}  // namespace

SolidAABBFn NPCManager::staticSolidQuery() const {
    Physics::VoxelDynamicsWorld* vw = m_physicsWorld ? m_physicsWorld->getVoxelWorld() : nullptr;
    if (!vw) return {};   // no world to check against -> the gate stands down (see SpawnGate.h)
    return [vw](const glm::vec3& lo, const glm::vec3& hi) {
        return vw->anyStaticSolidInAABB(lo, hi);
    };
}

Scene::NPCEntity* NPCManager::spawnNPC(const std::string& name, const std::string& animFile,
                                        const glm::vec3& position, NPCBehaviorType behaviorType,
                                        const std::vector<glm::vec3>& waypoints,
                                        float walkSpeed, float waitTime,
                                        const Scene::CharacterAppearance& appearance) {
    std::unique_ptr<Scene::NPCBehavior> behavior;

    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::Wander: {
            // Roam near the spawn anchor. Default radius here; FaunaSpawner
            // overrides per-species via spawnNPCWithBehavior.
            auto patrol = std::make_unique<Scene::PatrolBehavior>(
                std::vector<glm::vec3>{}, walkSpeed, waitTime);
            patrol->setWanderMode(position, 12.0f);
            behavior = std::move(patrol);
            break;
        }
        case NPCBehaviorType::BehaviorTree:
            behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
            break;
        case NPCBehaviorType::Scheduled:
            behavior = std::make_unique<Scene::ScheduledBehavior>(AI::Schedule::defaultSchedule());
            break;
        case NPCBehaviorType::Combat:
            behavior = std::make_unique<Scene::CombatBehavior>();
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }

    return spawnNPCWithBehavior(name, animFile, position, std::move(behavior), appearance);
}

Scene::NPCEntity* NPCManager::spawnNPCWithBehavior(const std::string& name, const std::string& animFile,
                                                     const glm::vec3& position,
                                                     std::unique_ptr<Scene::NPCBehavior> behavior,
                                                     const Scene::CharacterAppearance& appearance) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }

    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    // SPAWN GATE: never create a character inside static geometry (see SpawnGate.h).
    glm::vec3 spawnPos = position;
    if (!applySpawnGate(staticSolidQuery(), m_allowEmbeddedSpawns, name, spawnPos))
        return nullptr;

    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, spawnPos, name, animFile, appearance);
    npc->setBehavior(std::move(behavior));

    // Wire pathfinder to PatrolBehavior if available
    if (m_pathfinder) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
        }
    }

    // Register with EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }

    // Wire context
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry, m_chunkManager, m_raycastVisualizer);
    npc->setCombatSystem(m_combatSystem);
    if (m_navGraph) npc->setNavGraph(m_navGraph.get());
    if (m_pathService) npc->setPathService(m_pathService.get());

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    // The gate ran pre-construction against the humanoid default; the species' real
    // capsule exists only now, so check the body that actually got built.
    if (!verifySpawnForRealBody(staticSolidQuery(), m_allowEmbeddedSpawns, name, rawPtr)) {
        // removeNPC, NOT a bare m_npcs.erase: registerEntity() was handed this entity's
        // raw pointer ABOVE, so erasing the unique_ptr alone frees the object while
        // EntityRegistry keeps pointing at it -- a use-after-free for any registry
        // lookup/iteration, and it also poisons the id (registerEntity rejects
        // duplicates, and its return is unchecked here). removeNPC unregisters, drops
        // the attached light, then erases. (solution-auditor, round 7.)
        removeNPC(name);
        return nullptr;
    }

    LOG_INFO("NPCManager", "Spawned NPC '{}' at ({}, {}, {})", name, spawnPos.x, spawnPos.y, spawnPos.z);
    return rawPtr;
}

bool NPCManager::removeNPC(const std::string& name) {
    auto it = m_npcs.find(name);
    if (it == m_npcs.end()) return false;

    // Unregister from EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->unregisterEntity(entityId);
    }

    // Remove attached light
    if (it->second->getAttachedLightId() >= 0 && m_lightManager) {
        m_lightManager->removeLight(it->second->getAttachedLightId());
    }

    m_npcs.erase(it);
    LOG_INFO("NPCManager", "Removed NPC '{}'", name);
    return true;
}

Scene::NPCEntity* NPCManager::getNPC(const std::string& name) const {
    auto it = m_npcs.find(name);
    return (it != m_npcs.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> NPCManager::getAllNPCNames() const {
    std::vector<std::string> names;
    names.reserve(m_npcs.size());
    for (const auto& [name, _] : m_npcs) {
        names.push_back(name);
    }
    return names;
}

void NPCManager::update(float deltaTime) {
    using Clock = std::chrono::high_resolution_clock;
    const auto tSepStart = Clock::now();
    m_updateStats = UpdateStats{};
    m_updateStats.npcCount = static_cast<uint32_t>(m_npcs.size());
    Scene::AnimatedVoxelCharacter::consumeFullUpdateCount();   // reset for this frame
    Scene::AnimatedVoxelCharacter::beginFrame(
        Scene::AnimatedVoxelCharacter::getUpdateTickBudget());

    // Separation pushes: NPCs have no local avoidance, so simultaneous schedule
    // transitions pile bodies into pinch points (measured: an 11-NPC jam at the
    // tavern-corner fence). XZ repulsion published to each behavior's blackboard
    // ("sepPush"); movers blend it into their steering.
    //
    // This was O(n²), which the original comment flagged as needing "a spatial grid
    // before city-scale populations". Measured at n=1024: 524k pairs/frame cost ~42 ms
    // — by far the largest single cost in the frame, and completely invisible at the
    // village scale (n≈14) it was written for. Now a uniform XZ hash grid at the
    // interaction radius, so each NPC only tests the 3x3 cells around it: O(n) for
    // realistic densities, and identical results (same pairs, same pushes).
    {
        constexpr float kSepRadius = 1.4f;
        const size_t n = m_npcs.size();

        std::vector<Scene::NPCEntity*> ents;
        std::vector<glm::vec3> pos;
        std::vector<glm::vec3> push(n, glm::vec3(0.0f));
        ents.reserve(n); pos.reserve(n);
        for (auto& [name, npc] : m_npcs) {
            ents.push_back(npc.get());
            pos.push_back(npc->getPosition());
        }

        // Cell size == radius, so any pair within kSepRadius shares or neighbours a cell.
        auto cellKey = [](int cx, int cz) -> int64_t {
            return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cz);
        };
        std::unordered_map<int64_t, std::vector<int>> grid;
        grid.reserve(n * 2);
        std::vector<std::pair<int,int>> cell(n);
        for (size_t i = 0; i < n; ++i) {
            const int cx = static_cast<int>(std::floor(pos[i].x / kSepRadius));
            const int cz = static_cast<int>(std::floor(pos[i].z / kSepRadius));
            cell[i] = {cx, cz};
            grid[cellKey(cx, cz)].push_back(static_cast<int>(i));
        }

        for (size_t i = 0; i < n; ++i) {
            const auto [cx, cz] = cell[i];
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    auto it = grid.find(cellKey(cx + dx, cz + dz));
                    if (it == grid.end()) continue;
                    for (int j : it->second) {
                        if (static_cast<size_t>(j) <= i) continue;   // each pair once
                        glm::vec2 d(pos[i].x - pos[j].x, pos[i].z - pos[j].z);
                        float dist = glm::length(d);
                        if (dist >= kSepRadius) continue;
                        const glm::vec2 dir = dist > 0.01f
                            ? d / dist
                            : glm::vec2(((i * 2654435761u) % 7) / 3.5f - 1.0f, 1.0f);  // coincident: split deterministically
                        const float p = (kSepRadius - dist) / kSepRadius;              // 0..1
                        push[i] += glm::vec3(dir.x, 0.0f, dir.y) * p;
                        push[j] -= glm::vec3(dir.x, 0.0f, dir.y) * p;
                    }
                }
            }
        }

        for (size_t i = 0; i < n; ++i)
            if (auto* bt = dynamic_cast<Scene::BehaviorTreeBehavior*>(ents[i]->getBehavior()))
                bt->getBlackboard().set("sepPush", push[i]);
    }

    m_updateStats.separationMs =
        std::chrono::duration<double, std::milli>(Clock::now() - tSepStart).count();

    // Split behaviour from character update: the behaviour tick is NOT update-LOD
    // gated, so a distant crowd can still cost full price there even when every
    // character defers its pose evaluation.
    const auto tUpdStart = Clock::now();
    for (auto& [name, npc] : m_npcs) {
        npc->update(deltaTime);
    }
    m_updateStats.characterMs =
        std::chrono::duration<double, std::milli>(Clock::now() - tUpdStart).count();
    m_updateStats.fullCharacterTicks = Scene::AnimatedVoxelCharacter::consumeFullUpdateCount();

    // Social simulation tick (runs at reduced frequency)
    m_socialTickTimer -= deltaTime;
    if (m_socialTickTimer <= 0.0f) {
        m_socialTickTimer = SOCIAL_TICK_INTERVAL;

        // Convert real seconds to game hours for social system update
        float deltaHours = 0.0f;
        if (m_dayNightCycle) {
            float dayLen = m_dayNightCycle->getDayLengthSeconds();
            if (dayLen > 0.0f) {
                deltaHours = SOCIAL_TICK_INTERVAL * (24.0f / dayLen) * m_dayNightCycle->getTimeScale();
            }
        }

        if (deltaHours > 0.0f) {
            // Update per-NPC needs and worldview decay
            for (auto& [name, npc] : m_npcs) {
                npc->getNeeds().update(deltaHours);
                npc->getWorldView().update(deltaHours);
            }

            // Decay relationships toward neutral
            m_relationships.update(deltaHours);

            // Build participant list and run social interactions
            std::vector<AI::SocialParticipant> participants;
            for (auto& [name, npc] : m_npcs) {
                AI::SocialParticipant p;
                p.id = name;
                p.position = npc->getPosition();
                p.needs = &npc->getNeeds();
                p.worldView = &npc->getWorldView();
                // Read currentActivity from behavior blackboard if available
                if (auto* btBehavior = dynamic_cast<Scene::BehaviorTreeBehavior*>(npc->getBehavior())) {
                    p.currentActivity = btBehavior->getBlackboard().getString("currentActivity", "Wander");
                }
                participants.push_back(p);
            }
            m_socialSystem.update(deltaHours, participants, m_relationships);
        }
    }
}

void NPCManager::buildNavGrid() {
    if (!m_chunkManager) {
        LOG_WARN("NPCManager", "Cannot build NavGrid: ChunkManager not set");
        return;
    }

    // Determine XZ bounds from all loaded chunk origins
    if (m_chunkManager->chunkMap.empty()) {
        LOG_WARN("NPCManager", "Cannot build NavGrid: no chunks loaded");
        return;
    }

    glm::ivec2 minXZ(std::numeric_limits<int>::max());
    glm::ivec2 maxXZ(std::numeric_limits<int>::min());
    for (const auto& [coord, chunk] : m_chunkManager->chunkMap) {
        glm::ivec3 origin = ChunkManager::chunkCoordToOrigin(coord);
        minXZ.x = std::min(minXZ.x, origin.x);
        minXZ.y = std::min(minXZ.y, origin.z);
        maxXZ.x = std::max(maxXZ.x, origin.x + 31);
        maxXZ.y = std::max(maxXZ.y, origin.z + 31);
    }

    // Guard against an unbounded build. Streaming persists chunks across the whole
    // explored world, so the bounding box of all loaded chunks can span thousands of cells
    // per axis; NavGrid + NavGraph build one cell per (x,z) column, making this O(area) —
    // it effectively hangs (e.g. a streamed world spanning ~1500x1300 columns never
    // finishes). Cap the region to a sane extent centred on the bounds so the build stays
    // bounded. NPCs outside the capped region get no nav until per-region/streamed nav lands.
    constexpr int kMaxNavHalfExtent = 256; // -> at most 512x512 columns
    const glm::ivec2 span = maxXZ - minXZ;
    if (span.x > 2 * kMaxNavHalfExtent || span.y > 2 * kMaxNavHalfExtent) {
        const glm::ivec2 center = (minXZ + maxXZ) / 2;
        minXZ = center - glm::ivec2(kMaxNavHalfExtent);
        maxXZ = center + glm::ivec2(kMaxNavHalfExtent);
        LOG_WARN_FMT("NPCManager", "NavGrid region clamped to [" << minXZ.x << "," << minXZ.y
                     << "]..[" << maxXZ.x << "," << maxXZ.y << "] - loaded chunks span too far "
                     "(streamed world?); NPCs outside this region have no nav");
    }

    m_navGrid = std::make_unique<NavGrid>(m_chunkManager);
    m_navGrid->buildFromRegion(minXZ, maxXZ);
    m_pathfinder = std::make_unique<AStarPathfinder>(m_navGrid.get());

    // 3D surface graph (Layer 1) over the same region — used by path-following behaviors
    // (StoryDrivenBehavior) so autonomous/routine NPCs route around obstacles + edges.
    // Stop any prior path service before replacing the graph it reads, then bring up a
    // fresh one pointing at the new graph.
    if (m_pathService) m_pathService->stop();

    // Rasterize static NON-VOXEL obstacles (placed templates: wells, woodpiles,
    // furniture) into a blocked-cell set the graph treats as solid. Rebuilt here —
    // AFTER the old path service stopped, BEFORE the new graph builds — so no worker
    // reads the set while it changes. Oversized boxes are skipped, not truncated.
    m_navObstacles.clear();
    if (m_obstacleProvider) {
        constexpr int64_t kMaxBoxCells = 32768;   // a 32^3 box; bigger = likely a structure, skip
        auto packCell = [](const glm::ivec3& p) -> int64_t {
            return (static_cast<int64_t>(static_cast<uint16_t>(p.y)) << 48) |
                   (static_cast<int64_t>(static_cast<uint32_t>(p.x) & 0xFFFFFF) << 24) |
                   static_cast<int64_t>(static_cast<uint32_t>(p.z) & 0xFFFFFF);
        };
        int skipped = 0;
        for (const auto& [lo, hi] : m_obstacleProvider()) {
            const int64_t vol = static_cast<int64_t>(hi.x - lo.x + 1) *
                                (hi.y - lo.y + 1) * (hi.z - lo.z + 1);
            if (vol <= 0 || vol > kMaxBoxCells) { ++skipped; continue; }
            for (int x = lo.x; x <= hi.x; ++x)
                for (int y = lo.y; y <= hi.y; ++y)
                    for (int z = lo.z; z <= hi.z; ++z)
                        m_navObstacles.insert(packCell(glm::ivec3(x, y, z)));
        }
        if (!m_navObstacles.empty() || skipped > 0)
            LOG_INFO_FMT("NPCManager", "nav obstacles: " << m_navObstacles.size()
                         << " blocked cells (" << skipped << " oversized boxes skipped)");
    }

    // Composite nav solidity = what characters actually collide with:
    //   1. cube voxels (hasVoxelAt),
    //   2. the static-occupancy grids' UPPER-CELL content — micro-thin geometry
    //      (parcel fences) fills a cell's height and must block, while thin FLOOR
    //      sheets (street paving micros in the bottom of a cell) must stay
    //      walkable, so only content above ~step height (y+0.34) blocks;
    //   3. the placed-object obstacle overlay (wells, furniture — not voxels at all).
    // Without 2 and 3 the graph routed straight through fences/props and NPCs
    // treadmilled against their collision (measured live, 6/14 never converged).
    {
        Physics::VoxelDynamicsWorld* vw =
            m_physicsWorld ? m_physicsWorld->getVoxelWorld() : nullptr;
        auto packCell = [](const glm::ivec3& p) -> int64_t {
            return (static_cast<int64_t>(static_cast<uint16_t>(p.y)) << 48) |
                   (static_cast<int64_t>(static_cast<uint32_t>(p.x) & 0xFFFFFF) << 24) |
                   static_cast<int64_t>(static_cast<uint32_t>(p.z) & 0xFFFFFF);
        };
        if (vw || !m_navObstacles.empty()) {
            m_navGraph = std::make_unique<NavGraph>(
                VoxelQueryFunc([this, vw, packCell](const glm::ivec3& p) {
                    if (m_chunkManager && m_chunkManager->hasVoxelAt(p)) return true;
                    if (!m_navObstacles.empty() && m_navObstacles.count(packCell(p)) > 0)
                        return true;
                    if (vw) {
                        const glm::vec3 lo(static_cast<float>(p.x), p.y + 0.34f,
                                           static_cast<float>(p.z));
                        const glm::vec3 hi(p.x + 1.0f, p.y + 1.0f, p.z + 1.0f);
                        if (vw->anyStaticSolidInAABB(lo, hi)) return true;
                    }
                    return false;
                }));
        } else {
            m_navGraph = std::make_unique<NavGraph>(m_chunkManager);
        }
    }
    m_navGraph->buildRegion(minXZ, maxXZ, NavAgentProfile{});
    m_pathService = std::make_unique<PathService>(m_navGraph.get());
    m_pathService->start();

    // Re-wire all PatrolBehaviors to the new pathfinder and invalidate stale paths.
    // If an NPC is on a nearWall cell (physics body would clip the wall), 
    // relocate it to the nearest safe cell center.
    for (auto& [name, npc] : m_npcs) {
        npc->setNavGraph(m_navGraph.get());
        npc->setPathService(m_pathService.get());
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
            patrol->invalidatePath();
        }

        // Check if NPC is stuck on a nearWall cell and relocate
        glm::vec3 pos = npc->getPosition();
        const NavCell* cell = m_navGrid->getCell(
            static_cast<int>(std::floor(pos.x)),
            static_cast<int>(std::floor(pos.z)));
        if (cell && cell->nearWall) {
            const NavCell* safe = m_navGrid->findNearestNonWall(pos);
            if (safe) {
                glm::vec3 safePos(
                    static_cast<float>(safe->x) + 0.5f,
                    static_cast<float>(safe->surfaceY) + 1.0f,
                    static_cast<float>(safe->z) + 0.5f);
                npc->setPosition(safePos);
                LOG_INFO("NPCManager", "Relocated NPC '{}' from nearWall ({},{}) to ({},{},{})",
                         name, cell->x, cell->z, safePos.x, safePos.y, safePos.z);
            }
        }
    }

    LOG_INFO("NPCManager", "Built NavGrid: XZ [{},{}] to [{},{}], {} cells ({} walkable)",
             minXZ.x, minXZ.y, maxXZ.x, maxXZ.y,
             m_navGrid->cellCount(), m_navGrid->walkableCellCount());
}

namespace {
// XZ distance² from cell-center (cx,cz) to the polyline `wps`. Used to tell whether a
// terrain change crosses a route the NPC is actually walking (smoothed waypoints can be
// far apart, so we test segments, not just vertices).
float pathDist2ToColumn(const std::vector<glm::vec3>& wps, int cx, int cz) {
    if (wps.empty()) return 1e30f;
    const float px = cx + 0.5f, pz = cz + 0.5f;
    if (wps.size() == 1) {
        const float dx = px - wps[0].x, dz = pz - wps[0].z;
        return dx * dx + dz * dz;
    }
    float best = 1e30f;
    for (size_t i = 1; i < wps.size(); ++i) {
        const float ax = wps[i - 1].x, az = wps[i - 1].z;
        const float bx = wps[i].x,     bz = wps[i].z;
        const float dx = bx - ax, dz = bz - az;
        const float len2 = dx * dx + dz * dz;
        float t = len2 > 1e-6f ? ((px - ax) * dx + (pz - az) * dz) / len2 : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));
        const float qx = ax + t * dx, qz = az + t * dz;
        const float ex = px - qx, ez = pz - qz;
        best = std::min(best, ex * ex + ez * ez);
    }
    return best;
}
constexpr float kReplanRadius = 1.5f;   // cells; route within this of a change → replan
} // namespace

void NPCManager::onVoxelChanged(const glm::ivec3& worldPos) {
    if (!m_navGrid) return;
    m_navGrid->rebuildCell(worldPos.x, worldPos.z);
    if (m_navGraph) m_navGraph->rebuildColumn(worldPos.x, worldPos.z, NavAgentProfile{});
    // Cached paths through the changed column are now stale.
    if (m_pathService) m_pathService->invalidateCacheNear(worldPos.x, worldPos.z, 1);

    // Phase 2 — Immediate path invalidation: any NPC whose current path passes
    // within ~1 block of the changed cell is stale and must replan now, not after 1.5s.
    // Phase 6 TODO: also post WorldEvent::TerrainChanged(worldPos) to WorldEventBus here
    // so the stimulus-response layer can react behaviourally before the path replan.
    for (auto& [name, npc] : m_npcs) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            for (const glm::vec3& wp : patrol->getPathNodes()) {
                int wx = static_cast<int>(std::floor(wp.x));
                int wz = static_cast<int>(std::floor(wp.z));
                if (std::abs(wx - worldPos.x) <= 1 && std::abs(wz - worldPos.z) <= 1) {
                    patrol->invalidatePath();
                    break;
                }
            }
        } else if (auto* story = dynamic_cast<Scene::StoryDrivenBehavior*>(npc->getBehavior())) {
            if (pathDist2ToColumn(story->getPathWaypoints(), worldPos.x, worldPos.z)
                    <= kReplanRadius * kReplanRadius) {
                story->invalidatePath();
            }
        }
    }
}

void NPCManager::onRegionChanged(const glm::ivec3& minPos, const glm::ivec3& maxPos) {
    if (!m_navGrid) return;
    m_navGrid->rebuildRegion(minPos.x, minPos.z, maxPos.x, maxPos.z);
    if (m_navGraph) {
        for (int x = minPos.x; x <= maxPos.x; ++x)
            for (int z = minPos.z; z <= maxPos.z; ++z)
                m_navGraph->rebuildColumn(x, z, NavAgentProfile{});
    }
    // Drop cached paths crossing the changed box (+1 margin).
    if (m_pathService)
        m_pathService->invalidateCacheRegion({minPos.x, minPos.z}, {maxPos.x, maxPos.z}, 1);

    // Phase 2 — Invalidate paths for any NPC whose waypoints cross the changed region.
    for (auto& [name, npc] : m_npcs) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            for (const glm::vec3& wp : patrol->getPathNodes()) {
                int wx = static_cast<int>(std::floor(wp.x));
                int wz = static_cast<int>(std::floor(wp.z));
                if (wx >= minPos.x - 1 && wx <= maxPos.x + 1 &&
                    wz >= minPos.z - 1 && wz <= maxPos.z + 1) {
                    patrol->invalidatePath();
                    break;
                }
            }
        } else if (auto* story = dynamic_cast<Scene::StoryDrivenBehavior*>(npc->getBehavior())) {
            // Replan if any followed waypoint falls in the expanded box.
            for (const glm::vec3& wp : story->getPathWaypoints()) {
                int wx = static_cast<int>(std::floor(wp.x));
                int wz = static_cast<int>(std::floor(wp.z));
                if (wx >= minPos.x - 1 && wx <= maxPos.x + 1 &&
                    wz >= minPos.z - 1 && wz <= maxPos.z + 1) {
                    story->invalidatePath();
                    break;
                }
            }
        } else if (auto* sched = dynamic_cast<Scene::ScheduledBehavior*>(npc->getBehavior())) {
            // Same contract for the scheduled built-in mover.
            for (const glm::vec3& wp : sched->getPathWaypoints()) {
                int wx = static_cast<int>(std::floor(wp.x));
                int wz = static_cast<int>(std::floor(wp.z));
                if (wx >= minPos.x - 1 && wx <= maxPos.x + 1 &&
                    wz >= minPos.z - 1 && wz <= maxPos.z + 1) {
                    sched->invalidatePath();
                    break;
                }
            }
        }
    }
}

const NPCManager::AnimTemplate* NPCManager::getOrLoadTemplate(const std::string& animFile) {
    auto it = m_templateCache.find(animFile);
    if (it != m_templateCache.end()) {
        return &it->second;
    }

    // Load from file
    AnimTemplate tmpl;
    Phyxel::AnimationSystem animSys;
    if (!animSys.loadFromFile(animFile, tmpl.skeleton, tmpl.clips, tmpl.voxelModel)) {
        LOG_ERROR("NPCManager", "Failed to load template anim file: {}", animFile);
        return nullptr;
    }

    LOG_INFO("NPCManager", "Cached anim template '{}' ({} bones, {} shapes, {} clips)",
             animFile, tmpl.skeleton.bones.size(), tmpl.voxelModel.shapes.size(), tmpl.clips.size());

    auto [insertIt, _] = m_templateCache.emplace(animFile, std::move(tmpl));
    return &insertIt->second;
}

Scene::NPCEntity* NPCManager::spawnProceduralNPC(const std::string& name, const std::string& seedAnimFile,
                                                   const glm::vec3& position, NPCBehaviorType behaviorType,
                                                   const std::string& role,
                                                   const std::vector<glm::vec3>& waypoints,
                                                   float walkSpeed, float waitTime,
                                                   const Scene::CharacterAppearance& appearance) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }
    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    const AnimTemplate* tmpl = getOrLoadTemplate(seedAnimFile);
    if (!tmpl) {
        return nullptr;
    }

    // Generate appearance: use provided appearance, or auto-generate from name+role
    Scene::CharacterAppearance finalAppearance = appearance;
    if (!role.empty()) {
        auto morph = Scene::detectMorphology(tmpl->skeleton);
        finalAppearance = Scene::CharacterAppearance::generateFromSeed(name, role, morph);
    }

    // Create NPC entity with procedural skeleton (no file re-read)
    // SPAWN GATE: never create a character inside static geometry (see SpawnGate.h).
    glm::vec3 spawnPos = position;
    if (!applySpawnGate(staticSolidQuery(), m_allowEmbeddedSpawns, name, spawnPos))
        return nullptr;

    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, spawnPos, name, finalAppearance,
                                                   tmpl->skeleton, tmpl->voxelModel, tmpl->clips);

    // Set behavior
    std::unique_ptr<Scene::NPCBehavior> behavior;
    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::Wander: {
            // Roam near the spawn anchor. Default radius here; FaunaSpawner
            // overrides per-species via spawnNPCWithBehavior.
            auto patrol = std::make_unique<Scene::PatrolBehavior>(
                std::vector<glm::vec3>{}, walkSpeed, waitTime);
            patrol->setWanderMode(position, 12.0f);
            behavior = std::move(patrol);
            break;
        }
        case NPCBehaviorType::BehaviorTree:
            behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
            break;
        case NPCBehaviorType::Scheduled:
            behavior = std::make_unique<Scene::ScheduledBehavior>(AI::Schedule::defaultSchedule());
            break;
        case NPCBehaviorType::Combat:
            behavior = std::make_unique<Scene::CombatBehavior>();
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }
    npc->setBehavior(std::move(behavior));

    // Wire pathfinder to PatrolBehavior if available
    if (m_pathfinder) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
        }
    }

    // Register with EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry, m_chunkManager, m_raycastVisualizer);
    npc->setCombatSystem(m_combatSystem);
    if (m_navGraph) npc->setNavGraph(m_navGraph.get());
    if (m_pathService) npc->setPathService(m_pathService.get());

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    // The gate ran pre-construction against the humanoid default; the species' real
    // capsule exists only now, so check the body that actually got built.
    if (!verifySpawnForRealBody(staticSolidQuery(), m_allowEmbeddedSpawns, name, rawPtr)) {
        // removeNPC, NOT a bare m_npcs.erase: registerEntity() was handed this entity's
        // raw pointer ABOVE, so erasing the unique_ptr alone frees the object while
        // EntityRegistry keeps pointing at it -- a use-after-free for any registry
        // lookup/iteration, and it also poisons the id (registerEntity rejects
        // duplicates, and its return is unchecked here). removeNPC unregisters, drops
        // the attached light, then erases. (solution-auditor, round 7.)
        removeNPC(name);
        return nullptr;
    }

    LOG_INFO("NPCManager", "Spawned procedural NPC '{}' (role='{}') at ({}, {}, {})",
             name, role, position.x, position.y, position.z);
    return rawPtr;
}

Scene::NPCEntity* NPCManager::spawnPhysicsNPC(const std::string& name, const std::string& animFile,
                                                const glm::vec3& position, NPCBehaviorType behaviorType,
                                                const std::vector<glm::vec3>& waypoints,
                                                float walkSpeed, float waitTime,
                                                const Scene::CharacterAppearance& appearance) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }
    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    // SPAWN GATE: never create a character inside static geometry (see SpawnGate.h).
    glm::vec3 spawnPos = position;
    if (!applySpawnGate(staticSolidQuery(), m_allowEmbeddedSpawns, name, spawnPos))
        return nullptr;

    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, spawnPos, name, animFile, appearance, true);

    std::unique_ptr<Scene::NPCBehavior> behavior;
    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::Wander: {
            // Roam near the spawn anchor. Default radius here; FaunaSpawner
            // overrides per-species via spawnNPCWithBehavior.
            auto patrol = std::make_unique<Scene::PatrolBehavior>(
                std::vector<glm::vec3>{}, walkSpeed, waitTime);
            patrol->setWanderMode(position, 12.0f);
            behavior = std::move(patrol);
            break;
        }
        case NPCBehaviorType::BehaviorTree:
            behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
            break;
        case NPCBehaviorType::Scheduled:
            behavior = std::make_unique<Scene::ScheduledBehavior>(AI::Schedule::defaultSchedule());
            break;
        case NPCBehaviorType::Combat:
            behavior = std::make_unique<Scene::CombatBehavior>();
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }
    npc->setBehavior(std::move(behavior));

    // Wire pathfinder to PatrolBehavior if available
    if (m_pathfinder) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
        }
    }

    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry, m_chunkManager, m_raycastVisualizer);
    npc->setCombatSystem(m_combatSystem);
    if (m_navGraph) npc->setNavGraph(m_navGraph.get());
    if (m_pathService) npc->setPathService(m_pathService.get());

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    // The gate ran pre-construction against the humanoid default; the species' real
    // capsule exists only now, so check the body that actually got built.
    if (!verifySpawnForRealBody(staticSolidQuery(), m_allowEmbeddedSpawns, name, rawPtr)) {
        // removeNPC, NOT a bare m_npcs.erase: registerEntity() was handed this entity's
        // raw pointer ABOVE, so erasing the unique_ptr alone frees the object while
        // EntityRegistry keeps pointing at it -- a use-after-free for any registry
        // lookup/iteration, and it also poisons the id (registerEntity rejects
        // duplicates, and its return is unchecked here). removeNPC unregisters, drops
        // the attached light, then erases. (solution-auditor, round 7.)
        removeNPC(name);
        return nullptr;
    }

    LOG_INFO("NPCManager", "Spawned physics NPC '{}' at ({}, {}, {})", name, position.x, position.y, position.z);
    return rawPtr;
}

Scene::NPCEntity* NPCManager::spawnPhysicsProceduralNPC(const std::string& name, const std::string& seedAnimFile,
                                                          const glm::vec3& position, NPCBehaviorType behaviorType,
                                                          const std::string& role,
                                                          const std::vector<glm::vec3>& waypoints,
                                                          float walkSpeed, float waitTime,
                                                          const Scene::CharacterAppearance& appearance) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }
    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    const AnimTemplate* tmpl = getOrLoadTemplate(seedAnimFile);
    if (!tmpl) {
        return nullptr;
    }

    Scene::CharacterAppearance finalAppearance = appearance;
    if (!role.empty()) {
        auto morph = Scene::detectMorphology(tmpl->skeleton);
        finalAppearance = Scene::CharacterAppearance::generateFromSeed(name, role, morph);
    }

    // SPAWN GATE: never create a character inside static geometry (see SpawnGate.h).
    glm::vec3 spawnPos = position;
    if (!applySpawnGate(staticSolidQuery(), m_allowEmbeddedSpawns, name, spawnPos))
        return nullptr;

    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, spawnPos, name, finalAppearance,
                                                   tmpl->skeleton, tmpl->voxelModel, tmpl->clips, true);

    std::unique_ptr<Scene::NPCBehavior> behavior;
    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::Wander: {
            // Roam near the spawn anchor. Default radius here; FaunaSpawner
            // overrides per-species via spawnNPCWithBehavior.
            auto patrol = std::make_unique<Scene::PatrolBehavior>(
                std::vector<glm::vec3>{}, walkSpeed, waitTime);
            patrol->setWanderMode(position, 12.0f);
            behavior = std::move(patrol);
            break;
        }
        case NPCBehaviorType::BehaviorTree:
            behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
            break;
        case NPCBehaviorType::Scheduled:
            behavior = std::make_unique<Scene::ScheduledBehavior>(AI::Schedule::defaultSchedule());
            break;
        case NPCBehaviorType::Combat:
            behavior = std::make_unique<Scene::CombatBehavior>();
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }
    npc->setBehavior(std::move(behavior));

    // Wire pathfinder to PatrolBehavior if available
    if (m_pathfinder) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
        }
    }

    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry, m_chunkManager, m_raycastVisualizer);
    npc->setCombatSystem(m_combatSystem);
    if (m_navGraph) npc->setNavGraph(m_navGraph.get());
    if (m_pathService) npc->setPathService(m_pathService.get());

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    // The gate ran pre-construction against the humanoid default; the species' real
    // capsule exists only now, so check the body that actually got built.
    if (!verifySpawnForRealBody(staticSolidQuery(), m_allowEmbeddedSpawns, name, rawPtr)) {
        // removeNPC, NOT a bare m_npcs.erase: registerEntity() was handed this entity's
        // raw pointer ABOVE, so erasing the unique_ptr alone frees the object while
        // EntityRegistry keeps pointing at it -- a use-after-free for any registry
        // lookup/iteration, and it also poisons the id (registerEntity rejects
        // duplicates, and its return is unchecked here). removeNPC unregisters, drops
        // the attached light, then erases. (solution-auditor, round 7.)
        removeNPC(name);
        return nullptr;
    }

    LOG_INFO("NPCManager", "Spawned physics procedural NPC '{}' (role='{}') at ({}, {}, {})",
             name, role, position.x, position.y, position.z);
    return rawPtr;
}


} // namespace Core
} // namespace Phyxel
