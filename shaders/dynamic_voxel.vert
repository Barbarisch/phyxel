#version 450

layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in vec3 inWorldPosition;   // per-instance: world position of subcube
layout(location = 2) in uint inTextureIndex;    // per-instance texture atlas index
layout(location = 3) in uint inFaceID;          // per-instance: face ID (0-5)
layout(location = 4) in vec3 inScale;           // per-instance: scale factor (vec3 for non-uniform scaling)
layout(location = 5) in vec4 inRotation;        // per-instance: rotation quaternion (x, y, z, w)
layout(location = 6) in ivec3 inLocalPosition;  // per-instance: original local position in 3x3x3 grid

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
} ubo;

layout(location = 0) out flat uint textureIndex;  // pass texture index to frag shader
layout(location = 1) out vec2 texCoord;           // pass texture coordinates to frag shader
layout(location = 2) out vec4 shadowCoord;        // pass shadow coordinates to frag shader
layout(location = 3) out flat uint flags;         // pass flags to frag shader
layout(location = 4) out vec3 outNormal;          // pass normal to frag shader

// Rotate a vector by a quaternion
vec3 rotateByQuaternion(vec3 v, vec4 q) {
    // q = (x, y, z, w) where w is the scalar part
    vec3 qvec = q.xyz;
    float qw = q.w;
    
    // v' = v + 2 * cross(qvec, cross(qvec, v) + qw * v)
    vec3 cross1 = cross(qvec, v) + qw * v;
    vec3 cross2 = cross(qvec, cross1);
    return v + 2.0 * cross2;
}

void main() {
    // Use per-instance scale to maintain correct subcube size
    const vec3 SUBCUBE_SCALE = inScale;
    
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
    
    // Apply scale to face offset and center it around the origin
    // faceOffset ranges from 0-1, so we need to center it: (faceOffset - 0.5) * scale
    vec3 scaledOffset = (faceOffset - 0.5) * SUBCUBE_SCALE;
    
    // Apply rotation to the scaled offset using quaternion rotation
    vec3 rotatedOffset = rotateByQuaternion(scaledOffset, inRotation);
    
    // Use the actual physics position from inWorldPosition as center
    vec3 worldPos = inWorldPosition + rotatedOffset;
    
    // Calculate UV coordinates for texture mapping
    // UV coordinates must match the vertex generation pattern for each face
    vec2 uv = vec2(0.0);
    
    if (inFaceID == 0u) {        // Front face (+Z) - North - looks good with flip
        // Vertices: (0,0,1), (1,0,1), (1,1,1), (0,1,1)
        uv = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    } else if (inFaceID == 1u) { // Back face (-Z) - South - looks good
        // Vertices: (1,0,0), (0,0,0), (0,1,0), (1,1,0) - x flipped
        uv = vec2(1.0 - float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    } else if (inFaceID == 2u) { // Right face (+X) - East - 180 degree rotation
        // Vertices: (1,0,1), (1,0,0), (1,1,0), (1,1,1) - z flipped
        uv = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    } else if (inFaceID == 3u) { // Left face (-X) - West - looks good
        // Vertices: (0,0,0), (0,0,1), (0,1,1), (0,1,0)
        uv = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    } else if (inFaceID == 4u) { // Top face (+Y) - horizontal mirror
        // Vertices: (0,1,1), (1,1,1), (1,1,0), (0,1,0) - z flipped
        uv = vec2(1.0 - float((vertexID >> 0) & 1u), float((vertexID >> 1) & 1u));
    } else if (inFaceID == 5u) { // Bottom face (-Y) - looks good
        // Vertices: (0,0,0), (1,0,0), (1,0,1), (0,0,1)
        uv = vec2(float((vertexID >> 0) & 1u), 1.0 - float((vertexID >> 1) & 1u));
    }
    
    // Apply texture coordinate scaling based on object scale
    // Microcube: 1/9 ≈ 0.111, Subcube: 1/3 ≈ 0.333, Cube: 1.0
    if (SUBCUBE_SCALE.x < 0.2) {
        // Microcubes: scale < 0.2 (actually 1/9 ≈ 0.111)
        // Decode packed subcube and microcube positions from localPosition.x
        // Bits 0-1: subcube X, Bits 2-3: subcube Y, Bits 4-5: subcube Z
        // Bits 6-7: microcube X, Bits 8-9: microcube Y, Bits 10-11: microcube Z
        int packed = inLocalPosition.x;
        uint subcubeLocalX = uint(packed & 0x3);
        uint subcubeLocalY = uint((packed >> 2) & 0x3);
        uint subcubeLocalZ = uint((packed >> 4) & 0x3);
        uint microcubeLocalX = uint((packed >> 6) & 0x3);
        uint microcubeLocalY = uint((packed >> 8) & 0x3);
        uint microcubeLocalZ = uint((packed >> 10) & 0x3);
        
        const float SUBCUBE_UV_SCALE = 1.0 / 3.0;
        const float MICROCUBE_UV_SCALE = 1.0 / 9.0;
        
        // First, get the subcube's UV region
        vec2 subcubeGridPos = vec2(0.0);
        if (inFaceID == 0u) {
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalY));
        } else if (inFaceID == 1u) {
            subcubeGridPos = vec2(float(subcubeLocalX), float(subcubeLocalY));
        } else if (inFaceID == 2u) {
            subcubeGridPos = vec2(float(2u - subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (inFaceID == 3u) {
            subcubeGridPos = vec2(float(subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (inFaceID == 4u) {
            subcubeGridPos = vec2(float(2u - subcubeLocalX), float(2u - subcubeLocalZ));
        } else if (inFaceID == 5u) {
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalZ));
        }
        
        // Then, get the microcube's position within that subcube
        vec2 microcubeGridPos = vec2(0.0);
        if (inFaceID == 0u) {
            microcubeGridPos = vec2(float(microcubeLocalX), float(2u - microcubeLocalY));
        } else if (inFaceID == 1u) {
            microcubeGridPos = vec2(float(microcubeLocalX), float(microcubeLocalY));
        } else if (inFaceID == 2u) {
            microcubeGridPos = vec2(float(2u - microcubeLocalZ), float(2u - microcubeLocalY));
        } else if (inFaceID == 3u) {
            microcubeGridPos = vec2(float(microcubeLocalZ), float(2u - microcubeLocalY));
        } else if (inFaceID == 4u) {
            microcubeGridPos = vec2(float(2u - microcubeLocalX), float(2u - microcubeLocalZ));
        } else if (inFaceID == 5u) {
            microcubeGridPos = vec2(float(microcubeLocalX), float(2u - microcubeLocalZ));
        }
        
        // Combine: subcube offset + microcube offset within subcube
        vec2 subcubeUVBase = subcubeGridPos * SUBCUBE_UV_SCALE;
        vec2 microcubeUVOffset = microcubeGridPos * MICROCUBE_UV_SCALE;
        uv = (uv * MICROCUBE_UV_SCALE) + subcubeUVBase + microcubeUVOffset;
        
    } else if (SUBCUBE_SCALE.x < 0.8) {
        // For subcubes: scale < 0.8 (actually 1/3 ≈ 0.333) - use 1/3 UV scaling with proper grid positioning
        const float SUBCUBE_UV_SCALE = 1.0 / 3.0;  // Each subcube uses 1/3 of texture
        
        // Map the 3x3x3 subcube grid to 2D texture coordinates based on face orientation
        // Each face needs specific coordinate mapping and flipping to match texture orientation
        // Use the original local position to maintain correct texture segments
        vec2 subcubeGridPos = vec2(0.0);
        uint subcubeLocalX = uint(inLocalPosition.x);
        uint subcubeLocalY = uint(inLocalPosition.y);
        uint subcubeLocalZ = uint(inLocalPosition.z);
        
        if (inFaceID == 0u) {        // North (Front) - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalY));
        } else if (inFaceID == 1u) { // South (Back) - works correctly
            subcubeGridPos = vec2(float(subcubeLocalX), float(subcubeLocalY));
        } else if (inFaceID == 2u) { // East (Right) - both X and Y flipped
            subcubeGridPos = vec2(float(2u - subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (inFaceID == 3u) { // West (Left) - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalZ), float(2u - subcubeLocalY));
        } else if (inFaceID == 4u) { // Top - both X and Y flipped  
            subcubeGridPos = vec2(float(2u - subcubeLocalX), float(2u - subcubeLocalZ));
        } else if (inFaceID == 5u) { // Bottom - Y flipped
            subcubeGridPos = vec2(float(subcubeLocalX), float(2u - subcubeLocalZ));
        }
        
        // Scale the base UV to subcube size and add offset for this subcube's position
        uv = (uv * SUBCUBE_UV_SCALE) + (subcubeGridPos * SUBCUBE_UV_SCALE);
    }
    // For full-sized cubes (scale = 1.0): use uv as-is (full texture)
    
    // Calculate normal
    vec3 normal = vec3(0.0);
    if (inFaceID == 0u) normal = vec3(0.0, 0.0, 1.0);
    else if (inFaceID == 1u) normal = vec3(0.0, 0.0, -1.0);
    else if (inFaceID == 2u) normal = vec3(1.0, 0.0, 0.0);
    else if (inFaceID == 3u) normal = vec3(-1.0, 0.0, 0.0);
    else if (inFaceID == 4u) normal = vec3(0.0, 1.0, 0.0);
    else if (inFaceID == 5u) normal = vec3(0.0, -1.0, 0.0);
    
    // Rotate normal
    outNormal = rotateByQuaternion(normal, inRotation);

    // Shadow coord
    const mat4 biasMat = mat4( 
        0.5, 0.0, 0.0, 0.0,
        0.0, 0.5, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.5, 0.5, 0.0, 1.0 
    );
    shadowCoord = biasMat * ubo.lightSpaceMatrix * vec4(worldPos, 1.0);
    
    // Dummy flags
    flags = 0u;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    
    // Pass texture data to fragment shader
    textureIndex = inTextureIndex;
    texCoord = uv;
}
