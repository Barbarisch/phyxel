#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // shared ambient / shadow / aerial model

// Far-tree LOD mesh shading — far_terrain.frag's twin (same atlas, same lighting) plus a
// screen-door dither fade. Trees must NEVER change size with distance (the scale-fade read
// as "trees shrinking as I approach" — user-rejected); they dissolve at constant scale.

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in flat uint vTex;
layout(location = 2) in flat uint vFace;
layout(location = 3) in flat float vFade;   // 0 = fully dissolved, 1 = solid
layout(location = 4) in flat float vLvlLo;  // level partition (see far_tree_mesh.vert)
layout(location = 5) in flat float vLvlHi;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
    float emissiveMultiplier;
    vec3 cameraPosition;    // NOT the live camera here — use cameraWorld (see far_terrain.frag)
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;
    mat4 biasedLightSpace;
    vec3 cameraWorld;
    int  debugShadowMode;
    float shadowDepthRange;
    vec4  grassDisplacers[16];      // declared only to reach the far-cascade fields below
    vec4  grassDisplacersAux[16];
    ivec4 grassDisplacerMeta;
    mat4 biasedLightSpaceNear;
    vec4 shadowCascadeNear;
    mat4 lightSpaceMatrixNear;
    mat4 biasedLightSpaceFar;       // FAR shadow cascade
    vec4 shadowCascadeFar;          // x = range end (0 = off), y = far depthRange
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
} ubo;

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;
layout(set = 0, binding = 10) uniform sampler2D shadowMapFar;   // far shadow cascade

layout(std430, set = 0, binding = 4) readonly buffer AtlasUVBuffer {
    uint count512;
    uint fallbackIndex;
    uint count1024;
    uint _pad1;
    vec4 textureUVs[];
} atlasUVs;

layout(location = 0) out vec4 outColor;

vec3 faceNormal(uint f) {
    if (f == 0u) return vec3(0, 0, 1);
    if (f == 1u) return vec3(0, 0, -1);
    if (f == 2u) return vec3(1, 0, 0);
    if (f == 3u) return vec3(-1, 0, 0);
    if (f == 4u) return vec3(0, 1, 0);
    return vec3(0, -1, 0);
}

vec2 worldFaceUV(vec3 wp, uint f) {
    if (f >= 4u) return wp.xz;
    if (f == 2u || f == 3u) return wp.zy;
    return wp.xy;
}

// 4x4 ordered Bayer dither: stable per-pixel threshold, no temporal shimmer.
float bayer4(vec2 p) {
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 ip = ivec2(mod(p, 4.0));
    return (m[ip.x + ip.y * 4] + 0.5) / 16.0;
}

void main() {
    float b = bayer4(gl_FragCoord.xy);
    if (vFade < b) discard;               // screen-door fade — size never changes
    // Level partition (complementary across adjacent-level draws — see far_tree_mesh.vert):
    // keep iff this pixel's threshold falls inside this draw's level window.
    if (b >= vLvlLo || b < vLvlHi) discard;

    uint cls   = (vTex >> 15) & 1u;
    uint layer = vTex & 0x7FFFu;
    uint count = (cls == 1u) ? atlasUVs.count1024 : atlasUVs.count512;
    bool fb    = (vTex == 0xFFFFu || layer >= count);
    float L    = fb ? float(atlasUVs.fallbackIndex) : float(layer);

    vec2 uv = worldFaceUV(vWorldPos, vFace);
    vec4 albedo = fb || cls == 0u ? texture(textureArray,   vec3(uv, L))
                                  : texture(textureArrayHi, vec3(uv, L));

    // Lighting: the SHARED model (lighting.glsl) — identical to far_terrain.frag so the
    // tree/terrain handoff cannot show a colour seam. Open sky, no shadow map in this pass.
    vec3 N = faceNormal(vFace);
    vec3 sunL = normalize(-ubo.sunDirection);
    float ndl = max(dot(N, sunL), 0.0);
    // FAR shadow cascade: LOD trees shade each other and receive terrain shadows — the
    // sun side of a distant forest reads lit, its interior falls into shade.
    float shadowF = 1.0;
    if (ubo.shadowCascadeFar.x > 0.0)
        shadowF = phxShadowFast(shadowMapFar,
                                ubo.biasedLightSpaceFar *
                                    vec4(vWorldPos - ubo.cameraWorld, 1.0),
                                ubo.shadowCascadeFar.y);
    vec3 color = phxAmbientAtmos(N, 1.0, ubo.ambientColor) * albedo.rgb
               + ndl * shadowF * ubo.sunColor * albedo.rgb;
    if (ubo.debugShadowMode == 1) { outColor = phxShadowOnly(shadowF); return; }
    color = phxAerialPerspective(color, vWorldPos - ubo.cameraWorld,
                                 ubo.sunDirection, ubo.sunColor, ubo.hazeHorizonColor, ubo.hazeZenithColor);
    outColor = vec4(color, 1.0);
}
