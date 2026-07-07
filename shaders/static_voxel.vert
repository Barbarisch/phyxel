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
layout(location = 4) in uint inLight;           // per-instance: 4 per-corner skylight nibbles (bits0-15)
layout(location = 5) in uint inLight2;          // per-instance: per-corner block RGB (corner0 bits0-11, corner1 bits12-23)
layout(location = 6) in uint inLight3;          // per-instance: per-corner block RGB (corner2 bits0-11, corner3 bits12-23)
layout(location = 7) in uint inTint;            // per-instance: packed 0xRRGGBB tint (0xFFFFFF = none)

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
    float emissiveMultiplier;
    vec3 cameraPosition;
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;          // proj*view, precombined once per frame on CPU
    mat4 biasedLightSpace;  // shadow bias * lightSpaceMatrix, precombined on CPU
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
layout(location = 6) out float vSkyLight;          // baked skylight, normalized 0..1 — SMOOTH (interpolated per-corner)
layout(location = 7) out vec3  vBlockColor;        // baked coloured block light, 0..1/channel — SMOOTH (interpolated per-corner)
layout(location = 8) out vec3  vTint;              // per-voxel tint (low 24 bits of inTint)
layout(location = 9) out flat uint vState;         // per-voxel state (high byte of inTint): 0 normal,1 flaming,2 smoldering,3 charred,4 wet

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

    // For a CUBE face (scaleLevel 0) bits 20-31 instead carry the greedy-merged
    // rectangle extents: sizeU (bit0 axis) and sizeV (bit1 axis), stored as size-1.
    // sizeU=sizeV=1 => a normal unit face. (Ignored for subcube/microcube.)
    uint sizeU = (subcubeEncoded & 0x3Fu) + 1u;
    uint sizeV = (microcubeEncoded & 0x3Fu) + 1u;
    
    // Decode subcube position from 6-bit encoding (x + y*3 + z*9)
    uint subcubeLocalX = subcubeEncoded % 3u;
    uint subcubeLocalY = (subcubeEncoded / 3u) % 3u;
    uint subcubeLocalZ = subcubeEncoded / 9u;
    
    // Decode microcube position from 6-bit encoding
    uint microcubeLocalX = microcubeEncoded % 3u;
    uint microcubeLocalY = (microcubeEncoded / 3u) % 3u;
    uint microcubeLocalZ = microcubeEncoded / 9u;

    // Merged FINE (subcube/microcube) faces carry their rectangle extents in the LIGHT word's high
    // bits (bits 16-31), because packedData bits 20-31 are fully taken by the fine grid ORIGIN.
    // Unmerged fine faces (and all cube faces) write 0 here => extent 1 => a single cell, so the
    // decode is byte-identical to the old per-face path. See BinaryGreedyMeshingPlan.md §4.1.
    uint fineSizeU = ((inLight >> 16) & 0xFFu) + 1u;
    uint fineSizeV = ((inLight >> 24) & 0xFFu) + 1u;
    // Per-face axis mapping for the two in-plane extents (identical to the cube sizeVec mapping):
    //   Z faces (0,1): u=x, v=y   X faces (2,3): u=z, v=y   Y faces (4,5): u=x, v=z
    vec3 fineSizeVec;
    if (faceID == 0u || faceID == 1u)      fineSizeVec = vec3(float(fineSizeU), float(fineSizeV), 1.0);
    else if (faceID == 2u || faceID == 3u) fineSizeVec = vec3(1.0, float(fineSizeV), float(fineSizeU));
    else                                   fineSizeVec = vec3(float(fineSizeU), 1.0, float(fineSizeV));

    // UV origin shift for merged fine runs on FLIPPED texture axes. A merged instance encodes the
    // min-LOCAL cell as its grid origin (so the WORLD quad extends the right way), but on axes where
    // the grid->UV mapping is inverted (the (2 - local) entries in the gridPos tables below), the
    // min-UV corner is the OTHER end of the run, so the UV origin must slide down by (extent-1)
    // cells. U is inverted on faces 2 (+X) and 4 (+Y); V is inverted on every face. This shift is a
    // no-op when the extent is 1 (unmerged), so it never perturbs the single-cell path.
    bool fineFlipU = (faceID == 2u || faceID == 4u);
    vec2 fineUVOriginShift = vec2(fineFlipU ? -(float(fineSizeU) - 1.0) : 0.0,
                                  -(float(fineSizeV) - 1.0));  // V inverted on all faces

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
        // Regular cube — may span a greedy-merged sizeU x sizeV rectangle.
        // Scale the two in-plane axes of the unit quad by the rectangle extents.
        // bit0 axis = sizeU, bit1 axis = sizeV (depends on the face normal):
        //   Z faces (0,1): bit0=x, bit1=y   X faces (2,3): bit0=z, bit1=y   Y faces (4,5): bit0=x, bit1=z
        vec3 sizeVec;
        if (faceID == 0u || faceID == 1u)      sizeVec = vec3(float(sizeU), float(sizeV), 1.0);
        else if (faceID == 2u || faceID == 3u) sizeVec = vec3(1.0, float(sizeV), float(sizeU));
        else                                   sizeVec = vec3(float(sizeU), 1.0, float(sizeV));
        scale = 1.0;
        worldPos = basePos + faceOffset * sizeVec;

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

        // fineSizeVec extends the quad across a merged run of subcells (1 for unmerged faces).
        worldPos = basePos + subcubeOffset + (faceOffset * fineSizeVec * SUBCUBE_SCALE);

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

        // fineSizeVec extends the quad across a merged run of microcells (1 for unmerged faces).
        worldPos = basePos + subcubeOffset + microcubeOffset + (faceOffset * fineSizeVec * MICROCUBE_SCALE);

    } else {
        // Reserved scale level (fallback to cube)
        scale = 1.0;
        worldPos = basePos + faceOffset;
    }
    
    // Calculate UV coordinates for texture mapping
    // UV coordinates must match the vertex generation pattern for each face
    vec2 uv = vec2(0.0);
    
    // Shadow coordinates: bias(clip [-1,1] -> UV [0,1]) * lightSpace, precombined on CPU
    shadowCoord = ubo.biasedLightSpace * vec4(worldPos, 1.0);

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
    } else if (faceID == 1u) { // Back face (-Z) - South - Y flipped to match other side faces
        // Vertices: (1,0,0), (0,0,0), (0,1,0), (1,1,0) - x flipped
        baseUV = vec2(1.0 - float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
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
        // Regular cube: tile the texture sizeU x sizeV across the merged rectangle.
        // The fragment shader wraps with fract() into the atlas tile. baseUV.x derives
        // from vertexID bit0 (sizeU axis), baseUV.y from bit1 (sizeV axis).
        uv = baseUV * vec2(float(sizeU), float(sizeV));

    } else if (scaleLevel == 1u) {
        // Subcube: modify UV to sample 1/3 of texture (6x6 out of 18x18)
        const float SUBCUBE_UV_SCALE = 1.0 / 3.0;
        
        // Map the 3x3x3 subcube grid to 2D texture coordinates based on face orientation
        vec2 subcubeGridPos = vec2(0.0);
        
        if (faceID == 0u) {        // North (Front) - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalY));
        } else if (faceID == 1u) { // South (Back) - Y flipped to match
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalY));
        } else if (faceID == 2u) { // East (Right) - both X and Y flipped
            subcubeGridPos = vec2(float(2u - subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (faceID == 3u) { // West (Left) - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (faceID == 4u) { // Top - both X and Y flipped  
            subcubeGridPos = vec2(float(2u - subcubeLocalX), float(2u - subcubeLocalZ));
        } else if (faceID == 5u) { // Bottom - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalZ));
        }
        
        // Scale the base UV to subcube size and add offset for this subcube's position. For a
        // merged run, baseUV spans fineSizeU x fineSizeV cells (bit0->U, bit1->V), tiling the
        // parent-cube texture across the run; subcubeGridPos is the ORIGIN cell's grid position.
        uv = (baseUV * vec2(float(fineSizeU), float(fineSizeV)) * SUBCUBE_UV_SCALE)
           + (subcubeGridPos * SUBCUBE_UV_SCALE)
           + (fineUVOriginShift * SUBCUBE_UV_SCALE);

    } else if (scaleLevel == 2u) {
        // Microcube: modify UV to sample 1/9 of texture (2x2 out of 18x18)
        const float SUBCUBE_UV_SCALE = 1.0 / 3.0;
        const float MICROCUBE_UV_SCALE = 1.0 / 9.0;
        
        // First, get the subcube's UV region
        vec2 subcubeGridPos = vec2(0.0);
        if (faceID == 0u) {
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalY));
        } else if (faceID == 1u) {
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalY));
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
            microcubeGridPos = vec2(float(microcubeLocalX), float(2u - microcubeLocalY));
        } else if (faceID == 2u) {
            microcubeGridPos = vec2(float(2u - microcubeLocalZ), float(2u - microcubeLocalY));
        } else if (faceID == 3u) {
            microcubeGridPos = vec2(float(microcubeLocalZ), float(2u - microcubeLocalY));
        } else if (faceID == 4u) {
            microcubeGridPos = vec2(float(2u - microcubeLocalX), float(2u - microcubeLocalZ));
        } else if (faceID == 5u) {
            microcubeGridPos = vec2(float(microcubeLocalX), float(2u - microcubeLocalZ));
        }
        
        // Combine: subcube offset + microcube offset within subcube. For a merged run, baseUV
        // spans fineSizeU x fineSizeV microcells; subcube/microcube grid pos = the ORIGIN cell.
        vec2 subcubeUVBase = subcubeGridPos * SUBCUBE_UV_SCALE;
        vec2 microcubeUVOffset = microcubeGridPos * MICROCUBE_UV_SCALE;
        uv = (baseUV * vec2(float(fineSizeU), float(fineSizeV)) * MICROCUBE_UV_SCALE)
           + subcubeUVBase + microcubeUVOffset
           + (fineUVOriginShift * MICROCUBE_UV_SCALE);
        
    } else {
        // Reserved: default to full texture
        uv = baseUV;
    }
    
    // CPU pre-filtering: Only vertices for visible faces are sent to GPU
    // No need for face visibility checking - all vertices here should be rendered
    gl_Position = ubo.viewProj * vec4(worldPos, 1.0);
    outWorldPos = worldPos;
    // inTint packs 0xRRGGBB tint in bits 0-23 and the voxel STATE in bits 24-31.
    // Tint multiplies the material albedo in voxel.frag; state drives glow/wet/etc.
    uint tintRGB = inTint & 0xFFFFFFu;
    vState = (inTint >> 24) & 0xFFu;
    vTint = vec3(float((tintRGB >> 16) & 0xFFu),
                 float((tintRGB >>  8) & 0xFFu),
                 float( tintRGB        & 0xFFu)) / 255.0;
    
    // Pass texture data to fragment shader
    textureIndex = inTextureIndex;
    texCoord = uv;
    flags = inFlags;
    // Smooth skylight: bits 0-15 hold one 4-bit sky value per quad corner. The corner index is
    // the in-plane (bit0,bit1) of the cube vertex (vertexID is 0-7 cube corners; faceOffset uses
    // only bits 0-1), so mask with 3. Outputting per-vertex (non-flat) lets the rasterizer
    // interpolate it, turning blocky per-face steps into smooth gradients + ambient occlusion.
    uint corner = vertexID & 3u;
    uint cornerSky = (inLight >> (corner * 4u)) & 0xFu;
    vSkyLight = float(cornerSky) / 15.0;
    // Smooth per-corner block light: corners 0,1 in inLight2, corners 2,3 in inLight3
    // (12 bits each: R bits0-3, G bits4-7, B bits8-11). Interpolated like skylight.
    uint blkSrc   = (corner < 2u) ? inLight2 : inLight3;
    uint blkShift = (corner < 2u) ? (corner * 12u) : ((corner - 2u) * 12u);
    uint rgb12 = (blkSrc >> blkShift) & 0xFFFu;
    vBlockColor = vec3(float(rgb12 & 0xFu),
                       float((rgb12 >> 4u) & 0xFu),
                       float((rgb12 >> 8u) & 0xFu)) / 15.0;
}
