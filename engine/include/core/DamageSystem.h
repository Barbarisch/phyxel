#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include <vector>
#include <functional>
#include "core/DamageStage.h"

namespace Phyxel {

class ChunkManager;
class GpuParticlePhysics;
namespace Core { class CoherentFragmentManager; }

// Result of one area-damage application.
// kDamageStageMax / damageStage() -- the quantization displayStage() is built on.
struct DamageResult {
    int voxelsBroken = 0;   // static voxels removed
    int voxelsGrazed = 0;   // in range but under break threshold (damage accumulated)
    int debrisSpawned = 0;  // dynamic pieces queued (cubes/subcubes/microcubes)
    // Grazed voxels whose accumulated damage crossed a VISIBLE stage boundary -- the ones
    // whose appearance actually changed (docs/VoxelDamageVisualization.md §3). Echoed by
    // apply_damage so a caller can assert a graze moved something: `voxelsGrazed` only says
    // a hit landed, never that the surface now looks any different.
    int voxelsStageChanged = 0;
    // Phase 4 (DebrisInteractionPlan): the blast also PUSHES what already moves - existing GPU
    // debris (queued, applied on the next GPU tick) and CPU bodies (furniture, fragments, item
    // props; applied now). Echoed so a caller can assert the push happened.
    bool  impulseQueued   = false;   // GPU debris impulse queued (false: no debris alive / no GPU)
    int   cpuBodiesPushed = 0;
    float impulse         = 0.0f;    // N*s at the centre
    float impulseRadius   = 0.0f;    // m
};

// P1 destruction core (see docs/DestructionSystemV2.md). Applies a shaped energy
// hit to the voxel field: energy radiates from the impact, attenuated by
// distance and by solid voxels in the way (shielding). Voxels whose received
// energy exceeds their material toughness break; the overkill ratio decides
// whether they pop off intact or shatter into subcubes/microcubes. Broken
// static voxels are removed from the chunk and respawned as GPU debris.
class DamageSystem {
public:
    DamageSystem(ChunkManager* chunkManager, GpuParticlePhysics* gpu)
        : m_cm(chunkManager), m_gpu(gpu) {}

    // Blast -> push (Phase 4). Grounded in the break path's own launch law: a voxel this blast
    // breaks leaves at BASE_SPEED * sqrt(E_reached / toughness). A LOOSE piece has no bond to
    // break, so it should move at least as fast - the impulse is sized so that a reference Stone
    // piece at the centre gets exactly the speed a Stone voxel broken there gets:
    //   J(E) = m_ref * BASE_SPEED * sqrt(E / toughness_ref)        (ref = IMPULSE_REF_MATERIAL)
    // (momentum ~ sqrt(energy)). Linear falloff to IMPULSE_RADIUS_SCALE x the blast radius (the
    // shock reaches past the carve), blended IMPULSE_UP_BIAS toward +Y (blasts throw things up).
    // A first guess, J = 0.05 * E, gave a 300-energy blast's loose Stone 2.5 m/s at the centre
    // against >= 4 m/s for the pieces it broke - measured live: a settled pile beside it moved
    // 17 mm (floor friction mu 0.8 stops 1.25 m/s in ~10 cm).
    static constexpr const char* IMPULSE_REF_MATERIAL = "Stone";
    static float blastImpulse(float energy);   // N*s at the centre, from the reference material's data
    /// The speed a loose reference piece gets at the blast centre: blastImpulse / m_ref =
    /// BASE_SPEED * sqrt(E / toughness_ref). WaterCore E1 gives water at r the same speed a loose
    /// piece at r gets (docs/WaterCore.md 19.1).
    static float blastSpeed(float energy);
    /// WaterCore E1: called for every blast (every DamageSystem instance, every caller) with the
    /// centre, the effective radius and the energy - the hook that lets the water feel it. Fired only
    /// when the Phase 4 push is on (push:false is the pre-push control, for the water too).
    using BlastListener = std::function<void(const glm::vec3& centre, float radius, float energy)>;
    /// docs/VoxelFoundationPlan.md F7.5 (section 7 step 5): while set, applyDamage first offers the event here; true =
    /// handled (the switched world's fracture + bodies ran), so today's carving does NOT run. `stub` (tests): set but
    /// never called (the red).
    using DestructionOverride = std::function<bool(const glm::vec3& centre, float radius, float energy,
                                                   const glm::vec3& direction, DamageResult& res)>;
    static void setDestructionOverride(DestructionOverride fn, bool stub = false);
    static bool destructionOverrideActive();
    static void setBlastListener(BlastListener l);
    static constexpr float IMPULSE_RADIUS_SCALE = 1.5f;
    static constexpr float IMPULSE_UP_BIAS      = 0.3f;
    /// Test control only (apply_damage "push": false): the same blast WITHOUT the Phase 4 push -
    /// what every blast did before Phase 4. Default on.
    void setPushExisting(bool on) { m_pushExisting = on; }

    /// Debris pieces refused process-wide because no GPU debris solver existed (disabled by
    /// PHYXEL_DISABLE_GPU_DEBRIS or failed init). Echoed by /api/debug/gpu_physics so a
    /// missing solver is observable instead of breaks silently making no debris.
    static uint64_t refusedDebrisTotal();

    /// THE one way a broken piece becomes debris (DebrisInteractionPlan 1d): blasts, chops and
    /// collapse (spawnDebris below), the B key and Python break_hovered_* (VoxelManipulationSystem).
    /// `centre` in world units, `scale` = piece edge (1, 1/3, 1/9). With no GPU solver the piece
    /// is REFUSED and counted in refusedDebrisTotal() — never dropped silently. Returns queued.
    static bool spawnBreakDebris(GpuParticlePhysics* gpu, const glm::vec3& centre, const glm::vec3& vel,
                                 float scale, const std::string& material, const glm::vec3& angularVel);

    /// Which slice of its parent cube's texture a piece shows, as its micro position inside that
    /// cube (mx*81 + my*9 + mz, 0..728). A pure function of the piece's world centre and edge —
    /// so a broken subcube/microcube keeps the slice it showed while static. 0 for full cubes.
    static uint32_t debrisSliceFor(const glm::vec3& centre, float scale);

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
    // `radii` (optional) makes the blast an ELLIPSOID instead of a sphere: a thin, wide
    // disc (e.g. {4, 0.7, 4}) severs a full trunk cross-section in a 1-voxel-tall slice
    // (clean fell, canopy survives) where a sphere either notches or vaporizes; it is also
    // the shaped-spell primitive (wall = long/thin/tall, line = long/thin/thin). Any
    // component > 0 activates it; {0,0,0} (default) keeps the spherical `radius`.
    // docs/DestructionSystemV2.md §15.6 B.
    DamageResult applyDamage(const glm::vec3& center, float radius, float energy,
                             const std::string& damageType = "force",
                             const glm::vec3& direction = glm::vec3(0.0f),
                             float supportY = NO_SUPPORT,
                             bool  collapse = true,
                             bool  coherentFragments = false,
                             const glm::vec3& radii = glm::vec3(0.0f));

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
    // STATIC: it reads only MaterialRegistry, never instance state, so the mesher and the
    // API can resolve toughness without owning a DamageSystem. Existing `ds->responseFor(x)`
    // call sites keep compiling.
    static MatResponse responseFor(const std::string& materialName);

    // What damage stage does a voxel of this material DISPLAY, given its accumulated damage?
    // THE single entry point for that question (docs/VoxelDamageVisualization.md §1):
    // the mesher, the /api/world/voxel readback and the graze re-mesh test all resolve
    // through here, so a stage means the same thing everywhere.
    //
    // Normalized by the material's OWN break toughness, not a global constant. With a global
    // reference of 30, Stone (toughness 110) reached maximum display at 27% of the way to
    // breaking and then said nothing for the remaining 73%, while Glass (35) did not saturate
    // until 86% -- the same rendered stage meant anywhere from 14% to 86% of the way to
    // failure, a 6.3x spread, on a feature whose entire purpose is to communicate proximity
    // to breaking.
    static uint8_t displayStage(const std::string& materialName,
                                float accumulatedDamage,
                                int   stageMax = -1);   // -1 = the RUNTIME count (P4 knob)

    // The 4-bit value the mesher packs into instance bits 11-14 for this voxel: displayStage()
    // spread back across the field's full range so voxel.frag's `/ 15.0` still yields
    // 0.0 / 0.33 / 0.67 / 1.0. Kept next to displayStage so the semantic stage and the packed
    // one can never be computed from different denominators.
    static uint8_t displayStageBits(const std::string& materialName, float accumulatedDamage);

    // TREE MATTER — trunk (Log*) or canopy (Leaf*) content in the cell, answered by the switched world's
    // tree-cell hook (ChunkManager::setVoxFoundTreeCell, F7.14); false without it (today's chunk scan went
    // with D4). Used by the structure-build vegetation gate: a build lot is CLEARED of whole trees, never
    // built through one.
    static bool isTreeMatterCell(ChunkManager* cm, const glm::ivec3& wp);
    // §15.6 C — fold small, spatially-CONTAINED components into the largest one.
    // A felled tree's micro-thin branch TIPS wind through cells that aren't cube-face
    // adjacent, so the support flood splits each tip into its own tiny detached component;
    // physicalized alone, each perches on the settled pile as a disconnected "floating
    // branch" (live user report). This folds every component that is BOTH small
    // (<= max(maxSatelliteCells, largest/4) cells) AND inside the largest's expanded
    // bounds back into the largest, so the crown falls as ONE coherent body — while
    // genuinely distinct severed pieces (two wall halves, a far chunk) stay separate.
    // Pure + deterministic (no engine state) so it is unit-tested directly.
    static void consolidateSatelliteComponents(std::vector<std::vector<glm::ivec3>>& components,
                                               int maxSatelliteCells = 60);

private:

    // Count solid voxels strictly between two world points (shielding ray-march).
    int solidVoxelsBetween(const glm::vec3& a, const glm::vec3& b) const;

    // Queue one debris piece into the GPU particle system. With no GPU solver the piece is
    // REFUSED and counted (refusedDebrisTotal) — never dropped silently.
    void spawnDebris(const glm::vec3& pos, const glm::vec3& vel, float scale,
                     const std::string& material);

    float frand();
    float frand(float lo, float hi);

    ChunkManager*       m_cm  = nullptr;
    GpuParticlePhysics* m_gpu = nullptr;
    bool m_pushExisting = true;
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
    // U4 approximate-statics tunables (conservative — "drastically undermined only"). Live
    // feel-tuning pending; these bound headless behavior.
    static constexpr int   UNDERMINE_MIN_MASS   = 12;    // ignore small overhangs (a few blocks)
    static constexpr float UNDERMINE_SUPPORT_FRAC = 0.34f; // base covering < 1/3 of footprint = collapse
    static constexpr int   UNDERMINE_MAX_MASS   = 1200;  // bigger flooded mass = it's ground, not a discrete structure -> bail
};

} // namespace Phyxel
