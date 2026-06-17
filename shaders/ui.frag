#version 450

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec4 inColor;

layout (push_constant) uniform PushConstants {
    vec2 scale;
    vec2 translate;
    float mode;     // 0 = alpha-mask (R8 font/rect), 1 = RGBA image tinted
} pc;

// binding 0 is either the R8 font atlas (mode 0) or an RGBA image (mode 1),
// selected by the bound descriptor set per draw run.
layout (set = 0, binding = 0) uniform sampler2D tex;

layout (location = 0) out vec4 outColor;

void main() {
    if (pc.mode < 0.5) {
        float alpha = texture(tex, inUV).r;
        outColor = vec4(inColor.rgb, inColor.a * alpha);
    } else {
        outColor = texture(tex, inUV) * inColor;
    }
}
