#version 450

// Grass blade SHADOW caster. Depth-only, but NOT a plain empty fragment shader: the blade
// silhouette is procedural (a triangular taper carved by discard in grass.frag), so a
// depth-only pass without the same cutout would record every blade as a full RECTANGLE and
// stamp solid blocks into the shadow map — grass would shade the ground like a wall.
// The taper here MUST stay identical to grass.frag's.

layout(location = 2) in float vGrad;   // 0 at blade base .. 1 at tip
layout(location = 3) in float vSide;   // -1..1 across blade width

void main() {
    float taper = 1.0 - vGrad * 0.92;   // must match grass.frag
    if (abs(vSide) > taper) discard;
}
