#include "core/WaterProfile.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Phyxel {

WaterProfile deriveWaterProfile(const WaterBodyIndex::Body* body, float cellSize,
                                const WaterWind& wind) {
    WaterProfile p;

    // ── ROUGHNESS (v4 W3) ────────────────────────────────────────────────────────────────────
    // Wind speed only, NOT fetch — and that is a deliberate, checked decision. Cox-Munk slope is
    // dominated by short gravity-capillary waves that reach their equilibrium range almost
    // immediately (Phillips 1958/1985), so a pond and a big lake under the SAME wind have similar
    // mean-square slope; inventing a fetch->roughness link would be fabrication.
    // ⚑The real mechanism that makes a small sheltered pond calmer is a wind-shadow: an internal
    // boundary layer in the lee of a treeline or bluff, extending ~40-60 canopy heights downwind
    // (Markfort et al. 2010, Water Resources Research 46, W03530). That is a TERRAIN-EXPOSURE
    // correction to the local wind speed feeding this function — a different bake input, not built.
    p.roughness = windRoughness(wind.speedMs);

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

    // ── WAVE ENERGY (v4 W3) ──────────────────────────────────────────────────────────────────
    // Was `clamp(log2(areaCells+1)/10, 0.15, 1)` — an area proxy whose own comment admitted the
    // /10 normaliser was eyeballed, and which was blind to wind heading, so the same lake built the
    // same sea whichever way the wind blew. Now real fetch-limited growth: measure how far the wind
    // crosses this body, and ask the SMB curve how developed a sea that supports.
    //
    // OCEAN gets UNLIMITED FETCH, not a frozen amplitude. The bake's ocean bbox is an artifact of
    // the baked region's edges, not the real water's extent, so taking a fetch from it would cap
    // the sea at the arbitrary size of the bake — hence the short-circuit. But "unlimited fetch"
    // means the tanh term saturates at 1, NOT that the whole scale is 1: the (U_A/U_A_ref)^2 wind
    // term still applies, so an ocean swell still grows with the wind.
    // ⚑W3's first build pinned Ocean to waveEnergy = 1.0 outright, which made the one body most
    // associated with a gale the one body whose swell ignored wind entirely (solution-auditor,
    // 2026-08-03; pinned by OceanSwellRespondsToWind).
    //
    // ⚑THE 0.15 FLOOR IS GONE, deliberately. It existed so "nothing is dead flat", but a pond
    // genuinely cannot carry a swell — at Beaufort 4 a 500 m pond reaches Hs 0.13 m, i.e. energy
    // 0.083. Its liveliness should come from the RIPPLE channel (roughness), which is wind-driven
    // and needs no fetch, not from a fake swell floor.
    if (body) {
        // waveHeightScale, NOT fetchLimitedEnergy — the fraction alone makes waves SHRINK as wind
        // rises (see the header note; caught by the W3 L4 probe).
        if (body->cls == WaterBodyIndex::Class::Ocean) {
            p.waveEnergy = fullyDevelopedScale(wind.speedMs);
        } else {
            const float fetch =
                fetchAlongWind(body->bboxMin, body->bboxMax, cellSize, wind.dirRadians);
            p.waveEnergy = waveHeightScale(fetch, wind.speedMs);
        }
    }
    return p;
}

float fetchAlongWind(const glm::ivec2& bboxMinCells, const glm::ivec2& bboxMaxCells,
                     float cellSize, float windDirRadians) {
    // Inclusive cell bounds: a single-cell body spans one cell, not zero.
    const float w = static_cast<float>(bboxMaxCells.x - bboxMinCells.x + 1) * cellSize;
    const float d = static_cast<float>(bboxMaxCells.y - bboxMinCells.y + 1) * cellSize;
    if (w <= 0.0f || d <= 0.0f) return 0.0f;

    const float ux = std::abs(std::cos(windDirRadians));
    const float uz = std::abs(std::sin(windDirRadians));

    // ⚑THIS WAS WRONG ONCE — the first version returned `w*ux + d*uz`, the rectangle's SUPPORT
    // WIDTH (its projected shadow onto the wind axis). Fetch is not a projection, it is a CHORD:
    // the distance the wind actually travels across the water. The longest chord of an
    // axis-aligned rectangle along unit u is min(w/|ux|, d/|uz|) — whichever pair of sides the
    // line exits through first. The two quantities coincide only at the cardinal angles and at the
    // rectangle's own diagonal angle, so a square tested at 45 degrees agrees and hides the bug;
    // on a 1280x256 body at 30 degrees the support width reads 1236 m against a true 512 m (+141%).
    // Caught by solution-auditor 2026-08-03 and pinned by
    // FetchOnAnElongatedBodyAtAnObliqueAngleIsTheTrueChord.
    //
    // Axis-aligned wind divides by zero on one term; that term is simply "never exits that pair of
    // sides", i.e. +infinity, so the min() picks the other. Handled explicitly rather than relying
    // on IEEE inf so the intent is readable.
    const float kEps = 1e-6f;
    const float alongW = (ux > kEps) ? (w / ux) : std::numeric_limits<float>::max();
    const float alongD = (uz > kEps) ? (d / uz) : std::numeric_limits<float>::max();
    return std::min(alongW, alongD);
}

float fetchLimitedEnergy(float fetchMeters, float windSpeedMs) {
    if (!(fetchMeters > 0.0f) || !(windSpeedMs > 0.0f)) return 0.0f;   // also rejects NaN
    constexpr float g = 9.81f;
    const float uA = 0.71f * std::pow(windSpeedMs, 1.23f);   // CERC wind-stress factor
    if (!(uA > 0.0f)) return 0.0f;
    const float X = g * fetchMeters / (uA * uA);             // dimensionless fetch
    const float e = std::tanh(0.0125f * std::pow(X, 0.42f));
    return std::clamp(e, 0.0f, 1.0f);
}

float waveHeightScale(float fetchMeters, float windSpeedMs) {
    const float frac = fetchLimitedEnergy(fetchMeters, windSpeedMs);
    if (frac <= 0.0f) return 0.0f;
    const float uA    = 0.71f * std::pow(windSpeedMs, 1.23f);
    const float uARef = 0.71f * std::pow(kReferenceWindMs, 1.23f);
    const float windRatio = (uA / uARef) * (uA / uARef);   // Hs ~ U_A^2, so the scale is squared
    return std::clamp(frac * windRatio, 0.0f, kMaxWaveHeightScale);
}

float fullyDevelopedScale(float windSpeedMs) {
    if (!(windSpeedMs > 0.0f)) return 0.0f;
    const float uA    = 0.71f * std::pow(windSpeedMs, 1.23f);
    const float uARef = 0.71f * std::pow(kReferenceWindMs, 1.23f);
    const float r = (uA / uARef) * (uA / uARef);
    return std::clamp(r, 0.0f, kMaxWaveHeightScale);
}

float windRoughness(float windSpeedMs) {
    const float u = std::max(windSpeedMs, 0.0f);
    // Cox & Munk total mean-square slope, clean sea surface.
    const float mss    = 0.003f + 0.00512f * u;
    const float mssRef = 0.003f + 0.00512f * kReferenceWindMs;
    return std::sqrt(mss / mssRef);
}

WaterProfile waterProfileAt(const WaterBodyIndex* bodies, float worldX, float worldZ,
                            float cellSize, const WaterWind& wind) {
    if (!bodies || bodies->bodies().empty()) return WaterProfile{};
    // nullptr when dry → neutral
    return deriveWaterProfile(bodies->bodyAt(worldX, worldZ), cellSize, wind);
}

void buildHydroUpload(const HydrologyMap& hydro, const WaterBodyIndex* bodies,
                      const WaterLookOverride& ovr, std::vector<float>& out,
                      const WaterWind& wind) {
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
                                                hydro.cellSize(), wind)
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
