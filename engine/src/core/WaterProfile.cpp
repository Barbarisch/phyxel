#include "core/WaterProfile.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {

WaterProfile deriveWaterProfile(const WaterBodyIndex::Body* body, float cellSize) {
    WaterProfile p;   // neutral: turbidity 0, roughness 1 (roughness is W3's to derive)

    // ── TURBIDITY (v4 W2) ────────────────────────────────────────────────────────────────────
    // OCEAN SHORT-CIRCUITS TO CLEAR, and that is not an oversight. Open ocean is the CLEAREST
    // natural water on Earth — Jerlov type I, Secchi 30-50 m. It reads opaque from above because it
    // is kilometres deep (Beer-Lambert eats everything over that path) and because grazing-angle
    // Fresnel turns it into a sky mirror — NOT because it is dirty. Turbidity is a coastal, lake and
    // river-mouth phenomenon: glacial flour, algal bloom, runoff. Deriving "ocean = murky" would
    // produce the right picture from a false premise and fall apart the moment you look into a
    // shallow lagoon.
    if (body && body->cls != WaterBodyIndex::Class::Ocean) {
        // Mean depth from the body's own volume estimate. volumeEst is Sum((level-terrain)*cell^2),
        // so dividing by (area * cell^2) gives metres. Guarded: a zero-area or zero-cellSize body
        // would divide by zero, and one NaN here propagates into the texture and then into every
        // water pixel of that body.
        const float denom = static_cast<float>(body->areaCells) * cellSize * cellSize;
        if (denom > 0.0f && std::isfinite(body->volumeEst)) {
            const float meanDepth = body->volumeEst / denom;
            // Linear ramp between the two anchors, clamped. Shallow -> turbid, deep -> clear.
            const float t = (kClearDepth - meanDepth) / (kClearDepth - kTurbidDepth);
            p.turbidity = std::clamp(t, 0.0f, 1.0f);
        }
        // denom == 0 leaves turbidity at 0: an unmeasurable body reads CLEAR, which is the
        // conservative choice (it keeps today's look rather than inventing murk).
    }

    // WAVE ENERGY — preserved bit-for-bit from the inline loop this replaces (tangible-water F,
    // RenderCoordinator.cpp). Changing it here would silently alter every existing world's sea, so
    // the formula is pinned by WaterProfileTest.EnergyFormulaIsPreserved.
    //
    // ⚑GROUND (inherited, and knowingly weak): fetch-limited waves — energy grows with body size.
    // log2 area over a ~1024-cell reference: a 4-cell lake ~0.23, a 100-cell lake ~0.66, floor 0.15
    // so nothing is dead flat. The "~1024-cell reference" is an eyeballed normaliser, not a measured
    // one; W3 replaces this whole expression with real fetch-limited growth (SMB/CERC) keyed on the
    // body's bbox along the wind direction.
    if (body && body->cls != WaterBodyIndex::Class::Ocean) {
        p.waveEnergy = std::clamp(
            std::log2(static_cast<float>(body->areaCells) + 1.0f) / 10.0f, 0.15f, 1.0f);
    }
    return p;
}

WaterProfile waterProfileAt(const WaterBodyIndex* bodies, float worldX, float worldZ,
                            float cellSize) {
    if (!bodies || bodies->bodies().empty()) return WaterProfile{};
    // nullptr when dry → neutral
    return deriveWaterProfile(bodies->bodyAt(worldX, worldZ), cellSize);
}

void buildHydroUpload(const HydrologyMap& hydro, const WaterBodyIndex* bodies,
                      const WaterLookOverride& ovr, std::vector<float>& out) {
    const std::vector<float>& lvl = hydro.levels();
    out.assign(lvl.size() * kHydroTexelFloats, 0.0f);

    const bool haveBodies = bodies && !bodies->bodies().empty();

    for (int cz = 0; cz < hydro.cellsZ(); ++cz) {
        for (int cx = 0; cx < hydro.cellsX(); ++cx) {
            const size_t i = static_cast<size_t>(cz) * hydro.cellsX() + cx;
            const float level = lvl[i];
            // Same wet test the inline loop used: the dry sentinel is hugely negative, so half of
            // it is a safe threshold against float noise.
            const bool wet = level > HydrologyMap::NO_WATER * 0.5f;

            WaterProfile p;   // dry columns keep the neutral profile at full energy (unchanged)
            if (wet) {
                // Through the SAME query the underwater overlay uses, so the two cannot disagree.
                p = haveBodies ? waterProfileAt(bodies,
                                                hydro.originX() + (cx + 0.5f) * hydro.cellSize(),
                                                hydro.originZ() + (cz + 0.5f) * hydro.cellSize(),
                                                hydro.cellSize())
                               : WaterProfile{};
                // The override is the positive control — it must reach real water only, so it is
                // applied INSIDE the wet branch. Overriding dry columns would change nothing
                // visible (they gate to zero alpha) but would make the probe's own semantics a lie.
                if (ovr.active) {
                    p.turbidity = ovr.turbidity;
                    p.roughness = ovr.roughness;
                }
            }

            float* texel = &out[i * kHydroTexelFloats];
            texel[0] = level;
            texel[1] = p.waveEnergy;
            texel[2] = p.turbidity;
            texel[3] = p.roughness;
        }
    }
}

}  // namespace Phyxel
