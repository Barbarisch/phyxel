#version 450

layout(location = 0) in uint vertexID;          // Cube corner ID (0–7)
layout(location = 1) in uint inPackedData;      // per-instance: packed position + face mask + future data
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
    // Extract cube-local offset (0 or 1 for x, y, z)
    // vertexID encoding: bit 0 = x, bit 1 = y, bit 2 = z
    float localX = float((vertexID >> 0) & 1u);
    float localY = float((vertexID >> 1) & 1u);
    float localZ = float((vertexID >> 2) & 1u);
    
    // Extract chunk-relative position from packed data (5 bits each for x,y,z)
    uint chunkX = (inPackedData >> 0) & 0x1Fu;   // bits 0-4
    uint chunkY = (inPackedData >> 5) & 0x1Fu;   // bits 5-9
    uint chunkZ = (inPackedData >> 10) & 0x1Fu;  // bits 10-14
    
    // Extract face mask from packed data (6 bits)
    uint faceMask = (inPackedData >> 15) & 0x3Fu; // bits 15-20
    
    // Future data available in bits 21-31 for subcube scaling, materials, etc.
    // uint futureData = (inPackedData >> 21) & 0x7FFu;
    
    // Calculate final world position
    vec3 chunkRelativePos = vec3(float(chunkX), float(chunkY), float(chunkZ));
    vec3 localOffset = vec3(localX, localY, localZ);
    vec3 worldPos = pushConstants.chunkBaseOffset + chunkRelativePos + localOffset;

    // Determine which face(s) this vertex belongs to based on its local coordinates
    // A vertex can belong to multiple faces (corner/edge vertices)
    bool onFrontFace  = (localZ == 1.0);  // +Z face
    bool onBackFace   = (localZ == 0.0);  // -Z face  
    bool onRightFace  = (localX == 1.0);  // +X face
    bool onLeftFace   = (localX == 0.0);  // -X face
    bool onTopFace    = (localY == 1.0);  // +Y face
    bool onBottomFace = (localY == 0.0);  // -Y face

    // Check if this vertex belongs to any visible face
    // Face bitfield: bit 0=front, 1=back, 2=right, 3=left, 4=top, 5=bottom
    bool frontVisible  = ((faceMask >> 0) & 1u) != 0u;
    bool backVisible   = ((faceMask >> 1) & 1u) != 0u;
    bool rightVisible  = ((faceMask >> 2) & 1u) != 0u;
    bool leftVisible   = ((faceMask >> 3) & 1u) != 0u;
    bool topVisible    = ((faceMask >> 4) & 1u) != 0u;
    bool bottomVisible = ((faceMask >> 5) & 1u) != 0u;

    bool vertexNeeded = (onFrontFace  && frontVisible)  ||
                        (onBackFace   && backVisible)   ||
                        (onRightFace  && rightVisible)  ||
                        (onLeftFace   && leftVisible)   ||
                        (onTopFace    && topVisible)    ||
                        (onBottomFace && bottomVisible);

    if (vertexNeeded) {
        gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
        fragColor = inInstanceColor;
    } else {
        // Move vertex far away to effectively cull it
        // This is a simple approach - more sophisticated would be geometry shader
        gl_Position = vec4(0.0, 0.0, -1000.0, 1.0);
        fragColor = vec3(0.0, 0.0, 0.0); // Black/transparent
    }
}
