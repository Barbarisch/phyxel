#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragDebugColor;
layout(location = 3) in flat uint debugMode;

layout(location = 0) out vec4 outColor;

void main() {
    // Debug visualization modes:
    // 0 = Wireframe/face direction (colored by axis)
    // 1 = Normal vectors (RGB mapped from normal)
    // 2 = Hierarchy levels (red=cube, green=subcube, blue=microcube)
    // 3 = UV coordinates
    // 4 = Emissive/Flags (yellow=emissive, dark=normal)
    
    if (debugMode == 0u) {
        // Wireframe mode - solid color based on face direction
        // Add subtle lighting to make geometry visible
        vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
        float lighting = max(dot(fragNormal, lightDir), 0.3);
        outColor = vec4(fragDebugColor * lighting, 1.0);
    } else if (debugMode == 1u) {
        // Normal visualization - directly output normal as color
        outColor = vec4(fragDebugColor, 1.0);
    } else if (debugMode == 2u) {
        // Hierarchy visualization - solid colors by level
        outColor = vec4(fragDebugColor, 1.0);
    } else {
        // UV/other debug modes
        outColor = vec4(fragDebugColor, 1.0);
    }
    
    // Optional: Add grid lines effect for wireframe mode
    if (debugMode == 0u) {
        // Calculate barycentric-like coordinates for edge detection
        vec3 pos = fract(fragWorldPos);
        float edge = min(min(pos.x, 1.0 - pos.x), min(pos.y, 1.0 - pos.y));
        edge = min(edge, min(pos.z, 1.0 - pos.z));
        
        // Highlight edges
        if (edge < 0.02) {
            outColor = vec4(1.0, 1.0, 1.0, 1.0);
        }
    }
}
