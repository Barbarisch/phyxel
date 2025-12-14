#version 450

layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in uint inPackedData;      // per-instance: packed position + face ID + future data
layout(location = 2) in uint inTextureIndex;    // per-instance texture atlas index (unused)

layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
    vec3 chunkBaseOffset;
} pushConstants;

void main() {
    // Extract chunk-relative position from packed data (5 bits each for x,y,z)
    uint chunkX = (inPackedData >> 0) & 0x1Fu;   // bits 0-4
    uint chunkY = (inPackedData >> 5) & 0x1Fu;   // bits 5-9
    uint chunkZ = (inPackedData >> 10) & 0x1Fu;  // bits 10-14
    
    // Extract face ID from packed data (3 bits)
    uint faceID = (inPackedData >> 15) & 0x7u;  // bits 15-17
    
    // Extract scale level and hierarchy data
    uint scaleLevel = (inPackedData >> 18) & 0x3u;      // bits 18-19: scale level
    uint subcubeEncoded = (inPackedData >> 20) & 0x3Fu; // bits 20-25: parent subcube position
    uint microcubeEncoded = (inPackedData >> 26) & 0x3Fu; // bits 26-31: microcube position
    
    // Decode subcube position
    uint subcubeLocalX = subcubeEncoded % 3u;
    uint subcubeLocalY = (subcubeEncoded / 3u) % 3u;
    uint subcubeLocalZ = subcubeEncoded / 9u;
    
    // Decode microcube position
    uint microcubeLocalX = microcubeEncoded % 3u;
    uint microcubeLocalY = (microcubeEncoded / 3u) % 3u;
    uint microcubeLocalZ = microcubeEncoded / 9u;
    
    // Calculate base position
    vec3 chunkRelativePos = vec3(float(chunkX), float(chunkY), float(chunkZ));
    vec3 basePos = pushConstants.chunkBaseOffset + chunkRelativePos;
    
    vec3 faceOffset = vec3(0.0);
    
    if (faceID == 0u) {        // Front face (+Z)
        faceOffset = vec3(float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u), 1.0);
    } else if (faceID == 1u) { // Back face (-Z)
        faceOffset = vec3(1.0 - float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u), 0.0);
    } else if (faceID == 2u) { // Right face (+X)
        faceOffset = vec3(1.0, float((vertexID >> 1) & 1u), 1.0 - float((vertexID >> 0) & 1u));
    } else if (faceID == 3u) { // Left face (-X)
        faceOffset = vec3(0.0, float((vertexID >> 1) & 1u), float((vertexID >> 0) & 1u));
    } else if (faceID == 4u) { // Top face (+Y)
        faceOffset = vec3(float((vertexID >> 0) & 1u), 1.0, 1.0 - float((vertexID >> 1) & 1u));
    } else if (faceID == 5u) { // Bottom face (-Y)
        faceOffset = vec3(float((vertexID >> 0) & 1u), 0.0, float((vertexID >> 1) & 1u));
    }
    
    vec3 worldPos;
    
    if (scaleLevel == 0u) {
        worldPos = basePos + faceOffset;
    } else if (scaleLevel == 1u) {
        const float SUBCUBE_SCALE = 1.0 / 3.0;
        vec3 subcubeOffset = vec3(float(subcubeLocalX), float(subcubeLocalY), float(subcubeLocalZ)) * SUBCUBE_SCALE;
        worldPos = basePos + subcubeOffset + (faceOffset * SUBCUBE_SCALE);
    } else if (scaleLevel == 2u) {
        const float SUBCUBE_SCALE = 1.0 / 3.0;
        const float MICROCUBE_SCALE = 1.0 / 9.0;
        vec3 subcubeOffset = vec3(float(subcubeLocalX), float(subcubeLocalY), float(subcubeLocalZ)) * SUBCUBE_SCALE;
        vec3 microcubeOffset = vec3(float(microcubeLocalX), float(microcubeLocalY), float(microcubeLocalZ)) * MICROCUBE_SCALE;
        worldPos = basePos + subcubeOffset + microcubeOffset + (faceOffset * MICROCUBE_SCALE);
    } else {
        worldPos = basePos + faceOffset;
    }
    
    gl_Position = pushConstants.lightSpaceMatrix * vec4(worldPos, 1.0);
}
