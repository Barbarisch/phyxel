// voxel_contact.glsl — dynamic body vs. the STATIC world, read from the SHARED micro occupancy.
//
// Shared by solver_voxel.comp (contact generation) and solver_hardcontact.comp (the
// post-solve safety projection) so the two passes can never disagree about where the
// ground is. docs/DebrisSettlingPlan.md §5 has the contact-model history; the short version:
//
//   The old path ran a per-voxel OBB-vs-cube SAT. Three defects made debris bubble:
//    (1) the contact point was the CENTRE of the voxel's face, not where the body touches;
//    (2) each voxel was tested in isolation, so internal faces between floor voxels produced
//        SIDEWAYS normals (sideways kicks, tunnelling);
//    (3) the warm-start key was the voxel id, lost every time a sliding body crossed a voxel.
//
// Model here: the body is sampled at 26 points on its own surface (8 corners, 12 edge
// midpoints, 6 face centres). A point inside solid is pushed out toward the NEAREST EMPTY
// neighbour cell (so internal faces never produce a normal); a point in empty space within
// `margin` of solid yields a speculative (separated, pen < 0) contact. The contact point is the
// sample itself (the true lever arm); the warm-start feature is (sample, direction).
//
// WHAT THE WORLD IS (DebrisInteractionPlan 1c, 2026-10-05). Debris used to read its own CUBE
// bitfield (512x256x512 around the origin, blind to sub-voxel geometry, filled by two paths that
// disagreed). It now reads THE occupancy CPU physics and lighting already share — the
// micro-resolution packed pool (occupancy.glsl), recentred on the viewer:
//   1. cube state of the sample's cell + its 26 neighbours: empty / mixed / solid / UNKNOWN.
//      Any unknown (outside the pool's box, chunk not resident, pool not ready) -> the sample is
//      unknown and the solver HOLDS the body this tick instead of letting it fall through a world
//      it knows nothing about;
//   2. no mixed cube nearby -> exactly the old cube-level search (a full-cube world collides as
//      before — the settle bench is the regression check);
//   3. a mixed cube nearby -> the same search at MICRO resolution, walking up to
//      DEBRIS_MICRO_ESCAPE_STEPS cells per direction when inside solid; deeper than that falls
//      back to the cube search with mixed counted as solid.
// CPU mirror: engine/include/core/DebrisContact.h (DebrisContactTest). If you change one, change BOTH.
//
// Requires the including shader to declare, BEFORE including this file:
//   #define PHX_OCC_BINDING_DIR / PHX_OCC_BINDING_POOL (its own binding slots for the pool), and a
//   push-constant block `pc` with an `ivec4 occBox` (box min chunk + readiness bit).

#include "solver_shared.h"
#include "occupancy.glsl"

const int VOXEL_SAMPLES = 26;
const int DEBRIS_MICRO_ESCAPE_STEPS = 4;   // == DebrisContact::kMicroEscapeSteps
const int DEBRIS_CUBE_EMPTY = 0, DEBRIS_CUBE_MIXED = 1, DEBRIS_CUBE_SOLID = 2, DEBRIS_CUBE_UNKNOWN = 3;

// Cube state from the pool: occupancy.glsl's cube bits, plus UNKNOWN where it has no data.
int debrisCubeState(ivec3 cube) {
    if (phxOccupancyState(cube * 9, pc.occBox) == PHX_OCC_UNKNOWN) return DEBRIS_CUBE_UNKNOWN;
    return phxCubeOccupancy(cube, pc.occBox);   // 0 empty, 1 mixed, 2 solid
}
bool debrisMicroSolid(ivec3 worldMicro) { return phxOccupancySolid(worldMicro, pc.occBox); }

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

// Vector from fractional f (0..1 in its cell) to the nearest point of the cell k steps along o.
vec3 toCellAlong(vec3 f, ivec3 o, int k) {
    vec3 v = vec3(0.0);
    for (int a = 0; a < 3; ++a) {
        if (o[a] > 0)      v[a] =  (float(k - 1) + (1.0 - f[a]));
        else if (o[a] < 0) v[a] = -(float(k - 1) + f[a]);
    }
    return v;
}

// Cube-level states of the sample's 27-neighbourhood, filled by voxelPointContact.
int g_cubeSt[27];
ivec3 g_cubeC;
bool g_cubeMixedAsSolid;
bool debrisCubeOccupied(ivec3 cube) {
    ivec3 o = cube - g_cubeC + ivec3(1);
    int s = g_cubeSt[o.x + o.y * 3 + o.z * 9];
    return s == DEBRIS_CUBE_SOLID || (g_cubeMixedAsSolid && s == DEBRIS_CUBE_MIXED);
}

// The escape search on a unit grid; `micro` selects the grid (true: micro cells, false: cubes).
// Mirrors DebrisContact::escapeSearch.
bool debrisEscape(vec3 x, float margin, bool micro, int maxSteps,
                  out vec3 n, out float pen, out int dirIdx) {
    ivec3 c = ivec3(floor(x));
    vec3  f = x - vec3(c);
    n = vec3(0.0); pen = 0.0; dirIdx = -1;
    bool inside = micro ? debrisMicroSolid(c) : debrisCubeOccupied(c);
    int  steps  = inside ? maxSteps : 1;
    float best = inside ? 1e30 : margin;
    vec3  bestV = vec3(0.0);
    for (int d = 0; d < 26; ++d) {
        ivec3 o = neighbourOffset(d);
        for (int k = 1; k <= steps; ++k) {
            ivec3 cell = c + o * k;
            bool occ = micro ? debrisMicroSolid(cell) : debrisCubeOccupied(cell);
            if (occ == inside) continue;   // inside: toward EMPTY; outside: toward SOLID
            vec3  v    = toCellAlong(f, o, k);
            float dist = length(v);
            if (dist < best) { best = dist; bestV = v; dirIdx = d; }
            break;
        }
    }
    if (dirIdx < 0) return false;
    if (inside) {
        n   = (best > 1e-6) ? bestV / best : vec3(neighbourOffset(dirIdx));
        n   = normalize(n);
        pen = best;
    } else {
        n   = (best > 1e-6) ? -bestV / best : -normalize(vec3(neighbourOffset(dirIdx)));
        pen = -best;
    }
    return true;
}

// Contact of world point x against the static world.
//   returns true and sets n (unit, out of the solid toward the body), pen (> 0 penetrating,
//   < 0 separated, only down to -margin) and the neighbour index used for the warm-start feature.
//   `unknown` is set (and false returned) when any cell it needed has no occupancy data.
// Mirrors DebrisContact::pointContact.
bool voxelPointContact(vec3 x, float margin, out vec3 n, out float pen, out int dirIdx, out bool unknown) {
    n = vec3(0.0); pen = 0.0; dirIdx = -1; unknown = false;
    ivec3 c = ivec3(floor(x));
    g_cubeC = c;
    bool anyMixed = false;
    for (int k = 0; k < 27; ++k) {
        int s = debrisCubeState(c + ivec3(k % 3, (k / 3) % 3, k / 9) - ivec3(1));
        if (s == DEBRIS_CUBE_UNKNOWN) { unknown = true; return false; }
        g_cubeSt[k] = s;
        if (s == DEBRIS_CUBE_MIXED) anyMixed = true;
    }
    if (anyMixed) {
        vec3 xm = x * 9.0;
        if (debrisEscape(xm, margin * 9.0, true, DEBRIS_MICRO_ESCAPE_STEPS, n, pen, dirIdx)) {
            pen /= 9.0;
            return true;
        }
        if (!debrisMicroSolid(ivec3(floor(xm)))) return false;   // open air, nothing within margin
        // Deep inside sub-voxel solid: fall through to the cube search, mixed counted as solid.
    }
    g_cubeMixedAsSolid = anyMixed;
    return debrisEscape(x, margin, false, 1, n, pen, dirIdx);
}

