#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D samplerColor;

layout(push_constant) uniform PushConstants {
    int   horizontal;
    float threshold;   // bright-pass cutoff; >0 ONLY on the first pass
    float knee;        // soft shoulder width, so bloom fades in instead of popping
} pushConsts;

// ⛔ BLOOM IS BROKEN -- this bright-pass is the prime suspect. It produces SPOTS across the frame
// rather than a smooth glow, most likely because isolated very bright pixels survive it and each
// becomes a blob (fireflies), widened further by the half-res blur. A per-tap clamp here is the
// first thing to try. Bloom ships disabled; see PostProcessor.h.
//
// Bright-pass with a soft knee. Without this the blur input is the WHOLE frame, so compositing it
// back roughly doubles the image -- which is exactly why bloom was disabled rather than debugged.
// Applied on the first pass only; re-applying it every iteration would erode the highlight away.
vec3 phxBrightPass(vec3 c, float threshold, float knee) {
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
    
    vec2 tex_offset = 1.0 / textureSize(samplerColor, 0); // gets size of single texel
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
