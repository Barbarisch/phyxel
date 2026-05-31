#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace Phyxel {

class ChunkManager;
class GpuParticlePhysics;

// Result of one area-damage application.
struct DamageResult {
    int voxelsBroken = 0;   // static voxels removed
    int voxelsGrazed = 0;   // in range but under break threshold (future: accumulate damage)
    int debrisSpawned = 0;  // dynamic pieces queued (cubes/subcubes/microcubes)
};

// P1 destruction core (see docs/DestructionSystem.md). Applies a shaped energy
// hit to the voxel field: energy radiates from the impact, attenuated by
// distance and by solid voxels in the way (shielding). Voxels whose received
// energy exceeds their material toughness break; the overkill ratio decides
// whether they pop off intact or shatter into subcubes/microcubes. Broken
// static voxels are removed from the chunk and respawned as GPU debris.
class DamageSystem {
public:
    DamageSystem(ChunkManager* chunkManager, GpuParticlePhysics* gpu)
        : m_cm(chunkManager), m_gpu(gpu) {}

    // Apply a radial energy hit at `center`. `direction` biases debris (and can
    // be (0,0,0) for a pure radial blast). `damageType` is informational for now.
    //
    // supportY (P3 structural integrity): if set above NO_SUPPORT, after the blast
    // any connected voxel group bordering the hole that can't reach a solid voxel
    // at world-Y <= supportY (the "anchor"/ground) — and is within MAX_FLOOD — is
    // detached and falls as dynamic debris. Default = disabled (opt-in, safe).
    static constexpr float NO_SUPPORT = -1.0e8f;
    DamageResult applyDamage(const glm::vec3& center, float radius, float energy,
                             const std::string& damageType = "force",
                             const glm::vec3& direction = glm::vec3(0.0f),
                             float supportY = NO_SUPPORT);

private:
    // Per-material destruction response (the tunable knobs).
    struct MatResponse {
        float toughness;    // energy needed to break one voxel
        float s1;           // overkill ratio: >= s1 → shatter to subcubes
        float s2;           // overkill ratio: >= s2 → shatter to microcubes
        float absorption;   // shielding: energy lost per solid voxel in the way
    };
    MatResponse responseFor(const std::string& materialName) const;

    // Count solid voxels strictly between two world points (shielding ray-march).
    int solidVoxelsBetween(const glm::vec3& a, const glm::vec3& b) const;

    // P3: detach connected voxel groups bordering the removed set that can't reach
    // an anchor (solid voxel at Y <= supportY). Detached voxels fall as debris.
    void collapseUnsupported(const std::vector<glm::ivec3>& removed, float supportY, DamageResult& res);

    // Queue one debris piece into the GPU particle system.
    void spawnDebris(const glm::vec3& pos, const glm::vec3& vel, float scale,
                     const std::string& material);

    float frand();
    float frand(float lo, float hi);

    ChunkManager*       m_cm  = nullptr;
    GpuParticlePhysics* m_gpu = nullptr;
    uint32_t            m_rng = 0x51ED2700u;

    // Tunables
    static constexpr float FALLOFF_P     = 1.5f;   // distance falloff sharpness
    static constexpr float BASE_SPEED    = 4.0f;   // debris launch speed scale
    static constexpr int   MAX_DEBRIS    = 4000;   // cap per applyDamage (keep < particle cap)
    static constexpr int   SUBCUBE_PIECES   = 12;  // pieces spawned per shattered cube
    static constexpr int   MICROCUBE_PIECES = 24;
    static constexpr int   MAX_FLOOD        = 3000; // cap per connected-component flood
    static constexpr int   MAX_COLLAPSE     = 6000; // cap total detached voxels per blast
};

} // namespace Phyxel
