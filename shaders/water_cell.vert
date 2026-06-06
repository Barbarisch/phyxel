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

    // vtype 0 = top face corner, 2 = side-face top corner, 1 = side-face bottom (skirt).
    float y = (vtype == 1) ? inSkirt[edge] : inCorners[idx];

    vec3 world = vec3(inCenterDepth.x + ox, y, inCenterDepth.z + oz);
    // Nudge side faces (vtype 1/2) slightly outward so a falling-water curtain doesn't
    // z-fight the solid cliff/terrain directly behind it.
    if (vtype != 0) {
        vec2 n = vec2(0.0);
        if      (edge == 0) n = vec2( 1.0, 0.0);
        else if (edge == 1) n = vec2(-1.0, 0.0);
        else if (edge == 2) n = vec2( 0.0, 1.0);
        else                n = vec2( 0.0,-1.0);
        world.x += n.x * 0.04;
        world.z += n.y * 0.04;
    }

    fragWorldPos = world;
    fragDepth    = inCenterDepth.w;
    fragSide     = (vtype == 0) ? 0.0 : 1.0;
    gl_Position  = pc.viewProj * vec4(world, 1.0);
}
