#version 450

// KINEMATIC foliage cards (F3, docs/DestructionSystemV2.md): leaf sprigs riding a MOVING
// coherent fragment (a felled tree). Same card-fan approach as foliage.vert, with three
// deliberate differences:
//   1. Position: instance coords are FRAGMENT-LOCAL (packed vs the fragment's foliage
//      origin); the world position comes from pc.model (the rigid body transform each
//      frame), not a static chunk origin.
//   2. Hash seeds use the LOCAL coords — they are constant while the fragment moves, so
//      card orientations stay stable during the fall (world-seeded hashes would re-roll
//      every frame and the canopy would shimmer/churn mid-air).
//   3. No wind (a falling/settled tree's canopy doesn't need the standing sway; keeps the
//      push-constant block within limits) and no shadow variant yet (v1, disclosed).

layout(location = 0) in uint inPacked;  // 0-4 x |5-9 y |10-14 z (fragment-local cell) |15-16 sx |17-18 sy |19-20 sz |21-24 sky
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
    mat4  model;         // fragment world transform * translate(foliageOrigin)
    float cardSize;      // leaf card half-extent
    float radius;        // safety cull cap (world units)
    uint  cardsPerVoxel;
} pc;

layout(location = 0) out flat uint vTex;
layout(location = 1) out vec2  vCard;
layout(location = 4) out float vShade;
layout(location = 5) out flat uint vMaskV;
layout(location = 6) out vec4  vShadowCoord;
layout(location = 7) out vec3  vWorldPos;

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

    // Subcube centre in FRAGMENT-LOCAL space; world via the rigid transform.
    vec3 subLocal  = vec3(lx, ly, lz) + (vec3(sx, sy, sz) + 0.5) / 3.0;
    vec3 subCenter = (pc.model * vec4(subLocal, 1.0)).xyz;

    int card   = gl_VertexIndex / 6;
    int corner = gl_VertexIndex - card * 6;

    // Per-card hash — seeded on LOCAL coords (stable while the fragment moves).
    vec2 seed = vec2(subLocal.x * 6.13 + subLocal.z * 11.7 + float(card) * 3.71,
                     subLocal.z * 4.19 - subLocal.y * 7.53 + float(card) * 5.31);
    float h0 = hash21(seed);
    float h1 = hash21(seed + 13.1);
    float h2 = hash21(seed + 27.7);
    float h3 = hash21(seed + 41.3);
    vShade = 0.82 + 0.18 * h3;
    vMaskV = uint(hash21(seed + 57.9) * 7.999);

    // Hashed 3D card orientation (world-axis basis; orientation is hash-random anyway, so
    // not rotating the basis with the body is visually indistinguishable and cheaper).
    float az = h0 * 6.2831853;
    float el = h1 * 3.1415926 - 1.5707963;
    vec3 nrm = vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
    vec3 up0 = (abs(nrm.y) < 0.9) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up0, nrm));
    vec3 up    = cross(nrm, right);

    // Card centre: jittered within the subcube volume (jitter in local, transformed scale ~1).
    vec3 jitterL = (vec3(h0, h1, h2) - 0.5) * (2.0 / 3.0) * 0.55;
    vec3 center  = (pc.model * vec4(subLocal + jitterL, 1.0)).xyz;

    vec2 quad[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
    vec2 q = quad[corner];
    vCard = q;

    float dist = length(ubo.cameraPosition - center);
    float scale = pc.cardSize * (0.8 + 0.4 * h2) * (dist > pc.radius ? 0.0 : 1.0);

    vec3 worldPos = center + (q.x * right + q.y * up) * scale;
    vWorldPos    = worldPos;
    vShadowCoord = ubo.biasedLightSpace * vec4(worldPos, 1.0);
    gl_Position  = ubo.viewProj * vec4(worldPos, 1.0);
}
