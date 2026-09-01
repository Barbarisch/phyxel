#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // shared ambient / shadow / aerial-perspective model

layout(location = 0) in flat uint textureIndex;  // from vertex shader
layout(location = 1) in vec2 texCoord;           // from vertex shader
layout(location = 2) in vec4 shadowCoord;        // from vertex shader
layout(location = 3) in flat uint flags;         // from vertex shader
layout(location = 4) in vec3 inNormal;           // from vertex shader
layout(location = 5) in vec3 inWorldPos;         // from vertex shader
layout(location = 6) in float vSkyLight;          // baked skylight 0..1 — SMOOTH (interpolated per-corner)
layout(location = 7) in vec3  vBlockColor;        // baked coloured block light 0..1/channel — SMOOTH (interpolated per-corner)
layout(location = 8) in vec3  vTint;              // per-voxel tint multiplier (1,1,1 = none). Decouples color from material.
layout(location = 9) in flat uint vState;         // per-voxel state: 0 normal,1 flaming,2 smoldering,3 charred,4 wet
layout(location = 10) in flat vec3 vChunkBaseAbs; // exact absolute chunk origin (varied-hash seed)
layout(location = 11) in flat vec3 vChunkBaseRel; // camera-relative chunk origin (recovers local pos)

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
    mat4 viewProj;
    mat4 biasedLightSpace;
    vec3 cameraWorld;
    int  debugShadowMode;   // 1 = shadow-only debug view (see lighting.glsl phxShadowOnly)
    float shadowDepthRange; // world-unit depth span of the light volume (bias normalization)
    vec4  grassDisplacers[16];      // declared only to reach the cascade fields below
    vec4  grassDisplacersAux[16];
    ivec4 grassDisplacerMeta;
    // Near shadow cascade (docs/NearShadowCascade.md): tight map whose 0.0195 u texel
    // resolves blade-scale casters. Receivers take min(near, mid) — the near map's 12%
    // border fade IS the cascade blend, and mid-only casters can never vanish up close.
    mat4 biasedLightSpaceNear;
    vec4 shadowCascadeNear;   // x = range end (0 = off), y = near depthRange, z = blend halfwidth
    mat4 lightSpaceMatrixNear;      // (prefix padding to reach the atmosphere fields below)
    mat4 biasedLightSpaceFar;
    vec4 shadowCascadeFar;
    mat4 lightSpaceMatrixFar;
    // ---- Atmosphere-derived lighting + exposure (2026-08-10) --------------------------------
    // The sky is a physical scattering model now, so these come from the SAME transmittance as the
    // sun's disc and colour. exposure converts radiance to something a display can show at all.
    vec3 ambientColor;
    vec3 hazeHorizonColor;
    vec3 hazeZenithColor;
    vec3 moonDirection;
    vec3 moonColor;
    float exposure;
    int   tonemapCurve;
    vec4  skyBodyDirRadius[4];   // (prefix padding to reach occupancyBox below)
    vec4  skyBodyDisc[4];
    vec4  skyBodyLitDir[4];
    vec4  skyBodyLight[4];
    int   skyBodyCount;
    ivec4 occupancyBox;   // xyz = box min corner in CHUNK coords, w = 1 when 11/12 are real
} ubo;

#include "occupancy.glsl"   // U2: the shared occupancy query + light-visibility DDA

// M3 sky visibility stays HERE rather than in occupancy.glsl: it is default-OFF pending
// M3-REDESIGN (measured at 24.6 ms/frame on a generated town), and only voxel.frag consumes
// it. Moving it into the shared include would invite the other consumers to adopt a term that
// is known to be unaffordable in this form.
/// M3 — how much of the sky hemisphere this surface can see, cosine-weighted, 0..1.
/// This REPLACES the deleted per-cell skylight flood. The flood decayed 1 per cube cell from the
/// nearest opening, so a room read 47% of full daylight eight cells from a single doorway and a
/// sealed room was merely dim. Here the falloff comes out of the geometry: a sealed room sees no
/// sky and is black, an opening admits exactly the directions that clear it.
///
/// Fixed direction set, shared with the CPU mirror — a jittered set would make captures noisy and
/// incomparable, which is precisely what the M3 gates must do.
///
/// ⚠️ REACH is what decides whether a room reads as sealed: a ray that runs out of budget inside a
/// closed room hits nothing and counts as sky. Measured — at 10 u a diagonal ray inside a 9x7x9
/// sealed room escaped and its corner read 0.077 instead of 0. Hence the two-rate march: micro
/// resolution close in (2-micro walls and ledges), coarser beyond, to buy the range a room needs.
const vec3 PHX_SKY_DIRS[9] = vec3[9](
    vec3( 0.000,  0.000, 1.000),
    vec3( 0.500,  0.000, 0.866), vec3(-0.500,  0.000, 0.866),
    vec3( 0.000,  0.500, 0.866), vec3( 0.000, -0.500, 0.866),
    vec3( 0.612,  0.612, 0.500), vec3(-0.612,  0.612, 0.500),
    vec3( 0.612, -0.612, 0.500), vec3(-0.612, -0.612, 0.500)
);

float phxSkyVisibility(vec3 surfaceWorld, vec3 geomNormal) {
    if ((ubo.occupancyBox.w & 4) == 0) return 1.0;   // sky tracing off, or occupancy absent

    vec3 Ng = normalize(geomNormal);
    vec3 up = abs(Ng.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, Ng));
    vec3 B = cross(Ng, T);

    vec3 start = surfaceWorld + Ng * (2.0 / 9.0);

    // Matched to the BAKE's measured settings (docs/UnifiedLightingPlan.md M3-REDESIGN).
    //
    // D1 measured this per-fragment path at 24.6 ms/frame and that number is what justified moving
    // sky visibility into a per-cell bake -- the very storage M0 existed to delete. But D1 ran at
    // FULL quality: 9 rays, reach 24, 512 cells. The bake then established by measurement that
    // 5 rays / reach 16 still seals a room at every wall thickness, with a doorway control alive.
    // The per-fragment path was never re-measured at those settings, so the number that retired it
    // was never its real cost.
    //
    // 5 rays keeps the vertical and the four 30-degree directions -- the ones that decide whether a
    // room is sealed -- and drops the four 60-degree diagonals, which largely duplicate them. That
    // is exactly the subset the bake uses, so PHX_SKY_DIRS[0..4] must stay in that order.
    // ⚠️ Reach still decides the largest room that can read as SEALED (D8).
    const float kReach = 16.0;
    const int   kRays  = 5;
    const int   kCells = int(kReach * 9.0) * 2;   // same cell budget the CPU mirror derives

    // GATE, the same idea that made M2's visibility term measure free: there, `dot(N, ldir) > 0`
    // and the radius test meant almost no marches actually ran. This trace had no gate at all --
    // every fragment paid all five rays every frame.
    //
    // Ray 0 is the surface normal itself, and it is the cheapest possible probe. If it escapes,
    // the surface has open sky directly above it and the remaining rays are being spent to confirm
    // a foregone conclusion: an unoccluded normal ray means the fragment is outdoors, which is the
    // overwhelming majority of fragments in any outdoor scene. Take the full weighted answer only
    // when that first ray is BLOCKED -- i.e. when the fragment might actually be enclosed, which is
    // exactly the case this whole system exists to resolve.
    //
    // This is conservative in the direction that matters: it can only ever return MORE sky for a
    // surface whose normal already sees sky. It cannot brighten an interior, because an interior
    // fragment's normal ray hits something and takes the full path.
    vec3 dir0 = normalize(T * PHX_SKY_DIRS[0].x + B * PHX_SKY_DIRS[0].y + Ng * PHX_SKY_DIRS[0].z);
    if (!phxDdaHitsSolid(start, start + dir0 * kReach, kCells, ubo.occupancyBox)) return 1.0;

    float lit = 0.0, total = 0.0;
    for (int r = 0; r < kRays; ++r) {
        vec3 d = PHX_SKY_DIRS[r];
        vec3 dir = normalize(T * d.x + B * d.y + Ng * d.z);
        float w = max(0.0, dot(dir, Ng));
        total += w;

        if (!phxDdaHitsSolid(start, start + dir * kReach, kCells, ubo.occupancyBox)) lit += w;
    }
    return total > 0.0 ? lit / total : 1.0;
}

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;     // class 0 albedo: 512px
layout(set = 0, binding = 2) uniform sampler2D shadowMap;             // mid-cascade shadow map
layout(set = 0, binding = 9) uniform sampler2D shadowMapNear;         // near cascade (fine texels)
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;   // class 1 albedo: 1024px
layout(set = 0, binding = 6) uniform sampler2DArray textureNormal;    // class 0 normal+rough: 512px
layout(set = 0, binding = 7) uniform sampler2DArray textureNormalHi;  // class 1 normal+rough: 1024px

// Point light (32 bytes, std430)
struct PointLightGPU {
    vec4 positionAndRadius;     // xyz = position, w = radius
    vec4 colorAndIntensity;     // xyz = color, w = intensity
};

// Spot light (64 bytes, std430)
struct SpotLightGPU {
    vec4 positionAndRadius;     // xyz = position, w = radius
    vec4 directionAndInnerCone; // xyz = direction, w = innerCone
    vec4 colorAndIntensity;     // xyz = color, w = intensity
    vec4 outerConeAndPadding;   // x = outerCone, yzw = padding
};

layout(std430, set = 0, binding = 3) readonly buffer LightBuffer {
    uint numPointLights;
    uint numSpotLights;
    uint _pad0;
    uint _pad1;
    PointLightGPU pointLights[32];
    SpotLightGPU spotLights[16];
} lights;

layout(std430, set = 0, binding = 4) readonly buffer AtlasUVBuffer {
    uint count512;        // layers in the 512px (class 0) array
    uint fallbackIndex;   // placeholder layer (class 0)
    uint count1024;       // layers in the 1024px (class 1) array
    uint _pad1;
    vec4 textureUVs[];    // retained for layout compat; no longer sampled
} atlasUVs;

layout(location = 0) out vec4 outColor;   // output color

// Cheap 2D hash -> [0,1). Used to pick a per-world-cell tile rotation (Phase A).
float hash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

// Project a world position onto the face plane (the two axes perpendicular to the
// face normal) -> continuous in-plane coords whose integer part is the world cell.
vec2 worldFaceUV(vec3 wp, vec3 n) {
    vec3 a = abs(n);
    if (a.y >= a.x && a.y >= a.z) return wp.xz;   // top/bottom
    if (a.x >= a.z)               return wp.zy;   // +/-X
    return wp.xy;                                 // +/-Z
}

// Sample albedo + normal/roughness for a per-face index. The index encodes the resolution
// class in bit 15 (0 = 512px, 1 = 1024px) and the within-class layer in bits 0..14. Out of
// range / sentinel (0xFFFF) indices fall back to the placeholder layer in the 512 class.
// nrm = raw 0..1 tangent-space normal (RGB), rough = roughness (A).
//
// When `varied` (materials.json "varied", static cube path only — docs/VoxelOrientation.md
// Phase A), each world cell's tile is hash-rotated (90deg step + optional flip) to break the
// per-cube grid repeat. textureGrad keeps mips correct across the per-tile rotation seam, and
// the tangent-space normal's xy is rotated to match so relief lights consistently.
void sampleVoxelPBR(uint texIndex, vec2 uv, bool varied, vec3 worldPos, vec3 faceNormal,
                    out vec4 albedo, out vec3 nrm, out float rough) {
    uint cls   = (texIndex >> 15) & 1u;
    uint layer = texIndex & 0x7FFFu;
    uint count = (cls == 1u) ? atlasUVs.count1024 : atlasUVs.count512;
    bool fb = (texIndex == 0xFFFFu || layer >= count);
    float L = fb ? float(atlasUVs.fallbackIndex) : float(layer);
    uint c = fb ? 0u : cls;

    // Sampling coords + screen-space gradients (explicit so divergent rotation is well-defined).
    vec2 suv = uv;
    vec2 gx  = dFdx(uv);
    vec2 gy  = dFdy(uv);
    int  rotStep = 0;
    bool flipped = false;
    if (varied) {
        vec2 p = worldFaceUV(worldPos, faceNormal);
        float h = hash21(floor(p) + 0.5);
        rotStep = int(floor(h * 4.0)) & 3;        // 0/90/180/270
        flipped = fract(h * 16.0) > 0.5;
        vec2 lp  = fract(p);
        vec2 dpx = dFdx(p);
        vec2 dpy = dFdy(p);
        if (flipped) { lp.x = 1.0 - lp.x; dpx.x = -dpx.x; dpy.x = -dpy.x; }
        vec2 ctr = lp - 0.5;
        if      (rotStep == 1) { ctr = vec2(-ctr.y, ctr.x); dpx = vec2(-dpx.y, dpx.x); dpy = vec2(-dpy.y, dpy.x); }
        else if (rotStep == 2) { ctr = -ctr;                dpx = -dpx;                dpy = -dpy;                }
        else if (rotStep == 3) { ctr = vec2(ctr.y, -ctr.x); dpx = vec2(dpx.y, -dpx.x); dpy = vec2(dpy.y, -dpy.x); }
        suv = ctr + 0.5;
        gx = dpx; gy = dpy;
    }

    vec4 nr;
    if (c == 1u) { albedo = textureGrad(textureArrayHi, vec3(suv, L), gx, gy); nr = textureGrad(textureNormalHi, vec3(suv, L), gx, gy); }
    else         { albedo = textureGrad(textureArray,   vec3(suv, L), gx, gy); nr = textureGrad(textureNormal,   vec3(suv, L), gx, gy); }
    nrm = nr.rgb;
    if (varied) {                                  // rotate tangent normal xy to match the tile
        vec2 nxy = nrm.xy * 2.0 - 1.0;
        if (flipped) nxy.x = -nxy.x;
        if      (rotStep == 1) nxy = vec2(-nxy.y, nxy.x);
        else if (rotStep == 2) nxy = -nxy;
        else if (rotStep == 3) nxy = vec2(nxy.y, -nxy.x);
        nrm.xy = nxy * 0.5 + 0.5;
    }
    rough = nr.a;
}

// Cook-Torrance GGX BRDF with metalness. F0 = 0.04 for dielectrics, lerps to albedo for
// metals (which also lose their diffuse lobe). N,V,L unit vectors; albedo linear.
vec3 pbrBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float rough, float metallic, vec3 radiance) {
    float ndl = max(dot(N, L), 0.0);
    if (ndl <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float ndh = max(dot(N, H), 0.0);
    float ndv = max(dot(N, V), 1e-4);
    float vdh = max(dot(V, H), 0.0);

    float a = max(rough * rough, 1e-3);
    float a2 = a * a;
    // GGX normal distribution
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * d * d);
    // Smith-GGX geometry (Schlick-Beckmann)
    float k = (a + 1.0); k = (k * k) / 8.0;
    float gv = ndv / (ndv * (1.0 - k) + k);
    float gl = ndl / (ndl * (1.0 - k) + k);
    float G = gv * gl;
    // Fresnel (Schlick): dielectric F0 = 0.04, lerping to albedo (tinted reflectance) for metals.
    // Roughness-aware grazing cap: rough surfaces (grass/dirt/stone) don't blow up to full
    // mirror reflectance at grazing angles, which otherwise produces per-texel specular sparkle
    // (fireflies) under a low sun. Glossy metal/gold keep their strong grazing specular.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Fgrazing = max(vec3(1.0 - rough), F0);
    vec3 F = F0 + (Fgrazing - F0) * pow(1.0 - vdh, 5.0);

    vec3 spec = (D * G) * F / (4.0 * ndv * ndl + 1e-3);
    // Rough surfaces (grass/dirt/stone, roughness >~0.8) shed their sun specular so they read
    // matte and don't sparkle from normal-map detail at the reflection hotspot. Glossy materials
    // (metal/gold, low roughness) keep their FULL specular glare. Clean matte/glossy separation.
    spec *= 1.0 - smoothstep(0.55, 0.95, rough);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);  // metals have no diffuse lobe
    // The Lambert 1/pi is RESTORED (2026-08-10). It used to be omitted deliberately, and the reason
    // given was sound at the time: "light intensities are authored for the prior (non-PBR) model, so
    // this keeps brightness parity". That rationale expired the moment the sun's colour and intensity
    // started coming from a physical scattering model (graphics/Atmosphere.h) — the sky is radiance
    // and the surfaces were pi times too bright relative to it, which showed up immediately as a
    // correct-looking sky above a washed-out world. A compensation for a problem that no longer
    // exists is just an error.
    // ⚠️ Point/spot light intensities WERE authored against the un-normalised term, so they are now
    // pi times dimmer and need retuning along with the fixture photometry work.
    const float kInvPi = 0.31830989;
    vec3 diffuse = kd * albedo * kInvPi;
    return (diffuse + spec) * radiance * ndl;
}

// Calculate attenuation for a light at distance d with given radius
float calcAttenuation(float d, float radius) {
    float linear = 4.5 / radius;
    float quadratic = 75.0 / (radius * radius);
    float atten = 1.0 / (1.0 + linear * d + quadratic * d * d);
    // Smooth cutoff at radius
    float falloff = clamp(1.0 - (d / radius), 0.0, 1.0);
    return atten * falloff;
}

void main() {
    // Sample albedo + normal/roughness for this face (handles the mixed-res class split).
    vec4 textureColor;
    vec3 nrmRaw;
    float rough;
    // Camera-relative rendering (docs/CameraRelativeRendering.md): inWorldPos is relative,
    // so the varied hash is seeded from the reconstructed ABSOLUTE position. vChunkBaseAbs
    // is exact (integer chunk origin); (inWorldPos - vChunkBaseRel) is the local offset at
    // small magnitude, so the sum is camera-independent — rotations never re-roll.
    bool varied = ((flags >> 15u) & 1u) != 0u;
    vec3 worldPosAbs = vChunkBaseAbs + (inWorldPos - vChunkBaseRel);
    sampleVoxelPBR(textureIndex, texCoord, varied, worldPosAbs, inNormal, textureColor, nrmRaw, rough);

    // Per-layer material props (metallic, roughness scalar) from the atlas SSBO. Global index
    // = within-class layer, offset by count512 for the 1024 class. The authored roughness scalar
    // (materials.json) drives roughness for ALL materials — natural surfaces (grass/dirt/stone)
    // are matte, metal/gold stay glossy — and we keep a little of the map for surface variation.
    // (Previously the scalar was applied only to metals, so dielectrics used the map's roughness,
    // which read too shiny and produced a sun glare on grass.)
    uint giCls = (textureIndex >> 15) & 1u;
    uint giLayer = textureIndex & 0x7FFFu;
    uint gi = (giCls == 1u) ? atlasUVs.count512 + giLayer : giLayer;
    float metallic = 0.0;
    float emStrength = 0.0;    // masked emission: >0 = bright albedo pixels also EMIT (enchanted log)
    float emThreshold = 0.55;  // albedo luminance above which a pixel glows
    if (gi < atlasUVs.count512 + atlasUVs.count1024) {
        vec4 mprops = atlasUVs.textureUVs[gi];
        metallic = mprops.x;
        rough = mprops.y;  // authored roughness is authoritative (matte nature, glossy metal); avoids grazing-angle specular sparkle from the shiny roughness map
        emStrength = mprops.z;
        emThreshold = mprops.w;
    }

    // Per-voxel damage (flags bits 11..14, 0..15) from DamageSystem accumulation: damaged
    // surfaces read as rougher (scuffed/worn) and slightly darker/dirtier.
    float dmg = float((flags >> 11u) & 0xFu) / 15.0;
    rough = mix(rough, 1.0, dmg);
    textureColor.rgb *= mix(1.0, 0.55, dmg);

    // Discard fully transparent fragments (cutout transparency)
    if (textureColor.a < 0.1) discard;

    // Discard mirror fragments — handled in the mirror pass
    if ((flags & (1u << 10u)) != 0u) discard;

    bool isEmissive = (flags & 1u) != 0u;

    // Geometric normal + per-face tangent basis. Voxel faces are axis-aligned, so a stable
    // tangent is derived from the face normal; this gives correct surface relief from the
    // tangent-space normal map even if fine feature orientation is approximate per face.
    vec3 Ng = normalize(inNormal);
    vec3 up = abs(Ng.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, Ng));
    vec3 B = cross(Ng, T);
    vec3 nTS = normalize(nrmRaw * 2.0 - 1.0);
    vec3 N = normalize(T * nTS.x + B * nTS.y + Ng * nTS.z);

    vec3 V = normalize(ubo.cameraPosition - inWorldPos);
    vec3 albedo = textureColor.rgb * vTint;   // per-voxel tint (material texture × tint color)

    // Per-voxel STATE visual modifiers (docs/VoxelAppearanceModel.md, Phase 2).
    // Flaming/smoldering: the mesher swapped the surface to the emissive burning_wood
    // ember texture — show it RAW (no tint) so fire colours read true, boosted so the
    // bloom pass catches it, and skip the lit path entirely.
    if (vState == 1u || vState == 2u) {
        float glow = (vState == 1u) ? 1.0 : 0.45;   // flaming bright, smoldering dim
        outColor = vec4(textureColor.rgb * ubo.emissiveMultiplier * glow, textureColor.a);
        return;
    } else if (vState == 3u) {                       // charred — dark, desaturated
        float l = dot(albedo, vec3(0.299, 0.587, 0.114));
        albedo = mix(vec3(l), albedo, 0.25) * 0.32;
    } else if (vState == 4u) {                       // wet — darker + glossier
        albedo *= 0.72;
        rough = min(rough, 0.15);
    }

    // Shadow — CONTACT-HARDENING soft shadows (PCSS). The old path was a fixed 1.5-texel
    // Poisson PCF: razor-hard edges that showed every shadow-map stair-step, an identical
    // sample pattern on every pixel (visible banding in the penumbra), and a hard cutoff at
    // the map border. Three changes, in order of visual impact:
    //   1. BLOCKER SEARCH -> variable penumbra. Real shadows are sharp where the occluder
    //      touches the receiver and blur with separation; a constant-width filter is the
    //      single most "CG" thing about a shadow. Penumbra scales with occluder distance.
    //   2. PER-PIXEL ROTATION of the Poisson disk (interleaved gradient noise). A fixed disk
    //      repeats its pattern across the screen and reads as banding once the filter is
    //      wide enough to matter; rotating each pixel turns that into fine dither.
    //   3. BORDER FADE. Beyond the fitted volume there is no shadow data, and snapping to
    //      fully-lit drew a visible line across the ground at the shadow distance.
    // Shadow: contact-hardening PCSS from the shared model (lighting.glsl). Bias,
    // penumbra, per-pixel dither rotation and border fade all live there, so this pass
    // cannot drift from grass/foliage the way five hand-synced copies did.
    float shadowFactor = 1.0;
    if (!isEmissive) {
        float ndlForBias = dot(N, normalize(-ubo.sunDirection));
        shadowFactor = phxShadowPCSS(shadowMap, shadowCoord, ndlForBias, gl_FragCoord.xy,
                                     ubo.shadowDepthRange);
        // Near cascade: min-compose with the fine map inside its range. min() = union of
        // shadows, so casters recorded only in one map (grass = near-only; foliage =
        // mid-only) still shade correctly, and the near map's border fade blends the split.
        if (ubo.shadowCascadeNear.x > 0.0 &&
            dot(inWorldPos, inWorldPos) <
                ubo.shadowCascadeNear.x * ubo.shadowCascadeNear.x) {
            const float kNearNormalOffset = 0.05;   // finer texels need less receiver offset
            vec4 nearCoord = ubo.biasedLightSpaceNear *
                             vec4(inWorldPos + N * kNearNormalOffset, 1.0);
            shadowFactor = min(shadowFactor,
                               phxShadowPCSS(shadowMapNear, nearCoord, ndlForBias,
                                             gl_FragCoord.xy, ubo.shadowCascadeNear.y));
        }
    }

    if (isEmissive) {
        // Tint the self-illumination by the block's own emitted colour (its baked block-light hue)
        // so a blue-glow block reads blue, a green one green, etc. — not just the texture colour.
        vec3 tint = vBlockColor;
        float m = max(tint.r, max(tint.g, max(tint.b, 0.001)));
        tint = (m > 0.05) ? tint / m : vec3(1.0);  // hue only; fall back to white if unknown
        outColor = vec4(albedo * ubo.emissiveMultiplier * tint, textureColor.a);
        return;
    }

    // Sky-ambient is a soft FILL light, not the key. The directional sun (below) is the key
    // light that gives the scene form + shadows. Keeping ambient near 1.0 washes out all
    // directionality (everything looks flat/omnidirectionally lit) — so we scale it down to a
    // fill level. A convex (gamma) curve on skylight makes partial sky fall off fast, so
    // interiors read dramatically dimmer than outdoors. kAmbientFloor keeps fully-sealed cells
    // from being pitch black before block lights (Phase 2) exist.
    // Sky ambient = soft FILL (never the key light), hemispherical and gated by baked
    // skylight. Model + constants: lighting.glsl.
    // M3: sky access is TRACED against real geometry, not read from a per-cell flood.
    // vSkyLight is the M0 placeholder (a constant 1.0); multiplying keeps the vertex path in
    // place for A/B until M4 decides whether to delete it.
    float skyVis = vSkyLight * phxSkyVisibility(inWorldPos + ubo.cameraWorld, Ng);
    float skyCurve = phxSkyGate(skyVis);
    // Each lighting term is ALSO captured on its own so the debug views below can show one
    // system at a time. Three systems light this engine and they disagree about geometry (sun =
    // rasterized shadow maps, per fragment; baked sky/block = one value per CUBE cell; forward
    // point/spot = per fragment, no occlusion). Telling them apart by eye is guesswork, and
    // guessing is what made an interior-light bug take days.
    vec3 dbgAmbient = phxAmbientAtmos(N, skyVis, ubo.ambientColor) * albedo;
    vec3  color = dbgAmbient;

    // Sun (directional) — the KEY light. Cook-Torrance, N·L shading, shadow-mapped. Gated by
    // sky access (curved) so surfaces with no sky exposure don't receive direct sun. This is
    // what casts shadows across the scene whenever the sun isn't directly overhead.
    vec3 sunL = normalize(-ubo.sunDirection);
    vec3 dbgDirect = pbrBRDF(N, V, sunL, albedo, rough, metallic, ubo.sunColor) * shadowFactor * skyCurve;
    color += dbgDirect;

    // Moonlight — the same directional model, fed by the atmosphere's phase-scaled moonlight colour,
    // so a new moon contributes literally nothing and a full moon reads clearly. Without this the
    // night sky was rendered but the WORLD was not lit by it: measured at 98% of the frame crushed to
    // black, with a full moon and a new moon producing identical frames.
    // ⚠️ Deliberately NOT multiplied by shadowFactor. The shadow cascades are fitted to the SUN's
    // direction, so at night that map describes a light source below the horizon and applying it to
    // the moon would stamp sun-shaped shadows from the wrong direction. Unshadowed moonlight is the
    // standard approximation and it is dim enough (~3.5% of sunlight) to be unobjectionable; fitting
    // the cascades to whichever body is dominant is the follow-up that earns real moon shadows.
    if (ubo.moonColor.b > 0.0) {
        vec3 m = pbrBRDF(N, V, normalize(-ubo.moonDirection), albedo, rough, metallic,
                         ubo.moonColor) * skyCurve;
        color += m;
        dbgDirect += m;   // moon is the same directional system as the sun
    }

    // Baked COLOURED block light from emissive voxels (torches/glow/crystals). Omnidirectional
    // fill (the bake stores no direction, like a lightmap) carrying each source's own colour, so a
    // glow block lights its room warm, a blue crystal blue, etc. Independent of sky access, so it's
    // the light source indoors / at night. Per-channel convex falloff for a natural rolloff.
    vec3 dbgBlock = (vBlockColor * vBlockColor) * albedo;
    color += dbgBlock;

    // Point lights
    vec3 dbgForward = vec3(0.0);
    for (uint i = 0u; i < lights.numPointLights && i < 32u; i++) {
        vec3 lightPos = lights.pointLights[i].positionAndRadius.xyz;
        float radius = lights.pointLights[i].positionAndRadius.w;
        vec3 lightColor = lights.pointLights[i].colorAndIntensity.xyz;
        float intensity = lights.pointLights[i].colorAndIntensity.w;
        vec3 toLight = lightPos - inWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            // Skip the trace when the surface faces away: pbrBRDF already returns black there, so
            // tracing would cost a march to change nothing. This is also why the reported
            // "wall lit from inside" bug was never the BRDF's fault — it is geometry the light
            // reaches around, which only a visibility term can cut.
            if (dot(N, ldir) > 0.0) {
                float vis = phxLightVisibility(inWorldPos + ubo.cameraWorld, Ng,
                                               lightPos + ubo.cameraWorld, ubo.occupancyBox);
                if (vis > 0.0) {
                    float atten = calcAttenuation(dist, radius);
                    dbgForward += pbrBRDF(N, V, ldir, albedo, rough, metallic,
                                          lightColor * intensity * atten);
                }
            }
        }
    }

    // Spot lights
    for (uint i = 0u; i < lights.numSpotLights && i < 16u; i++) {
        vec3 lightPos = lights.spotLights[i].positionAndRadius.xyz;
        float radius = lights.spotLights[i].positionAndRadius.w;
        vec3 spotDir = normalize(lights.spotLights[i].directionAndInnerCone.xyz);
        float innerCone = lights.spotLights[i].directionAndInnerCone.w;
        vec3 lightColor = lights.spotLights[i].colorAndIntensity.xyz;
        float intensity = lights.spotLights[i].colorAndIntensity.w;
        float outerCone = lights.spotLights[i].outerConeAndPadding.x;
        vec3 toLight = lightPos - inWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            float theta = dot(-ldir, spotDir);
            float spotFactor = smoothstep(outerCone, innerCone, theta);
            // Trace only inside the cone and on facing surfaces — outside either, the contribution
            // is already zero and a march would be pure cost.
            if (spotFactor > 0.0 && dot(N, ldir) > 0.0) {
                float vis = phxLightVisibility(inWorldPos + ubo.cameraWorld, Ng,
                                               lightPos + ubo.cameraWorld, ubo.occupancyBox);
                if (vis > 0.0) {
                    float atten = calcAttenuation(dist, radius);
                    dbgForward += pbrBRDF(N, V, ldir, albedo, rough, metallic,
                                          lightColor * intensity * atten * spotFactor);
                }
            }
        }
    }
    color += dbgForward;

    // Masked emission (docs/MaskedEmissiveSpec.md): the surface above was lit NORMALLY; now ADD glow
    // from the bright pixels of the albedo (e.g. an enchanted log's cracks) without a per-face flag.
    // The glow colour is the albedo's own bright pixels, boosted by emissiveMultiplier so the bloom
    // pass catches it. emStrength==0 for ordinary materials -> no cost effect.
    if (emStrength > 0.0) {
        float luma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
        float e = smoothstep(emThreshold, 1.0, luma) * emStrength;
        color += e * albedo * ubo.emissiveMultiplier;
    }

    // Aerial perspective (shared curve - lighting.glsl). inWorldPos is ALREADY
    // camera-relative in this pass, so it IS the camera->fragment vector.
    color = phxAerialPerspective(color, inWorldPos, ubo.sunDirection, ubo.sunColor, ubo.hazeHorizonColor, ubo.hazeZenithColor);

    // Debug view 2 is the GRASS WIND ramp. Everything that is not grass must go flat and
    // dark, or the shadow-only view underneath drowns the signal it exists to show.
    if (ubo.debugShadowMode == 2) { outColor = vec4(0.05, 0.05, 0.06, 1.0); return; }
    if (ubo.debugShadowMode == 1) { outColor = phxShadowOnly(shadowFactor); return; }

    // ---- PER-SYSTEM LIGHTING VIEWS (3..7) ------------------------------------------------
    // Show ONE lighting system at a time, so "which of the three lit this surface" is read off
    // the screen instead of inferred. Aerial perspective is deliberately NOT applied to these —
    // they are the raw contribution, not the final look.
    //
    //   3 SKY ACCESS   the baked per-CUBE-CELL skylight, greyscale, no albedo. Cube-stepped by
    //                  construction: this is the Minecraft-style flood's own resolution.
    //   4 BLOCK LIGHT  the baked per-cell coloured light from emissive voxels, no albedo.
    //                  If an EXTERIOR surface glows here with the source indoors, the flood is
    //                  leaking through the shell.
    //   5 FORWARD      point + spot lights only. These do NO occlusion test of any kind, so
    //                  anything lit here was lit without regard to walls.
    //   6 DIRECT       sun + moon, shadow-mapped against real geometry (sub-voxel accurate).
    //   7 SKY FILL     the hemispheric ambient term.
    if (ubo.debugShadowMode == 3) { outColor = vec4(vec3(skyVis), 1.0); return; }
    if (ubo.debugShadowMode == 4) { outColor = vec4(vBlockColor * vBlockColor, 1.0); return; }
    if (ubo.debugShadowMode == 5) { outColor = vec4(dbgForward, 1.0); return; }
    if (ubo.debugShadowMode == 6) { outColor = vec4(dbgDirect, 1.0); return; }
    if (ubo.debugShadowMode == 7) { outColor = vec4(dbgAmbient, 1.0); return; }
    // Mode 8 — OCCUPANCY HIT (M1 gate). Asks the GPU occupancy about the fragment's own position,
    // stepped slightly ALONG the surface normal into the solid, so a surface reports the matter it
    // belongs to rather than the air in front of it.
    //   red   = the occupancy agrees this surface is made of solid matter. EXPECTED everywhere a
    //           chunk voxel is drawn, at cube, subcube and microcube resolution alike.
    //   green = a surface IS drawn here but the occupancy says its own cell is empty. That is a
    //           DISAGREEMENT between the mesh and the occupancy, not a normal partial cell —
    //           green is the defect colour, and a healthy scene has none. (Gap 15 in
    //           StructurePipelineGaps.md — remove_subcube desyncing the grid — makes green.)
    //   blue  = outside the covered box, or occupancy not resident: reads as "no occlusion".
    // NOTE: the probe steps HALF A MICRO CELL along -N into the surface, so a 2-micro wall or a
    // 3-micro floor reads red on its own faces. Partial cells are not visible as green here; they
    // are proven instead by /api/debug/light_occupancy's per-micro counts, which report exactly
    // how many of a cell's 729 micro cells are filled.
    if (ubo.debugShadowMode == 8) {
        if ((ubo.occupancyBox.w & 1) == 0) { outColor = vec4(0.0, 0.0, 1.0, 1.0); return; }
        // inWorldPos is CAMERA-RELATIVE (every GPU position is world - camera); cameraWorld puts
        // it back into absolute world space, which is what the occupancy is indexed by.
        vec3 probe = inWorldPos + ubo.cameraWorld - N * (0.5 / 9.0);   // half a micro cell inward
        ivec3 wm = ivec3(floor(probe * 9.0));
        bool solid = phxOccupancySolid(wm, ubo.occupancyBox);
        // Distinguish "outside the box" from "inside but empty": re-ask at the same cell with the
        // guard already checked, and use the directory reach as the box test.
        ivec3 cc = ivec3(phxFloorDiv(wm.x, PHX_OCC_MICRO_PER_CHUNK),
                         phxFloorDiv(wm.y, PHX_OCC_MICRO_PER_CHUNK),
                         phxFloorDiv(wm.z, PHX_OCC_MICRO_PER_CHUNK)) - ubo.occupancyBox.xyz;
        bool inBox = cc.x >= 0 && cc.x < PHX_OCC_DIR_X && cc.y >= 0 && cc.y < PHX_OCC_DIR_Y &&
                     cc.z >= 0 && cc.z < PHX_OCC_DIR_Z;
        if (!inBox)  { outColor = vec4(0.0, 0.0, 1.0, 1.0); return; }
        outColor = solid ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
        return;
    }
    // Mode 9 — CELL FILL CLASS. Asks the GPU occupancy about all 729 micro cells of the cube this
    // fragment belongs to and classifies the cell, so that "the shader can read a PARTIALLY filled
    // cell" is answerable from the GPU side rather than only from the CPU mirror.
    //   red   = fully solid cell (729/729)
    //   GREEN = PARTIAL (1..728) — the case the old one-value-per-cell lighting could not
    //           represent, and the direct cause of the interior wall-base black band
    //   black = empty cell        blue = uncovered / not resident
    // Deliberately colour CLASSES, not a gradient: this output passes through AgX, so an intensity
    // ramp would not survive to be measured, whereas class colours are still separable by
    // dominant channel. 729 lookups per fragment is heavy and runs ONLY in this mode.
    if (ubo.debugShadowMode == 9) {
        if ((ubo.occupancyBox.w & 1) == 0) { outColor = vec4(0.0, 0.0, 1.0, 1.0); return; }
        vec3 probe = inWorldPos + ubo.cameraWorld - N * (0.5 / 9.0);
        ivec3 cubeW = ivec3(floor(probe));
        ivec3 cc = ivec3(phxFloorDiv(cubeW.x, 32), phxFloorDiv(cubeW.y, 32),
                         phxFloorDiv(cubeW.z, 32)) - ubo.occupancyBox.xyz;
        if (cc.x < 0 || cc.x >= PHX_OCC_DIR_X || cc.y < 0 || cc.y >= PHX_OCC_DIR_Y ||
            cc.z < 0 || cc.z >= PHX_OCC_DIR_Z) { outColor = vec4(0.0, 0.0, 1.0, 1.0); return; }
        int filled = 0;
        for (int i = 0; i < 729; ++i) {
            ivec3 inC = ivec3(i % 9, (i / 9) % 9, i / 81);
            if (phxOccupancySolid(cubeW * 9 + inC, ubo.occupancyBox)) ++filled;
        }
        if (filled == 0)        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        else if (filled == 729) outColor = vec4(1.0, 0.0, 0.0, 1.0);
        else                    outColor = vec4(0.0, 1.0, 0.0, 1.0);
        return;
    }

    outColor = vec4(color, textureColor.a);
}
