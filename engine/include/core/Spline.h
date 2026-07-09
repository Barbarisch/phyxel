#pragma once

#include <vector>

namespace Phyxel {

// ── Terrain shaping spline (docs/TerrainGenerationV2.md §2a — the art-direction primitive) ──
//
// A 1-D piecewise curve over sorted control points (x → y). This is the "how tall" knob that
// decouples terrain height from the "how mountainous" noise: a control channel (e.g. continentalness)
// is fed through a Spline to produce a height contribution, so the same noise can read as a gentle
// coast or a high plateau purely by reshaping the curve — no recompile (the points can come from the
// world recipe). Modeled on Minecraft 1.18's density-function splines.
//
// Interpolation between adjacent points is smoothstep (3t²−2t³): C1-smooth, monotone-safe (never
// overshoots the two bracketing y values), and flat at each control point. Inputs outside the point
// range clamp to the nearest endpoint's value. Pure + deterministic; trivially copyable, so it can be
// captured by value into the coarse-model's pure source (safe under the streaming worker's generator
// copy). An empty spline evaluates to 0.
class Spline {
public:
    struct Point {
        float x;
        float y;
    };

    Spline() = default;
    // Control points; sorted by x on construction (callers need not pre-sort).
    explicit Spline(std::vector<Point> points);

    float eval(float x) const;
    bool empty() const { return m_points.empty(); }
    const std::vector<Point>& points() const { return m_points; }

    // Convenience: a 2-point curve from (x0,y0) to (x1,y1). Because interpolation is smoothstep,
    // ramp(0,a,1,b).eval(t) == a + (b-a)*smoothstep(t) — i.e. it exactly reproduces a smoothstep ramp.
    static Spline ramp(float x0, float y0, float x1, float y1);

private:
    std::vector<Point> m_points;
};

}  // namespace Phyxel
