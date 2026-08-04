#version 450
//
// water_cell.vert — per-cell water surface + side skirts (Phase 2, see docs/Water.md).
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
layout(location = 4) in vec4 inFlow;        // Phase 3: xy = flow dir, z = strength, w = foam

// Must match water_cell.frag's block exactly (one push-constant range, both stages).
// 112 bytes — still under the 128-byte guaranteed push-constant minimum.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 screen;     // xy = screen size (px) — the fragment stage needs it for screen-space taps
    vec4 ripple;     // ripple window: xy = origin (world XZ), z = 1/windowSize, w = amplitude
} pc;

// Ripple/disturbance heightfield (small-scale plan Phase 3): impact rings, footstep wakes,
// splash rims. Sampled by WORLD XZ — a pure function of world position, so adjacent instances
// that share corner positions get identical offsets and the surface stays C0-continuous (never
// per-instance ripple data; that is how you get a cell grid).
layout(set = 1, binding = 2) uniform sampler2D rippleTex;

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out float fragColumnDepth; // sim column depth in cells
layout(location = 2) out float fragSide;        // 0 = top face, 1 = side face
layout(location = 3) out vec4  fragFlow;        // xy = flow dir, z = strength, w = foam

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

    // IS THIS EDGE A REAL CURTAIN? Only a genuine drop (a waterfall face, a step down to a lower
    // pool, an open border) should draw a side face at all. Between two water cells at the same
    // level there is nothing to show, and drawing one anyway is what painted a bright GRID over
    // every river and lake surface — reported as the water looking tiled.
    //
    // Two separate causes, both fixed here:
    //   1. THE OUTWARD NUDGE. Side faces are pushed 0.04 out so a falling curtain does not z-fight
    //      the cliff behind it. But that pushes them PAST the cell boundary and over the NEIGHBOUR's
    //      top quad, so even a perfectly collapsed side face laid a 0.04-wide strip of extra
    //      transparent geometry along every cell edge. Blended twice, that reads as a bright line.
    //   2. A SINGLE PER-EDGE BOTTOM. WaterManager passes min(corner, corner) as the edge bottom
    //      while the side face's TOP edge spans both corners, so wherever the surface slopes — i.e.
    //      everywhere on a flowing river — the "collapsed" face was actually a thin sliver.
    //
    // So: take the two corners belonging to THIS edge, and treat it as a curtain only if the skirt
    // bottom is meaningfully below both. Otherwise collapse the face onto its own corner heights
    // exactly (zero area, no fragments) and skip the nudge.
    float ca, cb;
    if      (edge == 0) { ca = inCorners[1]; cb = inCorners[2]; }   // +x
    else if (edge == 1) { ca = inCorners[0]; cb = inCorners[3]; }   // -x
    else if (edge == 2) { ca = inCorners[2]; cb = inCorners[3]; }   // +z
    else                { ca = inCorners[0]; cb = inCorners[1]; }   // -z
    // ⚑GROUND: 0.05 voxel. Below that a "drop" is smaller than the surface's own ripple detail and
    // there is physically nothing to render a wall of water for.
    bool curtain = (min(ca, cb) - inSkirt[edge]) > 0.05;

    // vtype 0 = top face corner, 2 = side-face top corner, 1 = side-face bottom (skirt).
    float y = (vtype == 1 && curtain) ? inSkirt[edge] : inCorners[idx];

    vec3 world = vec3(inCenterDepth.x + ox, y, inCenterDepth.z + oz);
    if (vtype != 0 && curtain) {
        vec2 n = vec2(0.0);
        if      (edge == 0) n = vec2( 1.0, 0.0);
        else if (edge == 1) n = vec2(-1.0, 0.0);
        else if (edge == 2) n = vec2( 0.0, 1.0);
        else                n = vec2( 0.0,-1.0);
        world.x += n.x * 0.04;
        world.z += n.y * 0.04;
    }

    // Ripple displacement — applied AFTER the curtain decision (which must read the UNDISPLACED
    // corners or side faces flicker on/off at the 0.05 threshold) and never to skirt bottoms
    // (vtype 1): a curtain hangs from a rippling lip, its base stays put. Scaled by column depth
    // so films barely move while ponds ring fully; clamped so a big impulse can't tear the surface.
    if (vtype != 1) {
        vec2 uv = (world.xz - pc.ripple.xy) * pc.ripple.z;
        if (uv.x > 0.0 && uv.y > 0.0 && uv.x < 1.0 && uv.y < 1.0) {
            float h = textureLod(rippleTex, uv, 0.0).r;
            float depthScale = clamp(inCenterDepth.w, 0.0, 1.0);
            world.y += clamp(h, -0.5, 0.5) * pc.ripple.w * depthScale;
        }
    }

    fragWorldPos    = world;
    fragColumnDepth = inCenterDepth.w;
    fragSide        = (vtype == 0) ? 0.0 : 1.0;
    fragFlow        = inFlow;
    gl_Position  = pc.viewProj * vec4(world, 1.0);
}
