#version 450
//
// water_cell.vert — per-cell water surface (Phase 2, see docs/WaterSystem.md).
//
// One instanced 1x1 quad per simulated surface cell, placed at the cell's fill
// height. Unlike the flat sea plane, this draws the actual simulated water field, so
// it shows flow, pooling, and bodies at any height.
//
layout(location = 0) in vec3 inQuad;     // unit quad in XZ: xz in [-0.5, 0.5], y = 0
layout(location = 1) in vec4 inInstance; // xyz = cell-center surface point, w = fill (unused here)

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
} pc;

layout(location = 0) out vec3 fragWorldPos;

void main() {
    vec3 world = vec3(inInstance.x + inQuad.x, inInstance.y, inInstance.z + inQuad.z);
    fragWorldPos = world;
    gl_Position = pc.viewProj * vec4(world, 1.0);
}
