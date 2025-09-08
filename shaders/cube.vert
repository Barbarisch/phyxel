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
} pushConstants;

layout(location = 0) out flat uint textureIndex;  // pass texture index to frag shader
layout(location = 1) out vec2 texCoord;           // pass texture coordinates to frag shader

void main() {
    // Extract chunk-relative position from packed data (5 bits each for x,y,z)
    uint chunkX = (inPackedData >> 0) & 0x1Fu;   // bits 0-4
    uint chunkY = (inPackedData >> 5) & 0x1Fu;   // bits 5-9
    uint chunkZ = (inPackedData >> 10) & 0x1Fu;  // bits 10-14
    
    // Extract face ID from packed data (3 bits)
    uint faceID = (inPackedData >> 15) & 0x7u;  // bits 15-17
    
    // Extract subcube data from bits 18-31
    uint subcubeData = (inPackedData >> 18) & 0x3FFFu;  // bits 18-31 (14 bits total)
    bool isSubcube = (subcubeData & 0x1u) != 0u;        // bit 18: subcube flag
    uint subcubeLocalX = (subcubeData >> 1) & 0x3u;     // bits 19-20: local X (0-2)
    uint subcubeLocalY = (subcubeData >> 3) & 0x3u;     // bits 21-22: local Y (0-2)
    uint subcubeLocalZ = (subcubeData >> 5) & 0x3u;     // bits 23-24: local Z (0-2)
    // bits 25-31 reserved for future use
    
    // Calculate base position (parent cube position for subcubes, cube position for regular cubes)
    vec3 chunkRelativePos = vec3(float(chunkX), float(chunkY), float(chunkZ));
    vec3 basePos = pushConstants.chunkBaseOffset + chunkRelativePos;
    
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
    
    vec3 worldPos;
    
    if (isSubcube) {
        // For subcubes: apply 1/3 scaling and local positioning within parent cube
        const float SUBCUBE_SCALE = 1.0 / 3.0;
        
        // Calculate subcube offset within parent cube (each subcube is 1/3 size)
        vec3 subcubeOffset = vec3(
            float(subcubeLocalX) * SUBCUBE_SCALE,
            float(subcubeLocalY) * SUBCUBE_SCALE,
            float(subcubeLocalZ) * SUBCUBE_SCALE
        );
        
        // Scale the face offset to subcube size and add to base position + subcube offset
        worldPos = basePos + subcubeOffset + (faceOffset * SUBCUBE_SCALE);
    } else {
        // For regular cubes: use standard 1.0 scale
        worldPos = basePos + faceOffset;
    }
    
    // Calculate UV coordinates for texture mapping
    // UV coordinates must match the vertex generation pattern for each face
    vec2 uv = vec2(0.0);
    
    if (faceID == 0u) {        // Front face (+Z)
        // Vertices: (0,0,1), (1,0,1), (1,1,1), (0,1,1)
        uv = vec2(float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    } else if (faceID == 1u) { // Back face (-Z)
        // Vertices: (1,0,0), (0,0,0), (0,1,0), (1,1,0) - x flipped
        uv = vec2(1.0 - float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    } else if (faceID == 2u) { // Right face (+X)
        // Vertices: (1,0,1), (1,0,0), (1,1,0), (1,1,1) - z flipped
        uv = vec2(1.0 - float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    } else if (faceID == 3u) { // Left face (-X)
        // Vertices: (0,0,0), (0,0,1), (0,1,1), (0,1,0)
        uv = vec2(float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    } else if (faceID == 4u) { // Top face (+Y)
        // Vertices: (0,1,1), (1,1,1), (1,1,0), (0,1,0) - z flipped
        uv = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    } else if (faceID == 5u) { // Bottom face (-Y)
        // Vertices: (0,0,0), (1,0,0), (1,0,1), (0,0,1)
        uv = vec2(float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    }
    
    // CPU pre-filtering: Only vertices for visible faces are sent to GPU
    // No need for face visibility checking - all vertices here should be rendered
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    
    // Pass texture data to fragment shader
    textureIndex = inTextureIndex;
    texCoord = uv;
}
