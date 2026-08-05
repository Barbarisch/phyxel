#version 450

// Far-tree impostors (world-look A1 rethink): one camera-facing card per tree, instanced per
// far-terrain tile. Instances come from the DETERMINISTIC flora plan on the far-terrain
// worker — no chunk data, so forests exist on tiles never visited, out to ~2 km.
// CYLINDRICAL billboarding (yaw only): trees stay upright as the camera pitches.

// Per-INSTANCE attributes (FarTreeInstance, 32B).
layout(location = 0) in vec4 inPosH;     // localX, worldY (trunk base, absolute), localZ, height
layout(location = 1) in float inCanopyR; // canopy half-width (world units)
layout(location = 2) in uint inPacked;   // bits 0-1 shape class | 8-15 R | 16-23 G | 24-31 B

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
    vec2 tileOriginRel;  // (tile min corner - camera).xz, double-subtracted on CPU
    vec2 tileOriginAbs;  // exact world-space min corner
    vec4 fades;          // fadeNear0, fadeNear1, fadeFar0, fadeFar1 (world units)
    vec2 handoff;        // x: residency minFade (1 = chunks not resident, stay solid), y: pad
} pc;

layout(location = 0) out vec2 vUV;          // x: -1..1 across the card, y: 0 base .. 1 tip
layout(location = 1) out flat uint vPacked;
layout(location = 2) out flat float vShade; // simple distance/sun shade factor
layout(location = 3) out flat float vFade;  // dither factor: 0 dissolved .. 1 solid

void main() {
    // Card corner from gl_VertexIndex: two CCW triangles over u in {-1,1}, v in {0,1}.
    const vec2 kCorner[6] = vec2[6](
        vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(-1.0, 0.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 corner = kCorner[gl_VertexIndex];

    // Trunk base in the camera-relative frame.
    vec3 base = vec3(pc.tileOriginRel.x + inPosH.x,
                     inPosH.y - ubo.cameraWorld.y,
                     pc.tileOriginRel.y + inPosH.z);

    // 3D distance — matches chunk streaming's residency sphere (see far_tree_mesh.vert:
    // XZ-only fading opened a bare ring around the nadir from elevated cameras).
    float dist = length(base);

    // Distance band: dissolve in past the residency edge, dissolve out at the far limit —
    // via SCREEN-DOOR DITHER in the fragment stage. Size never changes with distance (the
    // scale-fade read as trees shrinking: user-rejected).
    // Residency handoff floor on the NEAR component only (see far_tree_mesh.vert): until the
    // real chunks under this tile exist, the card must not dissolve — else a gap opens where
    // neither tier draws. The FAR fade-out is untouched (horizon dissolve is distance-only).
    float fadeIn  = max(smoothstep(pc.fades.x, pc.fades.y, dist), pc.handoff.x);
    float fadeOut = 1.0 - smoothstep(pc.fades.z, pc.fades.w, dist);
    vFade = fadeIn * fadeOut;

    // Cylindrical billboard frame: right = horizontal direction perpendicular to the view ray
    // toward THIS tree (per-instance, so a wide forest doesn't shear at the screen edges).
    vec2 toTree = (dist > 1e-3) ? base.xz / dist : vec2(0.0, 1.0);
    vec3 rightWS = vec3(-toTree.y, 0.0, toTree.x);

    // Card proportions: canopy half-width horizontally, full height vertically. A slight
    // width floor keeps distant trees ≥ ~a pixel so forests read as texture, not noise.
    float halfW = max(inCanopyR, 1.2);
    float h     = inPosH.w;

    vec3 rp = base + rightWS * (corner.x * halfW) + vec3(0.0, corner.y * h, 0.0);

    vUV     = vec2(corner.x, corner.y);
    vPacked = inPacked;
    // Cheap static shading: faces get darker toward the base and slightly sun-modulated so
    // stands don't render as flat green sheets. Full lighting is not worth it at 400u+.
    vShade  = 0.85 + 0.15 * clamp(dot(vec3(0.0, 1.0, 0.0), -normalize(ubo.sunDirection)), 0.0, 1.0);

    gl_Position = ubo.proj * ubo.view * vec4(rp, 1.0);
}
