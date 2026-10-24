// water_body_force.glsl - WaterCore 31 (M4): the water's push on a body, as the pressure integral over its oriented box.
// docs/WaterCore.md 31.2 / 31.6. Valid as GLSL and as C++ (glm; engine/include/core/water/WaterBodyForce.h includes it), so the
// unit tests run the shader's own arithmetic.
//
// The includer defines   vec2 wbfP(vec3 x)   : the water at world point x - (kinematic pressure p / rho_w in m^2/s^2, the y of
// the point that pressure belongs to: a cell centre for a sampled field). p <= 0 means AIR there (no water to push with); a
// NaN p means UNKNOWN (outside the sampled water) and makes the whole result unknown (w < 0: the caller falls back).
//
// The rule. a = (1 / V) sum over the faces of (-n p dA): the acceleration for a body of the water's own density (multiply by the
// body's buoyancy ratio b = rho_w / rho_body). Each face is cut into n x n patches (n = clamp(ceil(side / h), 1, 4)). At a patch the
// water is read near its lowest point when that holds water (a body the water does not see - a resting floater is not rasterized - has water
// in its cells), else ONE cell outside the face (inside a moving body the cells are walls with p = 0; one cell out, no body overlaps
// the sampled cell); the pressure read gives the water's LEVEL there (p = g (level - y)), and the patch's clipped depth under that
// level is integrated exactly - so still water gives Archimedes exactly at any tilt, with no sideways push. Air at both points:
// nothing - never a pressure invented in air. So water on top of a body pushes it down and air under it pushes nothing (31.1: the
// cap a rising block threw over itself lifted it 3 m - the table law read the column's top as its waterline).
//
// Returns xyz = that acceleration (m/s^2, b = 1) and w = the submerged fraction the subgrid laws use (a_y / g clamped 0..1: V_sub
// / V in still water; 0 under a cap), or w = -1 when any sample was unknown.

#ifndef WATER_BODY_FORCE_GLSL
#define WATER_BODY_FORCE_GLSL

#ifdef __cplusplus
#define WBF_FN inline
#define WBF_ISNAN(x) std::isnan(x)
#else
#define WBF_FN
#define WBF_ISNAN(x) isnan(x)
#endif

// wbfP's "no water to read here" - inside a body (the volume's raster: look further out) or solid ground; any p at or under it
#define WBF_IN_BODY (-1.0e29f)

WBF_FN vec3 wbfAxis(int k, vec3 a0, vec3 a1, vec3 a2) { return k == 0 ? a0 : (k == 1 ? a1 : a2); }

// The mean over the unit square of max(0, a + b u + c v) - the clipped linear depth over a patch, EXACT (the integral of the
// hydrostatic pressure of water with a free surface). Midpoint samples clipped at the waterline left a tilted block in still water a
// sideways push of up to 3 % of g (WaterBodyForce.ATiltedBlockFeelsTheWaterItDisplaces) - it would have drifted. Near-degenerate
// slopes (|b| or |c| under 2 % of the scale: the closed form cancels in float) take the 1-D or the flat form; their error is O(b^2).
WBF_FN float wbfF3(float t) { return t > 0.0f ? t * t * t / 6.0f : 0.0f; }
WBF_FN float wbfF2(float t) { return t > 0.0f ? t * t / 2.0f : 0.0f; }
WBF_FN float wbfClipMean(float a, float b, float c) {
    const float sc = 0.02f * (abs(a) + abs(b) + abs(c)) + 1e-9f;
    const bool fb = abs(b) < sc, fc = abs(c) < sc;
    if (fb && fc) return max(0.0f, a + 0.5f * b + 0.5f * c);
    if (fb) { const float a1 = a + 0.5f * b; return (wbfF2(a1 + c) - wbfF2(a1)) / c; }
    if (fc) { const float a1 = a + 0.5f * c; return (wbfF2(a1 + b) - wbfF2(a1)) / b; }
    return (wbfF3(a + b + c) - wbfF3(a + b) - wbfF3(a + c) + wbfF3(a)) / (b * c);
}

// c = the box centre (world), he = its half extents along its own axes a0, a1, a2 (unit, world), h = the water's cell size, g > 0.
// levelCap: no sample may imply a water level above it - the highest real surface round the body (31.7: a body the volume rasterizes
// is a PUMP - the water it overlaps is pushed out by a source, never sucked back when it leaves - and that pressure implied levels
// metres above any water: a 1 m block bounced on it at 1-3 m/s and never rested). +inf = no cap (the analytic tests).
WBF_FN vec4 wbfAccel(vec3 c, vec3 he, vec3 a0, vec3 a1, vec3 a2, float h, float g, float levelCap) {
    vec3 sum = vec3(0.0f);
    bool unknown = false;
    for (int k = 0; k < 3; ++k) {
        const int k1 = (k + 1) % 3, k2 = (k + 2) % 3;
        const vec3 ak = wbfAxis(k, a0, a1, a2), t1 = wbfAxis(k1, a0, a1, a2), t2 = wbfAxis(k2, a0, a1, a2);
        const float s1 = 2.0f * he[k1], s2 = 2.0f * he[k2];
        // the patch count is bounded (4 a side): still water is exact with ONE patch a face (each patch integrates its clipped
        // depth exactly); more patches only resolve a level that varies across the face (waves) - cost, not correctness
        const int n1 = int(clamp(ceil(s1 / h), 1.0f, 4.0f)), n2 = int(clamp(ceil(s2 / h), 1.0f, 4.0f));
        const float l1 = s1 / float(n1), l2 = s2 / float(n2);
        for (int side = 0; side < 2; ++side) {
            const vec3 n = side == 0 ? ak : -ak;
            const vec3 fc = c + n * he[k];
            for (int i = 0; i < n1; ++i) {
                for (int j = 0; j < n2; ++j) {
                    const vec3 x0 = fc + t1 * (float(i) * l1 - 0.5f * s1) + t2 * (float(j) * l2 - 0.5f * s2);   // the patch's corner
                    const vec3 xf = x0 + t1 * (0.5f * l1) + t2 * (0.5f * l2);                                // its centre
                    // the water is read near the patch's LOWEST point (a fifth of the way to its centre): a patch the waterline crosses
                    // with its centre in air is still partly wet - read at its centre it was skipped, and a tilted block lost the
                    // wet half of its waterline patches (up to 3 % of g sideways, ATiltedBlockFeelsTheWaterItDisplaces)
                    const vec3 lo = x0 + t1 * (t1.y < 0.0f ? l1 : 0.0f) + t2 * (t2.y < 0.0f ? l2 : 0.0f);
                    const vec3 xs = lo + (xf - lo) * 0.2f;
                    // the water's level at the patch: the face point first (a level from further out on a sloped surface is the level
                    // BESIDE the face - preferring one cell out overstated a slope's push by a third, ASlopedSurfacePushesDownTheSlope);
                    // a point INSIDE a body (WBF_IN_BODY: the volume's raster - placed where the body is PREDICTED to be, so on entry it
                    // runs ahead of the body and swallowed the cells under its bottom face; a 1 m block fell to the floor) steps outward,
                    // a cell at a time, up to three
                    vec2 smp = wbfP(xs);
                    for (int st = 1; st <= 3 && !WBF_ISNAN(smp.x) && smp.x <= WBF_IN_BODY; ++st) smp = wbfP(xs + n * (h * float(st)));
                    if (WBF_ISNAN(smp.x)) { unknown = true; continue; }
                    float level;
                    if (smp.x <= WBF_IN_BODY) {
                        // still a body three cells out, or SOLID (a block lying on the pond's floor): no water to read - the water round
                        // it seeps in under a face at its level (the ring's: levelCap). A perfect seal held a 1 m wood block on the floor
                        // with the water above pressing it down; real wood lifts off a pond bed because water gets under it.
                        if (levelCap > 1.0e37f) continue;
                        level = levelCap;
                    } else {
                        if (smp.x <= 0.0f) continue;                   // air: nothing pushes
                        level = min(smp.y + smp.x / g, levelCap);      // p = g (level - y): the hydrostatic level of that pressure
                    }
                    const float depthMean = wbfClipMean(level - x0.y, -t1.y * l1, -t2.y * l2);
                    sum -= n * (g * depthMean * l1 * l2);
                }
            }
        }
    }
    const float V = 8.0f * he.x * he.y * he.z;
    const vec3 a = sum / max(V, 1e-12f);
    if (unknown) return vec4(a, -1.0f);
    return vec4(a, clamp(a.y / g, 0.0f, 1.0f));
}

#endif
