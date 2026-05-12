#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D sceneColor;
layout (set = 0, binding = 1) uniform sampler2D bloomBlur;
layout (set = 0, binding = 2) uniform sampler2D ssaoTex;
layout (set = 0, binding = 3) uniform sampler2D oitAccum;   // OIT accumulation (RGBA16F)
layout (set = 0, binding = 4) uniform sampler2D oitReveal;  // OIT reveal factor (R8_UNORM)

void main() 
{
    vec3 color = texture(sceneColor, inUV).rgb;
    vec3 bloom = texture(bloomBlur, inUV).rgb;
    float ao   = texture(ssaoTex, inUV).r;

    // OIT composite: blend transparent geometry onto opaque scene
    vec4 accum  = texture(oitAccum, inUV);
    float reveal = texture(oitReveal, inUV).r;
    if (accum.a > 1e-5) {
        vec3 transparentColor = accum.rgb / accum.a;
        // reveal = product of (1 - alpha) across all transparent layers
        // 0 = fully covered by transparent, 1 = nothing transparent
        color = mix(transparentColor, color, reveal);
    }
    
    // Add bloom
    color += bloom;

    // Apply SSAO (ambient occlusion darkens indirect light)
    color *= ao;
    
    // Simple Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(color, 1.0);
}
