#version 450
#extension GL_GOOGLE_include_directive : require
#include "wind.glsl"

// Leaf foliage cards. ONE instance per exposed billboarded-leaf subcube (FoliageInstanceData); this
// shader fans it into `cardsPerVoxel` leaf cards, each a quad in a HASHED 3D orientation (crossed
// cards filling the subcube volume → volumetric foliage from every angle). Rounded silhouette is
// carved in foliage.frag. Wind = shared procedural gust field (wind.glsl): the sprig pivots about
// its base coherently, plus per-card basis flutter. Sibling of grass.vert. ANY motion change here
// must be mirrored in foliage_shadow.vert (shadows must track the rendered cards exactly).

layout(location = 0) in uint inPacked;  // 0-4 x |5-9 y |10-14 z (cube local) |15-16 sx |17-18 sy |19-20 sz |21-24 sky
layout(location = 1) in uint inTex;     // low16 = leaf texture index |16-19 R |20-23 G |24-27 B (block light)

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
    float cardSize;          // leaf card half-extent
    float windStrength;      // master wind amplitude (multiplies the shared field)
    float radius;            // safety cull cap (world units)
    uint  cardsPerVoxel;
    // Shared wind field (WindSystem writes grass + foliage identically each frame).
    float windDirX;
    float windDirZ;
    float windBase;          // steady bend strength
    float gustAmp;           // gust amplitude on top of base
    float gustScale;         // gust spatial frequency (1/world units)
    float windScrollX;   // CPU-integrated gust-field offset (see WindSystem::State::scroll)
    float windScrollZ;         // gust front travel speed (world units/s)
    // Camera-relative rendering: chunkBaseOffset above is (world - camera); exact ABSOLUTE
    // chunk origin for hash/phase seeds (never relative, or cards re-roll as the camera moves).
    float absBaseX;
    float absBaseY;
    float absBaseZ;
    float windAniso;   // gust-front crosswind stretch; must match grass (one wind field)
} pc;

layout(location = 0) out flat uint vTex;    // leaf texture index
layout(location = 1) out vec2  vCard;       // card-plane coords in [-1,1] (mask UV)
layout(location = 4) out float vShade;      // per-card shading (hashed, for leaf-to-leaf variation)
layout(location = 5) out flat uint vMaskV;  // per-card mask variant (bit0 flipX, bit1 flipY, bit2 swap)
layout(location = 6) out vec4  vShadowCoord; // biased light-space coord (shadow RECEIVING)
layout(location = 7) out vec3  vWorldPos;    // for view-dependent backlit transmission
layout(location = 8) out float vFade;        // radius-edge dither fade (1 = solid)

float hash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

void main() {
    uint packed = inPacked;
    float lx = float(packed & 0x1Fu);
    float ly = float((packed >> 5) & 0x1Fu);
    float lz = float((packed >> 10) & 0x1Fu);
    float sx = float((packed >> 15) & 0x3u);
    float sy = float((packed >> 17) & 0x3u);
    float sz = float((packed >> 19) & 0x3u);
    vTex = inTex & 0xFFFFu;

    // Subcube centre in world space: cube-local + (sub + 0.5)/3 (subcube = 1/3 cube).
    vec3 subCenter = pc.chunkBaseOffset + vec3(lx, ly, lz) + (vec3(sx, sy, sz) + 0.5) / 3.0;
    // Hash-domain coords: wrap to a 2048-unit period before hashing/phase math. Raw
    // far-from-origin coords lose all fractional precision inside hash21 (cards then
    // share one orientation → the whole canopy turns coplanar and vanishes edge-on).
    vec3 scHash = mod(vec3(pc.absBaseX, pc.absBaseY, pc.absBaseZ)
                      + vec3(lx, ly, lz) + (vec3(sx, sy, sz) + 0.5) / 3.0, 2048.0);

    int card   = gl_VertexIndex / 6;
    int corner = gl_VertexIndex - card * 6;

    // Per-card hash (seeded on subcube world cell + card index → stable, seamless across chunks).
    vec2 seed = vec2(scHash.x * 6.13 + scHash.z * 11.7 + float(card) * 3.71,
                     scHash.z * 4.19 - scHash.y * 7.53 + float(card) * 5.31);
    float h0 = hash21(seed);
    float h1 = hash21(seed + 13.1);
    float h2 = hash21(seed + 27.7);
    float h3 = hash21(seed + 41.3);
    vShade = 0.82 + 0.18 * h3;  // subtle per-card brightness variation
    vMaskV = uint(hash21(seed + 57.9) * 7.999);  // 8 mask orientations per texture (flip/swap)

    // Hashed 3D card orientation → orthonormal basis (right, up) spanning the card plane.
    float az = h0 * 6.2831853;
    float el = h1 * 3.1415926 - 1.5707963;
    vec3 nrm = vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
    vec3 up0 = (abs(nrm.y) < 0.9) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up0, nrm));
    vec3 up    = cross(nrm, right);

    // Card centre: jittered within the subcube volume so the cards form a small clustered sprig.
    vec3 jitter = (vec3(h0, h1, h2) - 0.5) * (2.0 / 3.0) * 0.55;
    vec3 center = subCenter + jitter;

    // Shared procedural wind (wind.glsl). The MAIN bend samples the gust field at the SPRIG
    // (subcube) position with no per-card term, so every card of a sprig moves together —
    // sprig-to-sprig variation comes from the travelling gust field, not desynchronized noise
    // (the Crysis main/detail split; the old `+ float(card)` phase read as jitter).
    vec2 wd = vec2(pc.windDirX, pc.windDirZ);
    float gust = windGustAt(scHash.xz, vec2(pc.windScrollX, pc.windScrollZ), wd, pc.gustScale, pc.windAniso);
    float bend = (pc.windBase + pc.gustAmp * gust) * pc.windStrength;

    // DETAIL flutter: oscillate the card basis a few degrees around nrm — leaves glint in
    // gusts, stay still in calm air (every factor is 0 at wind speed 0). Angle is scaled by a
    // normalized master amplitude (windStrength/0.05 default → 1) so the wind toggle kills it.
    // Kept SLOW and small — fast random-phase card rotation reads as canopy jitter/shimmer.
    float fphase = scHash.x * 1.7 + scHash.z * 1.3 + float(card) * 2.39;
    float fang   = sin(ubo.elapsedTime * 2.1 + fphase) * 0.05
                 * (pc.gustAmp * gust + 0.2 * pc.windBase) * clamp(pc.windStrength * 20.0, 0.0, 1.0);
    float ca = cos(fang), sa = sin(fang);
    vec3 rightW = right * ca + up * sa;
    vec3 upW    = up * ca - right * sa;

    // Quad corners in card plane, [-1,1]^2.
    vec2 quad[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
    vec2 q = quad[corner];
    vCard = q;

    // Radius edge (2026-08-06, LodTierLedger no-pop rule): cards DISSOLVE via Bayer dither
    // in the frag over the last 10% of the radius instead of popping out whole-chunk at the
    // cutoff. Geometry never scales; the collapse below survives only as the beyond-radius
    // safety cull (zero-area = no fragments at all).
    float dist = length(ubo.cameraPosition - center);
    vFade = 1.0 - smoothstep(pc.radius * 0.9, pc.radius, dist);
    float scale = pc.cardSize * (0.8 + 0.4 * h2) * (dist > pc.radius ? 0.0 : 1.0);

    vec3 worldPos = center + (q.x * rightW + q.y * upW) * scale;
    // Pivot about the sprig base: displacement ∝ height within the sprig, so the base stays
    // anchored and tips sway — replaces the old rigid whole-card XZ drift that made canopies
    // look like they were floating rather than being pushed.
    float hFrac = clamp((worldPos.y - (subCenter.y - 0.55)) * 0.9, 0.0, 1.0);
    worldPos.xz += wd * (bend * hFrac);
    vWorldPos    = worldPos;
    vShadowCoord = ubo.biasedLightSpace * vec4(worldPos, 1.0);
    gl_Position  = ubo.viewProj * vec4(worldPos, 1.0);
}
