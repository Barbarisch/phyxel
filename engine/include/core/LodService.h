#pragma once

#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

/**
 * @brief C1 of docs/ContinuousLodPlan.md — THE shared LOD metric.
 *
 * The engine ships at least ten independent, hardcoded distance cutoffs
 * (plan §0.3). They are all in WORLD UNITS, so none of them responds to field
 * of view or resolution: at 4K, or at a narrow FOV, everything fades at exactly
 * the same world distance even though it covers far more pixels. This class is
 * the one place a cutoff may be computed, so they all scale together.
 *
 * The metric is projected size in PIXELS. Standard perspective math:
 *   frustum slice height at distance d = 2*d*tan(fovY/2)
 *   pixels per world unit               = viewportH / (2*d*tan(fovY/2))
 *   projected pixels of a `size` object = size * viewportH / (2*d*tan(fovY/2))
 *
 * NOTE (plan §2.4): this is a CELL-FOOTPRINT metric, not Nanite's geometric-
 * deviation SSE. Using size as an error proxy is legitimate and has precedent
 * (3D Tiles' geometricError) but it is more conservative — it holds detail
 * longer on flat surfaces where a big cell may deviate very little.
 */
class LodService {
public:
    /// The configuration the engine's existing hand-tuned thresholds were
    /// picked at: EngineConfig.h:19-20 (1600x900) and Camera.h:90 (fovY 45deg).
    /// Pixel budgets are calibrated here so that AT THIS CONFIG the new
    /// screen-space path reproduces the old world-unit distances exactly.
    static constexpr float kReferenceViewportHeight = 900.0f;
    static constexpr float kReferenceFovYDegrees = 45.0f;

    struct ViewParams {
        float viewportHeight = kReferenceViewportHeight;
        float tanHalfFovY = 0.41421356f; // tan(22.5deg)
    };

    static float tanHalfFovYFromDegrees(float fovYDeg) {
        return std::tan(fovYDeg * 0.5f * 3.14159265358979f / 180.0f);
    }

    static ViewParams makeView(float viewportHeight, float fovYDegrees) {
        ViewParams v;
        v.viewportHeight = std::max(1.0f, viewportHeight);
        v.tanHalfFovY = std::max(1e-6f, tanHalfFovYFromDegrees(fovYDegrees));
        return v;
    }

    static ViewParams referenceView() {
        return makeView(kReferenceViewportHeight, kReferenceFovYDegrees);
    }

    /// Pixels-per-world-unit at unit distance. All conversions go through this.
    static float pixelScale(const ViewParams& v) {
        return v.viewportHeight * 0.5f / v.tanHalfFovY;
    }

    /// Screen height in pixels of a `worldSize` object at `distance`.
    static float projectedPixels(float worldSize, float distance, const ViewParams& v) {
        return worldSize * pixelScale(v) / std::max(distance, 1e-4f);
    }

    /// Inverse: the distance at which `worldSize` projects to `targetPixels`.
    /// This is what converts a hand-tuned world distance into a pixel budget
    /// and back, so re-homing an existing threshold is exact at the reference
    /// config and correct everywhere else.
    static float distanceForPixels(float worldSize, float targetPixels, const ViewParams& v) {
        return worldSize * pixelScale(v) / std::max(targetPixels, 1e-4f);
    }

    /// Convert a legacy hand-tuned world distance into the pixel budget it
    /// implies at the reference config. Use once, at the call site being
    /// re-homed; store the pixel budget, not the distance.
    static float pixelBudgetForLegacyDistance(float worldSize, float legacyDistance) {
        return projectedPixels(worldSize, legacyDistance, referenceView());
    }

    /// Coarsest power-of-two LOD level whose cell still projects to at least
    /// `targetPixels` (plan §2.4). Level 0 = `baseCellSize`, level n = 2^n.
    /// Clamped to [0, maxLevel].
    static int levelForDistance(float baseCellSize, float distance, float targetPixels,
                                const ViewParams& v, int maxLevel = 8) {
        // Coarsening one level is safe while the DOUBLED cell still projects to
        // no more than the budget. (An earlier version had this comparison
        // inverted, which made point-blank range select the COARSEST level and
        // broke monotonicity; caught by LevelForDistanceRespectsMaxLevel and
        // LevelForDistanceIsMonotonicInDistance.)
        int level = 0;
        float size = std::max(baseCellSize, 1e-4f);
        while (level < maxLevel &&
               projectedPixels(size * 2.0f, distance, v) <= targetPixels) {
            size *= 2.0f;
            ++level;
        }
        return level;
    }

    /// Scale factor between the CURRENT view and the reference config the engine's
    /// legacy world-unit thresholds were hand-tuned at. Exactly 1.0 at the reference
    /// config. THE single implementation — RenderCoordinator delegates here and tests
    /// call this same function, so a mutation to it cannot slip past the tests
    /// (it previously could: the test file re-derived the formula in a local helper,
    /// so disabling the correction entirely left every test green).
    static float viewScaleVsReference(float viewportHeight, float fovYDegrees) {
        return pixelScale(makeView(viewportHeight, fovYDegrees)) / pixelScale(referenceView());
    }

    /// Character LOD level for a squared distance, given the legacy thresholds and a
    /// view scale. Pure, so it is directly testable without a Vulkan device.
    /// Preserves the legacy strictly-greater semantics exactly.
    static int characterLodLevel(float distSq, float lod1Distance, float lod2Distance,
                                 float viewScale) {
        const float l2 = lod2Distance * viewScale;
        const float l1 = lod1Distance * viewScale;
        if (lod2Distance > 0.0f && distSq > l2 * l2) return 2;
        if (lod1Distance > 0.0f && distSq > l1 * l1) return 1;
        return 0;
    }

    /// Fade weight across a transition band, 1 = fully near, 0 = fully faded.
    /// Shared so every subsystem fades on the same curve rather than popping at
    /// its own private radius.
    static float fadeWeight(float projectedPx, float fadeOutPx, float fadeInPx) {
        if (fadeInPx <= fadeOutPx) return projectedPx > fadeOutPx ? 1.0f : 0.0f;
        const float t = (projectedPx - fadeOutPx) / (fadeInPx - fadeOutPx);
        return std::min(1.0f, std::max(0.0f, t));
    }
};

} // namespace Core
} // namespace Phyxel
