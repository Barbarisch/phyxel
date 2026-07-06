#version 450

// Leaf foliage cards. ONE instance per exposed billboarded-leaf subcube (FoliageInstanceData); this
// shader fans it into `cardsPerVoxel` leaf cards, each a quad in a HASHED 3D orientation (crossed
// cards filling the subcube volume → volumetric foliage from every angle). Rounded silhouette is
// carved in foliage.frag. Gentle wind from ubo.elapsedTime. Sibling of grass.vert.

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
layout(location = 2) out float vSky;        // baked skylight 0..1
layout(location = 3) out vec3  vBlock;      // baked block light 0..1/channel
layout(location = 4) out float vShade;      // per-card shading (hashed, for leaf-to-leaf variation)
layout(location = 5) out flat uint vMaskV;  // per-card mask variant (bit0 flipX, bit1 flipY, bit2 swap)

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
    vSky   = float((packed >> 21) & 0xFu) / 15.0;
    vBlock = vec3(float((inTex >> 16) & 0xFu),
                  float((inTex >> 20) & 0xFu),
                  float((inTex >> 24) & 0xFu)) / 15.0;
    vTex = inTex & 0xFFFFu;

    // Subcube centre in world space: cube-local + (sub + 0.5)/3 (subcube = 1/3 cube).
    vec3 subCenter = pc.chunkBaseOffset + vec3(lx, ly, lz) + (vec3(sx, sy, sz) + 0.5) / 3.0;
    // Hash-domain coords: wrap to a 2048-unit period before hashing/phase math. Raw
    // far-from-origin coords lose all fractional precision inside hash21 (cards then
    // share one orientation → the whole canopy turns coplanar and vanishes edge-on).
    vec3 scHash = mod(subCenter, 2048.0);

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

    // Gentle coherent wind: whole card drifts, tips more; phase varies with world position
    // (hash-domain coords — sin() of a raw far coord is float garbage).
    vec2 windDir = normalize(vec2(0.8, 0.35));
    float phase = scHash.x * 0.5 + scHash.z * 0.45 + float(card);
    float sway  = sin(ubo.elapsedTime * 1.3 + phase) * pc.windStrength;
    center.xz  += windDir * sway;

    // Quad corners in card plane, [-1,1]^2.
    vec2 quad[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
    vec2 q = quad[corner];
    vCard = q;

    // Safety distance cull: collapse the card to a point beyond radius (keeps far trees cheap if a
    // small radius is ever set; default radius is large so trees keep leaves).
    float dist = length(ubo.cameraPosition - center);
    float scale = pc.cardSize * (0.8 + 0.4 * h2) * (dist > pc.radius ? 0.0 : 1.0);

    vec3 worldPos = center + (q.x * right + q.y * up) * scale;
    gl_Position = ubo.viewProj * vec4(worldPos, 1.0);
}
