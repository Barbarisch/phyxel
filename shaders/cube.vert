#version 450

layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in uint inPackedData;      // per-instance: packed position + face ID + future data
layout(location = 2) in vec3 inInstanceColor;   // per-instance color

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 chunkBaseOffset;  // World position of chunk origin (0,0,0) corner
} pushConstants;

layout(location = 0) out vec3 fragColor;  // pass to frag shader

void main() {
    // Extract chunk-relative position from packed data (5 bits each for x,y,z)
    uint chunkX = (inPackedData >> 0) & 0x1Fu;   // bits 0-4
    uint chunkY = (inPackedData >> 5) & 0x1Fu;   // bits 5-9
    uint chunkZ = (inPackedData >> 10) & 0x1Fu;  // bits 10-14
    
    // Extract face ID from packed data (3 bits)
    uint faceID = (inPackedData >> 15) & 0x7u;  // bits 15-17
    // Future data available in bits 18-31 for subcube scaling, materials, etc.
    
    // Calculate cube base position
    vec3 chunkRelativePos = vec3(float(chunkX), float(chunkY), float(chunkZ));
    vec3 cubeBasePos = pushConstants.chunkBaseOffset + chunkRelativePos;
    
    // Generate face vertices based on faceID and vertexID
    // Face IDs: 0=front(+Z), 1=back(-Z), 2=right(+X), 3=left(-X), 4=top(+Y), 5=bottom(-Y)
    // vertexID: 0-3 for the 4 corners of each face
    
    vec3 faceOffset = vec3(0.0);
    
    if (faceID == 0u) {        // Front face (+Z)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),  // x: 0 or 1
            float((vertexID >> 1) & 1u),  // y: 0 or 1
            1.0                           // z: always 1
        );
    } else if (faceID == 1u) { // Back face (-Z)
        faceOffset = vec3(
            1.0 - float((vertexID >> 0) & 1u),  // x: 1 or 0 (flipped for correct winding)
            float((vertexID >> 1) & 1u),        // y: 0 or 1
            0.0                                 // z: always 0
        );
    } else if (faceID == 2u) { // Right face (+X)
        faceOffset = vec3(
            1.0,                          // x: always 1
            float((vertexID >> 1) & 1u),  // y: 0 or 1
            1.0 - float((vertexID >> 0) & 1u)   // z: 1 or 0 (flipped for correct winding)
        );
    } else if (faceID == 3u) { // Left face (-X)
        faceOffset = vec3(
            0.0,                          // x: always 0
            float((vertexID >> 1) & 1u),  // y: 0 or 1
            float((vertexID >> 0) & 1u)   // z: 0 or 1
        );
    } else if (faceID == 4u) { // Top face (+Y)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),  // x: 0 or 1
            1.0,                          // y: always 1
            1.0 - float((vertexID >> 1) & 1u)   // z: 1 or 0 (flipped for correct winding)
        );
    } else if (faceID == 5u) { // Bottom face (-Y)
        faceOffset = vec3(
            float((vertexID >> 0) & 1u),  // x: 0 or 1
            0.0,                          // y: always 0
            float((vertexID >> 1) & 1u)   // z: 0 or 1
        );
    }
    
    vec3 worldPos = cubeBasePos + faceOffset;
    
    // CPU pre-filtering: Only vertices for visible faces are sent to GPU
    // No need for face visibility checking - all vertices here should be rendered
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    fragColor = inInstanceColor;
}
