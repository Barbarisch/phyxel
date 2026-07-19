#version 450
#extension GL_GOOGLE_include_directive : require
#include "wind.glsl"

// Lightweight grass blades. ONE instance per grass-topped voxel (GrassInstanceData); this shader
// procedurally fans it into `bladesPerVoxel` blades using gl_VertexIndex (6 verts/blade, no
// vertex buffer). Two silhouettes (pc.bladeStyle): BOXY voxel-aesthetic (default) = thin
// elongated crisp RECTANGLE, rest height quantized to the 1/9-voxel microcube grid; SMOOTH
// legacy = ribbon tapering to a point. Both share the SAME smooth wind motion — the shared
// procedural gust field (wind.glsl, fed by the CPU WindSystem via push constants); the boxy
// look is silhouette-only, never quantized motion (quantized offsets read as janky popping).
// Sprout-in growth uses ubo.elapsedTime; distance fade uses ubo.cameraPosition.
// See GrassRenderPipeline / docs/VegetationWindPlan.md.

layout(location = 0) in uint inPacked;   // bits: 0-4 x |5-9 y |10-14 z |15-18 sky |19-22 R |23-26 G |27-30 B
layout(location = 1) in uint inTex;      // low16 = grass top-face texture index (colour source)

// Full set-0 UBO (must match UniformBufferObject in vulkan/VulkanDevice.h). We only read view/proj,
// cameraPosition and the trailing elapsedTime, but the layout up to them must be declared.
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
    float emissiveMultiplier;
    vec3 cameraPosition;
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;          // proj*view, precombined once per frame on CPU
    mat4 biasedLightSpace;  // shadow bias * lightSpaceMatrix, precombined on CPU
    vec3 cameraWorld;       // true camera world position (camera-relative rendering)
    // Grass interaction displacers (VegetationWindPlan Phase 4 v1): characters within the
    // grass radius, uploaded per frame by RenderCoordinator via VulkanDevice::setGrassDisplacers.
    // xyz = CAMERA-RELATIVE feet position (same space as rootWorld below), w = push radius.
    vec4  grassDisplacers[16];
    vec4  grassDisplacersAux[16];   // x = strength envelope 0..1 (eased attack/release on CPU)
    ivec4 grassDisplacerMeta;   // x = active displacer count (0 = feature entirely inert)
} ubo;

layout(push_constant) uniform PushConstants {
    vec3  chunkBaseOffset;   // chunk world origin
    float bladeHeight;
    float windStrength;      // master wind amplitude (multiplies the shared field)
    float radius;            // grass drawn only within this distance of the camera
    float fadeRange;         // world units of height fade-out before the radius edge
    float growDuration;      // seconds for the sprout-in ramp
    uint  bladesPerVoxel;
    // Shared wind field (WindSystem writes grass + foliage identically each frame).
    float windDirX;
    float windDirZ;
    float windBase;          // steady bend strength
    float gustAmp;           // gust amplitude on top of base
    float gustScale;         // gust spatial frequency (1/world units)
    float gustSpeed;         // gust front travel speed (world units/s)
    uint  bladeStyle;        // 0 = smooth tapered ribbon, 1 = boxy rectangle (default)
    // Camera-relative rendering: chunkBaseOffset above is (world - camera); these carry the
    // exact ABSOLUTE chunk origin for the hash/clump/wind-phase seeds (must never be relative
    // or blades re-roll as the camera moves).
    float absBaseX;
    float absBaseY;
    float absBaseZ;
    float pushStrength;      // displacer bend amplitude (0 disables interaction response)
} pc;

layout(location = 0) out flat uint vTex;   // grass texture index
layout(location = 1) out vec2  vUV;        // colour-sample UV into the grass tile
layout(location = 2) out float vGrad;      // 0 at blade base .. 1 at tip (silhouette + AO)
layout(location = 3) out float vSide;      // -1..1 across blade width (silhouette taper)
layout(location = 4) out float vSky;       // baked skylight 0..1
layout(location = 5) out vec3  vBlock;     // baked block light 0..1/channel

// Cheap hash -> [0,1)
float hash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

// Smooth bilinear value noise in [0,1] — the MEADOW field. Sampled at a large spatial
// period so grass height/density drift over tens of voxels while staying locally uniform
// (user-set look: "height varies over large distance, short distance is uniform & dense").
float vnoise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    // Decode voxel local position + baked light.
    uint packed = inPacked;
    float lx = float(packed & 0x1Fu);
    float ly = float((packed >> 5) & 0x1Fu);
    float lz = float((packed >> 10) & 0x1Fu);
    vSky      = float((packed >> 15) & 0xFu) / 15.0;
    vBlock    = vec3(float((packed >> 19) & 0xFu),
                     float((packed >> 23) & 0xFu),
                     float((packed >> 27) & 0xFu)) / 15.0;
    vTex = inTex & 0xFFFFu;

    // Min corner of the voxel's top face, CAMERA-RELATIVE (all position math below).
    vec3 cellBase = pc.chunkBaseOffset + vec3(lx, ly + 1.0, lz);
    // Hash-domain coordinates: ABSOLUTE world cell (exact integers via pc.absBase*), WRAPPED
    // to a 2048-unit period before any hashing/phase math. Raw far coords break float
    // precision inside hash21 (fract(400000 * 127.1) has no fractional bits), and RELATIVE
    // coords would re-roll every blade as the camera moves. absBase + local is integer-exact,
    // so mod() is exact; the 2km repeat is imperceptible.
    vec3 cellHash = mod(vec3(pc.absBaseX, pc.absBaseY, pc.absBaseZ) + vec3(lx, ly + 1.0, lz), 2048.0);

    // Segmented blades (Phase 3-lite): each blade is SEGMENTS stacked quads, so the v^bendExp
    // displacement profile below renders as an actual CURVE — blades bow under wind and around
    // characters instead of shearing as one rigid rectangle (single-quad blades read stiff).
    const int SEGMENTS = 3;
    const int VERTS_PER_BLADE = SEGMENTS * 6;   // must match vertsPerBlade in GrassRenderPipeline.cpp
    int blade  = gl_VertexIndex / VERTS_PER_BLADE;
    int rem    = gl_VertexIndex - blade * VERTS_PER_BLADE;
    int segIdx = rem / 6;
    int corner = rem - segIdx * 6;

    // Group blades into a few tight TUFTS per voxel (clumps) rather than scattering them evenly —
    // even spacing reads as isolated spikes; clustered blades read as grass. Each clump has a hashed
    // center in the cell; blades jitter within a small radius of it.
    const int BLADES_PER_CLUMP = 7;
    int clumpId      = blade / BLADES_PER_CLUMP;
    int bladeInClump = blade - clumpId * BLADES_PER_CLUMP;

    // MEADOW field: one smooth low-frequency noise (wavelength ~26 voxels, 2 octaves) drives
    // BOTH blade height and coverage, so tall lush zones and shorter sparser zones drift over
    // large distances while any few-meter neighborhood stays uniform and dense. (The old
    // per-5-voxel/per-2-voxel hash patches made short-scale holes + per-blade height chaos —
    // the exact opposite of the wanted look.)
    float meadow = vnoise2(cellHash.xz * (1.0 / 26.0)) * 0.72
                 + vnoise2(cellHash.xz * (1.0 / 9.0) + 41.7) * 0.28;

    // Coverage: dense everywhere — the meadow field only thins the shortest zones slightly
    // (~25% fewer clumps at the low end), never bald patches at tuft scale.
    float coverage  = 0.78 + 0.30 * meadow;
    int   numClumps = (int(pc.bladesPerVoxel) + BLADES_PER_CLUMP - 1) / BLADES_PER_CLUMP;
    float clumpFrac = (float(clumpId) + 0.5) / float(max(numClumps, 1));
    float keep      = step(0.25 + 0.55 * clumpFrac, coverage);

    // Clump center spans the FULL [0,1]^2 top face. A center margin here (early versions used
    // 0.18..0.82) starves every voxel edge of roots — each cube grows an isolated middle island
    // and the voxel grid shows through as bare seam lines. Edge-to-edge centers let adjacent
    // cells' tufts meet, so coverage reads as one continuous meadow.
    vec2 cseed = vec2(cellHash.x * 3.17 + cellHash.z * 7.71 + float(clumpId) * 13.1,
                      cellHash.z * 2.39 - cellHash.x * 5.11 + float(clumpId) * 7.31);
    vec2 clumpCenter = vec2(0.02 + 0.96 * hash21(cseed),
                            0.02 + 0.96 * hash21(cseed + 5.27));

    // Per-blade hash (seeded on clump + blade-in-clump), used for jitter/height/yaw/stagger.
    vec2 seed = cseed + float(bladeInClump) * 2.73;
    float h0 = hash21(seed);
    float h1 = hash21(seed + 11.7);
    float h2 = hash21(seed + 23.3);
    float h3 = hash21(seed + 41.9);

    // Blade root = clump center + small jitter (tight tuft radius). Clamp keeps roots ON this
    // voxel's top face (an overhanging root floats in mid-air at a terrain step-down edge).
    vec2 jitter = (vec2(h0, h1) - 0.5) * 0.16;
    vec2 root2  = clamp(clumpCenter + jitter, vec2(0.005), vec2(0.995));
    vec3 rootWorld = cellBase + vec3(root2.x, 0.0, root2.y);

    // Quad corners (2 tris): (u in {0,1}, v in {0,1} within THIS segment); v then maps to the
    // blade-length fraction so consecutive segments share their boundary rows seamlessly.
    vec2 quad[6] = vec2[6](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));
    vec2 q = quad[corner];
    float uCentered = q.x - 0.5;   // -0.5..0.5 across width
    float v = (float(segIdx) + q.y) / float(SEGMENTS);   // 0 base .. 1 tip along the whole blade
    bool boxy = (pc.bladeStyle == 1u);

    vGrad = v;
    // Silhouette is the ONLY style difference: boxy = crisp full rectangle (vSide 0 defeats the
    // frag taper discard); smooth = ribbon tapering to a point.
    vSide = boxy ? 0.0 : uCentered * 2.0;

    // Blade height: the smooth meadow field sets the LOCAL stand height (0.55x in short zones
    // up to 1.5x in lush zones, drifting over ~26 voxels); per-blade jitter is deliberately
    // small (±10%) so neighboring blades read as one even stand, not random spikes. Boxy
    // blades still quantize the REST height to whole 1/9-voxel microcube steps — a STATIC
    // voxel-grid trait; motion below stays smooth.
    float heightMul = mix(0.55, 1.5, smoothstep(0.08, 0.92, meadow));
    float H = pc.bladeHeight * heightMul * (0.90 + 0.20 * h2);
    if (boxy) H = max(round(H * 9.0), 2.0) / 9.0;

    // Sprout-in growth: staggered start per blade, then held at full height.
    float plant = h0 * pc.growDuration * 0.6;
    float grow  = clamp((ubo.elapsedTime - plant) / max(pc.growDuration, 0.001), 0.0, 1.0);
    H *= grow;

    // Distance fade: shrink height to 0 approaching the radius edge (blade collapses, invisible).
    float dist = length(ubo.cameraPosition - rootWorld);
    float fade = 1.0 - clamp((dist - (pc.radius - pc.fadeRange)) / max(pc.fadeRange, 0.001), 0.0, 1.0);
    H *= fade * keep;   // keep = patch-coverage gate (0 collapses the blade)

    // Interaction (VegetationWindPlan Phase 4 v1): characters push nearby blades radially
    // outward with a squared falloff, and trodden blades flatten (height squash) instead of
    // just leaning. STATELESS — a blade springs back the frame its displacer moves away.
    // Zero displacers leaves every value below bit-identical to the non-interactive path
    // (pushXZ = 0, tread = 0), preserving the wind-0 stillness invariant.
    vec2  pushXZ = vec2(0.0);
    float tread  = 0.0;
    if (ubo.grassDisplacerMeta.x > 0 && H > 0.0) {
        for (int i = 0; i < ubo.grassDisplacerMeta.x; ++i) {
            vec4  d      = ubo.grassDisplacers[i];   // xyz camera-relative feet pos, w radius
            vec2  delta  = rootWorld.xz - d.xz;
            float distXZ = length(delta);
            // Vertical gate: only blades near the displacer's feet level respond (grass on a
            // ledge 2u above/below a walking character must not move).
            float vGate = 1.0 - clamp(abs(rootWorld.y - d.y) * 0.5, 0.0, 1.0);
            float t     = clamp(1.0 - distXZ / max(d.w, 0.001), 0.0, 1.0) * vGate;
            if (t <= 0.0) continue;
            // Dead-center roots get a stable hashed direction instead of a zero-length one.
            vec2 outDir = (distXZ > 0.02) ? delta / distXZ
                                          : normalize(vec2(h0, h1) - 0.5 + vec2(0.001, 0.0));
            // Strength envelope (CPU-eased attack/release) — grass eases back up behind a
            // character instead of popping upright the frame the displacer leaves.
            float env = ubo.grassDisplacersAux[i].x;
            pushXZ += outDir * (t * t * env);
            tread   = max(tread, t * t * env);
        }
        pushXZ *= pc.pushStrength;
        tread  *= clamp(pc.pushStrength, 0.0, 1.0);
        H      *= 1.0 - 0.30 * tread;   // trodden grass flattens toward the ground
    }

    // Horizontal blade orientation (yaw), width offset across the blade. Thin blades so a dense
    // tuft reads as many strands rather than a solid blob.
    float yaw = h3 * 6.2831853;
    vec2 dir = vec2(cos(yaw), sin(yaw));
    float bladeWidth = boxy ? 0.045 : 0.042;
    vec3 widthOffset = vec3(dir.x, 0.0, dir.y) * (uCentered * bladeWidth);

    // Shared procedural wind (wind.glsl): the blade bends downwind by the local gust-field
    // strength — gust fronts travel across the whole field coherently instead of each blade
    // running its own sine. Hash-domain coords (precision footgun, see cellHash above).
    // TUNING (anti-jitter): the stiffness lag and flutter are deliberately SMALL — a wide lag
    // spread or fast random-phase flutter desynchronizes neighboring blades back into the
    // "randomish" shimmer this system exists to kill.
    vec2 wd = vec2(pc.windDirX, pc.windDirZ);
    float stiffness = h2;
    float response  = mix(1.2, 0.8, stiffness);   // soft blades respond a touch more
    float lag       = stiffness * 0.06;           // tight spread — wide lag desyncs neighbors into shimmer
    // Trodden blades are pinned underfoot — they stop waving instead of thrashing while flat.
    float windDamp = 1.0 - 0.6 * tread;
    // INERTIA: grass doesn't snap to the instantaneous gust — low-pass the field with three
    // time-lagged taps (~0.5 s box filter) so fronts arrive as smooth swells, not jitter.
    // Travelling-front realism survives (the taps scroll with the same field); high-frequency
    // content is what gets removed.
    vec2  gp = cellHash.xz + root2;
    float tg = ubo.elapsedTime - lag;
    float gust = (windGustAt(gp, tg,        wd, pc.gustScale, pc.gustSpeed)
                + windGustAt(gp, tg - 0.25, wd, pc.gustScale, pc.gustSpeed)
                + windGustAt(gp, tg - 0.50, wd, pc.gustScale, pc.gustSpeed)) * (1.0 / 3.0);
    float bend = (pc.windBase + pc.gustAmp * gust) * response * pc.windStrength * windDamp;

    // Gentle slow flutter perpendicular to the wind, amplitude ∝ local gust strength — calm air
    // means calm grass (windBase+gustAmp are both 0 at speed 0, so everything below is exactly 0).
    float phase   = (cellHash.x + root2.x) * 2.9 + (cellHash.z + root2.y) * 2.3 + h3 * 6.2831853;
    float flutter = sin(ubo.elapsedTime * 1.9 + phase) * 0.055
                  * (pc.gustAmp * gust + 0.15 * pc.windBase) * pc.windStrength * windDamp;
    // Bend profile: base stays planted, tip displaces most. bendExp is the per-blade FLEX —
    // soft blades (low h2) yield along their whole length, stiff blades hold their base and
    // give mostly at the tip. With SEGMENTS rows this renders as a visible arc, not a shear.
    // Displacer push composes with wind here so the tip-drop length preservation below applies
    // to BOTH: a pushed blade bows over and hugs the ground, it doesn't stretch sideways.
    float bendExp = mix(1.6, 2.4, stiffness);
    float profile = pow(v, bendExp);
    vec2 swayDir = wd * bend + vec2(-wd.y, wd.x) * flutter + pushXZ;
    float swayMag = length(swayDir);
    if (swayMag > 1.4) swayDir *= 1.4 / swayMag;   // total-bend clamp (wind + push composed)
    vec3 windOffset = vec3(swayDir.x, 0.0, swayDir.y) * (profile * H * 2.0);

    vec3 worldPos = rootWorld + widthOffset + windOffset;
    worldPos.y   += v * H;
    // Approximate length preservation: drop the tip as it displaces laterally so strong gusts
    // read as the blade BENDING over, not stretching sideways.
    float lat = length(windOffset.xz);
    worldPos.y -= 0.4 * lat * lat / max(H, 0.001);

    // Colour-sample UV: a stable per-blade point in the grass tile (subtle per-blade variation).
    // Hash-domain coords again — fract() of a raw far coord is quantized.
    vUV = fract(vec2(cellHash.x + root2.x, cellHash.z + root2.y) * 0.5 + vec2(h0, h1) * 0.3);

    gl_Position = ubo.viewProj * vec4(worldPos, 1.0);
}
