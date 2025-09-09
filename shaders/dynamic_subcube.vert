#version 450

layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in vec3 inWorldPosition;   // per-instance: world position of subcube
layout(location = 2) in uint inTextureIndex;    // per-instance texture atlas index
layout(location = 3) in uint inFaceID;          // per-instance: face ID (0-5)
layout(location = 4) in float inScale;          // per-instance: scale factor (1/3 for subcubes, 1.0 for cubes)
layout(location = 5) in vec4 inRotation;        // per-instance: rotation quaternion (x, y, z, w)

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out flat uint textureIndex;  // pass texture index to frag shader
layout(location = 1) out vec2 texCoord;           // pass texture coordinates to frag shader

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
    
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    
    // Pass texture data to fragment shader
    textureIndex = inTextureIndex;
    texCoord = uv;
}
