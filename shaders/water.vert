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
layout(location = 2) out float fragWaveFoam;   // crest sharpness 0..1, drives whitecaps

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
    float size     = pc.params.y;
    float amp      = pc.params.z;
    float windRad  = pc.params.w;
    float waveLen  = max(pc.params2.w, 0.5);

    // Centre the sheet on the camera in XZ; the disc is unit-radius, so scale it out.
    // ⚑GROUND: 0.75 * size (= 1.5 * the chunk render distance). The sheet replaced a SQUARE quad
    // whose diagonal corners reached ~1.41x its half-extent, so a disc at 0.5 * size would fall
    // short of the horizon in the screen corners and show an arc of missing water. The far plane
    // sits at 0.5 * size along the view axis, and the diagonal half-FOV is ~30 degrees, so the disc
    // must reach at least (0.5 / cos 30) ~= 0.58 * size; 0.75 clears that with margin.
    vec2 base = camPos.xz + inPos.xz * (size * 0.75);
    vec3 world = vec3(base.x, seaLevel, base.y);

    vec3 ddx = vec3(1.0, 0.0, 0.0);   // d(position)/dx starts as the flat tangent
    vec3 ddz = vec3(0.0, 0.0, 1.0);

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

        // FLATTEN TOWARD THE HORIZON. Past a few hundred metres a wave is far below a pixel, and
        // leaving it in only produces shimmering aliasing on the coarse outer rings. Fading the
        // displacement out also guarantees the sheet's rim stays exactly at sea level, so it meets
        // the far/near boundary flush.
        float rim = smoothstep(1.0, 0.55, length(inPos.xz));
        disp *= rim;
        ddx = mix(vec3(1.0, 0.0, 0.0), ddx, rim);
        ddz = mix(vec3(0.0, 0.0, 1.0), ddz, rim);

        world += disp;
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
