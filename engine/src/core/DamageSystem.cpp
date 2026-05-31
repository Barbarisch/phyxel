#include "core/DamageSystem.h"
#include "core/ChunkManager.h"
#include "core/MaterialRegistry.h"
#include "core/GpuParticlePhysics.h"
#include "core/Cube.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {

float DamageSystem::frand() {
    m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
    return (m_rng & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}
float DamageSystem::frand(float lo, float hi) { return lo + (hi - lo) * frand(); }

DamageSystem::MatResponse DamageSystem::responseFor(const std::string& materialName) const {
    // Base toughness from the material's bondStrength; per-material brittleness
    // (s1/s2 — lower = shatters into fine pieces more readily) and absorption.
    float bond = 0.5f;
    const auto* def = Core::MaterialRegistry::instance().getMaterial(materialName);
    if (def) bond = std::max(0.05f, def->physics.bondStrength);

    MatResponse r;
    // Default (balanced) — toughness derived from bondStrength for unlisted materials.
    r.toughness = bond * 120.0f;
    r.s1 = 2.5f; r.s2 = 6.0f; r.absorption = 0.6f;

    // Tuned exemplars — distinct toughness + brittleness so materials FEEL different.
    if (materialName == "Stone") {        // tough, brittle → shatters fine, shields well
        r.toughness = 110.0f; r.s1 = 1.8f; r.s2 = 4.0f; r.absorption = 0.9f;
    } else if (materialName == "Glass") { // weak, extremely brittle → powders instantly
        r.toughness = 35.0f;  r.s1 = 1.3f; r.s2 = 2.2f; r.absorption = 0.3f;
    } else if (materialName == "Wood") {  // medium, ductile → stays chunky, light shielding
        r.toughness = 70.0f;  r.s1 = 3.5f; r.s2 = 9.0f; r.absorption = 0.45f;
    } else if (materialName == "Metal") { // toughest, ductile → resists, big chunks, strong shield
        r.toughness = 200.0f; r.s1 = 4.5f; r.s2 = 11.0f; r.absorption = 1.2f;
    } else if (materialName == "Dirt") {  // weak, crumbly → subcubes easily, low shield
        r.toughness = 45.0f;  r.s1 = 1.6f; r.s2 = 4.0f; r.absorption = 0.4f;
    }
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
                                       const std::string& /*damageType*/, const glm::vec3& direction) {
    DamageResult res;
    if (!m_cm || radius <= 0.0f || energy <= 0.0f) return res;

    glm::vec3 dirBias = (glm::length(direction) > 1e-3f) ? glm::normalize(direction) : glm::vec3(0.0f);

    glm::ivec3 lo(static_cast<int>(std::floor(center.x - radius)),
                  static_cast<int>(std::floor(center.y - radius)),
                  static_cast<int>(std::floor(center.z - radius)));
    glm::ivec3 hi(static_cast<int>(std::ceil(center.x + radius)),
                  static_cast<int>(std::ceil(center.y + radius)),
                  static_cast<int>(std::ceil(center.z + radius)));

    for (int x = lo.x; x <= hi.x; ++x)
    for (int y = lo.y; y <= hi.y; ++y)
    for (int z = lo.z; z <= hi.z; ++z) {
        glm::ivec3 wp(x, y, z);
        Cube* cube = m_cm->getCubeAt(wp);
        if (!cube) continue;

        glm::vec3 vc(x + 0.5f, y + 0.5f, z + 0.5f);
        float dist = glm::distance(center, vc);
        if (dist > radius) continue;

        float fall = std::pow(std::max(0.0f, 1.0f - dist / radius), FALLOFF_P);
        if (fall <= 0.0f) continue;

        std::string mat = cube->getMaterialName();
        MatResponse mr = responseFor(mat);
        int shield = solidVoxelsBetween(center, vc);
        float reached = energy * fall * std::exp(-mr.absorption * static_cast<float>(shield));

        // Damage accumulation: prior sub-threshold hits add to this one. The voxel
        // breaks once total energy exceeds toughness, so repeated weak hits chip
        // through. Tier is based on total energy at break (chip = chunky, big hit = dust).
        float effective = reached + cube->getAccumulatedDamage();
        float ratio = effective / mr.toughness;

        if (ratio < 1.0f) {
            cube->addDamage(reached);   // weakened but intact (cracks; visual feedback = P4)
            res.voxelsGrazed++;
            continue;
        }

        // Outward launch direction (radial + optional hit-direction bias).
        glm::vec3 outDir = (dist > 1e-3f) ? (vc - center) / dist : glm::vec3(0.0f, 1.0f, 0.0f);
        outDir = glm::normalize(outDir + dirBias * 0.5f);
        float speed = BASE_SPEED * std::sqrt(ratio);

        // Remove the static voxel (fast: marks chunk dirty, defers face rebuild)
        // and clear its collision occupancy cell.
        m_cm->removeCubeFast(wp);
        if (m_gpu) m_gpu->setOccupied(x, y, z, false);
        res.voxelsBroken++;

        if (res.debrisSpawned >= MAX_DEBRIS) continue; // capped: voxel still removed

        // Tier the shatter by overkill ratio.
        if (ratio < mr.s1) {
            // Intact dynamic cube.
            spawnDebris(vc, outDir * speed, 1.0f, mat);
            res.debrisSpawned++;
        } else if (ratio < mr.s2) {
            // Shatter into subcubes (1/3).
            for (int i = 0; i < SUBCUBE_PIECES && res.debrisSpawned < MAX_DEBRIS; ++i) {
                glm::vec3 jitter(frand(-0.4f, 0.4f), frand(-0.4f, 0.4f), frand(-0.4f, 0.4f));
                glm::vec3 v = outDir * speed * frand(0.7f, 1.3f) + jitter * speed;
                spawnDebris(vc + jitter, v, 1.0f / 3.0f, mat);
                res.debrisSpawned++;
            }
        } else {
            // Pulverize into microcubes (1/9), sampled + capped.
            for (int i = 0; i < MICROCUBE_PIECES && res.debrisSpawned < MAX_DEBRIS; ++i) {
                glm::vec3 jitter(frand(-0.45f, 0.45f), frand(-0.45f, 0.45f), frand(-0.45f, 0.45f));
                glm::vec3 v = outDir * speed * frand(0.6f, 1.5f) + jitter * speed * 1.5f;
                spawnDebris(vc + jitter, v, 1.0f / 9.0f, mat);
                res.debrisSpawned++;
            }
        }
    }

    LOG_INFO("DamageSystem", "applyDamage E={} r={} -> broken={} grazed={} debris={}",
             energy, radius, res.voxelsBroken, res.voxelsGrazed, res.debrisSpawned);
    return res;
}

} // namespace Phyxel
