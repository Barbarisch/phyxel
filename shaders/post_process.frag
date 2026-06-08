#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D sceneColor;
layout (set = 0, binding = 1) uniform sampler2D bloomBlur;
layout (set = 0, binding = 2) uniform sampler2D ssaoTex;
layout (set = 0, binding = 3) uniform sampler2D oitAccum;   // OIT accumulation (RGBA16F)
layout (set = 0, binding = 4) uniform sampler2D oitReveal;  // OIT reveal factor (R8_UNORM)

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

    outColor = vec4(color, 1.0);
}
