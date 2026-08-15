#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // phxTonemap — THE single tone map for the whole frame

// Tone mapping lives HERE, not in the scene shaders, and that ordering matters twice over:
//   1. It runs AFTER compositing. Ten shaders each tone-mapping their own output meant every
//      blended pass (water, OIT glass) was tone-mapped BEFORE it was blended, which is simply the
//      wrong order -- you cannot correctly blend two independently compressed images.
//   2. It leaves the scene target as linear HDR. A per-shader AgX clamped everything to ~1.0, so
//      there were no highlights above white left for bloom to find.

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D sceneColor;
layout (set = 0, binding = 1) uniform sampler2D bloomBlur;
layout (set = 0, binding = 2) uniform sampler2D ssaoTex;
layout (set = 0, binding = 3) uniform sampler2D oitAccum;   // OIT accumulation (RGBA16F)
layout (set = 0, binding = 4) uniform sampler2D oitReveal;  // OIT reveal factor (R8_UNORM)

// Exposure + curve arrive as push constants rather than a UBO: two scalars, changed per frame by
// POST /api/debug/tonemap, and this pass owns no descriptor set beyond its samplers.
layout(push_constant) uniform GradePush {
    float exposure;
    int   curve;    // 0 = linear (A/B control), 1 = AgX
} grade;

// EDITOR-PARITY COMPOSITE.
//
// The editor viewport displays the RAW offscreen scene texture (linear,
// R16G16B16A16F) — the swapchain post-process pass is hidden under the
// dockspace there, so anything done here is only ever visible in STANDALONE
// games. Games therefore must composite to exactly the editor's look:
// scene color + OIT transparency, nothing else. The swapchain is
// VK_FORMAT_B8G8R8A8_SRGB, so the hardware applies the linear->sRGB encode;
// do NOT add a manual pow(1/2.2) here (double gamma = washed-out white).
//
// Disabled legacy effects (kept bound so the pipeline layout is unchanged):
//  - bloom add: blur input has NO brightness threshold, so it added a blurred
//    copy of the whole frame (~2x brightness, washed-out).
//  - SSAO multiply: normal reconstruction degenerates at grazing angles,
//    drawing a dark band along the ground horizon (screen center).
//  - Reinhard tonemap: the editor reference look has none.
// Re-enable deliberately (thresholded bloom, fixed SSAO) once they match the
// editor preview too — they must be visible during authoring, not only in
// packaged games.
void main()
{
    vec3 color = texture(sceneColor, inUV).rgb;

    // OIT composite: blend transparent geometry onto opaque scene
    vec4 accum  = texture(oitAccum, inUV);
    float reveal = texture(oitReveal, inUV).r;
    if (accum.a > 1e-5) {
        vec3 transparentColor = accum.rgb / accum.a;
        // reveal = product of (1 - alpha) across all transparent layers
        // 0 = fully covered by transparent, 1 = nothing transparent
        color = mix(transparentColor, color, reveal);
    }

    outColor = vec4(phxTonemap(color, grade.exposure, grade.curve), 1.0);
}
