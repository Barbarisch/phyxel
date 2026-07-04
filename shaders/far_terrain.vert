#version 450

// Far-terrain LOD tiles (blocky quantized columns synthesized from the world's
// heightmap — see FarTerrainMesher). Dedicated 16-byte FarVertex format; deliberately
// independent of the static voxel pipeline and its InstanceData/light words.

layout(location = 0) in vec3 inPos;      // tile-local x/z, absolute world y
layout(location = 1) in uint inPacked;   // bits 0-15 atlas tex index, bits 16-18 faceID

// Set-0 UBO (must match UniformBufferObject in vulkan/VulkanDevice.h up to the fields we read).
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
} ubo;

layout(push_constant) uniform PushConstants {
    vec2 tileOrigin;    // world-space min corner (x, z) of the tile
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out flat uint vTex;
layout(location = 2) out flat uint vFace;

void main() {
    vec3 wp = vec3(pc.tileOrigin.x + inPos.x, inPos.y, pc.tileOrigin.y + inPos.z);
    vWorldPos = wp;
    vTex  = inPacked & 0xFFFFu;
    vFace = (inPacked >> 16) & 0x7u;
    gl_Position = ubo.proj * ubo.view * vec4(wp, 1.0);
}
