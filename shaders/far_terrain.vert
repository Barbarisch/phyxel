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
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;          // proj*view, precombined once per frame on CPU
    mat4 biasedLightSpace;  // shadow bias * lightSpaceMatrix, precombined on CPU
    vec3 cameraWorld;       // true camera position (camera-relative rendering)
} ubo;

// Camera-relative rendering (docs/CameraRelativeRendering.md): positions reach clip space
// in the camera-relative frame (rel origin, in-shader Y subtract), while the fragment's
// texture projection keeps the EXACT absolute frame so material tiling stays world-pinned.
layout(push_constant) uniform PushConstants {
    vec2 tileOriginRel;  // (tile min corner - camera).xz, double-subtracted on CPU
    vec2 tileOriginAbs;  // exact world-space min corner (x, z)
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out flat uint vTex;
layout(location = 2) out flat uint vFace;

void main() {
    // Relative frame for clip space: inPos.y is baked ABSOLUTE in the tile mesh, so
    // subtract the camera's Y here (small magnitudes — no precision hazard).
    vec3 rp = vec3(pc.tileOriginRel.x + inPos.x,
                   inPos.y - ubo.cameraWorld.y,
                   pc.tileOriginRel.y + inPos.z);
    // Absolute frame for the fragment's per-world-unit texture projection.
    vWorldPos = vec3(pc.tileOriginAbs.x + inPos.x, inPos.y, pc.tileOriginAbs.y + inPos.z);
    vTex  = inPacked & 0xFFFFu;
    vFace = (inPacked >> 16) & 0x7u;
    gl_Position = ubo.proj * ubo.view * vec4(rp, 1.0);
}
