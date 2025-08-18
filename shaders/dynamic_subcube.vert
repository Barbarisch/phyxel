#version 450

layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in vec3 inWorldPosition;   // per-instance: world position of subcube
layout(location = 2) in vec3 inInstanceColor;   // per-instance color
layout(location = 3) in uint inFaceID;          // per-instance: face ID (0-5)
layout(location = 4) in float inScale;          // per-instance: scale factor (1/3 for subcubes, 1.0 for cubes)

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec3 fragColor;

void main() {
    // Use per-instance scale to maintain correct subcube size
    const float SUBCUBE_SCALE = inScale;
    
    // Generate face vertices based on faceID and vertexID
    vec3 faceOffset = vec3(0.0);
    
    if (inFaceID == 0u) {        // Front face (+Z)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),  // x: 0 or 1
            float((vertexID >> 1) & 1u),  // y: 0 or 1
            1.0                           // z: always 1
        );
    } else if (inFaceID == 1u) { // Back face (-Z)
        faceOffset = vec3(
            1.0 - float((vertexID >> 0) & 1u),
            float((vertexID >> 1) & 1u),
            0.0
        );
    } else if (inFaceID == 2u) { // Right face (+X)
        faceOffset = vec3(
            1.0,
            float((vertexID >> 1) & 1u),
            1.0 - float((vertexID >> 0) & 1u)
        );
    } else if (inFaceID == 3u) { // Left face (-X)
        faceOffset = vec3(
            0.0,
            float((vertexID >> 1) & 1u),
            float((vertexID >> 0) & 1u)
        );
    } else if (inFaceID == 4u) { // Top face (+Y)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),
            1.0,
            1.0 - float((vertexID >> 1) & 1u)
        );
    } else if (inFaceID == 5u) { // Bottom face (-Y)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),
            0.0,
            float((vertexID >> 1) & 1u)
        );
    }
    
    // Apply scale to face offset and center it around the world position
    // faceOffset ranges from 0-1, so we need to center it: (faceOffset - 0.5) * scale
    vec3 scaledOffset = (faceOffset - 0.5) * SUBCUBE_SCALE;
    
    // Use the actual physics position from inWorldPosition as center
    vec3 worldPos = inWorldPosition + scaledOffset;
    
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    fragColor = vec3(0.0, 1.0, 0.0); // BRIGHT GREEN for testing - physics position
}
