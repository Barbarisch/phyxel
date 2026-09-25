#pragma once

#include <atomic>
#include <cstdint>

namespace Phyxel {
namespace Core {

/**
 * Per-voxel damage -> visible stage quantization.
 *
 * SINGLE SOURCE OF TRUTH. Two callers must agree on this function or the feature
 * breaks in a way that is invisible to a state-only test:
 *   - ChunkRenderManager packs the stage into instance bits 11-14 (what the shader draws);
 *   - DamageSystem's graze path re-meshes a chunk only when a hit moves a voxel ACROSS a
 *     stage boundary (docs/VoxelDamageVisualization.md §3).
 * If the graze path quantized differently from the mesher it would either re-mesh for
 * changes no one can see, or skip a re-mesh for one they can.
 *
 * The denominator is a PARAMETER, not baked in, because it is scheduled to change:
 * P0 passes kDamageDisplayRef (the shipped prototype constant), P1 passes the material's
 * own break toughness (3.2). Callers name their denominator so the swap is one line and
 * the mismatch in between is visible rather than silent.
 */

/// Prototype display reference, in apply_damage energy units. P1 (3.2) replaces every
/// caller's use of this with DamageSystem::responseFor(material).toughness. Kept here,
/// and named, so the two call sites cannot drift apart before then.
inline constexpr float kDamageDisplayRef = 30.0f;

/// Widest value the 4-bit instance field (bits 11-14) can hold. This is a HARDWARE limit,
/// not a design choice -- see kDamageStagesVisible for the number of stages actually used.
inline constexpr int kDamageStageMax = 15;

/// How many VISIBLE damage stages the engine distinguishes, above pristine.
/// P2 (3.5) set this; P4 (6.4) re-derived it from measurement -- see the table below.
///
/// SET BY MEASUREMENT (P4 / 6.4), not by argument. It was 3 on cost grounds alone; the
/// legibility axis overturned that. Both axes, same scene, one engine session via the runtime
/// knob below:
///
///   stages | damage-induced faces | low-damage legibility @12u | blind below damage ratio
///   -------|----------------------|----------------------------|-------------------------
///     15   |        +196          |           5.14%            |         0.033
///      7   |        +109          |           5.30%            |         0.071
///      3   |         +51          |  2.67% -- ON THE 2.27% NOISE FLOOR
///
/// Reading, in the order that matters:
///  * 7 MATCHES 15's legibility within noise at every distance (12/16/48/96 u) and at both
///    damage levels, for 56% of its face cost. That makes 15 strictly dominated -- it buys
///    nothing a viewer can see.
///  * 3 IS BLIND EARLY, and this is the finding that moved the constant. damageStage() rounds
///    at f*n + 0.5, so the first visible stage needs damage ratio >= 0.5/n: 0.167 at 3 stages.
///    A voxel spends the first ~17% of its life rendering PRISTINE. Measured at ratio 0.12 the
///    3-stage arm sat on the pristine-vs-pristine noise floor at all four distances -- the
///    damage was not faint, it was absent. A first pickaxe swing landing no visible mark is a
///    gameplay defect, not a saving.
///  * The face cost is real but small in absolute terms (+109 vs +51 on a 24x10 wall, against
///    a ~270k-face scene at the M4 operating point) because only damaged voxels pay it. Cost
///    was the right tie-breaker between 7 and 15; it is the wrong one between 7 and 3.
///
/// The original cost reasoning still holds and is why this is 7 and not 15:
///  * MERGE COST. Damage is part of the greedy-merge key by design, so a damage GRADIENT
///    locally becomes the un-merged case. Fewer distinct levels means wider bands, longer
///    surviving merge runs, fewer faces. Measured ratio 15:3 = 3.84x damage-induced faces.
///  * RE-MESH COUNT. The graze path rebuilds a chunk only when a hit crosses a stage boundary
///    (3.7), so this many stages means at most this many rebuilds per voxel over its whole life.
///
/// Superseded by the table above: the P1 claim that consecutive stages differ by ~1.4/255 and
/// are therefore illegible. That was measured between ADJACENT stages of 15; it says nothing
/// about whether a stage is distinguishable from PRISTINE, which is the question that decides
/// the count. Re-measure with tools/damage_stage_legibility.py before changing this again --
/// and keep its noise-floor row, without which a floor reading looks like faint cracking.
inline constexpr int kDamageStagesVisible = 7;

/**
 * RUNTIME stage count, for P4's A/B (6.4). Ships equal to kDamageStagesVisible; the debug
 * endpoint POST /api/debug/damage_stages changes it so 3 / 7 / 15 can be compared in ONE
 * engine session against ONE scene, which three separate builds cannot do.
 *
 * ATOMIC, and every consumer must READ IT ONCE INTO A LOCAL rather than per voxel. It is read
 * during chunk meshing, which runs on worker threads, so a plain mutable global would be a
 * cross-thread read inside a 32,768-cell loop. Reading once per rebuild is simultaneously the
 * race fix (a rebuild uses one consistent value throughout) and the performance answer (no
 * atomic load in the inner loop); the forced full re-mesh after a change leaves no chunk
 * holding a stale count.
 */
inline std::atomic<int>& damageStagesVisibleRef() {
    static std::atomic<int> v{kDamageStagesVisible};
    return v;
}
inline int damageStagesVisible() {
    return damageStagesVisibleRef().load(std::memory_order_relaxed);
}
/// Clamped to [1, kDamageStageMax]; returns the value actually applied.
/// Above kDamageStageMax the quantized value would overflow instance bits 11-14 into bit 15,
/// the `varied` texture-rotation flag, silently hash-rotating the face texture -- which on
/// coursed materials breaks pattern continuity at voxel edges. Below 1 there are no stages.
inline int setDamageStagesVisible(int n) {
    if (n < 1) n = 1;
    if (n > kDamageStageMax) n = kDamageStageMax;
    damageStagesVisibleRef().store(n, std::memory_order_relaxed);
    return n;
}

/// Map a visible stage (0..kDamageStagesVisible) onto the 4-bit instance field.
///
/// The field stays 4 bits wide and the SHADER IS UNTOUCHED: voxel.frag divides the packed
/// value by 15.0, so stages spread across {0, 5, 10, 15} give it 0.0 / 0.33 / 0.67 / 1.0
/// exactly as before. Narrowing the emitted range to 0..3 instead would have required editing
/// voxel.frag -- which means rebuilding every shader and committing the .spv (glslc does not
/// track includes), and leaving a bare `3.0` in GLSL that must be kept in sync with a C++
/// constant by hand. That is a drift bug waiting to happen, for no gain: what matters for both
/// merge cost and legibility is the NUMBER OF DISTINCT VALUES, not their magnitude.
inline constexpr uint8_t packDamageStage(int visibleStage,
                                         int stagesVisible = kDamageStagesVisible) {
    if (stagesVisible < 1) stagesVisible = 1;
    if (visibleStage <= 0) return 0;
    if (visibleStage >= stagesVisible) return static_cast<uint8_t>(kDamageStageMax);
    return static_cast<uint8_t>((visibleStage * kDamageStageMax + stagesVisible / 2) / stagesVisible);
}

/**
 * Quantize accumulated damage to a visible stage in [0, stageMax].
 *
 * CLAMPED, and the clamp is load-bearing: the stage is packed into instance bits 11-14
 * and bit 15 is the `varied` per-voxel texture-rotation flag (ChunkRenderManager face
 * packing; read at voxel.frag:259). A stage of 16 or more would overflow bits 11-14 and
 * silently switch varied ON for that face, hash-rotating its texture -- which on coursed
 * materials (masonry, layered stone) breaks pattern continuity at voxel edges. So the
 * clamp is not defensive tidiness; it prevents a specific visual corruption.
 *
 * @param accumulated Accumulated damage energy (same units as apply_damage `energy`).
 * @param denom       Energy at which the voxel displays as fully damaged (> 0).
 * @param stageMax    Highest stage to emit; clamped to [1, kDamageStageMax].
 */
inline uint8_t damageStage(float accumulated,
                           float denom,
                           int   stageMax = kDamageStageMax) {
    if (stageMax < 1) stageMax = 1;
    if (stageMax > kDamageStageMax) stageMax = kDamageStageMax;
    if (!(denom > 0.0f)) return 0;                  // also catches NaN denominators
    if (!(accumulated > 0.0f)) return 0;            // pristine, and NaN-safe

    float f = accumulated / denom;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return static_cast<uint8_t>(f * static_cast<float>(stageMax) + 0.5f);
}

/**
 * Did a hit move this voxel across a visible stage boundary?
 *
 * This is the whole of 3.7's re-mesh decision, kept pure so it is unit-testable without
 * a ChunkManager (which needs Vulkan, and so cannot be built in tests/core). A graze that
 * does not cross a boundary changes zero pixels, and a single blast can graze thousands of
 * voxels -- re-meshing for those is pure cost.
 */
inline bool damageStageChanged(float before,
                               float after,
                               float denom,
                               int   stageMax = kDamageStageMax) {
    return damageStage(before, denom, stageMax) != damageStage(after, denom, stageMax);
}

/**
 * Fracture CHARACTER for a material, from its shatter brittleness (P5, 4.4b).
 *
 * `crack.glsl` divides by `kCrackCell * style`, so LARGER style = LARGER cells = SPARSER.
 * brittleS1 already encodes shatter behaviour and spans ~1.3 (Glass, shatters readily) to
 * ~4.5 (Steel, resists), so the direction is right -- but passing it through raw is not:
 * style 4.5 would give 1.5 m cells, LARGER THAN A VOXEL, so a 1 m face would show less than
 * one cell.
 *
 * Normalized onto [0.75, 1.60]:
 *     Glass 1.3 -> 0.75  (~4.0 cells per 1 m face, dense and fine)
 *     Stone 1.8 -> 0.88  (~3.4)
 *     Wood  3.5 -> 1.33  (~2.3)
 *     Steel 4.5 -> 1.60  (~1.9, sparse and wide)
 *
 * THE FLOOR OF 0.75 IS LOAD-BEARING, not cosmetic. P3 measured that the primary network has to
 * sit on the subcube lattice to stay legible at 16 units; a style much below 0.75 pushes brittle
 * materials back toward the microcube lattice and reintroduces the sub-pixel failure P3 fixed
 * (stage steps 4.55/2.08/2.02 against a 2.91 noise floor -- worse than the flat darkening it
 * replaced). Do not widen the range downward without re-running R4's distance ladder.
 *
 * STILL A HYPOTHESIS, like the stage count: the range is a starting point to be measured at the
 * extremes, not a settled number.
 */
inline float crackStyleFor(float brittleS1) {
    const float lo = 1.3f, hi = 4.5f;
    float s = brittleS1 < lo ? lo : (brittleS1 > hi ? hi : brittleS1);
    return 0.75f + (s - lo) / (hi - lo) * 0.85f;
}

} // namespace Core
} // namespace Phyxel
