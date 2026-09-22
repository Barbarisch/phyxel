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

/// Highest stage index the mesher emits. The instance field is 4 bits wide (11-14), so
/// 15 is the widest value that fits; 3.5 will lower this to 3 (P2) WITHOUT narrowing the
/// field, leaving 4-15 reserved.
inline constexpr int kDamageStageMax = 15;

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
