// water_core.glsl — shared layout of the WaterCore GPU kernels (docs/WaterCore.md §15.11, §26.6).
// Mirrors engine/include/core/water/WaterCore.h: a MAC grid of nx*ny*nz cells, fill fractions f at
// cell centres, face velocities u (nx+1,ny,nz), v (nx,ny+1,nz), w (nx,ny,nz+1), occupancy occ
// (0 Air, 1 Solid, 2 Unknown = hold). Every kernel takes the same push block so the host can share one struct.
//
// STORAGE (26.6 P2a). Two layouts, chosen per volume by pc.bricked:
//  * dense (bricked = 0; the particle transport only): the grid's own index formulas (WaterGrid::idx/uIdx/vIdx/wIdx).
//  * bricked (bricked = 1): world-aligned bricks of 8^3 cells held in SLOTS of 512; a cell's index is slot x 512 + local,
//    and a face lattice stores each cell's LOWER face at its cell's index. A brick map (one int per brick of the volume's
//    brick box: slot, 0 = absent) and a slot table live in the Bricks buffer. Slot 0 is the ABSENT brick: all zero, never
//    written - an absent cell reads as dry air (f 0, velocity 0, p 0, occupancy Air). A thread's id IS its storage index,
//    so `x[id]` addresses the thread's own cell or face in both layouts.
// A kernel that uses bricks defines WC_BRICKS_BINDING (its binding number for the Bricks buffer) before the include.

#ifndef WATER_CORE_GLSL
#define WATER_CORE_GLSL

layout(push_constant) uniform WcPush {
    int   nx, ny, nz;
    int   mode;          // kernel-specific sub-mode (direction, parity, layer, lattice...)
    float dt;            // substep, s
    float h;             // cell size, m
    float thr;           // liquidThreshold (0.5)
    float gravity;       // m/s^2
    float filmHold;      // filmHoldDepth / h (cell units)
    float param0;        // kernel-specific
    float param1;
    int   iparam;
    float thetaMin;      // SolverParams::thetaMin - the ghost-fluid clamp (wc_theta.glsl; 24.1)
    // 26.6 bricks
    int   bricked;       // 1 = bricked storage
    int   slotsUsed;     // slots 0..slotsUsed-1 are addressed (0 = absent)
    int   ox, oy, oz;    // the volume origin's offset inside its first brick (cells, 0..7)
    int   bnx, bny, bnz; // the brick box (bricks)
    int   slotTableOff;  // Bricks[] offsets: the slot table (packed brick coords per slot) and the column table
    int   colTableOff;
    int   counterOff;    // Bricks[counterOff + k]: k 0 = flux into absent cells (faces), 1 = sentinel dirty
    int   colCount;      // resident brick columns (column dispatches: 64 threads each)
    int   surfaceGhost;  // 26.9: 1 = the hydrostatic ghost at a clamped surface (wc_theta.glsl wcSurfaceGhost); 0 = p_air = 0
    float quietRise;     // 26.9: m/s - under it a partial cell under an emptier one fills before water crosses its ceiling
} pc;

#ifdef WC_BRICKS_BINDING
layout(std430, set = 0, binding = WC_BRICKS_BINDING) buffer Bricks { int bricks[]; };

int brickSlot(ivec3 q) {   // q = cell + origin offset (brick-box cell coords)
    const ivec3 b = q >> 3;
    if (b.x < 0 || b.y < 0 || b.z < 0 || b.x >= pc.bnx || b.y >= pc.bny || b.z >= pc.bnz) return 0;
    return bricks[b.x + pc.bnx * (b.y + pc.bny * b.z)];
}
uint brickIdx(int x, int y, int z) {
    const ivec3 q = ivec3(x + pc.ox, y + pc.oy, z + pc.oz);
    return uint(brickSlot(q)) * 512u + uint((q.x & 7) + 8 * ((q.y & 7) + 8 * (q.z & 7)));
}
// a cell inside the box whose brick is not stored (sparse residency) - never the case in a dense (all-kept) volume
bool absentCell(int x, int y, int z) { return pc.bricked != 0 && brickSlot(ivec3(x + pc.ox, y + pc.oy, z + pc.oz)) == 0; }
#define WC_DENSE (pc.bricked == 0)
#else
uint brickIdx(int x, int y, int z) { return 0u; }
bool absentCell(int x, int y, int z) { return false; }
#define WC_DENSE true
#endif

uint cellIdx(int x, int y, int z) { return WC_DENSE ? uint(x) + uint(pc.nx) * (uint(y) + uint(pc.ny) * uint(z)) : brickIdx(x, y, z); }
uint uIdx(int x, int y, int z)    { return WC_DENSE ? uint(x) + uint(pc.nx + 1) * (uint(y) + uint(pc.ny) * uint(z)) : brickIdx(x, y, z); }
uint vIdx(int x, int y, int z)    { return WC_DENSE ? uint(x) + uint(pc.nx) * (uint(y) + uint(pc.ny + 1) * uint(z)) : brickIdx(x, y, z); }
uint wIdx(int x, int y, int z)    { return WC_DENSE ? uint(x) + uint(pc.nx) * (uint(y) + uint(pc.ny) * uint(z)) : brickIdx(x, y, z); }
uint latIdx(int L, ivec3 c)       { return L == 0 ? uIdx(c.x, c.y, c.z) : (L == 1 ? vIdx(c.x, c.y, c.z) : wIdx(c.x, c.y, c.z)); }
bool inBounds(int x, int y, int z) { return x >= 0 && y >= 0 && z >= 0 && x < pc.nx && y < pc.ny && z < pc.nz; }

// Decode a flat thread id into lattice coordinates for a lattice of size (sx, sy, sz) - the DENSE layout only.
ivec3 decode(uint id, int sx, int sy) { return ivec3(int(id % uint(sx)), int((id / uint(sx)) % uint(sy)), int(id / uint(sx * sy))); }

// The site a thread owns. lattice 0 u, 1 v, 2 w (faces: x in [0, nx] for u ...), 3 cells. False = no site (an idle id,
// the absent slot, a padding cell or face outside the lattice). In both layouts the thread's id is the site's index.
bool threadSite(uint id, int lattice, out ivec3 c) {
    const int sx = pc.nx + (lattice == 0 ? 1 : 0), sy = pc.ny + (lattice == 1 ? 1 : 0), sz = pc.nz + (lattice == 2 ? 1 : 0);
#ifdef WC_BRICKS_BINDING
    if (pc.bricked != 0) {
        const uint slot = id >> 9u, local = id & 511u;
        if (slot == 0u || slot >= uint(pc.slotsUsed)) { c = ivec3(-1); return false; }
        const int packed = bricks[pc.slotTableOff + int(slot)];
        if (packed < 0) { c = ivec3(-1); return false; }   // a free slot
        const ivec3 b = ivec3(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        c = b * 8 + ivec3(int(local & 7u), int((local >> 3u) & 7u), int(local >> 6u)) - ivec3(pc.ox, pc.oy, pc.oz);
        return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < sx && c.y < sy && c.z < sz;
    }
#endif
    c = decode(id, sx, sy);
    return c.z < sz;
}
// The column (x, z) a column thread owns: dense - id = x + nx z; bricked - 64 threads per resident brick column
// (the column table), id = entry x 64 + lx + 8 lz. Column-indexed buffers (dropped, births, surf) use the id as index.
bool threadColumn(uint id, out int x, out int z) {
#ifdef WC_BRICKS_BINDING
    if (pc.bricked != 0) {
        const int e = int(id >> 6u), local = int(id & 63u);
        if (e >= pc.colCount) { x = -1; z = -1; return false; }
        const int packed = bricks[pc.colTableOff + e];
        x = (packed & 1023) * 8 + (local & 7) - pc.ox;
        z = ((packed >> 10) & 1023) * 8 + (local >> 3) - pc.oz;
        return x >= 0 && z >= 0 && x < pc.nx && z < pc.nz;
    }
#endif
    x = int(id % uint(pc.nx)); z = int(id / uint(pc.nx));
    return z < pc.nz;
}

#endif
