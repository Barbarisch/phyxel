#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D samplerColor;

layout(push_constant) uniform PushConstants {
    int   horizontal;
    float threshold;   // bright-pass cutoff; >0 ONLY on the first pass
    float knee;        // soft shoulder width, so bloom fades in instead of popping
    float radiusScale; // multiplies the texel offsets; 1.0 = shipped width (see below)
    float clampMul;    // firefly clamp: taps may exceed threshold by at most this. Huge = disabled.
} pushConsts;

// U5 (docs/UnifiedLightingPlan.md) -- FIREFLY CLAMP.
//
// PostProcessor.h records bloom as BROKEN (spots/blotches, confirmed 2026-08-15) and names the
// suspect: isolated very bright pixels clear the bright-pass, each becomes a blob, and the
// half-res blur doubles its width. The scene target is linear HDR, so one pixel can carry enormous
// radiance -- the sun's disc is drawn at 24x solar irradiance, and specular hits and the known
// grass/character sub-pixel speckle spike similarly. The bright-pass had NO upper bound.
//
// Clamping each tap to a multiple of the threshold makes bloom a function of bright AREA rather
// than peak VALUE, which is what a lens does: a large bright surface blooms strongly, a lone
// sub-pixel spark barely at all. Same insight as the Karis average, applied at the tap. It does
// NOT stop the sun or a hearth blooming -- those are many adjacent bright pixels, so the clamped
// taps still sum to a strong smooth glow.
//
// !! UNVERIFIED AGAINST THE ACTUAL DEFECT. Measured on two worlds -- flat LightingGates, and the
// vegetated DenseForestPerf which contains the named suspects (stars, airglow, grass speckle):
//     with clamp:    89/192 cells carried the bloom, top-3 share 9.0%
//     clamp OFF:     90/192 cells,                   top-3 share 10.2%
// Statistically identical, and neither shows the concentration blotching would produce. THE
// REPORTED DEFECT DID NOT REPRODUCE, so this clamp is a defensible safeguard, NOT a demonstrated
// fix. Bloom therefore stays default-OFF. To close this out, the scene/pose that showed the
// blotching is needed.
// (now a push constant: pushConsts.clampMul, so the clamp can be A/B'd live)

// Bright-pass with a soft knee. Without this the blur input is the WHOLE frame, so compositing it
// back roughly doubles the image -- which is exactly why bloom was disabled rather than debugged.
// Applied on the first pass only; re-applying it every iteration would erode the highlight away.
vec3 phxBrightPass(vec3 c, float threshold, float knee) {
    // Clamp BEFORE the knee: the knee decides how much of a tap survives, and a firefly that is
    // clamped afterwards has already skewed the weight it was given.
    float peak = max(c.r, max(c.g, c.b));
    float ceilv = threshold * max(pushConsts.clampMul, 1.0);
    if (peak > ceilv && peak > 1e-5) c *= ceilv / peak;

    float b = max(c.r, max(c.g, c.b));
    if (knee <= 0.0) return c * step(threshold, b);
    // quadratic shoulder over [threshold-knee, threshold+knee]
    float soft = clamp((b - threshold + knee) / (2.0 * knee), 0.0, 1.0);
    float w = max(b - threshold, soft * soft * knee) / max(b, 1e-5);
    return c * clamp(w, 0.0, 1.0);
}

void main() {
    // Gaussian weights
    float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    
    // BLUR WIDTH — the live A/B for the "spots, not a glow" report.
    // The chain is 5 passes per axis of this 9-tap gaussian, run at HALF resolution. Per pass
    // sigma is ~1.69 texels; variance adds across passes, so sigma_total ~ 1.69*sqrt(5) ~ 3.8
    // texels ~ 7.6 screen px, i.e. a halo of roughly +/-15 px on a 1600px frame. That is ~2% of
    // the frame width: every bright object keeps its own shape with a tight soft edge — a SPOT —
    // instead of spreading into a glow. Real bloom reaches a radius of hundreds of pixels via a
    // MIP PYRAMID, because adding passes at one resolution grows radius only as sqrt(n).
    // radiusScale widens the taps without resizing the buffers, so the hypothesis can be tested
    // live. It is a DIAGNOSTIC, not the fix: very wide taps at one resolution undersample and will
    // show their own banding — which is itself evidence for the pyramid.
    vec2 tex_offset = (1.0 / textureSize(samplerColor, 0)) * max(pushConsts.radiusScale, 1.0);
    vec3 result = texture(samplerColor, inUV).rgb * weight[0]; // current fragment's contribution

    // First pass only: keep just the highlights.
    if (pushConsts.threshold > 0.0) {
        vec3 c0 = texture(samplerColor, inUV).rgb;
        result = phxBrightPass(c0, pushConsts.threshold, pushConsts.knee) * weight[0];
    }
    
    if(pushConsts.horizontal == 1)
    {
        for(int i = 1; i < 5; ++i)
        {
            vec3 sp = texture(samplerColor, inUV + vec2(tex_offset.x * i, 0.0)).rgb;
            vec3 sn = texture(samplerColor, inUV - vec2(tex_offset.x * i, 0.0)).rgb;
            if (pushConsts.threshold > 0.0) {
                sp = phxBrightPass(sp, pushConsts.threshold, pushConsts.knee);
                sn = phxBrightPass(sn, pushConsts.threshold, pushConsts.knee);
            }
            result += (sp + sn) * weight[i];
        }
    }
    else
    {
        for(int i = 1; i < 5; ++i)
        {
            vec3 sp = texture(samplerColor, inUV + vec2(0.0, tex_offset.y * i)).rgb;
            vec3 sn = texture(samplerColor, inUV - vec2(0.0, tex_offset.y * i)).rgb;
            if (pushConsts.threshold > 0.0) {
                sp = phxBrightPass(sp, pushConsts.threshold, pushConsts.knee);
                sn = phxBrightPass(sn, pushConsts.threshold, pushConsts.knee);
            }
            result += (sp + sn) * weight[i];
        }
    }
    outColor = vec4(result, 1.0);
}
