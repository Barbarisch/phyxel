#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;      // UNUSED since P2.2 — bone transforms come from the SSBO below.
                     // Kept so the pipeline layout (and the shadow pipeline's) is unchanged.
    mat4 viewProj;
    vec4 bakedLight; // x = skylight (0..1), yzw = block light RGB (0..1) — sampled per character
} pushConsts;

// One model matrix per bone group, for every batched character. Indexing this per
// instance is what collapses a character's ~20 bone-group draws into ONE draw
// (docs/CharacterPipelineScaling.md P2.2).
layout(std430, set = 0, binding = 8) readonly buffer CharacterBones {
    mat4 boneModels[];
};

// Instance attributes
layout(location = 0) in vec3 inOffset;
layout(location = 1) in vec3 inScale;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uint inBoneIndex;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragBakedLight;

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
    
    // Apply bone transform (Model Matrix) once, reuse for clip position + world position
    mat4 model = boneModels[inBoneIndex];
    vec4 worldPos = model * vec4(localPos, 1.0);
    gl_Position = pushConsts.viewProj * worldPos;

    // Transform normal (only rotation from model matrix)
    fragNormal = mat3(model) * normal;
    fragColor = inColor.rgb;
    fragWorldPos = worldPos.xyz;
    fragBakedLight = pushConsts.bakedLight;
}
