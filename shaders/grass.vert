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

    int blade  = gl_VertexIndex / 6;
    int corner = gl_VertexIndex - blade * 6;

    // Group blades into a few tight TUFTS per voxel (clumps) rather than scattering them evenly —
    // even spacing reads as isolated spikes; clustered blades read as grass. Each clump has a hashed
    // center in the cell; blades jitter within a small radius of it.
    const int BLADES_PER_CLUMP = 7;
    int clumpId      = blade / BLADES_PER_CLUMP;
    int bladeInClump = blade - clumpId * BLADES_PER_CLUMP;

    // Patchy coverage: grass grows in irregular MULTI-VOXEL patches, not a uniform
    // per-voxel carpet. A large-scale patch field (5-voxel cells) + finer breakup give
    // each clump a coverage threshold: patch cores keep all their clumps (dense tufts),
    // patch edges thin out, and the gaps between patches stay bare.
    float patchBig  = hash21(floor(cellHash.xz / 5.0) * 1.31 + 17.7);
    float patchFine = hash21(floor(cellHash.xz / 2.0) * 2.17 + 5.9);
    float coverage  = patchBig * 0.72 + patchFine * 0.28;
    int   numClumps = (int(pc.bladesPerVoxel) + BLADES_PER_CLUMP - 1) / BLADES_PER_CLUMP;
    float clumpFrac = (float(clumpId) + 0.5) / float(max(numClumps, 1));
    float keep      = step(0.25 + 0.55 * clumpFrac, coverage);

    // Clump center within the [0,1]^2 top face (margin off the edges).
    vec2 cseed = vec2(cellHash.x * 3.17 + cellHash.z * 7.71 + float(clumpId) * 13.1,
                      cellHash.z * 2.39 - cellHash.x * 5.11 + float(clumpId) * 7.31);
    vec2 clumpCenter = vec2(0.18 + 0.64 * hash21(cseed),
                            0.18 + 0.64 * hash21(cseed + 5.27));

    // Per-blade hash (seeded on clump + blade-in-clump), used for jitter/height/yaw/stagger.
    vec2 seed = cseed + float(bladeInClump) * 2.73;
    float h0 = hash21(seed);
    float h1 = hash21(seed + 11.7);
    float h2 = hash21(seed + 23.3);
    float h3 = hash21(seed + 41.9);

    // Blade root = clump center + small jitter (tight tuft radius).
    vec2 jitter = (vec2(h0, h1) - 0.5) * 0.13;
    vec2 root2  = clamp(clumpCenter + jitter, vec2(0.04), vec2(0.96));
    vec3 rootWorld = cellBase + vec3(root2.x, 0.0, root2.y);

    // Quad corners (2 tris): (u in {0,1}, v in {0,1}).
    vec2 quad[6] = vec2[6](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));
    vec2 q = quad[corner];
    float uCentered = q.x - 0.5;   // -0.5..0.5 across width
    float v = q.y;                 // 0 base .. 1 tip
    bool boxy = (pc.bladeStyle == 1u);

    vGrad = v;
    // Silhouette is the ONLY style difference: boxy = crisp full rectangle (vSide 0 defeats the
    // frag taper discard); smooth = ribbon tapering to a point.
    vSide = boxy ? 0.0 : uCentered * 2.0;

    // Blade height with per-blade variance. Boxy blades quantize the REST height to whole
    // 1/9-voxel microcube steps — a STATIC voxel-grid trait; motion below stays smooth.
    float H = pc.bladeHeight * (0.65 + 0.6 * h2);
    if (boxy) H = max(round(H * 9.0), 2.0) / 9.0;

    // Sprout-in growth: staggered start per blade, then held at full height.
    float plant = h0 * pc.growDuration * 0.6;
    float grow  = clamp((ubo.elapsedTime - plant) / max(pc.growDuration, 0.001), 0.0, 1.0);
    H *= grow;

    // Distance fade: shrink height to 0 approaching the radius edge (blade collapses, invisible).
    float dist = length(ubo.cameraPosition - rootWorld);
    float fade = 1.0 - clamp((dist - (pc.radius - pc.fadeRange)) / max(pc.fadeRange, 0.001), 0.0, 1.0);
    H *= fade * keep;   // keep = patch-coverage gate (0 collapses the blade)

    // Horizontal blade orientation (yaw), width offset across the blade. Thin blades so a dense
    // tuft reads as many strands rather than a solid blob.
    float yaw = h3 * 6.2831853;
    vec2 dir = vec2(cos(yaw), sin(yaw));
    float bladeWidth = boxy ? 0.06 : 0.055;
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
    float lag       = stiffness * 0.12;           // stiff blades trail the front slightly
    float gust = windGustAt(cellHash.xz + root2, ubo.elapsedTime - lag, wd, pc.gustScale, pc.gustSpeed);
    float bend = (pc.windBase + pc.gustAmp * gust) * response * pc.windStrength;

    // Gentle slow flutter perpendicular to the wind, amplitude ∝ local gust strength — calm air
    // means calm grass (windBase+gustAmp are both 0 at speed 0, so everything below is exactly 0).
    float phase   = (cellHash.x + root2.x) * 2.9 + (cellHash.z + root2.y) * 2.3 + h3 * 6.2831853;
    float flutter = sin(ubo.elapsedTime * 2.7 + phase) * 0.10
                  * (pc.gustAmp * gust + 0.15 * pc.windBase) * pc.windStrength;
    // v*v → base stays planted, tip displaces most; scaled by blade height so short blades bend
    // proportionally, not wildly. Identical for both silhouettes — motion is ALWAYS smooth.
    vec2 swayDir = wd * bend + vec2(-wd.y, wd.x) * flutter;
    vec3 windOffset = vec3(swayDir.x, 0.0, swayDir.y) * (v * v * H * 2.0);

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
