#version 450

// Lightweight grass blades. ONE instance per grass-topped voxel (GrassInstanceData); this shader
// procedurally fans it into `bladesPerVoxel` blades using gl_VertexIndex (6 verts / blade, no
// vertex buffer). Wind sway + sprout-in growth use ubo.elapsedTime; distance fade uses
// ubo.cameraPosition. See GrassRenderPipeline / the grass plan.

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
} ubo;

layout(push_constant) uniform PushConstants {
    vec3  chunkBaseOffset;   // chunk world origin
    float bladeHeight;
    float windStrength;
    float radius;            // grass drawn only within this distance of the camera
    float fadeRange;         // world units of height fade-out before the radius edge
    float growDuration;      // seconds for the sprout-in ramp
    uint  bladesPerVoxel;
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

    // Min corner of the voxel's top face in world space (voxel occupies [p, p+1]; top at y+1).
    vec3 cellBase = pc.chunkBaseOffset + vec3(lx, ly + 1.0, lz);

    int blade  = gl_VertexIndex / 6;
    int corner = gl_VertexIndex - blade * 6;

    // Group blades into a few tight TUFTS per voxel (clumps) rather than scattering them evenly —
    // even spacing reads as isolated spikes; clustered blades read as grass. Each clump has a hashed
    // center in the cell; blades jitter within a small radius of it.
    const int BLADES_PER_CLUMP = 7;
    int clumpId      = blade / BLADES_PER_CLUMP;
    int bladeInClump = blade - clumpId * BLADES_PER_CLUMP;

    // Clump center within the [0,1]^2 top face (margin off the edges).
    vec2 cseed = vec2(cellBase.x * 3.17 + cellBase.z * 7.71 + float(clumpId) * 13.1,
                      cellBase.z * 2.39 - cellBase.x * 5.11 + float(clumpId) * 7.31);
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

    vSide = uCentered * 2.0;       // -1..1 for the frag silhouette taper
    vGrad = v;

    // Blade dimensions with per-blade variance. Thin blades so a dense tuft reads as many strands
    // rather than a solid blob.
    float bladeWidth  = 0.055;
    float H = pc.bladeHeight * (0.65 + 0.6 * h2);

    // Sprout-in growth: staggered start per blade, then held at full height.
    float plant = h0 * pc.growDuration * 0.6;
    float grow  = clamp((ubo.elapsedTime - plant) / max(pc.growDuration, 0.001), 0.0, 1.0);
    H *= grow;

    // Distance fade: shrink height to 0 approaching the radius edge (blade collapses, invisible).
    float dist = length(ubo.cameraPosition - rootWorld);
    float fade = 1.0 - clamp((dist - (pc.radius - pc.fadeRange)) / max(pc.fadeRange, 0.001), 0.0, 1.0);
    H *= fade;

    // Horizontal blade orientation (yaw), width offset across the blade.
    float yaw = h3 * 6.2831853;
    vec2 dir = vec2(cos(yaw), sin(yaw));
    vec3 widthOffset = vec3(dir.x, 0.0, dir.y) * (uCentered * bladeWidth);

    // Coherent wind: whole field bends along a shared direction, phase varying with world position
    // so it ripples. v*v → base stays planted, tip sways most.
    vec2 windDir = normalize(vec2(0.85, 0.35));
    float phase = rootWorld.x * 0.6 + rootWorld.z * 0.55;
    float sway  = sin(ubo.elapsedTime * 1.7 + phase) * pc.windStrength;
    // Scale sway by blade height so short blades bend proportionally, not wildly.
    vec3 windOffset = vec3(windDir.x, 0.0, windDir.y) * (sway * v * v * H * 2.0);

    vec3 worldPos = rootWorld + widthOffset + windOffset;
    worldPos.y   += v * H;

    // Colour-sample UV: a stable per-blade point in the grass tile (subtle per-blade variation).
    vUV = fract(vec2(rootWorld.x, rootWorld.z) * 0.5 + vec2(h0, h1) * 0.3);

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
