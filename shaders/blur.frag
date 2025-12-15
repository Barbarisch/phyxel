#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D samplerColor;

layout(push_constant) uniform PushConstants {
    int horizontal;
} pushConsts;

void main() {
    // Gaussian weights
    float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    
    vec2 tex_offset = 1.0 / textureSize(samplerColor, 0); // gets size of single texel
    vec3 result = texture(samplerColor, inUV).rgb * weight[0]; // current fragment's contribution
    
    if(pushConsts.horizontal == 1)
    {
        for(int i = 1; i < 5; ++i)
        {
            result += texture(samplerColor, inUV + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
            result += texture(samplerColor, inUV - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
        }
    }
    else
    {
        for(int i = 1; i < 5; ++i)
        {
            result += texture(samplerColor, inUV + vec2(0.0, tex_offset.y * i)).rgb * weight[i];
            result += texture(samplerColor, inUV - vec2(0.0, tex_offset.y * i)).rgb * weight[i];
        }
    }
    outColor = vec4(result, 1.0);
}
