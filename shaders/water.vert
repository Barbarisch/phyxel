#version 450
//
// water.vert — Phase 0/1 water surface (see docs/WaterSystem.md).
//
// Draws a single large quad locked to sea level and centered on the camera in XZ,
// giving an "infinite ocean" plane. Terrain occludes it via the depth buffer, so
// the surface only shows where open space meets sea level (the implicit model).
//
layout(location = 0) in vec3 inPos; // unit quad in the XZ plane: xz in [-0.5, 0.5], y = 0

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 params;     // x = seaLevel, y = quad size, zw unused
    vec4 params2;    // x = screen width, y = screen height, z = reflectionEnabled, w unused
} pc;

layout(location = 0) out vec3 fragWorldPos;

void main() {
    vec3  camPos   = pc.camPosTime.xyz;
    float seaLevel = pc.params.x;
    float size     = pc.params.y;

    // Center the plane on the camera in XZ; lock Y to sea level.
    vec3 world = vec3(camPos.x + inPos.x * size, seaLevel, camPos.z + inPos.z * size);
    fragWorldPos = world;
    gl_Position = pc.viewProj * vec4(world, 1.0);
}
