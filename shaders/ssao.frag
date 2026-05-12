#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outOcclusion;

layout(set = 0, binding = 0) uniform sampler2D depthTex;

layout(push_constant) uniform PC {
    mat4 proj;
    mat4 invProj;
    vec2 noiseScale;    // screenSize / 4.0
    float radius;       // world-space hemisphere radius
    float bias;         // depth bias to avoid self-occlusion
} pc;

// 16 hemisphere samples in tangent space (pre-generated, lerped toward origin)
const vec3 SAMPLES[16] = vec3[](
    vec3( 0.5381, 0.1856,-0.4319), vec3( 0.1379, 0.2486, 0.4430),
    vec3( 0.3371, 0.5679,-0.0057), vec3(-0.6999,-0.0451,-0.0019),
    vec3( 0.0689,-0.1598,-0.8547), vec3( 0.0560, 0.0069,-0.1843),
    vec3(-0.0146, 0.1402, 0.0762), vec3( 0.0100,-0.1924,-0.0344),
    vec3(-0.3577,-0.5301,-0.4358), vec3(-0.3169, 0.1063, 0.0158),
    vec3( 0.0103,-0.5869, 0.0046), vec3(-0.0897,-0.4940, 0.3287),
    vec3( 0.7119,-0.0154,-0.0918), vec3(-0.0533, 0.0596,-0.5411),
    vec3( 0.0352,-0.0631, 0.5460), vec3(-0.4776, 0.2847,-0.0271)
);

// Simple hash-based noise (avoids needing a noise texture)
vec2 randomVec(vec2 uv) {
    float n = sin(dot(uv * 1000.0, vec2(127.1, 311.7))) * 43758.5453;
    float n2 = sin(dot(uv * 1000.0, vec2(269.5, 183.3))) * 43758.5453;
    return normalize(vec2(fract(n), fract(n2)) * 2.0 - 1.0);
}

// Reconstruct view-space position from depth
vec3 viewPosFromDepth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = pc.invProj * ndc;
    return viewPos.xyz / viewPos.w;
}

void main() {
    float depth = texture(depthTex, inUV).r;

    // Background (far plane) → no occlusion
    if (depth >= 0.9999) {
        outOcclusion = 1.0;
        return;
    }

    vec3 fragPos = viewPosFromDepth(inUV, depth);

    // Reconstruct view-space normal from depth derivatives
    vec3 ddx  = viewPosFromDepth(inUV + vec2(1.0 / 1280.0, 0.0), texture(depthTex, inUV + vec2(1.0/1280.0, 0.0)).r) - fragPos;
    vec3 ddx2 = fragPos - viewPosFromDepth(inUV - vec2(1.0 / 1280.0, 0.0), texture(depthTex, inUV - vec2(1.0/1280.0, 0.0)).r);
    if (abs(ddx.z) > abs(ddx2.z)) ddx = ddx2;
    vec3 ddy  = viewPosFromDepth(inUV + vec2(0.0, 1.0 / 720.0), texture(depthTex, inUV + vec2(0.0, 1.0/720.0)).r) - fragPos;
    vec3 ddy2 = fragPos - viewPosFromDepth(inUV - vec2(0.0, 1.0 / 720.0), texture(depthTex, inUV - vec2(0.0, 1.0/720.0)).r);
    if (abs(ddy.z) > abs(ddy2.z)) ddy = ddy2;
    vec3 normal = normalize(cross(ddx, ddy));

    // TBN matrix to orient hemisphere samples
    vec2 rv = randomVec(inUV);
    vec3 randomVec3 = vec3(rv, 0.0);
    vec3 tangent   = normalize(randomVec3 - normal * dot(randomVec3, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < 16; i++) {
        // Accelerating interpolation to cluster samples closer to origin
        float scale = float(i) / 16.0;
        scale = mix(0.1, 1.0, scale * scale);

        vec3 sampleVec = TBN * SAMPLES[i];
        vec3 samplePos = fragPos + sampleVec * pc.radius * scale;

        // Project sample position to get its UV and depth
        vec4 offset = pc.proj * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        offset.y = 1.0 - offset.y; // Vulkan Y flip

        float sampleDepth = texture(depthTex, offset.xy).r;
        vec3 sampleViewPos = viewPosFromDepth(offset.xy, sampleDepth);

        // Range check: only count occlusion within radius
        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(fragPos.z - sampleViewPos.z + 0.001));
        occlusion += (sampleViewPos.z >= samplePos.z + pc.bias ? 1.0 : 0.0) * rangeCheck;
    }

    outOcclusion = 1.0 - (occlusion / 16.0);
}
