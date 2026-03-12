#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pushConsts;

// Instance attributes
layout(location = 0) in vec3 inOffset;
layout(location = 1) in vec3 inScale;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;

// Cube vertices (positions)
const vec3 positions[36] = vec3[36](
    // Front face
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3(-0.5,  0.5,  0.5), vec3(-0.5, -0.5,  0.5),
    // Back face
    vec3( 0.5, -0.5, -0.5), vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5), vec3( 0.5, -0.5, -0.5),
    // Top face
    vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5,  0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5, -0.5), vec3(-0.5,  0.5, -0.5), vec3(-0.5,  0.5,  0.5),
    // Bottom face
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5),
    vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5, -0.5, -0.5),
    // Right face
    vec3( 0.5, -0.5,  0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5), vec3( 0.5, -0.5,  0.5),
    // Left face
    vec3(-0.5, -0.5, -0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5), vec3(-0.5,  0.5, -0.5), vec3(-0.5, -0.5, -0.5)
);

// Cube normals
const vec3 normals[36] = vec3[36](
    // Front face
    vec3( 0.0,  0.0,  1.0), vec3( 0.0,  0.0,  1.0), vec3( 0.0,  0.0,  1.0),
    vec3( 0.0,  0.0,  1.0), vec3( 0.0,  0.0,  1.0), vec3( 0.0,  0.0,  1.0),
    // Back face
    vec3( 0.0,  0.0, -1.0), vec3( 0.0,  0.0, -1.0), vec3( 0.0,  0.0, -1.0),
    vec3( 0.0,  0.0, -1.0), vec3( 0.0,  0.0, -1.0), vec3( 0.0,  0.0, -1.0),
    // Top face
    vec3( 0.0,  1.0,  0.0), vec3( 0.0,  1.0,  0.0), vec3( 0.0,  1.0,  0.0),
    vec3( 0.0,  1.0,  0.0), vec3( 0.0,  1.0,  0.0), vec3( 0.0,  1.0,  0.0),
    // Bottom face
    vec3( 0.0, -1.0,  0.0), vec3( 0.0, -1.0,  0.0), vec3( 0.0, -1.0,  0.0),
    vec3( 0.0, -1.0,  0.0), vec3( 0.0, -1.0,  0.0), vec3( 0.0, -1.0,  0.0),
    // Right face
    vec3( 1.0,  0.0,  0.0), vec3( 1.0,  0.0,  0.0), vec3( 1.0,  0.0,  0.0),
    vec3( 1.0,  0.0,  0.0), vec3( 1.0,  0.0,  0.0), vec3( 1.0,  0.0,  0.0),
    // Left face
    vec3(-1.0,  0.0,  0.0), vec3(-1.0,  0.0,  0.0), vec3(-1.0,  0.0,  0.0),
    vec3(-1.0,  0.0,  0.0), vec3(-1.0,  0.0,  0.0), vec3(-1.0,  0.0,  0.0)
);

void main() {
    vec3 pos = positions[gl_VertexIndex];
    vec3 normal = normals[gl_VertexIndex];
    
    // Apply instance transform (Scale + Offset)
    // Note: Offset is relative to the bone center
    vec3 localPos = pos * inScale + inOffset;
    
    // Apply bone transform (Model Matrix)
    gl_Position = pushConsts.viewProj * pushConsts.model * vec4(localPos, 1.0);
    
    // Transform normal (only rotation from model matrix)
    fragNormal = mat3(pushConsts.model) * normal;
    fragColor = inColor.rgb;
    fragWorldPos = (pushConsts.model * vec4(localPos, 1.0)).xyz;
}
