#version 450

// World Rendering v2, M2 — instanced far-tree LOD meshes. Each instance is a REAL voxel tree
// (a TemplateLodChain level meshed once per species), placed by the same per-tile instance
// buffers the card tier uses. Fragment stage is far_terrain.frag — same material atlas, same
// lighting as the terrain and (approximately) the near chunks, which is what removes the
// card tier's color mismatch.

// Per-vertex (binding 0): template-local mesh, trunk base at origin.
layout(location = 0) in vec3 inPos;
layout(location = 1) in uint inPacked;   // bits 0-15 atlas tex index, bits 16-18 faceID

// Per-instance (binding 1): FarTreeInstance.
layout(location = 2) in vec4 inInstPosH;    // localX, worldY (trunk base, abs), localZ, height
layout(location = 3) in float inInstCanopyR;
layout(location = 4) in uint inInstPacked;  // species/tint (unused here — textures carry color)

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
    mat4 viewProj;
    mat4 biasedLightSpace;
    vec3 cameraWorld;
} ubo;

layout(push_constant) uniform PushConstants {
    vec2 tileOriginRel;   // (tile min corner - camera).xz
    vec2 tileOriginAbs;   // exact world-space min corner
    vec2 fadeIn;          // fade-in band (world units, camera distance)
    float baseHeight;     // species card height (mesh is UNSCALED; kept for layout stability)
    float minFade;        // residency handoff floor: 1 = real chunks under this tile are NOT
                          // resident yet, stay fully solid regardless of distance
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out flat uint vTex;
layout(location = 2) out flat uint vFace;
layout(location = 3) out flat float vFade;

void main() {
    // NO scale jitter and NO yaw hash, DELIBERATELY (user: "the lower detail trees dont seem
    // to correspond with high detail trees"). Flora stamps templates UNROTATED and UNSCALED,
    // so the LOD instance must match exactly: same template, same spot, same orientation,
    // same size. Correspondence at the handoff beats artificial variety — the near field is
    // the ground truth this tier degrades from.
    vec3 rp3 = inPos;

    // Trunk base in the camera-relative frame.
    vec3 base = vec3(pc.tileOriginRel.x + inInstPosH.x,
                     inInstPosH.y - ubo.cameraWorld.y,
                     pc.tileOriginRel.y + inInstPosH.z);
    // 3D distance, DELIBERATELY — chunk streaming loads terrain by 3D camera distance, so the
    // band where real trees end is a SPHERE around the camera, not a cylinder. Fading on XZ
    // distance opened a bare ring around the nadir from any elevated camera (user-reported
    // deadzone, 2026-08-02): horizontal distance small, 3D distance already past residency.
    float dist = length(base);

    // Fade-in past the residency edge (real trees own the near field). The fade value goes to
    // the fragment stage as a DITHER factor — geometry never changes size (the scale-fade
    // read as "trees shrinking as I approach": user-rejected).
    // minFade floors it: distance says "resident zone" but streaming is ASYNC — until the
    // real chunks under this tile are loaded+meshed, the LOD tree must NOT dissolve, or a
    // gap opens where neither tier draws (user-reported: "lower detail trees fade out
    // before the detailed trees render... for a bit of time there is nothing there").
    vFade = max(smoothstep(pc.fadeIn.x, pc.fadeIn.y, dist), pc.minFade);

    vec3 rp = base + rp3;
    vWorldPos = vec3(pc.tileOriginAbs.x + inInstPosH.x, inInstPosH.y, pc.tileOriginAbs.y + inInstPosH.z) + rp3;
    vTex  = inPacked & 0xFFFFu;
    vFace = (inPacked >> 16) & 0x7u;
    gl_Position = ubo.proj * ubo.view * vec4(rp, 1.0);
}
