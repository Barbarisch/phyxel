#version 450

layout(location = 0) in uint vertexID;          // Cube corner ID (0–7)
layout(location = 1) in vec3 inInstanceOffset;  // per-instance offset
layout(location = 2) in vec3 inInstanceColor;   // per-instance color
layout(location = 3) in uint inFaceMask;        // per-instance face visibility mask

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec3 fragColor;  // pass to frag shader

void main() {
    // Extract cube-local offset (0 or 1 for x, y, z)
    // vertexID encoding: bit 0 = x, bit 1 = y, bit 2 = z
    float x = float((vertexID >> 0) & 1u);
    float y = float((vertexID >> 1) & 1u);
    float z = float((vertexID >> 2) & 1u);

    // Determine which face(s) this vertex belongs to based on its coordinates
    // A vertex can belong to multiple faces (corner/edge vertices)
    bool onFrontFace  = (z == 1.0);  // +Z face
    bool onBackFace   = (z == 0.0);  // -Z face  
    bool onRightFace  = (x == 1.0);  // +X face
    bool onLeftFace   = (x == 0.0);  // -X face
    bool onTopFace    = (y == 1.0);  // +Y face
    bool onBottomFace = (y == 0.0);  // -Y face

    // Check if this vertex belongs to any visible face
    // Face bitfield: bit 0=front, 1=back, 2=right, 3=left, 4=top, 5=bottom
    bool frontVisible  = ((inFaceMask >> 0) & 1u) != 0u;
    bool backVisible   = ((inFaceMask >> 1) & 1u) != 0u;
    bool rightVisible  = ((inFaceMask >> 2) & 1u) != 0u;
    bool leftVisible   = ((inFaceMask >> 3) & 1u) != 0u;
    bool topVisible    = ((inFaceMask >> 4) & 1u) != 0u;
    bool bottomVisible = ((inFaceMask >> 5) & 1u) != 0u;

    bool vertexNeeded = (onFrontFace  && frontVisible)  ||
                        (onBackFace   && backVisible)   ||
                        (onRightFace  && rightVisible)  ||
                        (onLeftFace   && leftVisible)   ||
                        (onTopFace    && topVisible)    ||
                        (onBottomFace && bottomVisible);

    // Create local vertex position within unit cube (0 to 1)
    vec3 localOffset = vec3(x, y, z);

    // Compute final world-space position
    vec3 worldPos = inInstanceOffset + localOffset;

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
