#version 450
//
// water.vert — the sea surface (WaterSystemV3 Phase 2).
//
// WAS: a single quad locked to sea level. That is why the ocean could never read as an ocean — a
// perfectly flat plane has no shape for light to catch, whatever the fragment shader does.
//
// NOW: a camera-relative CARTESIAN CLIPMAP (built by buildSeaClipmap, see SeaMesh.h) displaced by a
// sum of GERSTNER waves. Gerstner (trochoidal) waves move each vertex in a circle rather than just
// up and down, so crests sharpen and troughs broaden — the shape real wind-driven water has, and the
// reason this reads as water where a plain sine sheet reads as a wobbling bedsheet.
//
// The mesh used to be a camera-centred RADIAL grid, and that was the cause of the "waves emanate
// from a point at the camera" vortex: a polar mesh's angular spacing grows with radius, so the swell
// aliased azimuthally, and aliasing inherits the sampling pattern's symmetry — radial sampling gives
// radial spokes. The clipmap has no radial structure and no centre, and each wave component now
// fades where the local mesh spacing can no longer sample it (see NYQ below).
//
// The normal is computed ANALYTICALLY from the wave derivatives rather than from neighbouring
// vertices: it stays correct no matter how coarse the grid gets toward the horizon, which is what
// lets the radial grid spend so few vertices out there.
//
layout(location = 0) in vec3 inPos; // unit disc in the XZ plane: xz in [-1,1], y = 0

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 params;     // x = seaLevel, y = hydro grid originX, z = wave amplitude, w = wind dir (rad)
    vec4 params2;    // x = screen width, y = screen height, z = reflectionEnabled, w = wave length
    vec4 params3;    // x = clipmap core spacing, y = core half-extent, z = hydro originZ, w = hydro invCellSize (0 = flat sea)
} pc;

// WATER LAYER (terrain-gen stage output; water-layer P1): per-column basin levels baked by the
// hydrology stage — the sea at sea level, every lake at its own spill. NEAREST-sampled: basins
// are piecewise-constant and filtering across a divide would tilt the surface.
layout(set = 1, binding = 3) uniform sampler2D hydroLevelTex;

// Per-column basin level + wave ENERGY at a world XZ (RG texture: R = level, G = energy from
// body size — tangible-water F). Falls back to the flat sea level at full energy when no layer
// is bound (invCellSize 0), outside the baked region (the open ocean beyond ±16 km), or on dry
// columns (sentinel) — the dry-land gate in the fragment stage removes the sheet over dry land.
float basinLevelAt(vec2 worldXZ, out float valid, out float energy) {
    valid = 0.0;
    energy = 1.0;
    float invCell = pc.params3.w;
    if (invCell <= 0.0) return pc.params.x;
    vec2 cellF = (worldXZ - vec2(pc.params.y, pc.params3.z)) * invCell;
    ivec2 sz = textureSize(hydroLevelTex, 0);
    if (cellF.x < 0.0 || cellF.y < 0.0 || cellF.x >= float(sz.x) || cellF.y >= float(sz.y))
        return pc.params.x;
    vec2 le = texelFetch(hydroLevelTex, ivec2(cellF), 0).rg;
    if (le.r < -1e5) return pc.params.x;   // dry column sentinel
    valid = 1.0;
    energy = le.g;
    return le.r;
}

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWaveNormal;
layout(location = 2) out float fragWaveFoam;    // crest sharpness 0..1, drives whitecaps
// Where this point sits in the wave cycle: -1 deep in a trough, +1 on a crest. The shore surf
// needs it — a wave breaks on its CREST as it runs into shallow water, so foam has to be gated on
// the wave phase or the whole surf zone turns uniformly white.
layout(location = 3) out float fragWavePhase;

// One Gerstner wave. Returns the xyz displacement and accumulates the analytic tangent/bitangent
// partial derivatives so the caller can build an exact normal.
//   dir       — horizontal travel direction (unit)
//   steepness — 0..1 crest sharpness; the sum across waves must stay <= 1 or the surface self-
//               intersects (loops over itself), which shows up as a shimmering knot at the crest.
vec3 gerstner(vec2 p, vec2 dir, float amplitude, float wavelength, float steepness, float t,
              inout vec3 ddx, inout vec3 ddz) {
    float k = 6.28318530718 / wavelength;          // spatial frequency
    float c = sqrt(9.81 / k);                      // deep-water phase speed: c = sqrt(g/k)
    float f = k * (dot(dir, p) - c * t);
    float a = steepness / k;                       // Gerstner's horizontal swing radius

    float sinF = sin(f), cosF = cos(f);

    ddx += vec3(-dir.x * dir.x * (steepness * sinF),
                 dir.x * (k * amplitude * cosF),
                -dir.x * dir.y * (steepness * sinF));
    ddz += vec3(-dir.x * dir.y * (steepness * sinF),
                 dir.y * (k * amplitude * cosF),
                -dir.y * dir.y * (steepness * sinF));

    return vec3(dir.x * (a * cosF), amplitude * sinF, dir.y * (a * cosF));
}

void main() {
    vec3  camPos   = pc.camPosTime.xyz;
    float t        = pc.camPosTime.w;
    float seaLevel = pc.params.x;
    // params.y (sheet size) is no longer used for geometry — the mesh carries absolute world radii.
    // Kept in the push block so the layout stays shared with the fragment stage and the overlay.
    float amp      = pc.params.z;
    float windRad  = pc.params.w;
    float waveLen  = max(pc.params2.w, 0.5);

    // The mesh already carries ABSOLUTE world-space offsets (see buildSeaMesh) — it is no longer
    // rescaled by the render distance, because doing so stretched the rings past the wave's Nyquist
    // limit as soon as the view distance grew, and the ocean aliased into smeared blobs. The skirt
    // reaches far enough to cover any view distance; the far plane clips the rest.
    vec2 base = camPos.xz + inPos.xz;
    // WATER LAYER: the sheet sits at each column's own basin level, so lakes render at their
    // spill height and the sea at sea level from ONE draw. Rims between basins produce stretched
    // quads one mesh cell wide; the fragment stage re-samples the level per pixel for the
    // dry-land gate, and the divide's terrain is above both basins' levels by definition, so
    // those wall pixels gate to zero alpha.
    float levelValid, bodyEnergy;
    float level = basinLevelAt(base, levelValid, bodyEnergy);
    vec3 world = vec3(base.x, level, base.y);
    // Wave energy proportional to BODY SIZE (tangible-water F): fetch-limited waves — the ocean
    // carries the full swell (energy 1), a big lake most of it, a mountain tarn barely a ripple
    // (floor 0.15). Replaces the old binary "not sea → 0.2×" rule; per-column, free.
    amp *= bodyEnergy;

    vec3 ddx = vec3(1.0, 0.0, 0.0);   // d(position)/dx starts as the flat tangent
    vec3 ddz = vec3(0.0, 0.0, 1.0);
    fragWavePhase = 0.0;

    if (amp > 0.0001) {
        // PER-COMPONENT NYQUIST LOD. This is the half of the vortex fix that lives in the shader.
        //
        // The mesh is a clipmap (see SeaMesh.h): uniform spacing near the camera, doubling with each
        // level outward. The core resolves the swell with margin, but the outer levels are coarser
        // than the swell's Nyquist limit — and a wave the mesh cannot sample does not render as a
        // smaller wave, it renders as garbage. On the old polar mesh that garbage inherited the
        // sampling pattern's radial symmetry and became spokes converging on the viewer.
        //
        // So each component is faded out on ITS OWN terms, by how many mesh samples fall across its
        // wavelength. Below ~2 samples it is unrepresentable and goes; above ~3.2 it is fully drawn.
        // This is NOT the "amplitude envelope at a fixed radius" that caused the original vortex:
        // that flattened ALL waves at one camera-relative distance, a visible ring travelling with
        // the viewer. This fades each wavelength where that wavelength stops being samplable, which
        // is exactly how the fragment shader's ripple octaves already retire.
        float coreSpacing = max(pc.params3.x, 0.01);
        float coreHalf    = max(pc.params3.y, 1.0);
        // Local grid spacing, reconstructed from the radius. The clipmap doubles spacing every time
        // it doubles extent, so spacing is proportional to radius outside the core — a smooth
        // function, deliberately, because a PER-VERTEX spacing would step by 2x across a level
        // boundary and crease the surface along that single row of vertices.
        float localSpacing = coreSpacing * max(1.0, length(inPos.xz) / coreHalf);
        #define NYQ(lambda) smoothstep(2.0, 3.2, (lambda) / localSpacing)

        // ⚑GROUND: a real wind sea is a spectrum, not one sinusoid. Components at spreading angles
        // off the wind with decreasing amplitude. Steepness sums to 0.87 (< 1), so crests stay sharp
        // without the surface self-intersecting into a shimmering knot.
        //
        // The LONG component is what keeps the far ocean from going dead flat. The three original
        // components are all <= 14 units, so they have all faded by the second clipmap level — and a
        // flat horizon was a specific complaint. A long, low swell survives to the outer levels
        // because a coarse mesh CAN sample it, which is also how a real sea looks at that distance:
        // you see the swell, not the chop. The three near-field components are unchanged, so the
        // close-up look this replaces is preserved exactly.
        vec2 w0 = vec2(cos(windRad), sin(windRad));
        vec2 w1 = vec2(cos(windRad + 0.6), sin(windRad + 0.6));
        vec2 w2 = vec2(cos(windRad - 0.9), sin(windRad - 0.9));
        vec2 w3 = vec2(cos(windRad + 0.25), sin(windRad + 0.25));

        float lam0 = waveLen, lam1 = waveLen * 0.61, lam2 = waveLen * 0.33, lam3 = waveLen * 5.0;
        float k0 = NYQ(lam0), k1 = NYQ(lam1), k2 = NYQ(lam2), k3 = NYQ(lam3);

        vec3 disp = vec3(0.0);
        disp += gerstner(base, w0, amp        * k0, lam0, 0.38 * k0, t, ddx, ddz);
        disp += gerstner(base, w1, amp * 0.52 * k1, lam1, 0.24 * k1, t, ddx, ddz);
        disp += gerstner(base, w2, amp * 0.28 * k2, lam2, 0.13 * k2, t, ddx, ddz);
        disp += gerstner(base, w3, amp * 0.70 * k3, lam3, 0.12 * k3, t, ddx, ddz);

        world += disp;
        // Normalised height in the wave cycle. Divide by the summed amplitude actually in play so
        // this lands in roughly -1..1 whatever the amplitude setting and whichever components the
        // LOD has retired — a fixed divisor would make the shore surf's crest gate drift as
        // components fade.
        float ampSum = amp * (k0 + 0.52 * k1 + 0.28 * k2 + 0.70 * k3);
        fragWavePhase = clamp(disp.y / max(ampSum, 1e-4), -1.0, 1.0);
    }

    // Analytic normal from the two partial derivatives. cross(ddz, ddx) yields +Y for a flat sheet.
    fragWaveNormal = normalize(cross(ddz, ddx));
    // Crest sharpness: the normal tilting away from vertical marks the steep faces where a real
    // swell breaks white. ⚑GROUND: a smoothstep with a DEAD ZONE, not a linear ramp — at Beaufort 4
    // whitecaps appear on a minority of the steepest crests, not as a continuous sheet. A linear
    // ramp put foam on nearly every wave face and read as painted-on corduroy.
    fragWaveFoam = smoothstep(0.10, 0.38, 1.0 - fragWaveNormal.y);

    fragWorldPos = world;
    gl_Position = pc.viewProj * vec4(world, 1.0);
}
