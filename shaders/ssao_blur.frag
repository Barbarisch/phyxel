#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outOcclusion;

layout(set = 0, binding = 0) uniform sampler2D ssaoTex;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoTex, 0));
    float result = 0.0;
    // 4×4 box blur
    for (int x = -2; x <= 1; x++) {
        for (int y = -2; y <= 1; y++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(ssaoTex, inUV + offset).r;
        }
    }
    outOcclusion = result / 16.0;
}
