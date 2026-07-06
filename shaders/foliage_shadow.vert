#version 450

// Foliage leaf-card SHADOW caster. Same card fan/orientation/wind math as foliage.vert (cards
// must shadow exactly where they render), but projected by the light-space matrix into the
// shadow map. Pairs with foliage_shadow.frag which alpha-tests the leaf-forge cutout mask, so
// canopies cast DAPPLED shadows — before this pass, leaf voxels cast no shadows at all (the
// mesher skips their solid faces; trunks alone shadowed the ground).

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
    float windStrength;
    float radius;            // safety cull cap (world units)
    uint  cardsPerVoxel;
    uint  _pad;
} pc;

layout(location = 0) out flat uint vTex;    // leaf texture index
layout(location = 1) out vec2  vCard;       // card-plane coords in [-1,1] (mask UV)
layout(location = 2) out flat uint vMaskV;  // per-card mask variant (bit0 flipX, bit1 flipY, bit2 swap)

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

    // Subcube centre in world space (identical to foliage.vert).
    vec3 subCenter = pc.chunkBaseOffset + vec3(lx, ly, lz) + (vec3(sx, sy, sz) + 0.5) / 3.0;
    vec3 scHash = mod(subCenter, 2048.0);   // hash-domain wrap (precision far from origin)

    int card   = gl_VertexIndex / 6;
    int corner = gl_VertexIndex - card * 6;

    vec2 seed = vec2(scHash.x * 6.13 + scHash.z * 11.7 + float(card) * 3.71,
                     scHash.z * 4.19 - scHash.y * 7.53 + float(card) * 5.31);
    float h0 = hash21(seed);
    float h1 = hash21(seed + 13.1);
    float h2 = hash21(seed + 27.7);
    vMaskV = uint(hash21(seed + 57.9) * 7.999);

    float az = h0 * 6.2831853;
    float el = h1 * 3.1415926 - 1.5707963;
    vec3 nrm = vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
    vec3 up0 = (abs(nrm.y) < 0.9) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up0, nrm));
    vec3 up    = cross(nrm, right);

    vec3 jitter = (vec3(h0, h1, h2) - 0.5) * (2.0 / 3.0) * 0.55;
    vec3 center = subCenter + jitter;

    vec2 windDir = normalize(vec2(0.8, 0.35));
    float phase = scHash.x * 0.5 + scHash.z * 0.45 + float(card);
    float sway  = sin(ubo.elapsedTime * 1.3 + phase) * pc.windStrength;
    center.xz  += windDir * sway;

    vec2 quad[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
    vec2 q = quad[corner];
    vCard = q;

    // Same safety cull as the visible pass so shadows match rendered cards.
    float dist = length(ubo.cameraPosition - center);
    float scale = pc.cardSize * (0.8 + 0.4 * h2) * (dist > pc.radius ? 0.0 : 1.0);

    vec3 worldPos = center + (q.x * right + q.y * up) * scale;
    gl_Position = ubo.lightSpaceMatrix * vec4(worldPos, 1.0);
}
