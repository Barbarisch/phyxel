#pragma once

#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"

#include <vector>

namespace Phyxel {

// ── Per-body water APPEARANCE profile (Water Appearance v4 — docs/Water.md) ──────────
//
// Every optical and mechanical property of water in this engine used to be a GLOBAL CONSTANT, which
// is why all water looked like the same water: one extinction vector, one scatter colour, one set of
// ripple amplitudes. This is the per-body replacement — derived from the hydrology bake's body
// identity (WaterBodyIndex) and carried to both water renderers through the hydrology texture.
//
// W1 (this increment) builds the PIPE ONLY and is deliberately invisible: `deriveWaterProfile`
// returns the NEUTRAL profile for every body, and the neutral values are defined to reproduce
// today's look exactly. W2 grounds turbidity (Jerlov types / Secchi->Kd) and W3 grounds roughness
// (Cox-Munk slope variance) and replaces waveEnergy's area proxy with fetch-limited wave growth.
struct WaterProfile {
    // 0 = clear water — EXACTLY today's Pope & Fry constants in water_common.glsl.
    // 1 = fully turbid. Nothing derived produces a non-zero value until W2; the debug override
    // (WaterLookOverride) is the only way to reach it in W1, which is what makes it a POSITIVE
    // CONTROL for "does the pipe actually reach pixels".
    float turbidity = 0.0f;

    // Gerstner amplitude scale. This one is NOT new — it shipped as tangible-water F and is
    // preserved here bit-for-bit so the refactor is provably behaviour-neutral. W3 replaces the
    // log-area proxy with real fetch-limited wave growth (SMB/CERC).
    float waveEnergy = 1.0f;

    // Multiplier on the fine ripple slope (water_common.glsl `waterRippleNormal`'s a1..a4).
    // 1 = today's fixed detail. Toward 0 the surface loses its micro-chop and becomes a mirror —
    // the mechanism behind "a still lake is glassy". W3 derives it from wind via Cox-Munk.
    float roughness = 1.0f;
};

// Force a profile onto every wet column, bypassing derivation. THE POSITIVE CONTROL: with this
// active a measurable pixel change proves the value travelled body -> texture -> shader -> frame.
// Driven by POST /api/debug/water_look. Never set by normal operation.
struct WaterLookOverride {
    bool  active    = false;
    float turbidity = 0.0f;
    float roughness = 1.0f;
};

// ── WIND (v4 W3) ──────────────────────────────────────────────────────────────────────────────
// The sea state was three globals (amplitude, wavelength, direction) with no notion of wind SPEED,
// so nothing could distinguish a calm day from a gale. Wind speed now drives how developed each
// body's sea is (via fetch) and how rough its surface is (via slope).
struct WaterWind {
    // ⚑GROUND: 6.7 m/s is the mid-point of Beaufort force 4 (11-16 kn = 5.5-7.9 m/s, WMO/Met
    // Office Beaufort scale) — the sea state the shipped swell constants were authored to, so this
    // default reproduces today's look.
    float speedMs = 6.7f;
    float dirRadians = 0.6f;   // matches WaterRenderPipeline's shipped m_windDirection
};

// The profile for one body (nullptr = no body at this column, e.g. open ocean beyond the bake).
// `cellSize` is the hydrology bake's cell size in world units — needed to turn the body's
// `volumeEst` into a MEAN DEPTH, which is what drives turbidity.
//
// ⚑TURBIDITY IS DERIVED FROM A PROXY, AND THE PROXY IS NAMED (v4 W2). The bake knows nothing about
// sediment, algae or catchment: it knows class, area and volume. So turbidity is inferred from
// MEAN DEPTH, which is the strongest thing the bake carries that actually tracks clarity in the
// real world — shallow bodies resuspend bottom sediment and have a high catchment-to-volume ratio,
// deep ones stratify and settle. The published Secchi figures line up with exactly that ordering:
// Crater Lake (deep) 44 m and Tahoe (deep) ~18 m vs Carlson's mesotrophic 2.3-4.6 m and eutrophic
// 0.9-2.3 m. It is a PROXY, not a measurement — a genuinely muddy deep reservoir will read clear.
WaterProfile deriveWaterProfile(const WaterBodyIndex::Body* body, float cellSize,
                                const WaterWind& wind = {});

// The profile at a world column — THE shared "what water is this?" query, so every consumer agrees.
// Returns the NEUTRAL profile wherever there is no body: dry land, outside the baked region, or a
// world with no body index at all.
//
// ⚑WHY THIS EXISTS SEPARATELY FROM THE TEXTURE: the surface shading reads its profile from the
// hydrology texture (per pixel), but the UNDERWATER overlay is a fullscreen pass with no per-pixel
// body — it needs one profile for "the water the camera is inside". If those two disagree, breaking
// the surface pops: the lake looks murky from above and clear from below. Same function, one truth.
WaterProfile waterProfileAt(const WaterBodyIndex* bodies, float worldX, float worldZ, float cellSize,
                            const WaterWind& wind = {});

// ── FETCH (v4 W3) ─────────────────────────────────────────────────────────────────────────────
// The distance the wind blows over open water before reaching a point — the quantity that decides
// how big a wave a body can actually build. It is why a pond cannot have a swell however hard the
// wind blows, and it replaces the shipped `log2(areaCells)/10` proxy whose own comment admits its
// normaliser was eyeballed.
//
// Returned in WORLD UNITS for wind blowing along `windDirRadians` (0 = +X, matching the sea
// shader's wind convention) across the body's bounding box, given in inclusive CELL coordinates.
//
// ⚑APPROXIMATION, stated: this is the longest chord of the BOUNDING BOX along the wind, not of the
// body's true outline. For a bay or a dogleg lake it OVERSTATES fetch, because the box spans water
// the wind never actually crosses. A true fetch would march the wet mask upwind per column; that is
// a far heavier query and this is the honest cheap stand-in.
float fetchAlongWind(const glm::ivec2& bboxMinCells, const glm::ivec2& bboxMaxCells,
                     float cellSize, float windDirRadians);

// Fraction of the FULLY-DEVELOPED sea a body reaches given its fetch and the wind — i.e. the
// per-body wave energy, in [0,1]. Replaces the shipped `clamp(log2(areaCells+1)/10, 0.15, 1)`,
// whose own comment admitted its normaliser was eyeballed and which was blind to wind heading.
//
// ⚑GROUND — CERC (1984), Shore Protection Manual, U.S. Army Corps of Engineers, Vol. I Ch. 3
// (eqs. 3-39/3-40, wind-stress factor eq. 3-28a), deep-water fetch-limited growth:
//     U_A = 0.71 * U^1.23              (wind-stress factor; U is the 10 m wind in m/s)
//     X   = g * F / U_A^2              (dimensionless fetch)
//     H   = 0.283 * tanh(0.0125 * X^0.42) * U_A^2 / g      (significant wave height)
// THE RETURNED VALUE IS THE `tanh(...)` TERM ITSELF, which is bounded [0,1] by construction and
// goes to 1 at unlimited fetch.
//
// ⚑A CONSTRUCTION THIS DELIBERATELY AVOIDS (grounding-auditor, 2026-08-03): the obvious-looking
// "energy = H_fetch-limited / H_fully-developed" with a Pierson-Moskowitz denominator is WRONG —
// SMB's asymptote (0.283) and PM's coefficient (0.21) disagree by ~35% AND use different reference
// heights (10 m vs 19.5 m), so that ratio would exceed 1 at large fetch, the exact opposite of
// "oceans -> 1". Using SMB's own tanh term needs no second formula and cannot exceed 1.
//
// ⚑KNOWN BIAS, not hidden: SMB is the least accurate of the common predictors and tends to
// OVERPREDICT H, especially near full development ("Shore protection manual's wave prediction
// reviewed", Ocean Engineering, 1992). JONSWAP (Hasselmann et al. 1973) is better but has no
// closed form suitable for this. SMB ships because it is the only practical closed form.
float fetchLimitedEnergy(float fetchMeters, float windSpeedMs);

// Amplitude multiplier for the shader's swell, relative to the REFERENCE sea (a fully-developed
// Beaufort-4 ocean). This — not `fetchLimitedEnergy` — is what scales the Gerstner amplitude.
//
// ⚑A BUG THIS EXISTS TO FIX (found by the W3 L4 probe, 2026-08-03). The first version multiplied
// the shader's fixed authored amplitude by `fetchLimitedEnergy` directly. But that is the FRACTION
// of the fully-developed sea, and X = gF/U_A^2 FALLS as wind rises, so a stronger wind produced a
// SMALLER swell — physically backwards. Absolute height is
//     Hs = 0.283 * tanh(0.0125*X^0.42) * U_A^2 / g
// and the U_A^2/g factor is exactly what the fraction drops. Measured at a real body: wind
// 6.7 -> 15 m/s moved the fraction 0.448 -> 0.206 (waves shrink) while true Hs went 0.70 -> 2.35 m
// (waves nearly triple). The rendered result was a near-perfect cancellation against the rising
// Cox-Munk roughness, which is why the first L4 read as "wind does nothing".
//
// Dividing by the reference sea's height leaves:
//     scale = tanh(0.0125*X^0.42) * (U_A(U) / U_A(U_ref))^2
// which is exactly 1 for a fully-developed sea at the reference wind — so today's ocean is
// unchanged — and rises above 1 in stronger winds, as it must.
//
// ⚑CLAMPED at 4x. SMB is documented to OVERPREDICT near full development, and an unclamped storm
// would drive the Gerstner steepness sum past the self-intersection limit the vertex shader
// depends on. The clamp is an engineering guard, not a physical claim.
float waveHeightScale(float fetchMeters, float windSpeedMs);

inline constexpr float kMaxWaveHeightScale = 4.0f;

// Amplitude multiplier for a FULLY-DEVELOPED sea at this wind — the ocean case, where fetch is not
// a limit at all. This is `waveHeightScale` with the tanh term at its saturation value of 1:
//     scale = (U_A(U) / U_A(U_ref))^2
//
// ⚑DO NOT approximate this by passing a "very large" fetch to waveHeightScale. That was tried with
// 1e6 m and is wrong: at Beaufort 4 it yields tanh = 0.965, not 1, so every ocean quietly lost 3.5%
// of its swell and the reference-sea identity (scale == 1 at the reference wind) broke. Saturating
// that tanh needs fetch on the order of 5000 km, and MORE as wind rises since X = gF/U_A^2. The
// closed form has no such error and no magic number.
float fullyDevelopedScale(float windSpeedMs);

// Fine-ripple slope scale from wind speed — the "roughness" channel. 1.0 at the reference wind, so
// the default reproduces today's ripple detail exactly; toward 0 the surface loses its micro-chop
// and becomes a mirror (which is what makes a calm lake reflect).
//
// ⚑GROUND — Cox, C. & Munk, W. (1954), "Measurement of the roughness of the sea surface from
// photographs of the sun's glitter", J. Opt. Soc. Am. 44(11):838-850: total mean-square slope
//     mss = 0.003 + 5.12e-3 * U        (clean sea surface; U at a 12.5 m reference height)
// Slope RMS is sqrt(mss), and the shader's ripple amplitude produces slope, so the correct scale
// factor between two wind speeds is the RATIO OF RMS SLOPES = sqrt(mss(U)/mss(U_ref)).
//
// ⚑TWO APPROXIMATIONS, both stated rather than buried:
//   * Cox-Munk's U is at 12.5 m and Beaufort/SMB use 10 m. A 1/7-power-law correction is
//     (12.5/10)^(1/7) ~= 1.03, i.e. ~3% — ignored as below the noise of everything else here.
//   * mss integrates the WHOLE slope spectrum (centimetre to metre scales); this shader's four
//     octaves span only ~0.8-4.5 world units, so applying a full-spectrum ratio to a narrow band
//     is an approximation, not an identity.
float windRoughness(float windSpeedMs);

// The wind at which both of the above return their reference values (roughness exactly 1.0).
inline constexpr float kReferenceWindMs = 6.7f;

// ── Turbidity mapping anchors (v4 W2) ─────────────────────────────────────────────────────────
// Mean depth (world units ~ metres) at which a body reads fully turbid / fully clear. ⚑GROUND by
// the trophic-state ordering above: eutrophic shallow lakes (Secchi 0.9-2.3 m, Carlson 1977) are
// characteristically a couple of metres deep; the clear oligotrophic references (Tahoe, Crater) are
// tens of metres. Bodies between the two interpolate smoothly.
inline constexpr float kTurbidDepth = 2.0f;    // <= this deep  -> turbidity 1
inline constexpr float kClearDepth  = 20.0f;   // >= this deep  -> turbidity 0

// Floats per hydrology texel. The render texture is R32G32B32A32_SFLOAT:
//   R = basin water level (HydrologyMap::NO_WATER = dry)   — water-layer P1
//   G = wave energy                                        — tangible-water F
//   B = turbidity                                          — v4 W1 (inert until W2)
//   A = roughness                                          — v4 W1 (inert until W3)
// Widened from RG32F in W1; the sea shader's push block is EXACTLY full at 128 B, so the texture is
// the only vehicle available for per-column data.
inline constexpr int kHydroTexelFloats = 4;

// Pack the hydrology render texture, row-major (index = z*cellsX + x), `kHydroTexelFloats` per cell.
// Pure and deterministic — this is the unit-testable half of the upload that used to be an inline
// loop in RenderCoordinator::drawFrame.
//
// `bodies` may be null (no body index baked) — then every wet column gets the neutral profile at
// full wave energy, which is the pre-tangible-water-F behaviour.
void buildHydroUpload(const HydrologyMap& hydro, const WaterBodyIndex* bodies,
                      const WaterLookOverride& ovr, std::vector<float>& out,
                      const WaterWind& wind = {});

}  // namespace Phyxel
