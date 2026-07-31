#version 450

// C2.1 (docs/ContinuousLodPlan.md): gl_DrawIDARB lets ONE multidraw cover every chunk in an
// arena block, by indexing per-draw data instead of re-pushing constants per chunk (an indirect
// draw cannot vary push constants or vertex bindings). Requires the shaderDrawParameters FEATURE
// to be enabled on the device -- see VulkanDevice C2.0b; availability by API version is NOT
// sufficient and a shader built against a device that did not enable it is invalid.
#extension GL_ARB_shader_draw_parameters : require

layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in uint inPackedData;      // per-instance: packed position + face ID + future data
layout(location = 2) in uint inTextureIndex;    // per-instance texture atlas index (unused)
layout(location = 4) in uint inLight;           // per-instance: fine-face merge extents in bits 16-31
                                                // (the pipeline supplies all 7 attributes; ShadowMap.cpp)

layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
    vec3 chunkBaseOffset;   // legacy per-chunk origin (one draw per chunk)
    uint useChunkDataSsbo;  // 0 = use chunkBaseOffset (DEFAULT, byte-identical to pre-C2.1)
    uint drawIndexBase;     // C2: gl_DrawIDARB restarts at 0 for EVERY vkCmdDrawIndexedIndirect,
                            // so each batch pushes the base index of its slice of `origins`.
                            // Without this, every batch after the first read the wrong origins --
                            // measured as a 14.9% pixel difference against the legacy path.
} pushConstants;

// Per-draw chunk origins for the multidraw path. xyz = camera-relative chunk origin.
layout(std430, set = 0, binding = 0) readonly buffer ChunkData {
    vec4 origins[];
} chunkData;

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

    // For a CUBE face (scaleLevel 0) bits 20-31 carry the greedy-merged rectangle extents
    // (sizeU on bit0 axis, sizeV on bit1 axis), stored as size-1. MUST match static_voxel.vert
    // or shadow casters collapse to 1x1 quads (tiny/short shadows).
    uint sizeU = (subcubeEncoded & 0x3Fu) + 1u;
    uint sizeV = (microcubeEncoded & 0x3Fu) + 1u;

    // Decode subcube position
    uint subcubeLocalX = subcubeEncoded % 3u;
    uint subcubeLocalY = (subcubeEncoded / 3u) % 3u;
    uint subcubeLocalZ = subcubeEncoded / 9u;
    
    // Decode microcube position
    uint microcubeLocalX = microcubeEncoded % 3u;
    uint microcubeLocalY = (microcubeEncoded / 3u) % 3u;
    uint microcubeLocalZ = microcubeEncoded / 9u;

    // Merged FINE (sub/micro) faces carry extents in the light word bits 16-31 (see
    // static_voxel.vert / BinaryGreedyMeshingPlan.md §4.1). MUST match static_voxel.vert or a
    // merged fine caster throws a 1-cell-wide shadow. Unmerged faces write 0 => extent 1.
    uint fineSizeU = ((inLight >> 16) & 0xFFu) + 1u;
    uint fineSizeV = ((inLight >> 24) & 0xFFu) + 1u;
    vec3 fineSizeVec;
    if (faceID == 0u || faceID == 1u)      fineSizeVec = vec3(float(fineSizeU), float(fineSizeV), 1.0);
    else if (faceID == 2u || faceID == 3u) fineSizeVec = vec3(1.0, float(fineSizeV), float(fineSizeU));
    else                                   fineSizeVec = vec3(float(fineSizeU), 1.0, float(fineSizeV));

    // Calculate base position
    vec3 chunkRelativePos = vec3(float(chunkX), float(chunkY), float(chunkZ));
    // Uniform branch: with useChunkDataSsbo == 0 this is exactly the old expression, so the
    // legacy per-chunk draw path is unchanged. gl_DrawIDARB is 0 for non-indirect draws.
    vec3 chunkOrigin = (pushConstants.useChunkDataSsbo != 0u)
        ? chunkData.origins[pushConstants.drawIndexBase + uint(gl_DrawIDARB)].xyz
        : pushConstants.chunkBaseOffset;
    vec3 basePos = chunkOrigin + chunkRelativePos;
    
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
        // Scale the in-plane axes of the unit quad by the greedy-merged rectangle extents,
        // matching static_voxel.vert (Z faces: u=x,v=y; X faces: u=z,v=y; Y faces: u=x,v=z).
        vec3 sizeVec;
        if (faceID == 0u || faceID == 1u)      sizeVec = vec3(float(sizeU), float(sizeV), 1.0);
        else if (faceID == 2u || faceID == 3u) sizeVec = vec3(1.0, float(sizeV), float(sizeU));
        else                                   sizeVec = vec3(float(sizeU), 1.0, float(sizeV));
        worldPos = basePos + faceOffset * sizeVec;
    } else if (scaleLevel == 1u) {
        const float SUBCUBE_SCALE = 1.0 / 3.0;
        vec3 subcubeOffset = vec3(float(subcubeLocalX), float(subcubeLocalY), float(subcubeLocalZ)) * SUBCUBE_SCALE;
        worldPos = basePos + subcubeOffset + (faceOffset * fineSizeVec * SUBCUBE_SCALE);
    } else if (scaleLevel == 2u) {
        const float SUBCUBE_SCALE = 1.0 / 3.0;
        const float MICROCUBE_SCALE = 1.0 / 9.0;
        vec3 subcubeOffset = vec3(float(subcubeLocalX), float(subcubeLocalY), float(subcubeLocalZ)) * SUBCUBE_SCALE;
        vec3 microcubeOffset = vec3(float(microcubeLocalX), float(microcubeLocalY), float(microcubeLocalZ)) * MICROCUBE_SCALE;
        worldPos = basePos + subcubeOffset + microcubeOffset + (faceOffset * fineSizeVec * MICROCUBE_SCALE);
    } else if (scaleLevel == 3u) {
        // C4 LOD CELL — MUST match static_voxel.vert or a coarse chunk casts a 1x1 shadow.
        float cell = float(1u << ((inPackedData >> 20) & 0x7u));
        worldPos = basePos + faceOffset * cell;
    } else {
        worldPos = basePos + faceOffset;
    }
    
    gl_Position = pushConstants.lightSpaceMatrix * vec4(worldPos, 1.0);
}
