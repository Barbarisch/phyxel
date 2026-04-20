#version 450
//
// static_voxel.vert — Vertex shader for static chunk voxels (cubes, subcubes, microcubes).
//
// Renders voxels baked into 32x32x32 chunks. Each instance is ONE face of a voxel.
// The CPU culls occluded faces and packs per-face data into 8-byte InstanceData.
//
// Voxel sizes:
//   scaleLevel 0 = cube      (1.0  scale) — full texture tile
//   scaleLevel 1 = subcube   (1/3  scale) — 1/3 of texture, offset by subcube grid pos (0-2)
//   scaleLevel 2 = microcube (1/9  scale) — 1/9 of texture, offset by subcube + microcube grid pos
//
// Texture mapping: each voxel face shows only its portion of the parent cube's texture.
// Subcube/microcube grid positions are packed into bits 20-31 of inPackedData
// (6-bit encoded as x + y*3 + z*9 for each level). Per-face axis mapping and
// Y-flips ensure UV continuity across adjacent voxels of the same parent cube.
//
// Binding 0 (per-vertex):   vertexID 0-3 for quad corners
// Binding 1 (per-instance): InstanceData (8 bytes) — packed position, face, scale, grid positions
//

layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in uint inPackedData;      // per-instance: packed position + face ID + future data
layout(location = 2) in uint inTextureIndex;    // per-instance texture atlas index
layout(location = 3) in uint inFlags;           // per-instance flags (emissive, etc.)

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    uint numInstances;
    float ambientLight;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 chunkBaseOffset;  // World position of chunk origin (0,0,0) corner
} pushConstants;

layout(location = 0) out flat uint textureIndex;  // pass texture index to frag shader
layout(location = 1) out vec2 texCoord;           // pass texture coordinates to frag shader
layout(location = 2) out vec4 shadowCoord;        // pass shadow coordinates to frag shader
layout(location = 3) out flat uint flags;         // pass flags to frag shader
layout(location = 4) out vec3 outNormal;          // pass normal to frag shader
layout(location = 5) out vec3 outWorldPos;        // pass world position to frag shader

void main() {
    // Extract chunk-relative position from packed data (5 bits each for x,y,z)
    uint chunkX = (inPackedData >> 0) & 0x1Fu;   // bits 0-4
    uint chunkY = (inPackedData >> 5) & 0x1Fu;   // bits 5-9
    uint chunkZ = (inPackedData >> 10) & 0x1Fu;  // bits 10-14
    
    // Extract face ID from packed data (3 bits)
    uint faceID = (inPackedData >> 15) & 0x7u;  // bits 15-17
    
    // Extract scale level and hierarchy data (NEW MICROCUBE SUPPORT)
    uint scaleLevel = (inPackedData >> 18) & 0x3u;      // bits 18-19: scale level (0=cube, 1=subcube, 2=microcube)
    uint subcubeEncoded = (inPackedData >> 20) & 0x3Fu; // bits 20-25: parent subcube position (encoded 3x3x3)
    uint microcubeEncoded = (inPackedData >> 26) & 0x3Fu; // bits 26-31: microcube position (encoded 3x3x3)
    
    // Decode subcube position from 6-bit encoding (x + y*3 + z*9)
    uint subcubeLocalX = subcubeEncoded % 3u;
    uint subcubeLocalY = (subcubeEncoded / 3u) % 3u;
    uint subcubeLocalZ = subcubeEncoded / 9u;
    
    // Decode microcube position from 6-bit encoding
    uint microcubeLocalX = microcubeEncoded % 3u;
    uint microcubeLocalY = (microcubeEncoded / 3u) % 3u;
    uint microcubeLocalZ = microcubeEncoded / 9u;
    
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
    float scale;
    
    if (scaleLevel == 0u) {
        // Regular cube (1.0 scale)
        scale = 1.0;
        worldPos = basePos + faceOffset;
        
    } else if (scaleLevel == 1u) {
        // Subcube (1/3 scale)
        const float SUBCUBE_SCALE = 1.0 / 3.0;
        scale = SUBCUBE_SCALE;
        
        // Calculate subcube offset within parent cube
        vec3 subcubeOffset = vec3(
            float(subcubeLocalX) * SUBCUBE_SCALE,
            float(subcubeLocalY) * SUBCUBE_SCALE,
            float(subcubeLocalZ) * SUBCUBE_SCALE
        );
        
        worldPos = basePos + subcubeOffset + (faceOffset * SUBCUBE_SCALE);
        
    } else if (scaleLevel == 2u) {
        // Microcube (1/9 scale)
        const float SUBCUBE_SCALE = 1.0 / 3.0;
        const float MICROCUBE_SCALE = 1.0 / 9.0;
        scale = MICROCUBE_SCALE;
        
        // Calculate subcube offset within parent cube
        vec3 subcubeOffset = vec3(
            float(subcubeLocalX) * SUBCUBE_SCALE,
            float(subcubeLocalY) * SUBCUBE_SCALE,
            float(subcubeLocalZ) * SUBCUBE_SCALE
        );
        
        // Calculate microcube offset within subcube
        vec3 microcubeOffset = vec3(
            float(microcubeLocalX) * MICROCUBE_SCALE,
            float(microcubeLocalY) * MICROCUBE_SCALE,
            float(microcubeLocalZ) * MICROCUBE_SCALE
        );
        
        worldPos = basePos + subcubeOffset + microcubeOffset + (faceOffset * MICROCUBE_SCALE);
        
    } else {
        // Reserved scale level (fallback to cube)
        scale = 1.0;
        worldPos = basePos + faceOffset;
    }
    
    // Calculate UV coordinates for texture mapping
    // UV coordinates must match the vertex generation pattern for each face
    vec2 uv = vec2(0.0);
    
    // Calculate shadow coordinates
    // Transform world position to light space
    // Vulkan clip space is [0,1] for Z, but [-1,1] for XY.
    // We need to transform from clip space [-1,1] to texture space [0,1]
    const mat4 biasMat = mat4( 
        0.5, 0.0, 0.0, 0.0,
        0.0, 0.5, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.5, 0.5, 0.0, 1.0 
    );
    shadowCoord = biasMat * ubo.lightSpaceMatrix * vec4(worldPos, 1.0);

    // Calculate normal based on faceID
    if (faceID == 0u) outNormal = vec3(0.0, 0.0, 1.0);       // Front (+Z)
    else if (faceID == 1u) outNormal = vec3(0.0, 0.0, -1.0); // Back (-Z)
    else if (faceID == 2u) outNormal = vec3(1.0, 0.0, 0.0);  // Right (+X)
    else if (faceID == 3u) outNormal = vec3(-1.0, 0.0, 0.0); // Left (-X)
    else if (faceID == 4u) outNormal = vec3(0.0, 1.0, 0.0);  // Top (+Y)
    else if (faceID == 5u) outNormal = vec3(0.0, -1.0, 0.0); // Bottom (-Y)
    else outNormal = vec3(0.0, 1.0, 0.0); // Default
    
    // Calculate base UV coordinates for the face (0.0 to 1.0 range)
    vec2 baseUV = vec2(0.0);
    
    if (faceID == 0u) {        // Front face (+Z) - North - looks good with flip
        // Vertices: (0,0,1), (1,0,1), (1,1,1), (0,1,1)
        baseUV = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    } else if (faceID == 1u) { // Back face (-Z) - South - looks good
        // Vertices: (1,0,0), (0,0,0), (0,1,0), (1,1,0) - x flipped
        baseUV = vec2(1.0 - float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    } else if (faceID == 2u) { // Right face (+X) - East - 180 degree rotation
        // Vertices: (1,0,1), (1,0,0), (1,1,0), (1,1,1) - z flipped
        // 180 degree rotation: flip both U and V
        baseUV = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    } else if (faceID == 3u) { // Left face (-X) - West - looks good
        // Vertices: (0,0,0), (0,0,1), (0,1,1), (0,1,0)
        baseUV = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    } else if (faceID == 4u) { // Top face (+Y) - horizontal mirror
        // Vertices: (0,1,1), (1,1,1), (1,1,0), (0,1,0) - z flipped
        baseUV = vec2(1.0 - float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    } else if (faceID == 5u) { // Bottom face (-Y) - looks good
        // Vertices: (0,0,0), (1,0,0), (1,0,1), (0,0,1)
        baseUV = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    }
    
    // Apply texture coordinate scaling based on scale level
    if (scaleLevel == 0u) {
        // Regular cube: use full texture
        uv = baseUV;
        
    } else if (scaleLevel == 1u) {
        // Subcube: modify UV to sample 1/3 of texture (6x6 out of 18x18)
        const float SUBCUBE_UV_SCALE = 1.0 / 3.0;
        
        // Map the 3x3x3 subcube grid to 2D texture coordinates based on face orientation
        vec2 subcubeGridPos = vec2(0.0);
        
        if (faceID == 0u) {        // North (Front) - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalY));
        } else if (faceID == 1u) { // South (Back) - works correctly
            subcubeGridPos = vec2(float(subcubeLocalX), float(subcubeLocalY));
        } else if (faceID == 2u) { // East (Right) - both X and Y flipped
            subcubeGridPos = vec2(float(2u - subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (faceID == 3u) { // West (Left) - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (faceID == 4u) { // Top - both X and Y flipped  
            subcubeGridPos = vec2(float(2u - subcubeLocalX), float(2u - subcubeLocalZ));
        } else if (faceID == 5u) { // Bottom - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalZ));
        }
        
        // Scale the base UV to subcube size and add offset for this subcube's position
        uv = (baseUV * SUBCUBE_UV_SCALE) + (subcubeGridPos * SUBCUBE_UV_SCALE);
        
    } else if (scaleLevel == 2u) {
        // Microcube: modify UV to sample 1/9 of texture (2x2 out of 18x18)
        const float SUBCUBE_UV_SCALE = 1.0 / 3.0;
        const float MICROCUBE_UV_SCALE = 1.0 / 9.0;
        
        // First, get the subcube's UV region
        vec2 subcubeGridPos = vec2(0.0);
        if (faceID == 0u) {
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalY));
        } else if (faceID == 1u) {
            subcubeGridPos = vec2(float(subcubeLocalX), float(subcubeLocalY));
        } else if (faceID == 2u) {
            subcubeGridPos = vec2(float(2u - subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (faceID == 3u) {
            subcubeGridPos = vec2(float(subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (faceID == 4u) {
            subcubeGridPos = vec2(float(2u - subcubeLocalX), float(2u - subcubeLocalZ));
        } else if (faceID == 5u) {
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalZ));
        }
        
        // Then, get the microcube's position within that subcube
        vec2 microcubeGridPos = vec2(0.0);
        if (faceID == 0u) {
            microcubeGridPos = vec2(float(microcubeLocalX), float(2u - microcubeLocalY));
        } else if (faceID == 1u) {
            microcubeGridPos = vec2(float(microcubeLocalX), float(microcubeLocalY));
        } else if (faceID == 2u) {
            microcubeGridPos = vec2(float(2u - microcubeLocalZ), float(2u - microcubeLocalY));
        } else if (faceID == 3u) {
            microcubeGridPos = vec2(float(microcubeLocalZ), float(2u - microcubeLocalY));
        } else if (faceID == 4u) {
            microcubeGridPos = vec2(float(2u - microcubeLocalX), float(2u - microcubeLocalZ));
        } else if (faceID == 5u) {
            microcubeGridPos = vec2(float(microcubeLocalX), float(2u - microcubeLocalZ));
        }
        
        // Combine: subcube offset + microcube offset within subcube
        vec2 subcubeUVBase = subcubeGridPos * SUBCUBE_UV_SCALE;
        vec2 microcubeUVOffset = microcubeGridPos * MICROCUBE_UV_SCALE;
        uv = (baseUV * MICROCUBE_UV_SCALE) + subcubeUVBase + microcubeUVOffset;
        
    } else {
        // Reserved: default to full texture
        uv = baseUV;
    }
    
    // CPU pre-filtering: Only vertices for visible faces are sent to GPU
    // No need for face visibility checking - all vertices here should be rendered
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    outWorldPos = worldPos;
    
    // Pass texture data to fragment shader
    textureIndex = inTextureIndex;
    texCoord = uv;
    flags = inFlags;
}
