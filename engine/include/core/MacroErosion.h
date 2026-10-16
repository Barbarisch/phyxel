#pragma once
// ============================================================================
// M1c Macro erosion — docs/WorldForgeV2Plan.md §5 M1 ("Macro bake"): stream-power erosion on the
// macro sheet. Per step: Priority-Flood fill with the sea as base level (Barnes 2014b, the engine's
// PriorityFlood), D8 receivers from the flood, a Braun & Willett 2013 stack (base level upward),
// drainage area by reverse stack, then the IMPLICIT stream-power update for n = 1 along the stack:
//     h_i^{t+1} = (h_i^t + dt·U_i + f_i·h_r^{t+1}) / (1 + f_i),   f_i = K·dt·A_i^m / d(i, r)
// which is unconditionally stable (Braun & Willett 2013 eq. for n = 1). Cells at or below sea
// level are base level: fixed, never uplifted or eroded. Pure and deterministic: one grid, one
// thread, fixed sweep order — the macro sheet is baked once per recipe, never per tile.
//
// Constants: NONE are shipped here. K, U, dt, m, steps come from the caller (the recipe), and the
// recipe defaults keep erosion OFF (steps = 0) until §10's K / U rows are GROUNDED.
// ============================================================================
#include <cstdint>
#include <vector>

namespace Phyxel {

struct MacroErosionParams {
    int   steps    = 0;      // implicit steps; 0 = no erosion (the inert default until grounded)
    float dt       = 0.0f;   // years per step
    float k        = 0.0f;   // erodibility, m^(1-2m)/yr for n = 1 (its unit depends on m)
    float m        = 0.5f;   // drainage-area exponent (Cordonnier 2016 §3.1: n = 1, m = 0.5; W&T 1999 concavity 0.40-0.50)
    float upliftMax = 0.0f;  // m/yr at the uplift field's maximum (uplift[i] in [0,1] scales it)
    float maxSlope  = 0.577f;// rise/run ceiling between a cell and its receiver (Cordonnier §5: tan 30°); 0 = off
    float cellU    = 512.0f; // metres per cell (1 u = 1 m)
    float seaLevel = 16.0f;  // base level
};

struct MacroErosionResult {
    std::vector<float> height;        // eroded heights (same size as the input)
    std::vector<int>   receiver;      // D8 receiver per cell from the LAST step's flood (self = base level)
    std::vector<float> drainageArea;  // m² per cell from the last step
    std::vector<int>   stack;         // Braun-Willett order: base level first, donors after receivers
    int   baseLevelCells = 0;
    float meanHeightBefore = 0.0f, meanHeightAfter = 0.0f;
};

/// Run `params.steps` implicit stream-power steps on a row-major w×h height grid. `uplift` is a
/// per-cell field in [0,1] (empty = no uplift). Returns the eroded grid plus the last step's
/// drainage structure (receivers, areas, stack) for the tests and for M2's channel work.
MacroErosionResult runMacroErosion(const std::vector<float>& height, int w, int h,
                                   const std::vector<float>& uplift, const MacroErosionParams& params);

/// Valley cells per km²: a cell lower than BOTH its east-west or BOTH its north-south neighbours
/// (a local trough), the structural "valley density" the lab predicts rises with erosion steps.
float macroValleyDensity(const std::vector<float>& height, int w, int h, float cellU);

/// Drainage density (km of channel per km²) of a receiver/area structure: a cell is channel when
/// its drainage area is >= `thresholdArea` (m²); its channel length is the distance to its
/// receiver. The §10 F4c rule: a band is only ever expressed RELATIVE to this threshold.
float macroDrainageDensity(const MacroErosionResult& r, int w, int h, float cellU, float thresholdArea);

} // namespace Phyxel
