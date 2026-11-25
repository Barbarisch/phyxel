#include "core/DamageSystem.h"
#include "core/DamageStage.h"
#include "core/ChunkManager.h"
#include "core/MaterialRegistry.h"
#include "core/GpuParticlePhysics.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "core/CoherentFragmentManager.h"
#include "core/KinematicVoxelManager.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <atomic>
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

DamageSystem::MatResponse DamageSystem::responseFor(const std::string& materialName) {
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

uint8_t DamageSystem::displayStage(const std::string& materialName,
                                   float accumulatedDamage,
                                   int   stageMax) {
    if (stageMax < 0) stageMax = Core::damageStagesVisible();
    // The denominator is the material's own break toughness, so "stage N of M" means the same
    // fraction of the way to failure on Glass as on Steel. responseFor guarantees it is > 0
    // (a break block supplies it, or the bondStrength fallback does with a 0.05 floor).
    return Core::damageStage(accumulatedDamage, responseFor(materialName).toughness, stageMax);
}

uint8_t DamageSystem::displayStageBits(const std::string& materialName,
                                       float accumulatedDamage) {
    return Core::packDamageStage(displayStage(materialName, accumulatedDamage));
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

static std::atomic<uint64_t> s_refusedDebris{0};

uint64_t DamageSystem::refusedDebrisTotal() { return s_refusedDebris.load(std::memory_order_relaxed); }

bool DamageSystem::spawnBreakDebris(GpuParticlePhysics* gpu, const glm::vec3& centre, const glm::vec3& vel,
                                    float scale, const std::string& material, const glm::vec3& angularVel) {
    if (!gpu || !gpu->isInitialized()) { s_refusedDebris.fetch_add(1, std::memory_order_relaxed); return false; }
    GpuParticlePhysics::SpawnParams sp;
    sp.position     = centre;
    sp.velocity     = vel;
    sp.angularVel   = angularVel;
    sp.scale        = glm::vec3(scale);
    sp.materialName = material;
    sp.lifetime     = 25.0f;
    sp.slice        = debrisSliceFor(centre, scale);
    gpu->queueSpawn(sp);
    return true;
}

uint32_t DamageSystem::debrisSliceFor(const glm::vec3& centre, float scale) {
    if (scale >= 0.8f) return 0u;                          // full cube: shows the whole face
    const int n = (scale < 0.2f) ? 9 : 3;                  // micro grid or subcube grid
    const glm::vec3 rel = centre - glm::floor(centre);     // centre inside its parent cube, [0,1)
    glm::ivec3 k = glm::ivec3(glm::floor(rel * static_cast<float>(n)));
    k = glm::clamp(k, glm::ivec3(0), glm::ivec3(n - 1));
    const glm::ivec3 m = (n == 9) ? k : k * 3;             // micro coords 0..8
    return static_cast<uint32_t>(m.x * 81 + m.y * 9 + m.z);
}

void DamageSystem::spawnDebris(const glm::vec3& pos, const glm::vec3& vel, float scale,
                               const std::string& material) {
    spawnBreakDebris(m_gpu, pos, vel, scale, material,
                     glm::vec3(frand(-4.0f, 4.0f), frand(-4.0f, 4.0f), frand(-4.0f, 4.0f)));
}


namespace { DamageSystem::BlastListener g_blastListener; }
void DamageSystem::setBlastListener(BlastListener l) { g_blastListener = std::move(l); }

float DamageSystem::blastSpeed(float energy) {
    if (!(energy > 0.0f)) return 0.0f;
    const float t = std::max(responseFor(IMPULSE_REF_MATERIAL).toughness, 1.0f);
    return BASE_SPEED * std::sqrt(energy / t);
}

float DamageSystem::blastImpulse(float energy) {
    if (!(energy > 0.0f)) return 0.0f;
    const auto* ref = Core::MaterialRegistry::instance().getMaterial(IMPULSE_REF_MATERIAL);
    const float m = (ref && ref->physics.mass > 0.0f) ? ref->physics.mass : 6.0f;   // materials.json Stone
    const float t = std::max(responseFor(IMPULSE_REF_MATERIAL).toughness, 1.0f);
    return m * BASE_SPEED * std::sqrt(energy / t);
}

namespace {
DamageSystem::DestructionOverride g_destructionOverride;   // F7.5
bool g_destructionOverrideStub = false;
}  // namespace

void DamageSystem::setDestructionOverride(DestructionOverride fn, bool stub) {
    g_destructionOverride = std::move(fn);
    g_destructionOverrideStub = stub;
}
bool DamageSystem::destructionOverrideActive() { return g_destructionOverride && !g_destructionOverrideStub; }

DamageResult DamageSystem::applyDamage(const glm::vec3& center, float radius, float energy,
                                       const std::string& /*damageType*/, const glm::vec3& direction,
                                       float supportY, bool collapse, bool coherentFragments,
                                       const glm::vec3& radii) {
    DamageResult res;
    if (!m_cm || energy <= 0.0f) return res;
    if (destructionOverrideActive() && g_destructionOverride(center, radius, energy, direction, res))
        return res;   // F7.5: the switched world broke (fracture + bodies); today's carving does not run
    if (g_blastListener && m_pushExisting) {   // WaterCore E1: the water feels every blast
        const float rEff = std::max(radius, std::max(radii.x, std::max(radii.y, radii.z)));
        g_blastListener(center, rEff, energy);
    }

    // Ellipsoid radii (§15.6 B): any positive component activates a shaped blast; otherwise
    // fall back to a sphere of scalar `radius`. Distance is normalized by R per axis below, so
    // a thin/wide disc removes a flat slice, a wall removes a slab, etc. Floor each axis so a
    // zero-thickness axis can't divide-by-zero.
    const bool shaped = (radii.x > 0.0f || radii.y > 0.0f || radii.z > 0.0f);
    const glm::vec3 R = shaped ? glm::max(radii, glm::vec3(0.05f)) : glm::vec3(radius);
    if (R.x <= 0.0f || R.y <= 0.0f || R.z <= 0.0f) return res;

    // (Today's cube scan, break application, collapse and batched re-mesh ran here. They read the Chunk store, which
    // D4 deleted; the switched world breaks through the destruction override above.)

    // ---- Phase 4: push what already moves. Runs for EVERY blast, including one that breaks
    // nothing (a spell landing on the ground beside a settled pile - the user report that
    // started Phase 4). The new debris this blast spawned is not kicked twice: the GPU skips
    // bodies that have never ticked (spawn age 0), and it carries its own launch velocity.
    res.impulse       = blastImpulse(energy);
    res.impulseRadius = std::max(R.x, std::max(R.y, R.z)) * IMPULSE_RADIUS_SCALE;
    if (m_pushExisting && m_gpu && m_gpu->isInitialized())
        res.impulseQueued = m_gpu->applyRadialImpulse(center, res.impulseRadius, res.impulse,
                                                      IMPULSE_UP_BIAS).queued;
    if (m_pushExisting && m_cm->physicsWorld && m_cm->physicsWorld->getVoxelWorld())
        res.cpuBodiesPushed = m_cm->physicsWorld->getVoxelWorld()->applyImpulse(
            center, res.impulseRadius, res.impulse, IMPULSE_UP_BIAS, glm::vec3(0.0f),
            DebrisShared::IMPULSE_RADIAL);

    LOG_INFO("DamageSystem", "applyDamage E={} r={} -> broken={} grazed={} debris={} push J={} r={} gpu={} cpu={}",
             energy, radius, res.voxelsBroken, res.voxelsGrazed, res.debrisSpawned,
             res.impulse, res.impulseRadius, res.impulseQueued, res.cpuBodiesPushed);
    (void)supportY; (void)collapse; (void)coherentFragments;
    return res;
}

// TREE matter (Phase 2 tree-object flood): the switched world answers (F7.14, ChunkManager::setVoxFoundTreeCell).
// Today's chunk scan (cube / subcube / microcube Log* / Leaf*) went with D4; without the hook nothing is tree matter.
bool DamageSystem::isTreeMatterCell(ChunkManager* cm, const glm::ivec3& wp) {
    if (cm && cm->voxFoundTreeCell()) return cm->voxFoundTreeCell()(wp);
    return false;
}

// §15.6 C — fold tiny, spatially-contained satellite components into the largest one so a
// felled tree lands as ONE body instead of shedding "floating branch" tips (see header doc).
void DamageSystem::consolidateSatelliteComponents(std::vector<std::vector<glm::ivec3>>& components,
                                                  int maxSatelliteCells) {
    if (components.size() < 2) return;

    // Largest component = the crown/trunk that everything else folds into.
    size_t big = 0;
    for (size_t i = 1; i < components.size(); ++i)
        if (components[i].size() > components[big].size()) big = i;
    if (components[big].empty()) return;

    // Expanded bounds of the crown (±2 cells of slack for the sub-voxel branch gaps).
    glm::ivec3 blo = components[big].front(), bhi = components[big].front();
    for (const glm::ivec3& c : components[big]) { blo = glm::min(blo, c); bhi = glm::max(bhi, c); }
    blo -= glm::ivec3(2); bhi += glm::ivec3(2);

    // A satellite folds in if it is small AND fully inside the crown's expanded bounds.
    // Scale the size gate with the crown so a big fell still absorbs proportionally-small tips.
    const size_t satMax = std::max<size_t>(
        static_cast<size_t>(std::max(0, maxSatelliteCells)), components[big].size() / 4);

    std::vector<glm::ivec3> crown = std::move(components[big]);
    std::vector<std::vector<glm::ivec3>> kept;
    kept.reserve(components.size());
    for (size_t i = 0; i < components.size(); ++i) {
        if (i == big) continue;
        std::vector<glm::ivec3>& comp = components[i];
        if (comp.empty()) continue;
        glm::ivec3 clo = comp.front(), chi = comp.front();
        for (const glm::ivec3& c : comp) { clo = glm::min(clo, c); chi = glm::max(chi, c); }
        const bool contained = clo.x >= blo.x && chi.x <= bhi.x &&
                               clo.y >= blo.y && chi.y <= bhi.y &&
                               clo.z >= blo.z && chi.z <= bhi.z;
        if (comp.size() <= satMax && contained)
            crown.insert(crown.end(), comp.begin(), comp.end());   // fold the tip into the crown
        else
            kept.push_back(std::move(comp));                       // distinct piece — keep it
    }
    kept.push_back(std::move(crown));
    components.swap(kept);
}

} // namespace Phyxel
