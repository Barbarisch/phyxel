#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D sceneColor;
layout (set = 0, binding = 1) uniform sampler2D bloomBlur;

void main() 
{
    vec3 color = texture(sceneColor, inUV).rgb;
    vec3 bloom = texture(bloomBlur, inUV).rgb;
    
    // Add bloom
    color += bloom;
    
    // Simple Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(color, 1.0);
}
