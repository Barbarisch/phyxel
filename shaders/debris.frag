#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    // U1: fragColor now arrives ALREADY LIT — DebrisRenderPipeline multiplies the particle colour
    // by the real sun + atmosphere ambient at its world position (see setLightSampler in
    // RenderCoordinator). Previously the light field it sampled was a constant, so this shader's
    // fixed direction was effectively the only shading debris had.
    //
    // What remains here is deliberately a FORM term, not a light: a fixed-direction wrap that gives
    // a tumbling cube readable faces. It is intentionally NOT the sun — debris has no shadowing and
    // a per-particle sun term would make chips flicker as they spin. Kept narrow (0.75..1.0) so it
    // shapes without changing brightness much.
    const vec3 kFormDir = normalize(vec3(0.5, 1.0, 0.3));
    float form = mix(0.75, 1.0, max(dot(fragNormal, kFormDir), 0.0));

    outColor = vec4(fragColor * form, 1.0);
}
