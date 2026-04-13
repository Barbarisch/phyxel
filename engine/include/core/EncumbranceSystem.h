#pragma once

namespace Phyxel {
namespace Core {

// ============================================================================
// EncumbranceLevel — D&D 5e variant encumbrance (PHB p.176)
//
// Standard carry capacity = STR score × 15 lbs.
// Variant encumbrance (used here):
//   Encumbered:         weight > STR × 5   → speed -10 ft
//   Heavily Encumbered: weight > STR × 10  → speed -20 ft, disadv on all ability
//                        checks, attack rolls, and saving throws using STR, DEX, or CON
// ============================================================================
enum class EncumbranceLevel {
    Unencumbered,      // ≤ STR × 5 lbs
    Encumbered,        // STR × 5 < weight ≤ STR × 10
    HeavilyEncumbered  // STR × 10 < weight ≤ STR × 15
};

class EncumbranceSystem {
public:
    // -----------------------------------------------------------------------
    // Capacity thresholds (all in pounds)
    // -----------------------------------------------------------------------

    /// Max weight before any penalty: STR × 15 lbs.
    static float carryCapacity(int strengthScore) { return strengthScore * 15.0f; }

    /// Variant: weight above this → Encumbered: STR × 5 lbs.
    static float encumberedThreshold(int strengthScore) { return strengthScore * 5.0f; }

    /// Variant: weight above this → HeavilyEncumbered: STR × 10 lbs.
    static float heavilyEncumberedThreshold(int strengthScore) { return strengthScore * 10.0f; }

    // -----------------------------------------------------------------------
    // Derived effects
    // -----------------------------------------------------------------------

    /// Determine encumbrance level from total carried weight and STR score.
    static EncumbranceLevel getLevel(float totalWeightLbs, int strengthScore) {
        if (totalWeightLbs > heavilyEncumberedThreshold(strengthScore))
            return EncumbranceLevel::HeavilyEncumbered;
        if (totalWeightLbs > encumberedThreshold(strengthScore))
            return EncumbranceLevel::Encumbered;
        return EncumbranceLevel::Unencumbered;
    }

    /// Speed penalty in feet (0, -10, -20).
    static int speedPenalty(EncumbranceLevel level) {
        switch (level) {
            case EncumbranceLevel::Encumbered:        return -10;
            case EncumbranceLevel::HeavilyEncumbered: return -20;
            default:                                  return 0;
        }
    }

    /// True when HeavilyEncumbered: disadvantage on STR/DEX/CON checks, attacks, saves.
    static bool hasPhysicalDisadvantage(EncumbranceLevel level) {
        return level == EncumbranceLevel::HeavilyEncumbered;
    }

    /// Apply speed penalty and return adjusted speed (clamped to 0).
    static int adjustedSpeed(int baseSpeed, float totalWeightLbs, int strengthScore) {
        int speed = baseSpeed + speedPenalty(getLevel(totalWeightLbs, strengthScore));
        return speed < 0 ? 0 : speed;
    }

    // -----------------------------------------------------------------------
    // Push / drag / lift limits (PHB p.176)
    // -----------------------------------------------------------------------

    /// Max push/drag/lift weight: STR × 30 lbs (or STR × 15 for creatures < Large).
    static float pushDragLift(int strengthScore, bool isLargeOrBigger = false) {
        return strengthScore * (isLargeOrBigger ? 30.0f : 15.0f);
    }
};

} // namespace Core
} // namespace Phyxel
