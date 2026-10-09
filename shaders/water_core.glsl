// water_core.glsl — shared layout of the WaterCore GPU kernels (docs/WaterCore.md §15.11).
// Mirrors engine/include/core/water/WaterCore.h: a MAC grid of nx*ny*nz cells, fill fractions f at
// cell centres, face velocities u (nx+1,ny,nz), v (nx,ny+1,nz), w (nx,ny,nz+1), occupancy occ
// (0 Air, 1 Solid, 2 Unknown = hold). Index formulas are the grid's own (WaterGrid::idx/uIdx/vIdx/wIdx).
// Every kernel takes the same push block so the host can share one struct.

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
} pc;

uint cellIdx(int x, int y, int z) { return uint(x) + uint(pc.nx) * (uint(y) + uint(pc.ny) * uint(z)); }
uint uIdx(int x, int y, int z)    { return uint(x) + uint(pc.nx + 1) * (uint(y) + uint(pc.ny) * uint(z)); }
uint vIdx(int x, int y, int z)    { return uint(x) + uint(pc.nx) * (uint(y) + uint(pc.ny + 1) * uint(z)); }
uint wIdx(int x, int y, int z)    { return uint(x) + uint(pc.nx) * (uint(y) + uint(pc.ny) * uint(z)); }
bool inBounds(int x, int y, int z) { return x >= 0 && y >= 0 && z >= 0 && x < pc.nx && y < pc.ny && z < pc.nz; }

// Decode a flat thread id into lattice coordinates for a lattice of size (sx, sy, sz).
ivec3 decode(uint id, int sx, int sy) { return ivec3(int(id % uint(sx)), int((id / uint(sx)) % uint(sy)), int(id / uint(sx * sy))); }

#endif
