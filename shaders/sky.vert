#version 450

// sky.vert — a fullscreen triangle that carries a world-space view ray to the fragment stage.
//
// NO vertex buffer, NO descriptor sets, NO index buffer: three vertices generated from
// gl_VertexIndex, and everything else in push constants. That makes the sky the cheapest possible
// pipeline to create and to bind, and it means the pass cannot be broken by a descriptor-layout
// change elsewhere.
//
// THE RAY IS BUILT FROM CAMERA BASIS VECTORS, not from inverse(viewProj), and that is deliberate.
// This engine has a reverse-Z depth convention with an INFINITE far plane, and the projection matrix
// is Y-flipped for Vulkan. Un-projecting a clip-space point is therefore three separate chances to
// get a convention wrong — and reverse-Z has already cost this project a NaN shadow matrix that
// silently disabled every shadow. Basis vectors sidestep all of it: the CPU hands over right, up and
// forward already scaled by the projection's own focal terms, so the sign of the Y flip travels
// inside camUp and no shader here needs to know it exists.

layout(push_constant) uniform SkyPush {
    vec4 camRight;    // xyz = camera right   * tan(fovX/2);  w = camera altitude in METRES
    vec4 camUp;       // xyz = camera up      * tan(fovY/2) (SIGNED: carries the Vulkan Y flip)
    vec4 camForward;  // xyz = camera forward (unit);  w = exposure (used by the frag stage)
    vec4 toSun;       // xyz = unit direction TOWARD the sun (NOT ubo.sunDirection, which is flipped)
                      // w = tonemap curve selector
    vec4 toMoon;      // xyz = unit direction TOWARD the moon
} push;

layout(location = 0) out vec3 vRayDir;

void main() {
    // Oversized triangle covering the whole viewport: (-1,-1), (3,-1), (-1,3).
    vec2 ndc = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                    (gl_VertexIndex == 2) ? 3.0 : -1.0);

    // Depth is irrelevant — this pipeline has depth test AND write disabled and draws before the
    // geometry, so 0.0 is simply a valid value inside [0,1] that cannot be clipped.
    gl_Position = vec4(ndc, 0.0, 1.0);

    vRayDir = push.camForward.xyz + push.camRight.xyz * ndc.x + push.camUp.xyz * ndc.y;
}
