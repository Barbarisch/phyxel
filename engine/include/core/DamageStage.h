#pragma once

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
 *     stage boundary (docs/VoxelDamageVisualization.md 3.7).
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

/// How many VISIBLE damage stages the engine distinguishes: pristine + 3 (hairline / open /
/// failing). P2, docs/VoxelDamageVisualization.md 3.5.
///
/// Why so few, and why coarser is better rather than merely cheaper:
///  * MERGE COST. Damage is part of the greedy-merge key by design, so a damage GRADIENT
///    locally becomes the un-merged case. Fewer distinct levels means wider bands, longer
///    surviving merge runs, fewer faces. A blast with 16 levels shatters merge runs into ~16
///    concentric single-voxel-wide bands.
///  * RE-MESH COUNT. The graze path rebuilds a chunk only when a hit crosses a stage boundary
///    (3.7), so 3 stages means at most 3 rebuilds per voxel over its whole life, not 16.
///  * IT IS NOT LEGIBLE ANYWAY. Measured on the live ladder at P1: consecutive stages differ by
///    ~1.4 luminance out of 255, under 1%. Sixteen perceptually distinct crack stages on a 1 m
///    face is not a real thing.
///
/// STILL A HYPOTHESIS: R4 (6.4) measures 3 vs 7 vs 15 for cost AND legibility in a real
/// settlement scene and ratifies or revises this. Change it there, with the table, not here.
inline constexpr int kDamageStagesVisible = 3;

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

} // namespace Core
} // namespace Phyxel
