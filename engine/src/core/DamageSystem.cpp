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
#include <deque>
#include <unordered_set>
#include <unordered_map>
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

        // LEAF cells are foliage: broken leaves are removed with NO voxel debris (F1 —
        // leaves must never appear as voxels, standing or breaking).
        const bool isLeafMat = (mat.rfind("Leaf", 0) == 0);

        // Sub-voxel cell: clear the whole subdivision and scatter subcube debris.
        if (b.subdivided) {
            Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
            if (ch && ch->clearSubdivisionAt(ChunkManager::worldToLocalCoord(wp))) {
                m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
                m_cm->markChunkDirty(ch);
                removed.push_back(wp);
                res.voxelsBroken++;
                for (int i = 0; !isLeafMat && i < SUBCUBE_PIECES && res.debrisSpawned < MAX_DEBRIS; ++i) {
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

        if (isLeafMat) continue;                       // foliage: no voxel debris
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
        collapseUnsupported(removed, supportY, res, coherentFragments, center, dirBias);
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

    // Full cube: one cube-sized debris piece (the original behavior). LEAF cells are
    // foliage — removed with NO voxel debris (F1: leaves never appear as voxels).
    if (Cube* c = m_cm->getCubeAt(wp)) {
        std::string mat = c->getMaterialName();
        m_cm->removeCubeFast(wp);
        m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
        if (mat.rfind("Leaf", 0) != 0 && res.debrisSpawned < MAX_DEBRIS) {
            glm::vec3 vel(frand(-0.5f, 0.5f), frand(-1.0f, -0.2f), frand(-0.5f, 0.5f));
            spawnDebris(vc, vel, 1.0f, mat);
            res.debrisSpawned++;
        }
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
    if (mat.rfind("Leaf", 0) != 0) {   // leaf cells: no voxel debris (foliage)
        for (int i = 0; i < 4 && res.debrisSpawned < MAX_DEBRIS; ++i) {
            glm::vec3 off(frand(-0.33f, 0.33f), frand(-0.33f, 0.33f), frand(-0.33f, 0.33f));
            glm::vec3 vel(frand(-0.6f, 0.6f), frand(-1.0f, -0.2f), frand(-0.6f, 0.6f));
            spawnDebris(vc + off, vel, 1.0f / 3.0f, mat);
            res.debrisSpawned++;
        }
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
        v.gridCell     = wp;                    // foliage identity (F3): cube-leaf sprig
        v.gridSlot     = glm::ivec3(1);         //   sits at the cube centre slot (1,1,1)
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
        v.gridCell     = wp;                    // foliage identity (F3)
        v.gridSlot     = s->getLocalPosition();
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
                    v.gridCell     = wp;                    // foliage identity (F3):
                    v.gridSlot     = glm::ivec3(sx, sy, sz); //   micro -> parent subcube sprig
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
// Content scan of a cell for tree materials (F5). Fine trees MIX micro-Log branch wood
// and micro-Leaf foliage in the SAME cell, so classifying a cell by its FIRST sub-voxel
// (the old cellMaterial approach) mislabels leaf-first mixed cells as "leaf" — the wood
// support flood skips them and every branch beyond is unreachable, leaving a floating
// ghost canopy when the trunk is severed. Classify by CONTENT instead: any Log present
// makes the cell wood-floodable (its in-cell leaves ride the fragment via the gather
// partition); only leaf-PURE cells are cargo.
struct CellTreeScan {
    bool log       = false;   ///< any Log content (cube, subcube, or microcube)
    bool leaf      = false;   ///< any Leaf content
    bool structLog = false;   ///< Log at CUBE or SUBCUBE granularity (see below)
    std::string logSpecies;   ///< suffix after "Log" of the first log found (valid iff log)
    std::string leafSpecies;  ///< suffix after "Leaf" of the first leaf found (valid iff leaf)
};
// A cell's tree SPECIES: its wood's, else its foliage's ("" = plain oak Log/Leaf).
static const std::string& cellSpecies(const CellTreeScan& s) {
    return s.log ? s.logSpecies : s.leafSpecies;
}
static CellTreeScan scanCellTree(ChunkManager* cm, const glm::ivec3& wp) {
    CellTreeScan s;
    // F6 — STRUCTURAL wood = log at cube (1 m) or subcube (1/3 m) cross-section. A
    // limb ≥ ~33 cm across can carry a trunk's load; micro-only wood (≤ 1/9 m ≈ 11 cm
    // twigs) cannot hold a multi-ton tree up, so it neither transmits support nor
    // anchors (the live pine stood on a ground-touching twig skirt without this).
    auto classify = [&s](const std::string& m, bool structural) {
        if (m.rfind("Log", 0) == 0) {
            if (!s.log) s.logSpecies = m.substr(3);
            s.log = true;
            if (structural) s.structLog = true;
        } else if (m.rfind("Leaf", 0) == 0) {
            if (!s.leaf) s.leafSpecies = m.substr(4);
            s.leaf = true;
        }
    };
    if (Cube* c = cm->getCubeAt(wp)) { classify(c->getMaterialName(), true); return s; }
    Chunk* ch = cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
    if (!ch) return s;
    const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
    for (Subcube* sc : ch->getStaticSubcubesAt(lp)) {
        if (sc) classify(sc->getMaterialName(), true);
        if (s.structLog && s.leaf) return s;   // nothing more to learn
    }
    for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
            for (int sz = 0; sz < 3; ++sz)
                for (Microcube* mc : ch->getMicrocubesAt(lp, {sx, sy, sz})) {
                    if (mc) classify(mc->getMaterialName(), false);
                    if (s.log && s.leaf) return s;   // structLog is final past subcubes
                }
    return s;
}
// TREE cell = contains trunk (Log*) or canopy (Leaf*) material (Phase 2 tree-object flood).
static bool isTreeCell(ChunkManager* cm, const glm::ivec3& wp) {
    CellTreeScan s = scanCellTree(cm, wp);
    return s.log || s.leaf;
}
// Any Log content (used by the rooted-trunk anchor check).
static bool isLogCell(ChunkManager* cm, const glm::ivec3& wp) {
    return scanCellTree(cm, wp).log;
}
// Structural wood (F6): log at cube/subcube granularity — the support graph.
static bool isStructuralLogCell(ChunkManager* cm, const glm::ivec3& wp) {
    return scanCellTree(cm, wp).structLog;
}
// CARGO cell (F1 + F6): tree matter with NO structural wood — leaf foliage and/or
// micro-only twig wood. Cargo hangs off structural wood: it never transmits support,
// never anchors, and its leaves never become voxel debris (they are foliage). A cell
// with a structural log inside is NOT cargo — its wood carries support.
static bool isCargoCell(ChunkManager* cm, const glm::ivec3& wp) {
    CellTreeScan s = scanCellTree(cm, wp);
    return (s.log || s.leaf) && !s.structLog;
}

// F8 — vertical support between TREE cells requires actual structural FACE
// CONTACT at subcube granularity, not mere cell adjacency. A carved notch can
// hollow the middle of a cell so its only structural wood is the TOP layer —
// the underside skin of the mass above. Cell-granular adjacency then "connects"
// that skin to the rooted stub below across a 2/3-cell air gap, and a trunk
// visibly standing on a few cargo micros never falls (live user case). The
// lower cell must hold structural Log in its TOP subcube layer AND the upper
// cell in its BOTTOM layer (a full Log cube spans all layers). Fresh trees
// pass everywhere (verified: trunk/flare cells hold wood in all three layers).
static bool cellLayerHasStructuralLog(ChunkManager* cm, const glm::ivec3& wp, int sy) {
    Chunk* ch = cm ? cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp)) : nullptr;
    if (!ch) return false;
    const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
    if (Cube* c = ch->getCubeAtFast(lp))
        return c->getMaterialName().rfind("Log", 0) == 0;
    for (Subcube* sc : ch->getStaticSubcubesAt(lp))
        if (sc && sc->getLocalPosition().y == sy &&
            sc->getMaterialName().rfind("Log", 0) == 0)
            return true;
    return false;
}
static bool verticalStructuralContact(ChunkManager* cm, const glm::ivec3& lower,
                                      const glm::ivec3& upper) {
    return cellLayerHasStructuralLog(cm, lower, 2) &&
           cellLayerHasStructuralLog(cm, upper, 0);
}

// F9 — a ground-touching Log cell is a ROOTED TRUNK only when trunk-like wood
// actually meets the ground: a full Log cube, or >=4 structural Log subcubes in
// the cell's BOTTOM layer. Any-log-over-terrain let a 1-subcube branch TIP
// drooping to the ground anchor an entire severed crown — live: after a fell, a
// static ghost thicket of canopy stayed standing in the player's path (and in
// the red test, the tree never fell at all). Grounding for the threshold: fresh
// forge trees measure 5+ bottom-layer subcubes in flare cells and 9 in trunk
// cubes; twig tips measure 1-2 (live scan_micro + /api/world/subcubes data).
static bool trunkRootContact(ChunkManager* cm, const glm::ivec3& wp) {
    Chunk* ch = cm ? cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp)) : nullptr;
    if (!ch) return false;
    const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
    if (Cube* c = ch->getCubeAtFast(lp))
        return c->getMaterialName().rfind("Log", 0) == 0;
    int n = 0;
    for (Subcube* sc : ch->getStaticSubcubesAt(lp))
        if (sc && sc->getLocalPosition().y == 0 &&
            sc->getMaterialName().rfind("Log", 0) == 0)
            ++n;
    return n >= 4;
}

bool DamageSystem::isStructuralWoodCell(ChunkManager* cm, const glm::ivec3& wp,
                                        std::string* logMaterial) {
    if (!cm) return false;
    // Structural = Log* at cube or subcube cross-section (F6). Micro-only twig
    // wood is cargo: it cannot carry a trunk's load, so it is not a choppable
    // trunk and must not read as one.
    auto structuralWood = [&](const std::string& m) {
        if (m.rfind("Log", 0) != 0) return false;
        if (logMaterial) *logMaterial = m;
        return true;
    };
    if (Cube* c = cm->getCubeAt(wp)) return structuralWood(c->getMaterialName());
    if (Chunk* ch = cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp))) {
        for (Subcube* sc : ch->getStaticSubcubesAt(ChunkManager::worldToLocalCoord(wp)))
            if (sc && structuralWood(sc->getMaterialName())) return true;
    }
    return false;
}

bool DamageSystem::isWoodCellAny(ChunkManager* cm, const glm::ivec3& wp,
                                 std::string* logMaterial) {
    if (isStructuralWoodCell(cm, wp, logMaterial)) return true;
    if (!cm) return false;
    if (Chunk* ch = cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp))) {
        const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
        for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
        for (int sz = 0; sz < 3; ++sz)
            for (Microcube* mc : ch->getMicrocubesAt(lp, {sx, sy, sz}))
                if (mc && mc->getMaterialName().rfind("Log", 0) == 0) {
                    if (logMaterial) *logMaterial = mc->getMaterialName();
                    return true;
                }
    }
    return false;
}

bool DamageSystem::closestWoodPointInCell(ChunkManager* cm, const glm::ivec3& wp,
                                          const glm::vec3& probe, glm::vec3& outPoint,
                                          std::string* logMaterial) {
    if (!cm) return false;
    Chunk* ch = cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
    if (!ch) return false;
    const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
    // Per-FRAGMENT nearest point, NOT a union AABB: a partially-chipped cell
    // holds disjoint wood fragments, and clamping into their union box can put
    // the "contact" in the air gap between them (auditor-caught defect).
    bool any = false;
    float bestD2 = 1e18f;
    std::string bestMat;
    auto consider = [&](const glm::vec3& lo, const glm::vec3& hi, const std::string& mat) {
        const glm::vec3 cl = glm::clamp(probe, lo, hi);
        const float d2 = glm::dot(cl - probe, cl - probe);
        if (!any || d2 < bestD2) { bestD2 = d2; outPoint = cl; bestMat = mat; }
        any = true;
    };
    if (Cube* c = ch->getCubeAtFast(lp)) {
        if (c->getMaterialName().rfind("Log", 0) == 0)
            consider(glm::vec3(wp), glm::vec3(wp) + 1.0f, c->getMaterialName());
    }
    for (Subcube* sc : ch->getStaticSubcubesAt(lp)) {
        if (!sc || sc->getMaterialName().rfind("Log", 0) != 0) continue;
        const glm::vec3 lo = glm::vec3(wp) + glm::vec3(sc->getLocalPosition()) / 3.0f;
        consider(lo, lo + glm::vec3(1.0f / 3.0f), sc->getMaterialName());
    }
    for (int sx = 0; sx < 3; ++sx)
    for (int sy = 0; sy < 3; ++sy)
    for (int sz = 0; sz < 3; ++sz)
        for (Microcube* mc : ch->getMicrocubesAt(lp, {sx, sy, sz})) {
            if (!mc || mc->getMaterialName().rfind("Log", 0) != 0) continue;
            const glm::vec3 lo = glm::vec3(wp) + glm::vec3(sx, sy, sz) / 3.0f
                               + glm::vec3(mc->getMicrocubeLocalPosition()) / 9.0f;
            consider(lo, lo + glm::vec3(1.0f / 9.0f), mc->getMaterialName());
        }
    if (!any) return false;
    if (logMaterial) *logMaterial = bestMat;
    return true;
}

DamageSystem::ChopKerfResult DamageSystem::carveChopKerf(const glm::ivec3& hitCell,
                                                         const glm::vec3& chopDir,
                                                         float kerfDepth,
                                                         bool coherentFragments,
                                                         const glm::vec3& contactPoint) {
    ChopKerfResult out;
    if (!m_cm || kerfDepth <= 0.0f) return out;
    glm::vec2 dir2(chopDir.x, chopDir.z);
    if (glm::length(dir2) < 1e-3f) return out;
    dir2 = glm::normalize(dir2);
    const glm::vec2 lat2(-dir2.y, dir2.x);
    static const glm::ivec3 H4[4] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
    const char* kHeartwood = "LogHeartwood";

    // ---- anchor at the notch frontier: the swing bites from wherever the cut
    // already reached. The entry cell empties as the notch deepens, so probe a
    // few cells along the chop direction for the first structural wood left.
    glm::ivec3 anchor = hitCell;
    if (!isWoodCellAny(m_cm, anchor)) {
        bool found = false;
        for (int k = 1; k <= 6 && !found; ++k) {
            const glm::ivec3 p(hitCell.x + static_cast<int>(std::round(dir2.x * k)), hitCell.y,
                               hitCell.z + static_cast<int>(std::round(dir2.y * k)));
            if (isWoodCellAny(m_cm, p)) { anchor = p; found = true; }
        }
        if (!found) return out;
    }

    // ---- the trunk cross-section at the hit height (bounded horizontal flood) ----
    std::vector<glm::ivec3> cells;
    {
        std::vector<glm::ivec3> stack{anchor};
        std::unordered_set<int> seen{64 | (64 << 8)};
        while (!stack.empty() && cells.size() < 64) {
            const glm::ivec3 v = stack.back(); stack.pop_back();
            cells.push_back(v);
            for (const auto& n : H4) {
                const glm::ivec3 nb = v + n, d = nb - anchor;
                if (std::abs(d.x) > 4 || std::abs(d.z) > 4) continue;
                if (!seen.insert((64 + d.x) | ((64 + d.z) << 8)).second) continue;
                if (isStructuralWoodCell(m_cm, nb)) stack.push_back(nb);
            }
        }
    }

    // ---- fill the enclosed hollow at the cut plane: trees are SHELLS by
    // construction, so without this the kerf opens into empty space. A cell ringed
    // by structural wood on all four sides becomes solid heartwood before carving.
    {
        glm::ivec3 lo = cells[0], hi = cells[0];
        for (const auto& c : cells) { lo = glm::min(lo, c); hi = glm::max(hi, c); }
        for (int x = lo.x; x <= hi.x; ++x)
        for (int z = lo.z; z <= hi.z; ++z) {
            const glm::ivec3 wp(x, hitCell.y, z);
            if (m_cm->hasVoxelAt(wp)) continue;
            bool enclosed = true;
            for (const auto& hdir : H4) {
                bool found = false;
                for (int k = 1; k <= 3 && !found; ++k)
                    found = isStructuralWoodCell(m_cm, wp + hdir * k);
                if (!found) { enclosed = false; break; }
            }
            if (!enclosed) continue;
            if (Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp))) {
                if (ch->addCube(ChunkManager::worldToLocalCoord(wp), kHeartwood)) {
                    m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, true);
                    m_cm->updateAfterCubePlace(wp);
                    cells.push_back(wp);
                }
            }
        }
    }

    // ---- kerf frame ----
    // Center the notch LINE on the blade's actual impact point when the caller
    // knows it (height clamped into the hit row, lateral offset clamped into the
    // trunk) — the bite starts exactly where the axe visibly lands.
    const bool hasContact = contactPoint.y > -1.0e8f;
    const float kerfY = hasContact
        ? std::min(hitCell.y + 0.85f, std::max(hitCell.y + 0.15f, contactPoint.y))
        : hitCell.y + 0.5f;
    float farD = -1e9f;
    glm::vec2 centroid(0.0f);
    for (const auto& c : cells) {
        const glm::vec2 cc(c.x + 0.5f, c.z + 0.5f);
        centroid += cc;
        farD = std::max(farD, glm::dot(cc, dir2) + 0.5f);
    }
    centroid /= static_cast<float>(cells.size());
    float latOff = 0.0f;
    if (hasContact) {
        const float raw = glm::dot(glm::vec2(contactPoint.x, contactPoint.z) - centroid, lat2);
        latOff = std::min(1.5f, std::max(-1.5f, raw));
    }

    // The frontier (nearD) is measured from the REMAINING WOOD inside the axe's
    // CORE CHANNEL only (a blade-sized window on the kerf line) — a partially
    // carved front cell must not pin the bite to re-carve the same region, and
    // the untouched side/top walls of the notch must not pin the frontier to the
    // entry face (they are outside the core channel by definition).
    const float coreHalfW = 0.35f, coreHalfH = 0.30f;
    auto inCore = [&](const glm::vec3& p) {
        return std::fabs(glm::dot(glm::vec2(p.x, p.z) - centroid, lat2) - latOff) <= coreHalfW &&
               std::fabs(p.y - kerfY) <= coreHalfH;
    };
    float nearD = 1e9f;
    for (const auto& c : cells) {
        Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(c));
        if (!ch) continue;
        const glm::ivec3 lp = ChunkManager::worldToLocalCoord(c);
        if (ch->getCubeAtFast(lp)) {
            // full cube: cell-granular test — does the core channel cross it?
            const glm::vec2 cc(c.x + 0.5f, c.z + 0.5f);
            if (std::fabs(glm::dot(cc - centroid, lat2)) <= coreHalfW + 0.5f &&
                std::fabs((c.y + 0.5f) - kerfY) <= coreHalfH + 0.5f)
                nearD = std::min(nearD, glm::dot(cc, dir2) - 0.5f);
            continue;
        }
        for (Subcube* sc : ch->getStaticSubcubesAt(lp)) {
            if (!sc || sc->getMaterialName().rfind("Log", 0) != 0) continue;
            const glm::vec3 sctr = glm::vec3(c) + glm::vec3(sc->getLocalPosition()) / 3.0f
                                 + glm::vec3(1.0f / 6.0f);
            if (!inCore(sctr)) continue;
            nearD = std::min(nearD, glm::dot(glm::vec2(sctr.x, sctr.z), dir2) - 1.0f / 6.0f);
        }
        for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
        for (int sz = 0; sz < 3; ++sz)
            for (Microcube* mc : ch->getMicrocubesAt(lp, {sx, sy, sz})) {
                if (!mc || mc->getMaterialName().rfind("Log", 0) != 0) continue;
                const glm::vec3 mctr = glm::vec3(c) + glm::vec3(sx, sy, sz) / 3.0f
                    + glm::vec3(mc->getMicrocubeLocalPosition()) / 9.0f + glm::vec3(1.0f / 18.0f);
                if (!inCore(mctr)) continue;
                nearD = std::min(nearD, glm::dot(glm::vec2(mctr.x, mctr.z), dir2) - 1.0f / 18.0f);
            }
    }
    // Core channel already cut through on this line: don't bail — clamp the
    // frontier so the frame math stays sane, let the slot carve nothing, and the
    // rim-sliver fallback below pulverizes whatever the blade actually struck.
    if (nearD > farD) nearD = farD - 0.01f;
    out.fullDepth = farD - nearD;   // REMAINING depth (frontier -> far face)
    out.cutFraction = std::min(1.0f, kerfDepth / out.fullDepth);
    // let the apex pass the far face so a full-depth cut truly severs
    const float depth = std::min(kerfDepth, out.fullDepth + 0.2f);
    const float remaining = out.fullDepth;

    // AXE-BLADE-SIZED bite, not a slab: the notch starts ~2 subcubes wide and
    // ~2 subcubes tall and only FLARES OPEN as the cut gets close to breaking
    // through (a feller widens the notch to finish; and the support flood can
    // only release the tree once the cut spans the trunk). Width/height are
    // driven by the REMAINING depth, so the shape needs no stored state.
    const float spanHalfW = [&] {
        float w = 0.55f;
        for (const auto& c : cells)
            w = std::max(w, std::fabs(glm::dot(glm::vec2(c.x + 0.5f, c.z + 0.5f) - centroid, lat2)) + 0.55f);
        return w;
    }();
    const float open = std::min(1.0f, std::max(0.0f, (1.8f - remaining) / 1.5f)); // 0 fresh -> 1 nearly through
    const float halfW = std::max(0.35f, spanHalfW * open);
    const float mouthHalfH = 0.30f + 0.20f * open;      // notch mouth -> full row when finishing
    const float apexHalfH = 0.06f;

    // The axe removes wood where the BLADE is: with a known contact point the
    // bite window hugs it (±~0.5 m along the cut), so a stationary chopper
    // cannot excavate beyond arm's reach — cutting deeper requires stepping in
    // (design feedback: one spot chunked out a cavern). The caller passes the
    // blade's contact ON the struck cell, so the window always overlaps that
    // wood — and it is NOT capped by kerfDepth: a blade legitimately deeper
    // than the frontier (side-wall contact, stepped-in chop) still bites AT the
    // blade (the cap made those swings carve nothing). Without contact info
    // (headless callers) the window is the classic frontier bite of kerfDepth.
    float dLo = -8.0f, dHi = depth;
    if (hasContact) {
        const float contactD =
            glm::dot(glm::vec2(contactPoint.x, contactPoint.z), dir2) - nearD;
        dLo = contactD - 0.55f;
        dHi = contactD + 0.45f;
        out.contactD = contactD;
    }
    out.nearD = nearD; out.farD = farD; out.dLo = dLo; out.dHi = dHi;
    // Apex taper sized to the bite actually available (window ∩ wood).
    const float tipLen =
        std::min(0.45f, std::max(0.15f, (dHi - std::max(dLo, 0.0f)) * 0.5f));

    auto insideWedge = [&](const glm::vec3& p) {
        const glm::vec2 p2(p.x, p.z);
        const float d = glm::dot(p2, dir2) - nearD;
        if (d < dLo || d > dHi) return false;
        if (std::fabs(glm::dot(p2 - centroid, lat2) - latOff) > halfW) return false;
        const float tipStart = dHi - tipLen;
        const float t = std::min(1.0f, std::max(0.0f, (d - tipStart) / tipLen));
        const float halfH = mouthHalfH + (apexHalfH - mouthHalfH) * t;
        return std::fabs(p.y - kerfY) <= halfH;
    };

    auto isWoodMat = [](const std::string& m) { return m.rfind("Log", 0) == 0; };
    auto subCenter = [](const glm::ivec3& wp, const glm::ivec3& s) {
        return glm::vec3(wp) + glm::vec3(s) / 3.0f + glm::vec3(1.0f / 6.0f);
    };
    auto microCenter = [](const glm::ivec3& wp, const glm::ivec3& s, const glm::ivec3& mm) {
        return glm::vec3(wp) + glm::vec3(s) / 3.0f + glm::vec3(mm) / 9.0f + glm::vec3(1.0f / 18.0f);
    };

    // ---- carve ----
    std::vector<glm::ivec3> seedCells;              // carved cells → collapse seeds
    std::unordered_set<Chunk*> touched;
    for (const glm::ivec3& wp : cells) {
        Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
        if (!ch) continue;
        const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);

        // quick reject: 27 sample points across the cell
        bool cellTouched = false;
        for (int cx = 0; cx <= 2 && !cellTouched; ++cx)
        for (int cy = 0; cy <= 2 && !cellTouched; ++cy)
        for (int cz = 0; cz <= 2 && !cellTouched; ++cz)
            cellTouched = insideWedge(glm::vec3(wp) + glm::vec3(cx, cy, cz) * 0.5f);
        if (!cellTouched) continue;

        // a full cube in the wedge's path becomes 27 subcubes first
        if (ch->getCubeAtFast(lp)) {
            ch->subdivideAt(lp);
            m_cm->updateAfterCubeSubdivision(wp);
        }

        bool carvedHere = false;

        // carve one micro slot: remove micros inside the wedge, then repaint
        // survivors whose face touches the cut as raw heartwood
        auto carveMicroSlot = [&](const glm::ivec3& s) {
            std::vector<glm::ivec3> toRemove;
            for (Microcube* mc : ch->getMicrocubesAt(lp, s)) {
                if (!mc || !isWoodMat(mc->getMaterialName())) continue;
                const glm::ivec3 mm = mc->getMicrocubeLocalPosition();
                if (insideWedge(microCenter(wp, s, mm))) toRemove.push_back(mm);
            }
            for (const auto& mm : toRemove) {
                if (ch->removeMicrocube(lp, s, mm)) { ++out.microsRemoved; carvedHere = true; }
            }
            if (toRemove.empty()) return;
            static const glm::vec3 A[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            for (Microcube* mc : ch->getMicrocubesAt(lp, s)) {
                if (!mc || !isWoodMat(mc->getMaterialName())) continue;
                const glm::vec3 c = microCenter(wp, s, mc->getMicrocubeLocalPosition());
                for (const auto& a : A)
                    if (insideWedge(c + a * (1.0f / 9.0f))) { mc->setMaterialName(kHeartwood); break; }
            }
        };

        // subcube pass (pointer list copied by value; removing the current one is safe)
        for (Subcube* sc : ch->getStaticSubcubesAt(lp)) {
            if (!sc || !isWoodMat(sc->getMaterialName())) continue;
            const glm::ivec3 s = sc->getLocalPosition();
            int in = 0, samples = 0;
            for (int cx = 0; cx <= 1; ++cx)
            for (int cy = 0; cy <= 1; ++cy)
            for (int cz = 0; cz <= 1; ++cz) {
                const glm::vec3 p = glm::vec3(wp) +
                    (glm::vec3(s) + glm::vec3(cx, cy, cz) * 0.94f + glm::vec3(0.03f)) / 3.0f;
                ++samples; if (insideWedge(p)) ++in;
            }
            ++samples; if (insideWedge(subCenter(wp, s))) ++in;
            if (in == 0) continue;
            if (in == samples) {                       // fully inside → whole subcube goes
                if (ch->removeSubcube(lp, s)) { out.microsRemoved += 27; carvedHere = true; }
                continue;
            }
            ch->subdivideSubcubeAt(lp, s);             // partial → refine and carve micros
            carveMicroSlot(s);
        }
        // slots that were already micro-resolution before this call
        for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
        for (int sz = 0; sz < 3; ++sz)
            carveMicroSlot({sx, sy, sz});

        if (!carvedHere) continue;
        out.carved = true;
        seedCells.push_back(wp);
        touched.insert(ch);

        // fully emptied? clear the subdivision placeholder + occupancy
        bool any = ch->getCubeAtFast(lp) != nullptr || !ch->getStaticSubcubesAt(lp).empty();
        for (int sx = 0; sx < 3 && !any; ++sx)
        for (int sy = 0; sy < 3 && !any; ++sy)
        for (int sz = 0; sz < 3 && !any; ++sz)
            any = !ch->getMicrocubesAt(lp, {sx, sy, sz}).empty();
        if (!any) {
            ch->clearSubdivisionAt(lp);
            m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
            ++out.cellsEmptied;
        }
    }
    // Remove ALL wood content from one cell (the axe shatters it outright).
    // Returns micro-equivalents removed. Used for rim slivers and neck shear.
    auto pulverizeCellWood = [&](const glm::ivec3& wp) -> int {
        Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
        if (!ch) return 0;
        const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
        int removed = 0;
        if (ch->getCubeAtFast(lp)) {
            ch->subdivideAt(lp);
            m_cm->updateAfterCubeSubdivision(wp);
        }
        for (Subcube* sc : ch->getStaticSubcubesAt(lp)) {
            if (!sc || sc->getMaterialName().rfind("Log", 0) != 0) continue;
            if (ch->removeSubcube(lp, sc->getLocalPosition())) removed += 27;
        }
        for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
        for (int sz = 0; sz < 3; ++sz) {
            std::vector<glm::ivec3> rm;
            for (Microcube* mc : ch->getMicrocubesAt(lp, {sx, sy, sz}))
                if (mc && mc->getMaterialName().rfind("Log", 0) == 0)
                    rm.push_back(mc->getMicrocubeLocalPosition());
            for (const auto& mm : rm)
                if (ch->removeMicrocube(lp, {sx, sy, sz}, mm)) ++removed;
        }
        if (removed > 0) {
            seedCells.push_back(wp);
            touched.insert(ch);
            bool any = ch->getCubeAtFast(lp) != nullptr || !ch->getStaticSubcubesAt(lp).empty();
            for (int sx = 0; sx < 3 && !any; ++sx)
            for (int sy = 0; sy < 3 && !any; ++sy)
            for (int sz = 0; sz < 3 && !any; ++sz)
                any = !ch->getMicrocubesAt(lp, {sx, sy, sz}).empty();
            if (!any) {
                ch->clearSubdivisionAt(lp);
                m_cm->updateOccupancyVoxel(wp.x, wp.y, wp.z, false);
                ++out.cellsEmptied;
            }
        }
        return removed;
    };

    // Chip a POCKET of micros around a point (fracture ladder §5.H: fractures
    // start at micro scale — a hit never vaporizes a whole cell below the full
    // break threshold). Refines wood near the point, removes the nearest micros
    // (biased slightly along the strike direction), up to maxMicros.
    auto chipPocket = [&](const glm::vec3& at, int maxMicros) -> int {
        constexpr float kR = 0.75f;
        struct MicroRef { glm::ivec3 cell, slot, mm; float score; };
        std::vector<MicroRef> found;
        const glm::vec3 dir3(dir2.x, 0.0f, dir2.y);
        const glm::ivec3 base(static_cast<int>(std::floor(at.x)),
                              static_cast<int>(std::floor(at.y)),
                              static_cast<int>(std::floor(at.z)));
        for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz) {
            const glm::ivec3 wp = base + glm::ivec3(dx, dy, dz);
            Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(wp));
            if (!ch) continue;
            const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
            // refine anything the pocket sphere touches down to micros
            if (ch->getCubeAtFast(lp) &&
                ch->getCubeAtFast(lp)->getMaterialName().rfind("Log", 0) == 0 &&
                glm::distance(glm::clamp(at, glm::vec3(wp), glm::vec3(wp) + 1.0f), at) <= kR) {
                ch->subdivideAt(lp);
                m_cm->updateAfterCubeSubdivision(wp);
            }
            for (Subcube* sc : ch->getStaticSubcubesAt(lp)) {
                if (!sc || sc->getMaterialName().rfind("Log", 0) != 0) continue;
                const glm::vec3 c = glm::vec3(wp) + glm::vec3(sc->getLocalPosition()) / 3.0f
                                  + glm::vec3(1.0f / 6.0f);
                if (glm::distance(c, at) <= kR + 0.2f)
                    ch->subdivideSubcubeAt(lp, sc->getLocalPosition());
            }
            for (int sx = 0; sx < 3; ++sx)
            for (int sy = 0; sy < 3; ++sy)
            for (int sz = 0; sz < 3; ++sz)
                for (Microcube* mc : ch->getMicrocubesAt(lp, {sx, sy, sz})) {
                    if (!mc || mc->getMaterialName().rfind("Log", 0) != 0) continue;
                    const glm::vec3 c = glm::vec3(wp) + glm::vec3(sx, sy, sz) / 3.0f
                        + glm::vec3(mc->getMicrocubeLocalPosition()) / 9.0f + glm::vec3(1.0f / 18.0f);
                    const float d = glm::distance(c, at);
                    if (d > kR) continue;
                    found.push_back({wp, {sx, sy, sz}, mc->getMicrocubeLocalPosition(),
                                     d - 0.25f * glm::dot(c - at, dir3)});
                }
        }
        std::sort(found.begin(), found.end(),
                  [](const MicroRef& a, const MicroRef& b) { return a.score < b.score; });
        out.pocketFound = static_cast<int>(found.size());
        int removed = 0;
        std::unordered_set<Chunk*> pocketChunks;
        for (const MicroRef& r : found) {
            if (removed >= maxMicros) break;
            Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(r.cell));
            if (!ch) continue;
            const glm::ivec3 lp = ChunkManager::worldToLocalCoord(r.cell);
            if (ch->removeMicrocube(lp, r.slot, r.mm)) {
                ++removed;
                pocketChunks.insert(ch);
            }
        }
        for (Chunk* ch : pocketChunks) touched.insert(ch);
        // emptiness finalize per distinct touched cell
        std::unordered_set<int64_t> doneCells;
        for (const MicroRef& r : found) {
            const int64_t k = packVoxel(r.cell.x, r.cell.y, r.cell.z);
            if (!doneCells.insert(k).second) continue;
            Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(r.cell));
            if (!ch) continue;
            const glm::ivec3 lp = ChunkManager::worldToLocalCoord(r.cell);
            seedCells.push_back(r.cell);
            bool any = ch->getCubeAtFast(lp) != nullptr || !ch->getStaticSubcubesAt(lp).empty();
            for (int sx = 0; sx < 3 && !any; ++sx)
            for (int sy = 0; sy < 3 && !any; ++sy)
            for (int sz = 0; sz < 3 && !any; ++sz)
                any = !ch->getMicrocubesAt(lp, {sx, sy, sz}).empty();
            if (!any) {
                ch->clearSubdivisionAt(lp);
                m_cm->updateOccupancyVoxel(r.cell.x, r.cell.y, r.cell.z, false);
                ++out.cellsEmptied;
            }
        }
        return removed;
    };

    // Rim-sliver fallback: the blade stops at its FIRST contact — often a
    // remnant sliver on the notch lip outside the slot's window. If the slot
    // removed nothing, CHIP a micro pocket at the contact (§5.H — never
    // vaporize a whole cell below the break threshold) so the cut progresses.
    if (out.microsRemoved == 0) {
        const glm::vec3 chipAt = hasContact
            ? glm::vec3(contactPoint.x, kerfY, contactPoint.z)
            : glm::vec3(anchor) + glm::vec3(0.5f);
        const int removed = chipPocket(chipAt, 140);
        if (removed > 0) { out.microsRemoved += removed; out.carved = true; }
        out.pocketChipped = removed;
    }

    // NECK SHEAR: a tree cannot hang on a splinter. Survey WHOLE planes in a
    // box around the cut — not just the bite's connected cross-section: a deep
    // notch fragments the plane into islands, and a disconnected sliver island
    // is invisible to the carve flood yet still carries support (live case: two
    // remnant islands held a fully-chopped tree up). And survey a small
    // VERTICAL BAND, not just the anchor row: the blade anchors where it lands
    // (often the fat rooted flare rows) while the actual neck — a few subcubes
    // one row up — sits on a row no swing ever anchors at (live case: a trunk
    // visibly standing on ~5 slivers, never falling). The weakest row with a
    // few subcubes' worth of wood (≲ 0.1 m² carrying tons of tree) shears.
    //
    // The band's structural cells also RE-SEED the release flood below: the
    // flood only evaluates components touching its seeds, so without this the
    // part ABOVE the cut is only re-checked when a bite's rim happens to touch
    // it — a top held by a cargo-only (micro) neck could stand forever. With
    // the re-seed, every biting swing re-evaluates the neighborhood's support,
    // and a cargo neck releases via the ordinary cargo cascade (F6).
    if (out.microsRemoved > 0) {
        int shearUnits = INT_MAX, shearY = anchor.y;
        std::vector<glm::ivec3> shearCells;
        for (int ry = anchor.y - 1; ry <= anchor.y + 2; ++ry) {
            int units = 0;
            std::vector<glm::ivec3> rowCells;
            for (int dx = -3; dx <= 3; ++dx)
            for (int dz = -3; dz <= 3; ++dz) {
                const glm::ivec3 c(anchor.x + dx, ry, anchor.z + dz);
                Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(c));
                if (!ch) continue;
                const glm::ivec3 lp = ChunkManager::worldToLocalCoord(c);
                int n = 0;
                if (ch->getCubeAtFast(lp)) {
                    if (ch->getCubeAtFast(lp)->getMaterialName().rfind("Log", 0) == 0)
                        n += 9;   // full cube = full 3x3 column area
                }
                for (Subcube* sc : ch->getStaticSubcubesAt(lp))
                    if (sc && sc->getMaterialName().rfind("Log", 0) == 0) ++n;
                if (n > 0) {
                    units += n;
                    rowCells.push_back(c);
                    seedCells.push_back(c);   // re-seed the release flood (cheap: flood skips cargo)
                }
            }
            if (units > 0 && units < shearUnits) {
                shearUnits = units; shearY = ry; shearCells = rowCells;
            }
        }
        if (shearUnits > 0 && shearUnits <= 6 && shearUnits != INT_MAX) {
            int sheared = 0;
            for (const glm::ivec3& c : shearCells) sheared += pulverizeCellWood(c);
            if (sheared > 0) {
                out.microsRemoved += sheared;
                LOG_INFO("DamageSystem", "kerf neck shear: {} subcubes of neck snapped at y={}",
                         shearUnits, shearY);
            }
        }
    }
    for (Chunk* ch : touched) m_cm->markChunkDirty(ch);

    // ---- tactile splinters: a few micro chips fly off the notch mouth back
    // toward the chopper. Deliberately FEW (≤6) — impact feedback, not a blast.
    if (out.microsRemoved > 0) {
        const glm::vec3 dir3(dir2.x, 0.0f, dir2.y);
        // Splinters fly from the actual blade contact when known, else from the
        // notch mouth on the (offset) kerf line.
        const glm::vec2 mouth2 = centroid + lat2 * latOff
                               + dir2 * (nearD - glm::dot(centroid, dir2));
        const glm::vec3 mouth = hasContact
            ? glm::vec3(contactPoint.x, kerfY, contactPoint.z)
            : glm::vec3(mouth2.x, kerfY, mouth2.y);
        // Chunky enough to read (0.2 ≈ between micro and subcube), slower arcs so
        // they hang in view (user feedback: splinters weren't visible at 1/9).
        const int n = std::min(8, out.microsRemoved);
        for (int i = 0; i < n; ++i) {
            const glm::vec3 jit(frand(-0.15f, 0.15f), frand(-0.1f, 0.2f), frand(-0.15f, 0.15f));
            const glm::vec3 vel = -dir3 * frand(1.0f, 2.2f)
                                + glm::vec3(0.0f, frand(1.6f, 2.8f), 0.0f)
                                + glm::vec3(frand(-0.9f, 0.9f), 0.0f, frand(-0.9f, 0.9f));
            spawnDebris(mouth + jit - dir3 * 0.15f, vel, 0.2f, kHeartwood);
            ++out.collapse.debrisSpawned;
        }
    }

    // ---- release: no blast — the ordinary support pass decides. While structural
    // wood still bridges the kerf the tree stays anchored; the swing that cuts it
    // through (micro-thin remnants are cargo and carry nothing) releases the tree,
    // which topples coherently about the cut with the chop direction as tip bias.
    if (!seedCells.empty()) {
        const glm::vec3 kerfCenter(centroid.x, kerfY, centroid.y);
        const glm::vec3 dir3(dir2.x, 0.0f, dir2.y);
        const int detached = collapseUnsupported(seedCells, NO_SUPPORT, out.collapse,
                                                 coherentFragments, kerfCenter, dir3);
        out.severed = detached > 0;
    }
    return out;
}

bool DamageSystem::collapseComponentCoherent(const std::vector<glm::ivec3>& component,
                                             const std::vector<glm::ivec3>& leafCargo,
                                             DamageResult& res,
                                             const glm::vec3& impactCenter,
                                             const glm::vec3& impactDir) {
    if (!m_fragMgr || !m_fragMgr->ready() ||
        static_cast<int>(component.size() + leafCargo.size()) > COHERENT_MAX_VOXELS) {
        return false;
    }

    using Clock = std::chrono::high_resolution_clock;
    const auto tGather0 = Clock::now();

    // Gather the whole component's geometry first; if any cell isn't coherently
    // gatherable, bail (scatter) BEFORE removing anything. Leaf-cargo cells ride along:
    // the canopy STAYS WITH the falling tree (F1 — leaves are never voxel debris).
    std::vector<Core::KinematicVoxel> frag;
    frag.reserve(component.size() + leafCargo.size());
    for (const glm::ivec3& v : component) {
        if (!gatherCellVoxels(m_cm, v, frag)) return false;
    }
    for (const glm::ivec3& v : leafCargo) {
        gatherCellVoxels(m_cm, v, frag);   // best-effort: an empty cargo cell is fine
    }
    if (frag.empty()) return false;

    // Wood drives the physics feel (mass/COM/hinge); leaves are render cargo.
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

    // Hinge topple (P2.3): seed a rotation about the CUT so the piece TIPS instead of
    // free-dropping. Tip direction, in precedence order:
    //   1. mass ASYMMETRY — horizontal offset of the wood's mass-weighted COM from the
    //      pivot (the lowest 1-unit band of wood ≈ the cut face): a top-heavy side wins;
    //   2. the CHOP direction (impactDir), when the piece is balanced;
    //   3. horizontally AWAY from the blast center;
    //   4. none of the above -> straight drop (previous behavior).
    // Seed magnitude: a fraction of the rod-topple rate sqrt(3g/L) (uniform rod falling
    // about its end reaches that at ground) — enough to break balance and pick the
    // direction; gravity + contacts do the rest. v = ω × r so the initial motion IS a
    // rotation about the pivot, not a spin-in-place. The subsequent tumble follows the
    // body's REAL inertia tensor (physicalize computes it from the merged boxes), so
    // asymmetric pieces keep tumbling asymmetrically after the seed.
    float mTot = 0.0f;
    glm::vec3 com(0.0f);
    float minY = 1e9f, maxY = -1e9f;
    for (const auto& v : wood) {
        float m = worldMass(v);
        mTot += m;
        com  += v.localPos * m;
        minY = std::min(minY, v.localPos.y);
        maxY = std::max(maxY, v.localPos.y);
    }
    com /= std::max(mTot, 1e-6f);
    float pTot = 0.0f;
    glm::vec3 pivot(0.0f);
    for (const auto& v : wood) {
        if (v.localPos.y < minY + 1.0f) {
            float m = worldMass(v);
            pTot += m;
            pivot += v.localPos * m;
        }
    }
    pivot /= std::max(pTot, 1e-6f);

    glm::vec3 tipDir(0.0f);
    glm::vec3 d = com - pivot; d.y = 0.0f;
    glm::vec3 h = impactDir;   h.y = 0.0f;
    glm::vec3 a = pivot - impactCenter; a.y = 0.0f;
    if      (glm::length(d) > 0.15f) tipDir = glm::normalize(d);
    else if (glm::length(h) > 0.10f) tipDir = glm::normalize(h);
    else if (glm::length(a) > 0.10f) tipDir = glm::normalize(a);

    glm::vec3 linVel(0.0f, -1.0f, 0.0f), angVel(0.0f);
    if (tipDir != glm::vec3(0.0f)) {
        float L = std::max(1.0f, maxY - minY + 1.0f);
        // Seed factor 0.8 of the rod-topple rate (was 0.35): live observation — a tall
        // flat-cut trunk drops onto its stump and the base contacts damp a weak seed to
        // sleep, leaving the tree balanced upright instead of felling. Disclosed tune on
        // the grounded sqrt(3g/L) form.
        float w = glm::clamp(0.8f * std::sqrt(3.0f * 9.81f / L), 0.5f, 2.5f);
        angVel = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), tipDir) * w;
        linVel = glm::cross(angVel, com - pivot);
    }

    // Spawn the body FIRST (physicalize copies the geometry — it does not need the world
    // cells cleared). Only if it succeeds do we remove the world cells. If it fails, the
    // cells are untouched, so we return false and the caller scatters them — NO silent
    // geometry loss (the "silent-drop" class bug the auditor flagged).
    const size_t woodCount = wood.size(), leafCount = leaves.size();
    // COLLISION PROXY (F2 + #13): SUBCUBE resolution — a full cube contributes one
    // unit box; a partial cell contributes one 1/3-box per OCCUPIED subcube slot
    // (occupied = a subcube record, or a quorum of >=4 micros — crumbs don't
    // collide). Greedy-merge keeps the box count bounded (F2's 2005-box pine stays
    // dead). Whole-CELL proxies made a fallen crown an impassable lattice of
    // invisible full-cell collision — the player couldn't approach the visible
    // trunk (live user report). Giant components (megaflora) keep the coarse
    // whole-cell proxy: at 1/3 cell size their bounds overflow the merge grid,
    // which would fall back to per-voxel boxes (the F2 pathology).
    std::vector<Core::KinematicVoxel> collision;
    const bool fineCollision = component.size() <= 800;
    collision.reserve(component.size() * (fineCollision ? 4 : 1));
    for (const glm::ivec3& c : component) {
        Chunk* cch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(c));
        const glm::ivec3 clp = cch ? ChunkManager::worldToLocalCoord(c) : glm::ivec3(0);
        const std::string mat = cellMaterial(m_cm, c);
        if (!fineCollision || !cch || cch->getCubeAtFast(clp)) {
            Core::KinematicVoxel v;
            v.localPos     = glm::vec3(c) + glm::vec3(0.5f);
            v.scale        = glm::vec3(1.0f);
            v.materialName = mat;
            collision.push_back(std::move(v));
            continue;
        }
        bool occupied[3][3][3] = {};
        for (Subcube* sc : cch->getStaticSubcubesAt(clp)) {
            if (!sc) continue;
            const glm::ivec3 s = sc->getLocalPosition();
            occupied[s.x][s.y][s.z] = true;
        }
        for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
        for (int sz = 0; sz < 3; ++sz) {
            if (!occupied[sx][sy][sz] &&
                cch->getMicrocubesAt(clp, {sx, sy, sz}).size() < 4)
                continue;
            Core::KinematicVoxel v;
            v.localPos     = glm::vec3(c) + (glm::vec3(sx, sy, sz) + 0.5f) / 3.0f;
            v.scale        = glm::vec3(1.0f / 3.0f);
            v.materialName = mat;
            collision.push_back(std::move(v));
        }
    }
    // Fragment render = wood + canopy cargo (leaves ride; F3 renders them as cards).
    std::vector<Core::KinematicVoxel> fragVoxels = std::move(wood);
    fragVoxels.insert(fragVoxels.end(),
                      std::make_move_iterator(leaves.begin()),
                      std::make_move_iterator(leaves.end()));
    std::string id = "collapse_" + std::to_string(m_fragSeq++);
    const auto tSpawn0 = Clock::now();
    uint32_t bid = m_fragMgr->spawn(id, std::move(fragVoxels), collision, glm::mat4(1.0f),
                                    linVel, angVel, worldMass);
    if (bid == 0) {
        LOG_WARN("DamageSystem", "coherent collapse: physicalize failed ({} cells) -> scatter",
                 component.size());
        return false;   // cells NOT removed -> caller falls back to per-cell scatter
    }

    const auto tRemove0 = Clock::now();
    {
        // Bulk removal grouped by chunk: one storage pass per chunk instead of a
        // full vector scan PER CELL (removeCellContent/clearSubdivisionAt was the
        // 758 ms topple-start hitch for a ~550-cell tree).
        std::unordered_map<Chunk*, std::vector<glm::ivec3>> byChunk;
        auto queue = [&](const glm::ivec3& v) {
            if (Chunk* ch = m_cm->getChunkAtCoord(ChunkManager::worldToChunkCoord(v)))
                byChunk[ch].push_back(ChunkManager::worldToLocalCoord(v));
            m_cm->updateOccupancyVoxel(v.x, v.y, v.z, false);
        };
        for (const glm::ivec3& v : component) queue(v);
        for (const glm::ivec3& v : leafCargo)  queue(v);
        for (auto& [ch, cells] : byChunk) {
            ch->clearCellsBulk(cells);
            m_cm->markChunkDirty(ch);
        }
    }
    const auto tEnd = Clock::now();
    auto ms = [](auto a, auto b) {
        return std::chrono::duration<float, std::milli>(b - a).count();
    };
    res.debrisSpawned += 1;   // one coherent body
    LOG_INFO("DamageSystem", "coherent collapse: {}+{} cells -> 1 rigid body ({} wood, {} canopy voxels riding)",
             component.size(), leafCargo.size(), woodCount, leafCount);
    // Hitch diagnosis (topple-start spike): which phase eats the frame.
    LOG_INFO("DamageSystem", "coherent timing: gather={}ms spawn(physicalize+faces)={}ms cellRemoval={}ms",
             ms(tGather0, tSpawn0), ms(tSpawn0, tRemove0), ms(tRemove0, tEnd));
    return true;
}

int DamageSystem::collapseUnsupported(const std::vector<glm::ivec3>& removed, float supportY,
                                      DamageResult& res, bool coherent,
                                      const glm::vec3& impactCenter, const glm::vec3& impactDir) {
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

    // F1/F6 — SUPPORT FLOWS THROUGH STRUCTURAL WOOD ONLY. Cargo cells (leaves + micro-
    // only twig wood) are excluded from the support flood entirely: they hang off
    // structural wood, they must not hold a tree up, and they must not bridge support
    // between interpenetrating canopies (the live birch hung off the oak twig-to-twig).
    // They are assigned afterwards as CARGO to whichever wood they are nearest.
    //
    // CASCADE (F6): structural wood reachable only THROUGH cargo cells (a limb beyond a
    // twig gap) is invisible to the rim flood — after collecting the cargo region, any
    // unvisited structural cell adjacent to it becomes a new support seed, and the loop
    // repeats until nothing new turns up. Without this, such wood floats exactly like
    // the pre-F5 ghost canopy.
    std::vector<glm::ivec3> stack;
    std::vector<glm::ivec3> component;
    std::vector<std::vector<glm::ivec3>> detachedComponents;
    std::vector<glm::ivec3> cargoSeeds;              // rim cargo cells (region entry points)
    std::unordered_set<int64_t> detachedSet;         // cells of all detached components

    std::unordered_map<int64_t, glm::ivec3> region;  // the cargo region (grows per round)
    std::deque<glm::ivec3> rq;
    auto addRegion = [&](const glm::ivec3& p) {
        if (!m_cm->hasVoxelAt(p) || !isCargoCell(m_cm, p)) return;
        int64_t k = packVoxel(p.x, p.y, p.z);
        if (region.count(k)) return;
        region[k] = p;
        rq.push_back(p);
    };
    size_t regionDoneComps = 0;   // components whose neighborhood is already in the region

    while (!seeds.empty() && totalDetached < MAX_COLLAPSE) {
        // ---- Support floods for this round's seeds.
        for (const glm::ivec3& seed : seeds) {
            if (totalDetached >= MAX_COLLAPSE) break;
            if (visited.count(packVoxel(seed.x, seed.y, seed.z))) continue;
            if (isCargoCell(m_cm, seed)) { cargoSeeds.push_back(seed); continue; }  // cargo pass

            // Flood-fill this connected solid component (bounded), checking for an anchor.
            component.clear();
            stack.clear();
            stack.push_back(seed);
            visited.insert(packVoxel(seed.x, seed.y, seed.z));
            bool supported = false;
            const char* why = "?";              // which anchor rule fired (flood diagnostics)
            glm::ivec3 whyAt(0);

            bool hasTerrain = false;   // does this component contain a non-tree (terrain) cell?
            while (!stack.empty()) {
                glm::ivec3 v = stack.back(); stack.pop_back();
                if (v.y <= yAnchor) { supported = true; why = "designer-anchor"; whyAt = v; break; }
                component.push_back(v);

                const bool vTree = isTreeCell(m_cm, v);
                if (!vTree) hasTerrain = true;
                // TREE-OBJECT anchor (Phase 2): a tree is rooted to the ground ONLY through its
                // trunk — a Log with terrain directly below. Incidental leaf-or-side terrain
                // contact must NOT anchor a severed top, so a tree cell never propagates the
                // flood into terrain (below); only this rooted-trunk check anchors a tree.
                if (vTree && isLogCell(m_cm, v)) {
                    glm::ivec3 below(v.x, v.y - 1, v.z);
                    if (m_cm->hasVoxelAt(below) && !isTreeCell(m_cm, below) &&
                        trunkRootContact(m_cm, v)) {   // F9: trunk-like wood at the ground, not a twig tip
                        supported = true; why = "rooted-trunk"; whyAt = v; break;
                    }
                }
                // Flooded past the cap → the MAIN MASS. Terrain uses MAX_FLOOD; a pure-tree
                // component (a big canopy) is not ground, so it floods to the higher tree cap.
                const int cap = hasTerrain ? MAX_FLOOD : TREE_MAX_FLOOD;
                if (static_cast<int>(component.size()) > cap) {
                    supported = true; why = hasTerrain ? "flood-cap-terrain" : "flood-cap-tree"; whyAt = v; break;
                }
                for (const auto& n : NB) {
                    glm::ivec3 nb = v + n;
                    int64_t key = packVoxel(nb.x, nb.y, nb.z);
                    if (m_cm->hasVoxelAt(nb)) {
                        if (isCargoCell(m_cm, nb)) continue;              // cargo NEVER transmits support
                        if (vTree && !isTreeCell(m_cm, nb)) continue;     // tree ↛ terrain (side contact)
                        // F8: vertical tree↔tree steps need real structural face
                        // contact — cell adjacency alone conducted support across
                        // carved-out air gaps (top-skin cells over hollowed necks).
                        if (n.y != 0 && vTree && isTreeCell(m_cm, nb) &&
                            !verticalStructuralContact(m_cm, n.y < 0 ? nb : v,
                                                             n.y < 0 ? v : nb))
                            continue;
                        // Reached a voxel already proven part of the main mass →
                        // supported (checked AFTER the conduction rules: an anchored
                        // neighbor only supports us if support could actually flow
                        // through that face).
                        if (anchored.count(key)) { supported = true; why = "main-mass"; whyAt = nb; break; }
                        if (visited.count(key)) continue;
                        visited.insert(key); stack.push_back(nb);
                    }
                }
                if (supported) break;
            }
            // Flood diagnostics: which anchor kept a piece standing (or that it fell) is
            // otherwise invisible — this is the first thing to read when a tree stands
            // after a cut or terrain over-collapses.
            LOG_DEBUG("DamageSystem", "support flood: seed ({},{},{}) -> {} cells {}",
                      seed.x, seed.y, seed.z, component.size(),
                      supported ? (std::string(why) + " at (" + std::to_string(whyAt.x) + "," +
                                   std::to_string(whyAt.y) + "," + std::to_string(whyAt.z) + ")")
                                : std::string("DETACHED"));

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

            // Big detachments are usually wrong (a whole tree released by a nick):
            // dump the root row's anchor state so the failed rooted-trunk check is
            // diagnosable from the log instead of by guesswork.
            if (component.size() > 50) {
                int minY = INT_MAX;
                for (const glm::ivec3& v : component) minY = std::min(minY, v.y);
                int logged = 0;
                for (const glm::ivec3& v : component) {
                    if (v.y != minY || logged >= 6) continue;
                    const glm::ivec3 below(v.x, v.y - 1, v.z);
                    const Cube* bc = m_cm->getCubeAt(below);
                    LOG_INFO("DamageSystem",
                             "DETACH-DIAG root cell ({},{},{}) isLog={} below: has={} tree={} cubeMat={}",
                             v.x, v.y, v.z, isLogCell(m_cm, v),
                             m_cm->hasVoxelAt(below), isTreeCell(m_cm, below),
                             bc ? bc->getMaterialName() : std::string("<no-cube>"));
                    ++logged;
                }
            }
            for (const glm::ivec3& v : component) detachedSet.insert(packVoxel(v.x, v.y, v.z));
            detachedComponents.push_back(component);
        }
        seeds.clear();

        // ---- Grow the cargo region: neighborhoods of the new detached components +
        // this round's rim cargo seeds, then BFS through connected cargo. While
        // draining, collect the next round's seeds — unvisited STRUCTURAL wood
        // touching the region (the cascade).
        for (; regionDoneComps < detachedComponents.size(); ++regionDoneComps)
            for (const glm::ivec3& cell : detachedComponents[regionDoneComps])
                for (const auto& n : NB) addRegion(cell + n);
        for (const glm::ivec3& s : cargoSeeds) addRegion(s);
        cargoSeeds.clear();
        int guard = 0;
        while (!rq.empty() && guard++ < TREE_MAX_FLOOD) {
            glm::ivec3 p = rq.front(); rq.pop_front();
            for (const auto& n : NB) {
                glm::ivec3 nb = p + n;
                addRegion(nb);
                int64_t key = packVoxel(nb.x, nb.y, nb.z);
                if (!visited.count(key) && !anchored.count(key) &&
                    m_cm->hasVoxelAt(nb) && isStructuralLogCell(m_cm, nb)) {
                    seeds.push_back(nb);   // structural wood beyond a cargo gap
                }
            }
        }
    }

    // ---- CARGO ASSIGNMENT: multi-source BFS over the region with ALL sources seeded
    // at distance 0 — cargo adjacent to STANDING structural wood (label STAND, seeded
    // first so equal-distance ties stay with the standing tree) and cargo adjacent to
    // each detached component (label k). Nearest label wins; cargo reached by no
    // source (its wood was destroyed outright) is ORPHANED.
    constexpr int STAND = -1;
    std::vector<std::vector<glm::ivec3>> cargoPer(detachedComponents.size());
    std::vector<glm::ivec3> orphanLeaves;
    std::vector<glm::ivec3> orphanWood;
    {
        std::unordered_map<int64_t, int> label;
        std::deque<std::pair<glm::ivec3, int>> q;
        auto standAdjacent = [&](const glm::ivec3& p) {
            for (const auto& m : NB) {
                glm::ivec3 w = p + m;
                if (!m_cm->hasVoxelAt(w) || !isStructuralLogCell(m_cm, w) ||
                    detachedSet.count(packVoxel(w.x, w.y, w.z))) continue;
                // F8 applies to cargo too: standing wood below/above must reach
                // the shared face. Cell adjacency alone kept carved cargo cells
                // STATIC over an air gap — a clump of micros left hovering above
                // the stump after the fell (live user report).
                if (m.y == -1 && !cellLayerHasStructuralLog(m_cm, w, 2)) continue;
                if (m.y == +1 && !cellLayerHasStructuralLog(m_cm, w, 0)) continue;
                return true;
            }
            return false;
        };
        for (const auto& [k, p] : region) {
            if (standAdjacent(p)) { label[k] = STAND; q.push_back({p, STAND}); }
        }
        for (size_t ci = 0; ci < detachedComponents.size(); ++ci) {
            for (const glm::ivec3& cell : detachedComponents[ci]) {
                for (const auto& n : NB) {
                    glm::ivec3 p = cell + n;
                    int64_t k = packVoxel(p.x, p.y, p.z);
                    if (!region.count(k) || label.count(k)) continue;
                    label[k] = static_cast<int>(ci);
                    q.push_back({p, static_cast<int>(ci)});
                }
            }
        }
        int guard = 0;
        while (!q.empty() && guard++ < TREE_MAX_FLOOD) {
            auto [p, l] = q.front(); q.pop_front();
            for (const auto& n : NB) {
                glm::ivec3 nb = p + n;
                int64_t k = packVoxel(nb.x, nb.y, nb.z);
                if (!region.count(k) || label.count(k)) continue;
                label[k] = l;
                q.push_back({nb, l});
            }
        }
        for (const auto& [k, p] : region) {
            auto it = label.find(k);
            if (it == label.end()) {
                // No wood reached it. Leaf-pure orphans vanish silently (foliage);
                // orphans CONTAINING wood (severed twig clusters) fall as pieces (F6).
                (scanCellTree(m_cm, p).log ? orphanWood : orphanLeaves).push_back(p);
            }
            else if (it->second >= 0)     cargoPer[it->second].push_back(p);
            // STAND cargo stays untouched.
        }
    }

    // ---- Process detached components with their canopy cargo.
    for (size_t k = 0; k < detachedComponents.size(); ++k) {
        if (totalDetached >= MAX_COLLAPSE) break;
        const auto& comp  = detachedComponents[k];
        const auto& cargo = cargoPer[k];
        if (coherent && collapseComponentCoherent(comp, cargo, res, impactCenter, impactDir)) {
            totalDetached += static_cast<int>(comp.size() + cargo.size());
        } else {
            for (const glm::ivec3& v : comp) {
                if (totalDetached >= MAX_COLLAPSE) break;
                totalDetached += dropDetachedCell(v, res);
            }
            // Scatter fallback: the canopy is removed WITHOUT voxel debris (leaves are
            // foliage — they must never appear as voxel debris).
            for (const glm::ivec3& v : cargo) { removeCellContent(m_cm, v); totalDetached++; }
        }
    }
    // Orphaned leaves (their wood was destroyed outright): removed, no voxel debris.
    for (const glm::ivec3& v : orphanLeaves) { removeCellContent(m_cm, v); totalDetached++; }

    // Orphaned WOOD (F6): severed twig clusters with no structural wood left nearby —
    // e.g. a long micro-resolution branch whose root cell was blasted. Each connected
    // cluster falls as its own coherent piece (scatter fallback if it can't cohere).
    if (!orphanWood.empty()) {
        std::unordered_set<int64_t> pool;
        for (const glm::ivec3& p : orphanWood) pool.insert(packVoxel(p.x, p.y, p.z));
        static const std::vector<glm::ivec3> kNoCargo;
        for (const glm::ivec3& start : orphanWood) {
            if (totalDetached >= MAX_COLLAPSE) break;
            if (!pool.count(packVoxel(start.x, start.y, start.z))) continue;
            component.clear();
            stack.clear();
            stack.push_back(start);
            pool.erase(packVoxel(start.x, start.y, start.z));
            while (!stack.empty()) {
                glm::ivec3 v = stack.back(); stack.pop_back();
                component.push_back(v);
                for (const auto& n : NB) {
                    glm::ivec3 nb = v + n;
                    int64_t key = packVoxel(nb.x, nb.y, nb.z);
                    if (pool.count(key)) { pool.erase(key); stack.push_back(nb); }
                }
            }
            if (coherent && collapseComponentCoherent(component, kNoCargo, res, impactCenter, impactDir)) {
                totalDetached += static_cast<int>(component.size());
            } else {
                for (const glm::ivec3& v : component) {
                    if (totalDetached >= MAX_COLLAPSE) break;
                    totalDetached += dropDetachedCell(v, res);
                }
            }
        }
    }

    res.voxelsBroken += totalDetached;
    if (totalDetached >= MAX_COLLAPSE)
        LOG_WARN("DamageSystem", "collapse hit hard cap ({} voxels) — chain reaction truncated", MAX_COLLAPSE);
    else if (totalDetached > 0)
        LOG_INFO("DamageSystem", "collapse: {} voxels detached and fell", totalDetached);
    return totalDetached;
}

} // namespace Phyxel
