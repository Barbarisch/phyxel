#version 450

layout(location = 0) in vec3 fragColor;   // from vertex shader

layout(location = 0) out vec4 outColor;   // output color

void main() {
    outColor = vec4(fragColor, 1.0);      // apply color with full opacity
}
