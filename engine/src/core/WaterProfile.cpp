#include "core/WaterProfile.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {

WaterProfile deriveWaterProfile(const WaterBodyIndex::Body* body) {
    WaterProfile p;   // neutral: turbidity 0, roughness 1 — today's look, exactly (see the header)

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
                const WaterBodyIndex::Body* b =
                    haveBodies ? bodies->body(bodies->bodyIdAt(
                                     hydro.originX() + (cx + 0.5f) * hydro.cellSize(),
                                     hydro.originZ() + (cz + 0.5f) * hydro.cellSize()))
                               : nullptr;
                p = deriveWaterProfile(b);
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
