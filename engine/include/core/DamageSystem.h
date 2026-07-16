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

    // STRUCTURAL wood test at any granularity (F6): true when the cell holds Log*
    // at cube or subcube cross-section (micro-only twig wood is cargo, not trunk).
    // Exposed for gameplay callers (axe chop) so trunk detection/measurement agrees
    // with the support flood's notion of a trunk. When `logMaterial` is non-null and
    // the cell is structural wood, it receives the wood's material name (e.g.
    // "LogBirch") for events/VFX.
    static bool isStructuralWoodCell(ChunkManager* cm, const glm::ivec3& wp,
                                     std::string* logMaterial = nullptr);
    // Wood at ANY granularity, including micro-only cells. The blade must be
    // able to bite wood the player can SEE — chip pockets convert surfaces to
    // micros, and a structural-only test made chopped surfaces undetectable
    // (swing 1 puffs, every later swing silently whiffs).
    static bool isWoodCellAny(ChunkManager* cm, const glm::ivec3& wp,
                              std::string* logMaterial = nullptr);
    // Tight AABB of the Log* content inside one cell (any granularity). False if
    // the cell holds no wood. Blade-contact detection clamps the axe head to
    // THIS, not the cell box: a flare/notch cell's wood can start a third of a
    // meter inside the cell, and a contact point on the cell face leaves the
    // blade-hugging bite window mostly in air (live: 14-micro nibbles at the
    // flare instead of real bites).
    static bool woodBoundsInCell(ChunkManager* cm, const glm::ivec3& wp,
                                 glm::vec3& outMin, glm::vec3& outMax,
                                 std::string* logMaterial = nullptr);

    // ---- Axe-chop kerf: FRACTURE, not blast (docs/DestructionSystemV2.md §5.E) ----
    // Carve one axe bite into the trunk cross-section at hitCell's height, entering
    // from the chopDir side (the chopper's side). STATELESS: each call bites
    // `kerfDepth` world-units from the CURRENT notch frontier (the first structural
    // wood along chopDir from hitCell), so repeated swings deepen the cut with no
    // caller-side bookkeeping. The bite is a slot carved at MICROCUBE resolution:
    // bitten cubes/subcubes are subdivided, micros inside the slot are removed, and
    // surviving wood on the cut faces is re-materialed to LogHeartwood (raw cut
    // wood). Enclosed hollow cells inside the trunk shell at the cut plane are
    // filled with heartwood first, so the kerf exposes solid wood — trees are
    // shells by construction. NO energy blast and NO debris scatter: after carving,
    // the swing's carved cells seed the ordinary support-collapse pass, so the
    // moment the kerf (plus cargo-thin remnants) stops carrying support, the tree
    // releases and topples — coherently (one hinged rigid body) when a fragment
    // manager is set and coherentFragments=true.
    struct ChopKerfResult {
        bool  carved = false;      // structural wood found at the plane, something carved
        bool  severed = false;     // the cut released a component (the tree fell)
        int   microsRemoved = 0;   // micro-resolution voxels removed this call
        int   cellsEmptied = 0;    // whole cells that ended empty
        float fullDepth = 0.0f;    // cross-section extent along the chop direction
        float cutFraction = 0.0f;  // kerfDepth / fullDepth, clamped to [0,1]
        DamageResult collapse;     // bookkeeping from the release pass
        // Diagnostics (logged per bite so a whiffed swing is explainable from the
        // log alone): the kerf frame this bite actually used.
        float nearD = 0.0f, farD = 0.0f;   // frontier / far face along chopDir
        float contactD = 0.0f;             // blade depth relative to the frontier
        float dLo = 0.0f, dHi = 0.0f;      // bite window along chopDir
        int   pocketFound = 0, pocketChipped = 0;  // rim-fallback candidates/removed
    };
    // `contactPoint` (optional): the blade's actual impact position — the notch
    // line centers there (height AND lateral offset, clamped into the trunk) and
    // the splinters fly from there, so the bite reads exactly where the axe
    // visibly lands. Each bite throws a few micro SPLINTERS back toward the
    // chopper (tactile feedback; ≤6 pieces, nothing like a blast). Pass y <
    // -1e8 (the default) for "unknown" — the slot then centers on the hit cell.
    ChopKerfResult carveChopKerf(const glm::ivec3& hitCell, const glm::vec3& chopDir,
                                 float kerfDepth, bool coherentFragments = true,
                                 const glm::vec3& contactPoint = glm::vec3(0.0f, -1.0e9f, 0.0f));

private:

    // Count solid voxels strictly between two world points (shielding ray-march).
    int solidVoxelsBetween(const glm::vec3& a, const glm::vec3& b) const;

    // P3: detach connected voxel groups bordering the removed set that can't reach
    // an anchor (solid voxel at Y <= supportY). Detached voxels fall as debris — or,
    // when `coherent` and a fragment manager is set, a small severed component topples
    // as ONE coherent rigid slab (P1.2b) instead of scattering. `impactCenter`/`impactDir`
    // carry the blast context down for the hinge-topple direction (P2.3).
    // Returns the number of voxels detached (0 = everything stayed supported).
    int collapseUnsupported(const std::vector<glm::ivec3>& removed, float supportY,
                            DamageResult& res, bool coherent,
                            const glm::vec3& impactCenter, const glm::vec3& impactDir);

    // Try to topple one severed component as a coherent rigid slab via m_fragMgr.
    // `leafCargo` = canopy cells assigned to this component (F1): they ride the fragment
    // as render cargo (leaves are never voxel debris). Returns true if physicalized
    // (cells removed + body spawned); false if the caller should fall back to per-cell
    // scatter. The body is seeded with a HINGE rotation about the cut (P2.3).
    bool collapseComponentCoherent(const std::vector<glm::ivec3>& component,
                                   const std::vector<glm::ivec3>& leafCargo, DamageResult& res,
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
