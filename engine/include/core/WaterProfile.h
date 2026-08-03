#pragma once

#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"

#include <vector>

namespace Phyxel {

// ── Per-body water APPEARANCE profile (Water Appearance v4 — docs/WaterAppearanceV4.md) ──────────
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

// The profile for one body (nullptr = no body at this column, e.g. open ocean beyond the bake).
//
// ⚑W1 IS NEUTRAL BY DESIGN: turbidity 0 and roughness 1 for every class. Only `waveEnergy` carries
// real per-body variation, and only because it already shipped. Do not read the class switch below
// as "oceans are the same as tarns" — it is a placeholder with the real derivation scheduled.
WaterProfile deriveWaterProfile(const WaterBodyIndex::Body* body);

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
                      const WaterLookOverride& ovr, std::vector<float>& out);

}  // namespace Phyxel
