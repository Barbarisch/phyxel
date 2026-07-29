#version 450
//
// water.vert — the sea surface (WaterSystemV3 Phase 2).
//
// WAS: a single quad locked to sea level. That is why the ocean could never read as an ocean — a
// perfectly flat plane has no shape for light to catch, whatever the fragment shader does.
//
// NOW: a camera-centred radial grid (built in WaterRenderPipeline) displaced by a sum of GERSTNER
// waves. Gerstner (trochoidal) waves move each vertex in a circle rather than just up and down, so
// crests sharpen and troughs broaden — the shape real wind-driven water has, and the reason this
// reads as water where a plain sine sheet reads as a wobbling bedsheet.
//
// The normal is computed ANALYTICALLY from the wave derivatives rather than from neighbouring
// vertices: it stays correct no matter how coarse the grid gets toward the horizon, which is what
// lets the radial grid spend so few vertices out there.
//
layout(location = 0) in vec3 inPos; // unit disc in the XZ plane: xz in [-1,1], y = 0

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 params;     // x = seaLevel, y = sheet size, z = wave amplitude, w = wind direction (rad)
    vec4 params2;    // x = screen width, y = screen height, z = reflectionEnabled, w = wave length
} pc;

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
    vec3 world = vec3(base.x, seaLevel, base.y);

    vec3 ddx = vec3(1.0, 0.0, 0.0);   // d(position)/dx starts as the flat tangent
    vec3 ddz = vec3(0.0, 0.0, 1.0);
    fragWavePhase = 0.0;

    if (amp > 0.0001) {
        // Three waves at spreading angles and decreasing scale. ⚑GROUND: a real wind sea is a
        // spectrum, not one sinusoid; three components at ~±30 degrees off the wind with
        // halving amplitude is the cheapest sum that stops the surface looking like corduroy.
        // Steepness sums to 0.75 (< 1), keeping the crests sharp but non-self-intersecting.
        vec2 w0 = vec2(cos(windRad), sin(windRad));
        vec2 w1 = vec2(cos(windRad + 0.6), sin(windRad + 0.6));
        vec2 w2 = vec2(cos(windRad - 0.9), sin(windRad - 0.9));

        vec3 disp = vec3(0.0);
        disp += gerstner(base, w0, amp,        waveLen,        0.38, t, ddx, ddz);
        disp += gerstner(base, w1, amp * 0.52, waveLen * 0.61, 0.24, t, ddx, ddz);
        disp += gerstner(base, w2, amp * 0.28, waveLen * 0.33, 0.13, t, ddx, ddz);

        // FLATTEN AT THE EDGE OF THE WAVE ZONE, and only there. Beyond it the mesh is a coarse
        // coverage skirt that cannot resolve a wave, so the swell has to reach zero by the join or
        // it aliases into blobs — but the zone is now sized (setWaveRadius) to outreach the far
        // plane, so this taper falls where nothing is drawn. That matters: an amplitude envelope
        // INSIDE the visible range is a ring centred on the viewer that follows the camera around,
        // which is what made the ocean look like it radiated from wherever the camera stood.
        float waveRadius = max(pc.params.y, 1.0);
        float rim = 1.0 - smoothstep(waveRadius * 0.88, waveRadius, length(inPos.xz));
        disp *= rim;
        ddx = mix(vec3(1.0, 0.0, 0.0), ddx, rim);
        ddz = mix(vec3(0.0, 0.0, 1.0), ddz, rim);

        world += disp;
        // Normalised height in the wave cycle. The summed amplitude is amp*(1 + 0.52 + 0.28), so
        // divide by that to land in roughly -1..1 regardless of the amplitude setting.
        fragWavePhase = clamp(disp.y / (amp * 1.8), -1.0, 1.0);
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
