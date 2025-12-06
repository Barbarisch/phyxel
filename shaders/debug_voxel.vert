#version 450

layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in uint inPackedData;      // per-instance: packed position + face ID + future data
layout(location = 2) in uint inTextureIndex;    // per-instance texture atlas index

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 chunkBaseOffset;  // World position of chunk origin (0,0,0) corner
    uint debugMode;        // Debug visualization mode (0=wireframe, 1=normals, 2=hierarchy)
} pushConstants;

layout(location = 0) out vec3 fragWorldPos;      // World position for fragment shader
layout(location = 1) out vec3 fragNormal;        // Normal vector for visualization
layout(location = 2) out vec3 fragDebugColor;    // Color based on debug mode
layout(location = 3) out flat uint debugMode;    // Pass debug mode to fragment shader

void main() {
    // Extract chunk-relative position from packed data (5 bits each for x,y,z)
    uint chunkX = (inPackedData >> 0) & 0x1Fu;   // bits 0-4
    uint chunkY = (inPackedData >> 5) & 0x1Fu;   // bits 5-9
    uint chunkZ = (inPackedData >> 10) & 0x1Fu;  // bits 10-14
    
    // Extract face ID from packed data (3 bits)
    uint faceID = (inPackedData >> 15) & 0x7u;  // bits 15-17
    
    // Extract scale level and hierarchy data
    uint scaleLevel = (inPackedData >> 18) & 0x3u;      // bits 18-19: scale level (0=cube, 1=subcube, 2=microcube)
    uint subcubeEncoded = (inPackedData >> 20) & 0x3Fu; // bits 20-25: parent subcube position
    uint microcubeEncoded = (inPackedData >> 26) & 0x3Fu; // bits 26-31: microcube position
    
    // Decode subcube position from 6-bit encoding (x + y*3 + z*9)
    uint subcubeLocalX = subcubeEncoded % 3u;
    uint subcubeLocalY = (subcubeEncoded / 3u) % 3u;
    uint subcubeLocalZ = subcubeEncoded / 9u;
    
    // Decode microcube position from 6-bit encoding
    uint microcubeLocalX = microcubeEncoded % 3u;
    uint microcubeLocalY = (microcubeEncoded / 3u) % 3u;
    uint microcubeLocalZ = microcubeEncoded / 9u;
    
    // Calculate base position
    vec3 chunkRelativePos = vec3(float(chunkX), float(chunkY), float(chunkZ));
    vec3 basePos = pushConstants.chunkBaseOffset + chunkRelativePos;
    
    // Generate face vertices and normals based on faceID
    vec3 faceOffset = vec3(0.0);
    vec3 normal = vec3(0.0);
    
    if (faceID == 0u) {        // Front face (+Z)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),
            float((vertexID >> 1) & 1u),
            1.0
        );
        normal = vec3(0.0, 0.0, 1.0);
    } else if (faceID == 1u) { // Back face (-Z)
        faceOffset = vec3(
            1.0 - float((vertexID >> 0) & 1u),
            float((vertexID >> 1) & 1u),
            0.0
        );
        normal = vec3(0.0, 0.0, -1.0);
    } else if (faceID == 2u) { // Right face (+X)
        faceOffset = vec3(
            1.0,
            float((vertexID >> 1) & 1u),
            1.0 - float((vertexID >> 0) & 1u)
        );
        normal = vec3(1.0, 0.0, 0.0);
    } else if (faceID == 3u) { // Left face (-X)
        faceOffset = vec3(
            0.0,
            float((vertexID >> 1) & 1u),
            float((vertexID >> 0) & 1u)
        );
        normal = vec3(-1.0, 0.0, 0.0);
    } else if (faceID == 4u) { // Top face (+Y)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),
            1.0,
            float((vertexID >> 1) & 1u)
        );
        normal = vec3(0.0, 1.0, 0.0);
    } else if (faceID == 5u) { // Bottom face (-Y)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),
            0.0,
            1.0 - float((vertexID >> 1) & 1u)
        );
        normal = vec3(0.0, -1.0, 0.0);
    }
    
    // Calculate scale based on hierarchy level
    float scale = 1.0;
    vec3 offset = vec3(0.0);
    
    if (scaleLevel == 1u) {  // Subcube
        scale = 1.0 / 3.0;
        offset = vec3(subcubeLocalX, subcubeLocalY, subcubeLocalZ) * scale;
    } else if (scaleLevel == 2u) {  // Microcube
        scale = 1.0 / 9.0;
        // First offset by subcube position (at 1/3 scale)
        vec3 subcubeOffset = vec3(subcubeLocalX, subcubeLocalY, subcubeLocalZ) * (1.0 / 3.0);
        // Then offset within subcube by microcube position (at 1/9 scale)
        vec3 microcubeOffset = vec3(microcubeLocalX, microcubeLocalY, microcubeLocalZ) * scale;
        offset = subcubeOffset + microcubeOffset;
    }
    
    // Apply scale to face offset and add hierarchy offset
    vec3 localPos = faceOffset * scale + offset;
    vec3 worldPos = basePos + localPos;
    
    // Output world position and normal
    fragWorldPos = worldPos;
    fragNormal = normal;
    
    // Generate debug color based on mode
    if (pushConstants.debugMode == 0u) {
        // Wireframe mode - color by face direction
        fragDebugColor = abs(normal);
    } else if (pushConstants.debugMode == 1u) {
        // Normal visualization - color = normal mapped to 0-1 range
        fragDebugColor = normal * 0.5 + 0.5;
    } else if (pushConstants.debugMode == 2u) {
        // Hierarchy visualization - color by scale level
        if (scaleLevel == 0u) {
            fragDebugColor = vec3(1.0, 0.0, 0.0);  // Red for cubes
        } else if (scaleLevel == 1u) {
            fragDebugColor = vec3(0.0, 1.0, 0.0);  // Green for subcubes
        } else {
            fragDebugColor = vec3(0.0, 0.0, 1.0);  // Blue for microcubes
        }
    } else {
        // Default: UV coordinates visualization
        vec2 uv = vec2(float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
        fragDebugColor = vec3(uv, 0.0);
    }
    
    debugMode = pushConstants.debugMode;
    
    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
