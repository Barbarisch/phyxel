#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in flat uint textureIndex;  // from vertex shader
layout(location = 1) in vec2 texCoord;           // from vertex shader
layout(location = 2) in vec4 shadowCoord;        // from vertex shader
layout(location = 3) in flat uint flags;         // from vertex shader
layout(location = 4) in vec3 inNormal;           // from vertex shader
layout(location = 5) in vec3 inWorldPos;         // from vertex shader
layout(location = 6) in flat float vSkyLight;    // baked skylight 0..1 (0 = enclosed/no sky access)
layout(location = 7) in flat vec3  vBlockColor;  // baked coloured block light 0..1/channel (emissive voxels)

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
} ubo;

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;     // class 0 albedo: 512px
layout(set = 0, binding = 2) uniform sampler2D shadowMap;             // shadow map sampler
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

// Sample albedo + normal/roughness for a per-face index. The index encodes the resolution
// class in bit 15 (0 = 512px, 1 = 1024px) and the within-class layer in bits 0..14. Out of
// range / sentinel (0xFFFF) indices fall back to the placeholder layer in the 512 class.
// nrm = raw 0..1 tangent-space normal (RGB), rough = roughness (A).
void sampleVoxelPBR(uint texIndex, vec2 uv, out vec4 albedo, out vec3 nrm, out float rough) {
    uint cls   = (texIndex >> 15) & 1u;
    uint layer = texIndex & 0x7FFFu;
    uint count = (cls == 1u) ? atlasUVs.count1024 : atlasUVs.count512;
    bool fb = (texIndex == 0xFFFFu || layer >= count);
    float L = fb ? float(atlasUVs.fallbackIndex) : float(layer);
    uint c = fb ? 0u : cls;
    vec4 nr;
    if (c == 1u) { albedo = texture(textureArrayHi, vec3(uv, L)); nr = texture(textureNormalHi, vec3(uv, L)); }
    else         { albedo = texture(textureArray,   vec3(uv, L)); nr = texture(textureNormal,   vec3(uv, L)); }
    nrm = nr.rgb;
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
    // NOTE: diffuse 1/pi omitted on purpose — light intensities are authored for the prior
    // (non-PBR) model, so this keeps brightness parity while adding GGX specular + normal maps.
    vec3 diffuse = kd * albedo;
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

// 16-sample Poisson disk for soft shadow PCF
const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624,  -0.39906216),
    vec2( 0.94558609,  -0.76890725),
    vec2(-0.094184101, -0.92938870),
    vec2( 0.34495938,   0.29387760),
    vec2(-0.91588581,   0.45771432),
    vec2(-0.81544232,  -0.87912464),
    vec2(-0.38277543,   0.27676845),
    vec2( 0.97484398,   0.75648379),
    vec2( 0.44323325,  -0.97511554),
    vec2( 0.53742981,  -0.47373420),
    vec2(-0.26496911,  -0.41893023),
    vec2( 0.79197514,   0.19090188),
    vec2(-0.24188840,   0.99706507),
    vec2(-0.81409955,   0.91437590),
    vec2( 0.19984126,   0.78641367),
    vec2( 0.14383161,  -0.14100790)
);

void main() {
    // Sample albedo + normal/roughness for this face (handles the mixed-res class split).
    vec4 textureColor;
    vec3 nrmRaw;
    float rough;
    sampleVoxelPBR(textureIndex, texCoord, textureColor, nrmRaw, rough);

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
    if (gi < atlasUVs.count512 + atlasUVs.count1024) {
        vec4 mprops = atlasUVs.textureUVs[gi];
        metallic = mprops.x;
        rough = mprops.y;  // authored roughness is authoritative (matte nature, glossy metal); avoids grazing-angle specular sparkle from the shiny roughness map
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
    vec3 albedo = textureColor.rgb;

    // Shadow — 16-sample Poisson disk PCF (uses the geometric normal's shadow coord).
    // Only inside the shadow map's [0,1] UV footprint; outside it (beyond the fitted volume)
    // there is no shadow data, so treat as lit rather than sampling the clamped edge (which would
    // smear the border texel's occlusion across everything off to the side).
    float shadowFactor = 1.0;
    bool inShadowMap = shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
                       shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0;
    if (!isEmissive && inShadowMap && shadowCoord.z > -1.0 && shadowCoord.z < 1.0 && shadowCoord.w > 0.0) {
        float shadowSum = 0.0;
        vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
        // Small constant bias: the shadow pass records occluder BACK faces (front-face culled),
        // so receivers no longer self-shadow and this can be tiny — keeping shadows attached to
        // bases (no peter-panning) instead of the old large 0.005 bias.
        const float kShadowBias = 0.0006;
        for (int i = 0; i < 16; i++) {
            float pcfDepth = texture(shadowMap, shadowCoord.xy + poissonDisk[i] * texelSize * 1.5).r;
            if (shadowCoord.z - kShadowBias > pcfDepth) shadowSum += 1.0;
        }
        shadowFactor = 1.0 - (shadowSum / 16.0);
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
    const float kAmbientFloor = 0.02;
    const float kSkyFill = 0.35;                        // sky ambient as a fraction (fill, not key)
    float skyCurve = vSkyLight * vSkyLight;             // gamma ~2 falloff
    float skyAmbient = ubo.ambientLight * skyCurve * kSkyFill;
    vec3 color = (skyAmbient + kAmbientFloor) * albedo;

    // Sun (directional) — the KEY light. Cook-Torrance, N·L shading, shadow-mapped. Gated by
    // sky access (curved) so surfaces with no sky exposure don't receive direct sun. This is
    // what casts shadows across the scene whenever the sun isn't directly overhead.
    vec3 sunL = normalize(-ubo.sunDirection);
    color += pbrBRDF(N, V, sunL, albedo, rough, metallic, ubo.sunColor) * shadowFactor * skyCurve;

    // Baked COLOURED block light from emissive voxels (torches/glow/crystals). Omnidirectional
    // fill (the bake stores no direction, like a lightmap) carrying each source's own colour, so a
    // glow block lights its room warm, a blue crystal blue, etc. Independent of sky access, so it's
    // the light source indoors / at night. Per-channel convex falloff for a natural rolloff.
    color += (vBlockColor * vBlockColor) * albedo;

    // Point lights
    for (uint i = 0u; i < lights.numPointLights && i < 32u; i++) {
        vec3 lightPos = lights.pointLights[i].positionAndRadius.xyz;
        float radius = lights.pointLights[i].positionAndRadius.w;
        vec3 lightColor = lights.pointLights[i].colorAndIntensity.xyz;
        float intensity = lights.pointLights[i].colorAndIntensity.w;
        vec3 toLight = lightPos - inWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            float atten = calcAttenuation(dist, radius);
            color += pbrBRDF(N, V, ldir, albedo, rough, metallic, lightColor * intensity * atten);
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
            float atten = calcAttenuation(dist, radius);
            float theta = dot(-ldir, spotDir);
            float spotFactor = smoothstep(outerCone, innerCone, theta);
            color += pbrBRDF(N, V, ldir, albedo, rough, metallic, lightColor * intensity * atten * spotFactor);
        }
    }

    outColor = vec4(color, textureColor.a);
}
