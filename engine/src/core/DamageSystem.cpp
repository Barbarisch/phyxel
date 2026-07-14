#include "core/DamageSystem.h"
#include "core/ChunkManager.h"
#include "core/MaterialRegistry.h"
#include "core/GpuParticlePhysics.h"
#include "core/CoherentFragmentManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/Cube.h"
#include "core/Chunk.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <chrono>

namespace Phyxel {

float DamageSystem::frand() {
    m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
    return (m_rng & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}
float DamageSystem::frand(float lo, float hi) { return lo + (hi - lo) * frand(); }

DamageSystem::MatResponse DamageSystem::responseFor(const std::string& materialName) const {
    const auto* def = Core::MaterialRegistry::instance().getMaterial(materialName);

    // Data-driven: a material with a "break" block in materials.json supplies its
    // toughness + brittleness (s1/s2) + absorption directly (see docs/DestructionSystemV2.md
    // §5.A). Materials WITHOUT a break block fall back to a bondStrength-derived toughness
    // with balanced brittleness — the same formula the old hardcoded default used.
    if (def && def->breakProfile.hasProfile) {
        const auto& b = def->breakProfile;
        return MatResponse{ b.toughness, b.brittleS1, b.brittleS2, b.absorption };
    }

    float bond = def ? std::max(0.05f, def->physics.bondStrength) : 0.5f;
    MatResponse r;
    r.toughness = bond * 120.0f;
    r.s1 = 2.5f; r.s2 = 6.0f; r.absorption = 0.6f;
    return r;
}

int DamageSystem::solidVoxelsBetween(const glm::vec3& a, const glm::vec3& b) const {
    if (!m_cm) return 0;
    glm::vec3 d = b - a;
    float len = glm::length(d);
    if (len < 1e-3f) return 0;
    glm::vec3 step = d / len;
    int samples = static_cast<int>(len * 2.0f); // ~2 samples per world unit
    int solid = 0;
    glm::ivec3 last(-9999);
    // Skip the endpoints: start past the impact cell, stop before the target cell.
    for (int i = 1; i < samples; ++i) {
        glm::vec3 p = a + step * (static_cast<float>(i) / 2.0f);
        glm::ivec3 cell(static_cast<int>(std::floor(p.x)),
                        static_cast<int>(std::floor(p.y)),
                        static_cast<int>(std::floor(p.z)));
        if (cell == last) continue;
        last = cell;
        // don't count the target voxel itself
        glm::ivec3 target(static_cast<int>(std::floor(b.x)),
                          static_cast<int>(std::floor(b.y)),
                          static_cast<int>(std::floor(b.z)));
        if (cell == target) continue;
        if (m_cm->hasVoxelAt(cell)) ++solid;
    }
    return solid;
}

void DamageSystem::spawnDebris(const glm::vec3& pos, const glm::vec3& vel, float scale,
                               const std::string& material) {
    if (!m_gpu) return;
    GpuParticlePhysics::SpawnParams sp;
    sp.position     = pos;
    sp.velocity     = vel;
    sp.angularVel   = glm::vec3(frand(-4.0f, 4.0f), frand(-4.0f, 4.0f), frand(-4.0f, 4.0f));
    sp.scale        = glm::vec3(scale);
    sp.materialName = material;
    sp.lifetime     = 25.0f;
    m_gpu->queueSpawn(sp);
}

DamageResult DamageSystem::applyDamage(const glm::vec3& center, float radius, float energy,
                                       const std::string& /*damageType*/, const glm::vec3& direction,
                                       float supportY, bool collapse, bool coherentFragments) {
    DamageResult res;
    if (!m_cm || radius <= 0.0f || energy <= 0.0f) return res;

    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();

    glm::vec3 dirBias = (glm::length(direction) > 1e-3f) ? glm::normalize(direction) : glm::vec3(0.0f);
    std::vector<glm::ivec3> removed; // for the P3 collapse pass

    glm::ivec3 lo(static_cast<int>(std::floor(center.x - radius)),
                  static_cast<int>(std::floor(center.y - radius)),
                  static_cast<int>(std::floor(center.z - radius)));
    glm::ivec3 hi(static_cast<int>(std::ceil(center.x + radius)),
                  static_cast<int>(std::ceil(center.y + radius)),
                  static_cast<int>(std::ceil(center.z + radius)));

    // A voxel that the blast breaks. Decided in the scan pass below, applied after.
    struct BreakRec {
        glm::ivec3  wp;
        glm::vec3   vc;
        glm::vec3   outDir;
        float       ratio;
        float       speed;
        std::string mat;
        MatResponse mr;
        bool        subdivided = false;  // cell holds subcubes/microcubes, not a full cube
    };
    std::vector<BreakRec> breaks;

    // ---- Phase A: scan + decide ----
    // Shielding (solidVoxelsBetween) is computed against the PRE-BLAST grid: we only
    // record break decisions here and defer every removeCubeFast to phase B. If we
    // removed voxels mid-scan, each break would un-shield the voxels behind it in
    // iteration order (x-outer), igniting a self-reinforcing cascade straight down
    // +x and carving an axis-aligned trench instead of a symmetric crater. A blast is
    // instantaneous, so all shielding must reflect the geometry at the moment of impact.
    for (int x = lo.x; x <= hi.x; ++x)
    for (int y = lo.y; y <= hi.y; ++y)
    for (int z = lo.z; z <= hi.z; ++z) {
        glm::ivec3 wp(x, y, z);
        Cube* cube = m_cm->getCubeAt(wp);
        bool subdivided = false;
        std::string mat;
        if (cube) {
            mat = cube->getMaterialName();
        } else if (m_cm->hasVoxelAt(wp)) {
            // Sub-voxel cell (subcubes/microcubes, e.g. a tree). Break the whole
            // cell as a unit so the blast can sever sub-voxel structures and feed
            // the collapse pass; use a representative material for toughness.
            subdivided = true;
            if (Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp))) {
                auto subs = ch->getStaticSubcubesAt(ChunkManager::worldToLocalCoord(wp));
                if (!subs.empty() && subs[0]) mat = subs[0]->getMaterialName();
            }
            if (mat.empty()) mat = "Wood";
        } else {
            continue;
        }

        glm::vec3 vc(x + 0.5f, y + 0.5f, z + 0.5f);
        float dist = glm::distance(center, vc);
        if (dist > radius) continue;

        float fall = std::pow(std::max(0.0f, 1.0f - dist / radius), FALLOFF_P);
        if (fall <= 0.0f) continue;

        MatResponse mr = responseFor(mat);
        int shield = solidVoxelsBetween(center, vc);
        float reached = energy * fall * std::exp(-mr.absorption * static_cast<float>(shield));

        // Damage accumulation: prior sub-threshold hits add to this one. The voxel
        // breaks once total energy exceeds toughness, so repeated weak hits chip
        // through. Tier is based on total energy at break (chip = chunky, big hit = dust).
        // (Accumulation is cube-only; sub-voxel cells break in one pass.)
        float effective = reached + (cube ? cube->getAccumulatedDamage() : 0.0f);
        float ratio = effective / mr.toughness;

        if (ratio < 1.0f) {
            if (cube) cube->addDamage(reached);   // weakened but intact (cracks; visual feedback = P4)
            res.voxelsGrazed++;
            continue;
        }

        // Outward launch direction (radial + optional hit-direction bias).
        glm::vec3 outDir = (dist > 1e-3f) ? (vc - center) / dist : glm::vec3(0.0f, 1.0f, 0.0f);
        outDir = glm::normalize(outDir + dirBias * 0.5f);
        float speed = BASE_SPEED * std::sqrt(ratio);

        breaks.push_back({ wp, vc, outDir, ratio, speed, std::move(mat), mr, subdivided });
    }

    // ---- Phase B: apply removals + spawn debris ----
    // The grid is mutated only here, after every shielding decision has been made.
    for (const BreakRec& b : breaks) {
        const glm::ivec3& wp = b.wp;
        const glm::vec3&  vc = b.vc;
        const glm::vec3&  outDir = b.outDir;
        const float       ratio  = b.ratio;
        const float       speed  = b.speed;
        const std::string& mat   = b.mat;
        const MatResponse& mr     = b.mr;

        // Sub-voxel cell: clear the whole subdivision and scatter subcube debris.
        if (b.subdivided) {
            Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
            if (ch && ch->clearSubdivisionAt(ChunkManager::worldToLocalCoord(wp))) {
                m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
                m_cm->markChunkDirty(ch);
                removed.push_back(wp);
                res.voxelsBroken++;
                for (int i = 0; i < SUBCUBE_PIECES && res.debrisSpawned < MAX_DEBRIS; ++i) {
                    const int S = 3, TOTAL = S * S * S;
                    int cell = i * TOTAL / SUBCUBE_PIECES;
                    int cx = cell % S, cy = (cell / S) % S, cz = (cell / (S * S)) % S;
                    glm::vec3 cellOff((cx + 0.5f) / S - 0.5f, (cy + 0.5f) / S - 0.5f, (cz + 0.5f) / S - 0.5f);
                    glm::vec3 vjit(frand(-0.3f, 0.3f), frand(-0.3f, 0.3f), frand(-0.3f, 0.3f));
                    spawnDebris(vc + cellOff, outDir * speed * frand(0.7f, 1.3f) + vjit * speed, 1.0f / 3.0f, mat);
                    res.debrisSpawned++;
                }
            }
            continue;
        }

        // Remove the static voxel (fast: marks chunk dirty, defers face rebuild)
        // and clear its collision occupancy cell.
        m_cm->removeCubeFast(wp);
        // Route through ChunkManager so both the GPU occupancy grid and the water sim
        // (via its occupancy callback) learn the cell is now open.
        m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
        removed.push_back(wp);
        res.voxelsBroken++;

        if (res.debrisSpawned >= MAX_DEBRIS) continue; // capped: voxel still removed

        // Tier the shatter by overkill ratio.
        if (ratio < mr.s1) {
            // Intact dynamic cube.
            spawnDebris(vc, outDir * speed, 1.0f, mat);
            res.debrisSpawned++;
        } else if (ratio < mr.s2) {
            // Shatter into subcubes (1/3). Spawn each piece at a DISTINCT sub-cell
            // center of the parent voxel's 3x3x3 grid so pieces never start
            // interpenetrating — overlapping spawns make the position-based solver
            // shove them apart, injecting energy ("popcorn"). Velocity jitter (not
            // position) still gives them spread + tumble.
            const int S = 3, TOTAL = S * S * S;  // 27 sub-cells
            for (int i = 0; i < SUBCUBE_PIECES && res.debrisSpawned < MAX_DEBRIS; ++i) {
                int cell = i * TOTAL / SUBCUBE_PIECES;  // distinct, spread across the grid
                int cx = cell % S, cy = (cell / S) % S, cz = (cell / (S * S)) % S;
                glm::vec3 cellOff((cx + 0.5f) / S - 0.5f, (cy + 0.5f) / S - 0.5f, (cz + 0.5f) / S - 0.5f);
                glm::vec3 vjit(frand(-0.3f, 0.3f), frand(-0.3f, 0.3f), frand(-0.3f, 0.3f));
                glm::vec3 v = outDir * speed * frand(0.7f, 1.3f) + vjit * speed;
                spawnDebris(vc + cellOff, v, 1.0f / 3.0f, mat);
                res.debrisSpawned++;
            }
        } else {
            // Pulverize into microcubes (1/9), sampled + capped. Distinct sub-cell
            // centers of the 9x9x9 grid — non-overlapping spawns (see subcube note).
            const int S = 9, TOTAL = S * S * S;  // 729 sub-cells
            for (int i = 0; i < MICROCUBE_PIECES && res.debrisSpawned < MAX_DEBRIS; ++i) {
                int cell = i * TOTAL / MICROCUBE_PIECES;  // distinct, spread across the grid
                int cx = cell % S, cy = (cell / S) % S, cz = (cell / (S * S)) % S;
                glm::vec3 cellOff((cx + 0.5f) / S - 0.5f, (cy + 0.5f) / S - 0.5f, (cz + 0.5f) / S - 0.5f);
                glm::vec3 vjit(frand(-0.45f, 0.45f), frand(-0.45f, 0.45f), frand(-0.45f, 0.45f));
                glm::vec3 v = outDir * speed * frand(0.6f, 1.5f) + vjit * speed * 1.5f;
                spawnDebris(vc + cellOff, v, 1.0f / 9.0f, mat);
                res.debrisSpawned++;
            }
        }
    }

    auto t1 = Clock::now();

    // P3/P4: collapse any voxel groups the blast severed from the main mass.
    if (collapse && !removed.empty()) {
        collapseUnsupported(removed, supportY, res, coherentFragments);
    }
    auto t2 = Clock::now();

    // Batch re-mesh: all the removeCubeFast calls above only flagged their chunks
    // dirty (deferRebuild). Flush once here so every touched chunk is re-meshed a
    // SINGLE time within this op, instead of once per removed voxel. This is the
    // core lag-spike fix: O(chunks touched) re-meshes instead of O(voxels removed).
    if (res.voxelsBroken > 0) {
        m_cm->updateDirtyChunks();
    }
    auto t3 = Clock::now();

    auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    LOG_INFO("DamageSystem", "applyDamage E={} r={} -> broken={} grazed={} debris={}",
             energy, radius, res.voxelsBroken, res.voxelsGrazed, res.debrisSpawned);
    // Per-phase timing (DEBUG): break-loop scans/breaks/spawns, collapse floods the
    // severed groups, remesh is the single batched chunk rebuild. Watch this if a
    // big op ever starts spiking again.
    LOG_DEBUG_FMT("DamageSystem",
             "timing break-loop=" << ms(t0, t1) << "ms collapse=" << ms(t1, t2)
             << "ms remesh=" << ms(t2, t3) << "ms total=" << ms(t0, t3) << "ms");
    return res;
}

// Pack a voxel coord into a 63-bit key (21 bits/axis, ±~1M range).
static inline int64_t packVoxel(int x, int y, int z) {
    auto m = [](int v) -> int64_t { return static_cast<int64_t>(v + 1048576) & 0x1FFFFF; };
    return (m(x) << 42) | (m(y) << 21) | m(z);
}

int DamageSystem::dropDetachedCell(const glm::ivec3& wp, DamageResult& res) {
    const glm::vec3 vc(wp.x + 0.5f, wp.y + 0.5f, wp.z + 0.5f);

    // Full cube: one cube-sized debris piece (the original behavior).
    if (Cube* c = m_cm->getCubeAt(wp)) {
        std::string mat = c->getMaterialName();
        m_cm->removeCubeFast(wp);
        m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
        glm::vec3 vel(frand(-0.5f, 0.5f), frand(-1.0f, -0.2f), frand(-0.5f, 0.5f));
        if (res.debrisSpawned < MAX_DEBRIS) { spawnDebris(vc, vel, 1.0f, mat); res.debrisSpawned++; }
        return 1;
    }

    // Sub-voxel cell (subcubes/microcubes — e.g. a tree). Clear the whole cell
    // via its owning chunk and scatter a few subcube-scale debris pieces.
    Chunk* chunk = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
    if (!chunk) return 0;
    const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
    std::string mat = "Wood";
    auto subs = chunk->getStaticSubcubesAt(lp);
    if (!subs.empty() && subs[0]) mat = subs[0]->getMaterialName();
    if (!chunk->clearSubdivisionAt(lp)) return 0;
    m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
    m_cm->markChunkDirty(chunk);
    for (int i = 0; i < 4 && res.debrisSpawned < MAX_DEBRIS; ++i) {
        glm::vec3 off(frand(-0.33f, 0.33f), frand(-0.33f, 0.33f), frand(-0.33f, 0.33f));
        glm::vec3 vel(frand(-0.6f, 0.6f), frand(-1.0f, -0.2f), frand(-0.6f, 0.6f));
        spawnDebris(vc + off, vel, 1.0f / 3.0f, mat);
        res.debrisSpawned++;
    }
    return 1;
}

// Gather one cell's static geometry — a full cube, its subcubes, AND its microcubes —
// as world-centered KinematicVoxels for a coherent slab (full micro resolution, P2.1).
// Returns false only if the cell has no gatherable content (caller scatters the component).
static bool gatherCellVoxels(ChunkManager* cm, const glm::ivec3& wp,
                             std::vector<Core::KinematicVoxel>& out) {
    if (Cube* c = cm->getCubeAt(wp)) {
        Core::KinematicVoxel v;
        v.localPos     = glm::vec3(wp) + glm::vec3(0.5f);
        v.scale        = glm::vec3(1.0f);
        v.materialName = c->getMaterialName();
        out.push_back(std::move(v));
        return true;
    }
    Chunk* ch = cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
    if (!ch) return false;
    const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
    bool any = false;
    for (Subcube* s : ch->getStaticSubcubesAt(lp)) {
        if (!s) continue;
        float sc = s->getScale();
        Core::KinematicVoxel v;
        // Subcube at grid slot g (0..2) spans [wp+g*sc, wp+(g+1)*sc]; center = wp+(g+0.5)*sc.
        v.localPos     = glm::vec3(wp) + (glm::vec3(s->getLocalPosition()) + 0.5f) * sc;
        v.scale        = glm::vec3(sc);
        v.materialName = s->getMaterialName();
        out.push_back(std::move(v));
        any = true;
    }
    // Microcubes (1/9): slot within cell = sub*3 + mic (0..8 per axis); center at +0.5/9.
    for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
            for (int sz = 0; sz < 3; ++sz) {
                for (Microcube* m : ch->getMicrocubesAt(lp, {sx, sy, sz})) {
                    if (!m) continue;
                    Core::KinematicVoxel v;
                    glm::vec3 fine = glm::vec3(sx, sy, sz) * 3.0f
                                   + glm::vec3(m->getMicrocubeLocalPosition());
                    v.localPos     = glm::vec3(wp) + (fine + 0.5f) / 9.0f;
                    v.scale        = glm::vec3(1.0f / 9.0f);
                    v.materialName = m->getMaterialName();
                    out.push_back(std::move(v));
                    any = true;
                }
            }
    return any;
}

// Remove one cell's content (cube or subdivision) WITHOUT spawning debris, keeping both
// occupancy grids + the chunk mesh in sync (mirrors dropDetachedCell's removal half).
static void removeCellContent(ChunkManager* cm, const glm::ivec3& wp) {
    if (cm->getCubeAt(wp)) {
        cm->removeCubeFast(wp);
        cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
        return;
    }
    Chunk* ch = cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
    if (!ch) return;
    if (ch->clearSubdivisionAt(ChunkManager::worldToLocalCoord(wp))) {
        cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
        cm->markChunkDirty(ch);
    }
}

// Material of a world cell's content (cube, first subcube, or first MICROCUBE — fine
// trees are micro-resolution, P2.1), "" if empty.
static std::string cellMaterial(ChunkManager* cm, const glm::ivec3& wp) {
    if (Cube* c = cm->getCubeAt(wp)) return c->getMaterialName();
    if (Chunk* ch = cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp))) {
        const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
        auto subs = ch->getStaticSubcubesAt(lp);
        if (!subs.empty() && subs[0]) return subs[0]->getMaterialName();
        // Micro-only cell: scan the 27 subcube slots for the first microcube.
        for (int sx = 0; sx < 3; ++sx)
            for (int sy = 0; sy < 3; ++sy)
                for (int sz = 0; sz < 3; ++sz) {
                    auto mics = ch->getMicrocubesAt(lp, {sx, sy, sz});
                    if (!mics.empty() && mics[0]) return mics[0]->getMaterialName();
                }
    }
    return {};
}
// TREE material = trunk (Log*) or canopy (Leaf*), vs terrain (Phase 2 tree-object flood).
static bool isTreeCell(ChunkManager* cm, const glm::ivec3& wp) {
    std::string m = cellMaterial(cm, wp);
    return m.rfind("Log", 0) == 0 || m.rfind("Leaf", 0) == 0;
}
static bool isLogCell(ChunkManager* cm, const glm::ivec3& wp) {
    return cellMaterial(cm, wp).rfind("Log", 0) == 0;
}

bool DamageSystem::collapseComponentCoherent(const std::vector<glm::ivec3>& component,
                                             DamageResult& res) {
    if (!m_fragMgr || !m_fragMgr->ready() ||
        static_cast<int>(component.size()) > COHERENT_MAX_VOXELS) {
        return false;
    }

    // Gather the whole component's geometry first; if any cell isn't coherently
    // gatherable, bail (scatter) BEFORE removing anything.
    std::vector<Core::KinematicVoxel> frag;
    frag.reserve(component.size());
    for (const glm::ivec3& v : component) {
        if (!gatherCellVoxels(m_cm, v, frag)) return false;
    }
    if (frag.empty()) return false;

    // "Leaves shed, wood topples" (2026-07-14 decision): only WOOD forms the coherent
    // rigid body; leaf voxels scatter as debris. (Standing leaves render as foliage
    // cards; the kinematic pipeline has no card support, and shedding is what a felled
    // tree does anyway.) A leaf-only component returns false -> the caller's per-cell
    // scatter IS the leaf poof.
    std::vector<Core::KinematicVoxel> wood, leaves;
    wood.reserve(frag.size());
    for (auto& v : frag) {
        (v.materialName.rfind("Leaf", 0) == 0 ? leaves : wood).push_back(std::move(v));
    }
    if (wood.empty()) return false;

    auto worldMass = [](const Core::KinematicVoxel& vx) {
        float vol = vx.scale.x * vx.scale.y * vx.scale.z;
        const auto* def = Core::MaterialRegistry::instance().getMaterial(vx.materialName);
        float density = def ? std::max(0.05f, def->physics.mass) : 1.0f;
        return std::max(density * vol, 0.001f);   // mass floor (H4 / auditor 1e-6 note)
    };

    // Spawn the body FIRST (physicalize copies the geometry — it does not need the world
    // cells cleared). Only if it succeeds do we remove the world cells. If it fails, the
    // cells are untouched, so we return false and the caller scatters them — NO silent
    // geometry loss (the "silent-drop" class bug the auditor flagged).
    const size_t woodCount = wood.size();
    std::string id = "collapse_" + std::to_string(m_fragSeq++);
    uint32_t bid = m_fragMgr->spawn(id, std::move(wood), glm::mat4(1.0f),
                                    glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f), worldMass);
    if (bid == 0) {
        LOG_WARN("DamageSystem", "coherent collapse: physicalize failed ({} cells) -> scatter",
                 component.size());
        return false;   // cells NOT removed -> caller falls back to per-cell scatter
    }

    for (const glm::ivec3& v : component) removeCellContent(m_cm, v);
    // Shed the leaves: light fluttering debris from each leaf voxel's world center.
    for (const auto& lv : leaves) {
        if (res.debrisSpawned >= MAX_DEBRIS) break;
        glm::vec3 vel(frand(-0.8f, 0.8f), frand(-0.6f, 0.2f), frand(-0.8f, 0.8f));
        spawnDebris(lv.localPos, vel, lv.scale.x, lv.materialName);
        res.debrisSpawned++;
    }
    res.debrisSpawned += 1;   // one coherent body
    LOG_INFO("DamageSystem", "coherent collapse: {} cells -> 1 rigid body ({} wood voxels, {} leaves shed)",
             component.size(), woodCount, leaves.size());
    return true;
}

void DamageSystem::collapseUnsupported(const std::vector<glm::ivec3>& removed, float supportY,
                                       DamageResult& res, bool coherent) {
    const int yAnchor = static_cast<int>(std::floor(supportY));
    std::unordered_set<int64_t> visited;   // every solid voxel enqueued by any flood
    std::unordered_set<int64_t> anchored;  // voxels proven connected to the supported main mass
    int totalDetached = 0;

    // Seeds: solid voxels bordering the removed set (the rim of the hole).
    static const glm::ivec3 NB[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    std::vector<glm::ivec3> seeds;
    for (const glm::ivec3& r : removed) {
        for (const auto& n : NB) {
            glm::ivec3 s = r + n;
            // "Any content" (full cube OR sub-voxel subdivision) so sub-voxel
            // structures like trees take part in the connectivity graph.
            if (m_cm->hasVoxelAt(s)) seeds.push_back(s);
        }
    }

    std::vector<glm::ivec3> stack;
    std::vector<glm::ivec3> component;
    for (const glm::ivec3& seed : seeds) {
        if (totalDetached >= MAX_COLLAPSE) break;
        if (visited.count(packVoxel(seed.x, seed.y, seed.z))) continue;

        // Flood-fill this connected solid component (bounded), checking for an anchor.
        component.clear();
        stack.clear();
        stack.push_back(seed);
        visited.insert(packVoxel(seed.x, seed.y, seed.z));
        bool supported = false;

        bool hasTerrain = false;   // does this component contain a non-tree (terrain) cell?
        while (!stack.empty()) {
            glm::ivec3 v = stack.back(); stack.pop_back();
            if (v.y <= yAnchor) { supported = true; break; }   // reached the designer anchor
            component.push_back(v);

            const bool vTree = isTreeCell(m_cm, v);
            if (!vTree) hasTerrain = true;
            // TREE-OBJECT anchor (Phase 2): a tree is rooted to the ground ONLY through its
            // trunk — a Log with terrain directly below. Incidental leaf-or-side terrain
            // contact must NOT anchor a severed top, so a tree cell never propagates the
            // flood into terrain (below); only this rooted-trunk check anchors a tree.
            if (vTree && isLogCell(m_cm, v)) {
                glm::ivec3 below(v.x, v.y - 1, v.z);
                if (m_cm->hasVoxelAt(below) && !isTreeCell(m_cm, below)) { supported = true; break; }
            }
            // Flooded past the cap → the MAIN MASS. Terrain uses MAX_FLOOD; a pure-tree
            // component (a big canopy) is not ground, so it floods to the higher tree cap.
            const int cap = hasTerrain ? MAX_FLOOD : TREE_MAX_FLOOD;
            if (static_cast<int>(component.size()) > cap) { supported = true; break; }
            for (const auto& n : NB) {
                glm::ivec3 nb = v + n;
                int64_t key = packVoxel(nb.x, nb.y, nb.z);
                // Reached a voxel already proven part of the main mass → supported.
                if (anchored.count(key)) { supported = true; break; }
                if (visited.count(key)) continue;
                if (m_cm->hasVoxelAt(nb)) {
                    // A tree cell does not spread the flood into terrain (leaf/side contact
                    // must not anchor a severed top); trunk/terrain spread normally.
                    if (vTree && !isTreeCell(m_cm, nb)) continue;
                    visited.insert(key); stack.push_back(nb);
                }
            }
            if (supported) break;
        }

        if (supported) {
            // Record this flood (processed component + the unprocessed frontier still
            // on the stack) as anchored. A flood that hits the MAX_FLOOD cap stops
            // early, leaving a visited frontier; without marking it anchored, that
            // frontier walls the rest of the connected ground into sub-cap pockets
            // that later seeds mistake for severed islands and drop — carving false
            // straight-line trenches across supported terrain.
            for (const glm::ivec3& v : component) anchored.insert(packVoxel(v.x, v.y, v.z));
            for (const glm::ivec3& v : stack)     anchored.insert(packVoxel(v.x, v.y, v.z));
            continue;
        }

        // Detached: topple as ONE coherent rigid slab if enabled + gatherable + within
        // budget (P1.2b), else scatter each cell as falling debris (the shipped path).
        if (coherent && totalDetached < MAX_COLLAPSE &&
            collapseComponentCoherent(component, res)) {
            totalDetached += static_cast<int>(component.size());
        } else {
            for (const glm::ivec3& v : component) {
                if (totalDetached >= MAX_COLLAPSE) break;
                totalDetached += dropDetachedCell(v, res);
            }
        }
    }
    res.voxelsBroken += totalDetached;
    if (totalDetached >= MAX_COLLAPSE)
        LOG_WARN("DamageSystem", "collapse hit hard cap ({} voxels) — chain reaction truncated", MAX_COLLAPSE);
    else if (totalDetached > 0)
        LOG_INFO("DamageSystem", "collapse: {} voxels detached and fell", totalDetached);
}

} // namespace Phyxel
