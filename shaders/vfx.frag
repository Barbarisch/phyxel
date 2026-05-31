#version 450

// Lightweight VFX particle fragment shader.
// Emissive (no lighting) and premultiplied for ADDITIVE blending
// (srcFactor = ONE, dstFactor = ONE). As a particle fades, its alpha
// drops toward 0 and its additive contribution vanishes — order
// independent, so no per-frame depth sort is needed on the CPU.

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Premultiply emissive color by current brightness (alpha).
    outColor = vec4(fragColor.rgb * fragColor.a, fragColor.a);
}
