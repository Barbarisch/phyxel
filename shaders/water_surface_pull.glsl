// water_surface_pull.glsl — WaterCore 29 S1: the water's surface mesh made ON THE DEVICE from the volume's dense surface field.
// The rules of buildWaterSurfaceMeshRect (engine/src/core/water/WaterSurfaceMesh.cpp) exactly - a top quad per run with corner
// heights averaged over the neighbouring columns that hold an overlapping run (the 27.15 "other drawer" rule first), a side face
// where a run meets a neighbour with no overlapping run that is not solid up to its top. Shared by wc_surface_mesh.comp (the
// corners, the meta words, the extras list, the test's expansion) and water_surface_pull.vert (the draw), so the two cannot drift.
//
// The includer defines WSP_SET, WSP_B_SURF, WSP_B_CORNERS, WSP_B_META, WSP_SURF_QUAL and WSP_MESH_QUAL (readonly in a vertex shader).
//   surfD[c]              one SurfaceColumn per dense column c = x + nx z (16 floats, WaterSurfaceMesh.h)
//   meshCorners[16 c + 4 r + k]  run r's corner heights, k: 0 (-x,-z) 1 (+x,-z) 2 (+x,+z) 3 (-x,+z)
//   meshMeta[c]           bits 0-2 the run count drawn (0 for a dry or recorded column), bit 4 + 4 r + s: run r's side s
//                         (s: 0 -x, 1 +x, 2 -z, 3 +z) exists
#ifndef WATER_SURFACE_PULL_GLSL
#define WATER_SURFACE_PULL_GLSL

struct SurfCol { float top[4]; float bottom[4]; float solidTopY; float runs; float foam; float u; float w; float body; float pad1; float pad2; };
layout(std430, set = WSP_SET, binding = WSP_B_SURF) WSP_SURF_QUAL buffer WspSurf { SurfCol surfD[]; };
layout(std430, set = WSP_SET, binding = WSP_B_CORNERS) WSP_MESH_QUAL buffer WspCorners { float meshCorners[]; };
layout(std430, set = WSP_SET, binding = WSP_B_META) WSP_MESH_QUAL buffer WspMeta { uint meshMeta[]; };

struct WspGeo { int ox, oz, nx, nz; float h; };

uint wspIdx(WspGeo g, int x, int z) { return uint(x) + uint(g.nx) * uint(z); }
bool wspIn(WspGeo g, int x, int z) { return x >= 0 && z >= 0 && x < g.nx && z < g.nz; }
int wspRuns(uint c) { return int(surfD[c].runs + 0.5); }   // as static_cast<int>(runs + 0.5f): a recorded column (-1) is 0
// col(): inside the field and not another drawer's column (27.15: runs < 0 - a recorded tile, the record draws it)
bool wspCol(WspGeo g, int x, int z) { return wspIn(g, x, z) && surfD[wspIdx(g, x, z)].runs >= 0.0; }
int wspOverlap(uint c, float lo, float hi) {
    const int n = wspRuns(c);
    for (int r = 0; r < 4; ++r) if (r < n && surfD[c].top[r] > lo && surfD[c].bottom[r] < hi) return r;
    return -1;
}
float wspCornerY(WspGeo g, int x, int z, int cx, int cz, float lo, float hi, float ownTop) {
    precise float gSum = 0.0; int gN = 0;
    for (int dz = -1; dz <= 0; ++dz) for (int dx = -1; dx <= 0; ++dx) {
        const int gx = x + cx + dx, gz = z + cz + dz;
        if (!wspIn(g, gx, gz)) continue;
        const uint gi = wspIdx(g, gx, gz);
        const float t0 = surfD[gi].top[0], b0 = surfD[gi].bottom[0];
        precise float t0h = t0 + g.h;
        if (surfD[gi].runs < 0.0 && t0 > b0 && b0 < hi && t0h > lo) { gSum += t0; ++gN; }
    }
    if (gN > 0) return gSum / float(gN);
    precise float sum = 0.0; int n = 0;
    for (int dz = -1; dz <= 0; ++dz) for (int dx = -1; dx <= 0; ++dx) {
        const int nx_ = x + cx + dx, nz_ = z + cz + dz;
        if (!wspCol(g, nx_, nz_)) continue;
        if (nx_ == x && nz_ == z) { sum += ownTop; ++n; continue; }
        const uint ni = wspIdx(g, nx_, nz_);
        const int r = wspOverlap(ni, lo, hi);
        if (r >= 0) { sum += surfD[ni].top[r]; ++n; }
    }
    return n > 0 ? sum / float(n) : ownTop;
}
const ivec2 kWspSideDir[4] = ivec2[4](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));

// One quad of the mesh: column c, run r, face 0 = the top, 1-4 = the side faces -x +x -z +z. False = no such quad.
// p[0..3] wound as buildWaterSurfaceMeshRect's quad(a, b, c, d); indices 0 1 2 0 2 3.
bool wspQuad(WspGeo g, uint c, int r, int face, out vec3 p[4], out vec3 nrm, out float depth, out float side, out float foam, out vec2 flow) {
    p[0] = vec3(0.0); p[1] = vec3(0.0); p[2] = vec3(0.0); p[3] = vec3(0.0);
    nrm = vec3(0.0, 1.0, 0.0); depth = 0.0; side = 0.0; foam = 0.0; flow = vec2(0.0);
    const int n = int(meshMeta[c] & 7u);
    if (r >= n) return false;
    if (face > 0 && (meshMeta[c] & (1u << uint(4 + 4 * r + face - 1))) == 0u) return false;
    const int x = int(c % uint(g.nx)), z = int(c / uint(g.nx));
    const float h = g.h;
    precise float ox = float(g.ox) * h, oz = float(g.oz) * h;
    precise float x0 = ox + float(x) * h, z0 = oz + float(z) * h;
    precise float x1 = x0 + h, z1 = z0 + h;
    const float y00 = meshCorners[16u * c + 4u * uint(r) + 0u], y10 = meshCorners[16u * c + 4u * uint(r) + 1u];
    const float y11 = meshCorners[16u * c + 4u * uint(r) + 2u], y01 = meshCorners[16u * c + 4u * uint(r) + 3u];
    const float top = surfD[c].top[r], bottom = surfD[c].bottom[r];
    precise float dep = top - bottom;
    depth = dep;
    if (r == n - 1) { foam = surfD[c].foam; flow = vec2(surfD[c].u, surfD[c].w); }   // G2: foam and flow belong to the top run
    vec3 outward;
    if (face == 0) {
        p[0] = vec3(x0, y00, z0); p[1] = vec3(x0, y01, z1); p[2] = vec3(x1, y11, z1); p[3] = vec3(x1, y10, z0);
        outward = vec3(0.0, 1.0, 0.0); side = 0.0;
    } else {
        const int s = face - 1;
        const ivec2 d = kWspSideDir[s];
        const uint nb = wspIdx(g, x + d.x, z + d.y);
        const float floorY = max(bottom, surfD[nb].solidTopY);
        vec2 a, b; float ya, yb;
        if (s == 0)      { a = vec2(x0, z0); b = vec2(x0, z1); ya = y00; yb = y01; }
        else if (s == 1) { a = vec2(x1, z1); b = vec2(x1, z0); ya = y11; yb = y10; }
        else if (s == 2) { a = vec2(x1, z0); b = vec2(x0, z0); ya = y10; yb = y00; }
        else             { a = vec2(x0, z1); b = vec2(x1, z1); ya = y01; yb = y11; }
        p[0] = vec3(a.x, ya, a.y); p[1] = vec3(b.x, yb, b.y); p[2] = vec3(b.x, floorY, b.y); p[3] = vec3(a.x, floorY, a.y);
        outward = vec3(float(d.x), 0.0, float(d.y)); side = 1.0;
    }
    vec3 nv = cross(p[2] - p[0], p[1] - p[3]);
    if (dot(nv, outward) < 0.0) nv = -nv;   // the geometric normal, oriented out of the water
    const float len = length(nv);
    nrm = len > 1e-12 ? nv / len : outward;
    return true;
}

#endif
