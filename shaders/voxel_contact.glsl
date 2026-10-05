// voxel_contact.glsl — dynamic body vs. the STATIC voxel occupancy grid.
//
// Shared by solver_voxel.comp (contact generation) and solver_hardcontact.comp (the
// post-solve safety projection) so the two passes can never disagree about where the
// ground is. docs/DebrisSettlingPlan.md §5 has the history; the short version:
//
//   The old path ran a per-voxel OBB-vs-cube SAT. Three defects made debris bubble:
//    (1) the contact point was the CENTRE of the voxel's face, not where the body touches,
//        so every resting contact applied a wrong torque (tilting, rocking, hovering on
//        edges, rolling cubes);
//    (2) each voxel was tested in isolation, so a body sunk a few cm into a flat floor got
//        SIDEWAYS normals from the faces between floor voxels ("internal edges") — sideways
//        kicks out of craters, and pushes DOWN through the slab (tunnelling);
//    (3) the warm-start key was the voxel id, so a sliding body lost its contact stiffness
//        every time it crossed a voxel boundary.
//
// Model here: the body is sampled at 26 points on its own surface (8 corners, 12 edge
// midpoints, 6 face centres). A point inside a solid cell is pushed out through the
// nearest EXPOSED face of that cell (a face whose neighbour is empty) — internal faces can
// never produce a normal. A point in an empty cell within `margin` of a solid neighbour's
// face yields a speculative (separated, pen < 0) contact so resting bodies keep their
// constraint. The contact point is the sample point itself (the true lever arm), and the
// warm-start feature is (sample, face direction) — stable while the body slides.
//
// Requires the including shader to declare `occupancy[]` (the static bitfield).

// OCC_X/Y/Z, OCC_HALF_X/Z, OCC_Y_OFFSET (the occupancy window): solver_shared.h.
#include "solver_shared.h"

bool isOccupied(int wx, int wy, int wz) {
    int lx = wx + OCC_HALF_X;
    int ly = wy + OCC_Y_OFFSET;
    int lz = wz + OCC_HALF_Z;
    if (lx < 0 || lx >= OCC_X || ly < 0 || ly >= OCC_Y || lz < 0 || lz >= OCC_Z) return false;
    int  lin  = lx + ly * OCC_X + lz * OCC_X * OCC_Y;
    uint word = uint(lin) >> 5u;
    uint bit  = uint(lin) & 31u;
    return (occupancy[word] & (1u << bit)) != 0u;
}
bool isOccupiedI(ivec3 c) { return isOccupied(c.x, c.y, c.z); }

const int VOXEL_SAMPLES = 26;

// Sample k in body-local units of the half-extents: corners, edge midpoints, face centres.
vec3 voxelSampleLocal(int k) {
    if (k < 8) {
        return vec3((k & 1) != 0 ? 1.0 : -1.0, (k & 2) != 0 ? 1.0 : -1.0, (k & 4) != 0 ? 1.0 : -1.0);
    }
    if (k < 20) {                       // 12 edges: axis a is 0, the other two are ±1
        int e = k - 8;
        int a = e / 4;
        float s0 = (e & 1) != 0 ? 1.0 : -1.0;
        float s1 = (e & 2) != 0 ? 1.0 : -1.0;
        if (a == 0) return vec3(0.0, s0, s1);
        if (a == 1) return vec3(s0, 0.0, s1);
        return vec3(s0, s1, 0.0);
    }
    int f = k - 20;                     // 6 faces
    vec3 v = vec3(0.0);
    v[f >> 1] = ((f & 1) != 0) ? 1.0 : -1.0;
    return v;
}
bool voxelSampleIsCorner(int k) { return k < 8; }

// Neighbour offset index d (0..25) over the 26-neighbourhood, centre excluded.
ivec3 neighbourOffset(int d) {
    int k = d < 13 ? d : d + 1;          // skip the centre (k == 13)
    return ivec3(k % 3, (k / 3) % 3, k / 9) - ivec3(1);
}

// Vector from x (fractional position f inside its cell) to the nearest point of the
// neighbour cell at offset o: per axis, 0 where o is 0, else the distance to that cell wall.
vec3 toNeighbour(vec3 f, ivec3 o) {
    vec3 v = vec3(0.0);
    for (int a = 0; a < 3; ++a) {
        if (o[a] > 0)      v[a] =  (1.0 - f[a]);
        else if (o[a] < 0) v[a] = -f[a];
    }
    return v;
}

// Contact of world point x against the static grid — a signed distance to the voxel solid,
// exact within one cell (26-neighbourhood).
//   returns true and sets n (unit, pointing OUT of the solid, i.e. from static toward the
//   body), pen (> 0 penetrating, < 0 separated by -pen, only down to -margin) and the
//   neighbour index used for the warm-start feature.
//   Inside solid, the escape is toward the NEAREST EMPTY neighbour: through a face on a flat
//   surface, but DIAGONALLY out of a concave edge/corner (a pit's inside edge). Only taking
//   face escapes there pushed corner samples out through the far top face — a 1 m kick.
//   Interior points (no empty cell in the 26-neighbourhood) return false.
bool voxelPointContact(vec3 x, float margin, out vec3 n, out float pen, out int dirIdx) {
    ivec3 c = ivec3(floor(x));
    vec3  f = x - vec3(c);              // 0..1 inside the cell
    n = vec3(0.0); pen = 0.0; dirIdx = -1;
    bool inside = isOccupiedI(c);
    float best = inside ? 1e30 : margin;
    vec3  bestV = vec3(0.0);
    for (int d = 0; d < 26; ++d) {
        ivec3 o = neighbourOffset(d);
        // Inside: escape toward an EMPTY neighbour. Outside: approach to a SOLID neighbour.
        if (isOccupiedI(c + o) == inside) continue;
        vec3  v    = toNeighbour(f, o);
        float dist = length(v);
        if (dist < best) { best = dist; bestV = v; dirIdx = d; }
    }
    if (dirIdx < 0) return false;
    if (inside) {
        n   = (best > 1e-6) ? bestV / best : vec3(neighbourOffset(dirIdx));
        n   = normalize(n);
        pen = best;
    } else {
        // The solid is in direction bestV; the normal points from it toward the point.
        n   = (best > 1e-6) ? -bestV / best : -normalize(vec3(neighbourOffset(dirIdx)));
        pen = -best;
    }
    return true;
}
