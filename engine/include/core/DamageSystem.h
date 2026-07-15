#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace Phyxel {

class ChunkManager;
class GpuParticlePhysics;
namespace Core { class CoherentFragmentManager; }

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
    // Structural integrity (default ON). After the blast, each connected voxel
    // group bordering the hole is flood-filled (bounded):
    //   - floods PAST the cap (MAX_FLOOD)  → it's the MAIN MASS → supported (anchor).
    //   - fully enclosed UNDER the cap      → genuinely severed → detaches and falls.
    // This "connected to the main mass" anchor handles caverns, below-ground, and
    // floating terrain (no ground-plane assumption). `supportY` is an OPTIONAL extra
    // anchor (solid voxels at Y <= supportY are always supported — designer pinning).
    // Per-event collapse is hard-capped (MAX_COLLAPSE) as a chain-reaction stop-gap.
    // Pass collapse=false to disable (e.g. pure cosmetic blasts).
    static constexpr float NO_SUPPORT = -1.0e8f;
    DamageResult applyDamage(const glm::vec3& center, float radius, float energy,
                             const std::string& damageType = "force",
                             const glm::vec3& direction = glm::vec3(0.0f),
                             float supportY = NO_SUPPORT,
                             bool  collapse = true,
                             bool  coherentFragments = false);

    // Sink for coherent-collapse: when set (and coherentFragments=true), a severed
    // component small enough is toppled as ONE rigid slab via this manager instead of
    // scattered into particles (docs/DestructionSystemV2.md §5.B, P1.2b). Non-owning.
    void setFragmentManager(Core::CoherentFragmentManager* m) { m_fragMgr = m; }

    // Per-material destruction response (the tunable knobs). Public + queryable so
    // Phase 0 can unit-test that materials.json "break" profiles drive it (data-driven).
    struct MatResponse {
        float toughness;    // energy needed to break one voxel
        float s1;           // overkill ratio: >= s1 → shatter to subcubes
        float s2;           // overkill ratio: >= s2 → shatter to microcubes
        float absorption;   // shielding: energy lost per solid voxel in the way
    };
    // Resolve a material's break response: reads its materials.json "break" block
    // when present, else a bondStrength-derived fallback. docs/DestructionSystemV2.md §5.A.
    MatResponse responseFor(const std::string& materialName) const;

private:

    // Count solid voxels strictly between two world points (shielding ray-march).
    int solidVoxelsBetween(const glm::vec3& a, const glm::vec3& b) const;

    // P3: detach connected voxel groups bordering the removed set that can't reach
    // an anchor (solid voxel at Y <= supportY). Detached voxels fall as debris — or,
    // when `coherent` and a fragment manager is set, a small severed component topples
    // as ONE coherent rigid slab (P1.2b) instead of scattering. `impactCenter`/`impactDir`
    // carry the blast context down for the hinge-topple direction (P2.3).
    void collapseUnsupported(const std::vector<glm::ivec3>& removed, float supportY,
                             DamageResult& res, bool coherent,
                             const glm::vec3& impactCenter, const glm::vec3& impactDir);

    // Try to topple one severed component as a coherent rigid slab via m_fragMgr.
    // Returns true if it was physicalized (cells removed + body spawned); false if
    // the caller should fall back to per-cell scatter. The body is seeded with a HINGE
    // rotation about the cut (P2.3): tip direction = the wood's COM asymmetry off the
    // pivot, falling back to the chop direction, then away-from-blast.
    bool collapseComponentCoherent(const std::vector<glm::ivec3>& component, DamageResult& res,
                                   const glm::vec3& impactCenter, const glm::vec3& impactDir);

    // Remove ALL content at one world cell — a full cube OR a sub-voxel
    // subdivision (subcubes/microcubes, e.g. a tree) — and spawn falling
    // debris. Returns 1 if the cell held content, else 0. Used by the collapse
    // pass so sub-voxel structures detach like full-cube ones.
    int dropDetachedCell(const glm::ivec3& wp, DamageResult& res);

    // Queue one debris piece into the GPU particle system.
    void spawnDebris(const glm::vec3& pos, const glm::vec3& vel, float scale,
                     const std::string& material);

    float frand();
    float frand(float lo, float hi);

    ChunkManager*       m_cm  = nullptr;
    GpuParticlePhysics* m_gpu = nullptr;
    Core::CoherentFragmentManager* m_fragMgr = nullptr;  // coherent-collapse sink (optional)
    uint32_t            m_rng = 0x51ED2700u;
    uint32_t            m_fragSeq = 0;   // unique-id counter for coherent collapse bodies

    // Tunables
    static constexpr float FALLOFF_P     = 1.5f;   // distance falloff sharpness
    static constexpr float BASE_SPEED    = 4.0f;   // debris launch speed scale
    static constexpr int   MAX_DEBRIS    = 4000;   // cap per applyDamage (keep < particle cap)
    static constexpr int   SUBCUBE_PIECES   = 12;  // pieces spawned per shattered cube
    static constexpr int   MICROCUBE_PIECES = 24;
    static constexpr int   MAX_FLOOD        = 3000; // cap per connected-component flood (terrain = main mass past this)
    // A PURE-TREE (Log/Leaf) component isn't ground, so "big" ≠ "main mass" for it — a large
    // canopy floods to this higher cap and is anchored only by a rooted trunk (Phase 2).
    static constexpr int   TREE_MAX_FLOOD   = 20000;
    static constexpr int   MAX_COLLAPSE     = 6000; // cap total detached voxels per blast
    // Coherent collapse: a severed component up to this many cells topples as one rigid
    // slab; bigger falls back to scatter. Placeholder until the P1.3 Release benchmark.
    static constexpr int   COHERENT_MAX_VOXELS = 2000;
};

} // namespace Phyxel
