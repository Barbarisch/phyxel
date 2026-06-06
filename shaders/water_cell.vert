#version 450
//
// water_cell.vert — per-cell water surface + side skirts (Phase 2, see docs/WaterSystem.md).
//
// Each instanced cell mesh is a sloped top quad plus four vertical side faces. The top
// corners take per-corner world-Y (averaged by WaterManager into a shared grid → seamless
// slopes). The side faces drop from each top edge to a per-edge skirt bottom, so drops,
// cliffs and waterfalls show a closed wall of water instead of a floating lip.
//
layout(location = 0) in vec4 inVert;        // (offsetX, offsetZ, vtype, edge)
layout(location = 1) in vec4 inCenterDepth; // xyz = cell-center surface point, w = column depth (cells)
layout(location = 2) in vec4 inCorners;     // per-corner world Y: (-x,-z),(+x,-z),(+x,+z),(-x,+z)
layout(location = 3) in vec4 inSkirt;       // per-edge side bottom world Y: (+x),(-x),(+z),(-z)

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
} pc;

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out float fragDepth;
layout(location = 2) out float fragSide; // 0 = top face, 1 = side face

void main() {
    float ox = inVert.x, oz = inVert.y;
    int vtype = int(inVert.z + 0.5);
    int edge  = int(inVert.w + 0.5);

    // Corner index from the vertex's xz sign (matches WaterManager corner order).
    int idx;
    if      (ox < 0.0 && oz < 0.0) idx = 0; // (-x,-z)
    else if (ox > 0.0 && oz < 0.0) idx = 1; // (+x,-z)
    else if (ox > 0.0 && oz > 0.0) idx = 2; // (+x,+z)
    else                           idx = 3; // (-x,+z)

    float y = (vtype == 0) ? inCorners[idx] : inSkirt[edge];

    vec3 world = vec3(inCenterDepth.x + ox, y, inCenterDepth.z + oz);
    fragWorldPos = world;
    fragDepth    = inCenterDepth.w;
    fragSide     = float(vtype);
    gl_Position  = pc.viewProj * vec4(world, 1.0);
}
