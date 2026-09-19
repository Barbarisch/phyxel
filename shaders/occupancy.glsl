// ============================================================================================
// occupancy.glsl — THE sub-voxel occupancy query and light-visibility trace, shared by every
// shader that shades a light. Sibling of lighting.glsl.
//
// WHY THIS EXISTS (docs/UnifiedLightingPlan.md U2 / D14). The visibility term lived inside
// voxel.frag alone, while THREE shaders shade point lights: voxel, character and
// transparent_voxel. So a lantern sealed inside a stone room correctly stopped lighting the
// world's voxels — and went right on lighting any CHARACTER standing outside it, and shining
// through GLASS. The reported bug was reported fixed while two thirds of the surfaces that can
// be lit still had no occlusion at all.
//
// CONTRACT, deliberately narrower than lighting.glsl's. lighting.glsl is pure functions with no
// implicit reads of anything. That is impossible here: the occupancy IS two storage buffers, so
// this file declares bindings 11 and 12. What it does NOT do is read any shader's UBO — the
// `occBox` value is passed in as a parameter, because voxel.frag, character.frag and
// transparent_voxel.frag each declare a different prefix of the shared uniform block and none of
// them can be assumed to have reached the same field.
//
// Include this ONLY from a shader whose pipeline uses the shared set-0 layout (every scene
// pipeline does — they all take vulkanDevice.getDescriptorSetLayout()).
//
// occBox: xyz = the covered box's min corner in CHUNK coords (it follows the viewer),
//         w   = bitfield — bit0 occupancy readable, bit1 light tracing on, bit2 sky tracing on.
// ============================================================================================

#ifndef PHYXEL_OCCUPANCY_GLSL
#define PHYXEL_OCCUPANCY_GLSL

layout(std430, set = 0, binding = 11) readonly buffer OccDirectory { uint occDir[]; };
layout(std430, set = 0, binding = 12) readonly buffer OccPool      { uint occPool[]; };

const uint  PHX_OCC_NO_CHUNK        = 0xFFFFFFFFu;
const int   PHX_OCC_DIR_X           = 32;
const int   PHX_OCC_DIR_Y           = 16;
const int   PHX_OCC_DIR_Z           = 32;
const int   PHX_OCC_CUBE_WORDS      = 1024;   // 32^3 bits
const int   PHX_OCC_MICRO_WORDS     = 23;     // 729 bits
const int   PHX_OCC_MICRO_PER_CHUNK = 288;    // 32 cubes * 9 micro

// Floor-divide. GLSL's / truncates toward zero exactly like C++'s, so this must exist for the same
// reason floorDiv does in VoxelLightOccupancy.cpp: world coordinates go negative, and truncation
// folds the two chunks either side of zero onto one directory slot.
int phxFloorDiv(int a, int b) {
    int q = a / b;
    int r = a - q * b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

/// Is this world MICRO position (world unit * 9) inside solid matter?
/// THE line-for-line mirror of Phyxel::Graphics::packedPoolSolidAt — that C++ function exists
/// precisely so this addressing is unit-tested before it ever runs on a GPU, where a mistake
/// produces a picture nobody can debug. If you change one, change BOTH.
/// Returns false outside the covered box or when occupancy is absent — degrading to "no
/// occlusion", never to invented geometry.
bool phxOccupancySolid(ivec3 worldMicro, ivec4 occBox) {
    if ((occBox.w & 1) == 0) return false;

    ivec3 chunkCoord = ivec3(phxFloorDiv(worldMicro.x, PHX_OCC_MICRO_PER_CHUNK),
                             phxFloorDiv(worldMicro.y, PHX_OCC_MICRO_PER_CHUNK),
                             phxFloorDiv(worldMicro.z, PHX_OCC_MICRO_PER_CHUNK));
    ivec3 c = chunkCoord - occBox.xyz;
    if (c.x < 0 || c.x >= PHX_OCC_DIR_X ||
        c.y < 0 || c.y >= PHX_OCC_DIR_Y ||
        c.z < 0 || c.z >= PHX_OCC_DIR_Z) return false;

    uint base = occDir[c.x + c.y * PHX_OCC_DIR_X + c.z * PHX_OCC_DIR_X * PHX_OCC_DIR_Y];
    if (base == PHX_OCC_NO_CHUNK) return false;

    // Chunk-local micro coords. Positive modulo, same reason as phxFloorDiv.
    ivec3 local = worldMicro - chunkCoord * PHX_OCC_MICRO_PER_CHUNK;

    ivec3 cube = local / 9;
    int ci = cube.z + cube.y * 32 + cube.x * 1024;

    uint solidBase = base + 1u;
    if (((occPool[solidBase + uint(ci >> 5)] >> uint(ci & 31)) & 1u) != 0u) return true;

    uint mixedBase = solidBase + uint(PHX_OCC_CUBE_WORDS);
    if (((occPool[mixedBase + uint(ci >> 5)] >> uint(ci & 31)) & 1u) == 0u) return false;

    // Binary search the ascending mixed-cube index list.
    uint n = occPool[base];
    uint idxBase = mixedBase + uint(PHX_OCC_CUBE_WORDS);
    uint lo = 0u, hi = n;
    while (lo < hi) {
        uint mid = (lo + hi) >> 1u;
        if (occPool[idxBase + mid] < uint(ci)) lo = mid + 1u; else hi = mid;
    }
    if (lo >= n || occPool[idxBase + lo] != uint(ci)) return false;

    ivec3 inCube = local - cube * 9;
    int bit = inCube.x + inCube.y * 9 + inCube.z * 81;
    uint microBase = idxBase + n + lo * uint(PHX_OCC_MICRO_WORDS);
    return ((occPool[microBase + uint(bit >> 5)] >> uint(bit & 31)) & 1u) != 0u;
}

/// Cube-level state of one CUBE cell: 0 = empty, 1 = MIXED (carries sub-voxel detail), 2 = solid.
/// The first half of phxOccupancySolid, addressed through the same micro path so there is ONE
/// addressing implementation to be wrong. CPU mirror: packedPoolCubeOccupancy.
int phxCubeOccupancy(ivec3 worldCube, ivec4 occBox) {
    if ((occBox.w & 1) == 0) return 0;
    ivec3 worldMicro = worldCube * 9;
    ivec3 chunkCoord = ivec3(phxFloorDiv(worldMicro.x, PHX_OCC_MICRO_PER_CHUNK),
                             phxFloorDiv(worldMicro.y, PHX_OCC_MICRO_PER_CHUNK),
                             phxFloorDiv(worldMicro.z, PHX_OCC_MICRO_PER_CHUNK));
    ivec3 c = chunkCoord - occBox.xyz;
    if (c.x < 0 || c.x >= PHX_OCC_DIR_X ||
        c.y < 0 || c.y >= PHX_OCC_DIR_Y ||
        c.z < 0 || c.z >= PHX_OCC_DIR_Z) return 0;
    uint base = occDir[c.x + c.y * PHX_OCC_DIR_X + c.z * PHX_OCC_DIR_X * PHX_OCC_DIR_Y];
    if (base == PHX_OCC_NO_CHUNK) return 0;
    ivec3 local = worldMicro - chunkCoord * PHX_OCC_MICRO_PER_CHUNK;
    ivec3 cube = local / 9;
    int ci = cube.z + cube.y * 32 + cube.x * 1024;
    uint solidBase = base + 1u;
    if (((occPool[solidBase + uint(ci >> 5)] >> uint(ci & 31)) & 1u) != 0u) return 2;
    uint mixedBase = solidBase + uint(PHX_OCC_CUBE_WORDS);
    if (((occPool[mixedBase + uint(ci >> 5)] >> uint(ci & 31)) & 1u) != 0u) return 1;
    return 0;
}

// --------------------------------------------------------------------------------------------
// THE TRAVERSAL — Amanatides & Woo DDA in MICRO space. Visits every micro cell the segment
// crosses, in order, and cannot skip one. CPU mirror: ddaHitsSolid() in VoxelLightOccupancy.cpp.
//
// This replaced a fixed-step march, which was structurally wrong rather than mistuned (D0): a
// fixed step is only safe when it is smaller than the thinnest feature; the thinnest feature is
// 1/9 u, so covering a 24 u ray safely costs ~432 samples. The two-rate compromise that made that
// affordable coarsened beyond 3 u and stepped straight over 1-micro roofs — a sealed room read
// 0.536 sky instead of 0, at the ONE wall thickness a hand-built rig had not used.
// --------------------------------------------------------------------------------------------
bool phxDdaHitsSolid(vec3 fromWorld, vec3 toWorld, int maxCells, ivec4 occBox) {
    vec3 a = fromWorld * 9.0, b = toWorld * 9.0;
    vec3 d = b - a;
    float len = length(d);
    if (len < 1e-6) return false;
    vec3 dir = d / len;

    ivec3 cell = ivec3(floor(a));
    ivec3 last = ivec3(floor(b));

    ivec3 stp;
    vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (dir[i] > 1e-9) {
            stp[i] = 1;
            tMax[i] = (float(cell[i] + 1) - a[i]) / dir[i];
            tDelta[i] = 1.0 / dir[i];
        } else if (dir[i] < -1e-9) {
            stp[i] = -1;
            tMax[i] = (a[i] - float(cell[i])) / -dir[i];
            tDelta[i] = 1.0 / -dir[i];
        } else {
            stp[i] = 0;
            tMax[i] = 3.4e38;
            tDelta[i] = 3.4e38;
        }
    }

    for (int n = 0; n < maxCells; ++n) {
        if (phxOccupancySolid(cell, occBox)) return true;
        if (cell == last) return false;
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) { cell.x += stp.x; tMax.x += tDelta.x; }
            else                 { cell.z += stp.z; tMax.z += tDelta.z; }
        } else {
            if (tMax.y < tMax.z) { cell.y += stp.y; tMax.y += tDelta.y; }
            else                 { cell.z += stp.z; tMax.z += tDelta.z; }
        }
        if (tMax.x > len && tMax.y > len && tMax.z > len) return false;
    }
    return false;
}

/// TWO-LEVEL segment test: does ANY solid matter lie on the segment? Walks CUBE cells (1 u) with
/// the same Amanatides & Woo stepping, answering each cube from its two bits: solid -> blocked,
/// empty -> continue, MIXED -> run the micro DDA over just this cube's slice of the segment.
/// Exactly the answer phxDdaHitsSolid gives (the CPU mirror packedPoolSegmentBlocked is tested
/// against the micro march on random segments), at a fraction of the cell visits: a 3.5 u segment
/// costs ~10 cube queries instead of ~80 micro ones, and a 16 u ray ~48 instead of 288 -- which is
/// what made the probe field's per-fragment leak guard affordable (G-141: 69.6 ms -> see §7).
/// The micro slice starts 1e-4 u inside its cube so float rounding at a cube boundary cannot
/// start it in a neighbour the segment never enters; see the note on end-cell semantics below.
bool phxSegmentBlocked(vec3 fromWorld, vec3 toWorld, ivec4 occBox) {
    vec3 d = toWorld - fromWorld;
    float len = length(d);
    if (len < 1e-6) return false;
    vec3 dir = d / len;

    ivec3 cell = ivec3(floor(fromWorld));
    ivec3 last = ivec3(floor(toWorld));

    ivec3 stp;
    vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (dir[i] > 1e-9) {
            stp[i] = 1;
            tMax[i] = (float(cell[i] + 1) - fromWorld[i]) / dir[i];
            tDelta[i] = 1.0 / dir[i];
        } else if (dir[i] < -1e-9) {
            stp[i] = -1;
            tMax[i] = (fromWorld[i] - float(cell[i])) / -dir[i];
            tDelta[i] = 1.0 / -dir[i];
        } else {
            stp[i] = 0;
            tMax[i] = 3.4e38;
            tDelta[i] = 3.4e38;
        }
    }

    // SEMANTICS, mirrored exactly. phxDdaHitsSolid tests its START cell, then every cell it steps
    // into EXCEPT the one containing the END point (its callers stop one cell short of a light on
    // purpose). So, per cube on the path (n = 0 is the cube the segment starts in):
    //   solid, not the last cube            -> hit (every micro cell of it on the path is tested);
    //   solid, last cube, n == 0            -> hit (the start cell is tested);
    //   solid, last cube, n > 0             -> hit unless the ONLY micro cell of it on the path is the
    //                                          end cell (first cell inside == end cell);
    //   mixed                               -> run the micro march over this cube's slice. The slice
    //                                          starts AT the segment start for n == 0 (so the start
    //                                          cell is the same cell) and 1e-4 u inside the cube for
    //                                          n > 0; it ends 1e-4 u past the cube's exit so the slice's
    //                                          own last cell is tested, and at `len` in the last cube
    //                                          so only the global end cell is skipped. In the last
    //                                          cube with n > 0 a slice whose first cell IS the end cell
    //                                          is skipped outright (the march would test it as a start).
    float tEnter = 0.0;
    int maxCubes = int(3.0 * len) + 4;
    ivec3 endMicro = ivec3(floor(toWorld * 9.0));
    for (int n = 0; n < maxCubes; ++n) {
        int st = phxCubeOccupancy(cell, occBox);
        float tExit = min(min(tMax.x, tMax.y), min(tMax.z, len));
        float a = (n == 0) ? 0.0 : tEnter + 1e-4;
        vec3 aPos = fromWorld + dir * a;
        bool firstIsEnd = (n > 0) && (cell == last) && all(equal(ivec3(floor(aPos * 9.0)), endMicro));
        if (st == 2) {
            if (cell != last || n == 0) return true;
            return !firstIsEnd;
        }
        if (st == 1 && !firstIsEnd) {
            float b = min(tExit + 1e-4, len);
            if (b > a && phxDdaHitsSolid(aPos, fromWorld + dir * b, 64, occBox)) return true;
        }
        if (cell == last || tExit >= len) return false;
        tEnter = tExit;
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) { cell.x += stp.x; tMax.x += tDelta.x; }
            else                 { cell.z += stp.z; tMax.z += tDelta.z; }
        } else {
            if (tMax.y < tMax.z) { cell.y += stp.y; tMax.y += tDelta.y; }
            else                 { cell.z += stp.z; tMax.z += tDelta.z; }
        }
    }
    return false;
}

/// As phxDdaHitsSolid, but reports the hit. M5 needs it: a bounce has to know what it hit and
/// which way that surface faces, and re-deriving either from a boolean is impossible.
///
/// `hitWorld`  = centre of the micro cell that was hit, in world units.
/// `hitNormal` = the face normal, taken from the axis the DDA last stepped along. That is exact
///               for voxel geometry, which is the one place a stepped normal is not an
///               approximation -- every surface really is axis-aligned.
bool phxDdaTrace(vec3 fromWorld, vec3 toWorld, int maxCells, ivec4 occBox,
                 out vec3 hitWorld, out vec3 hitNormal) {
    hitWorld = toWorld;
    hitNormal = vec3(0.0, 1.0, 0.0);

    vec3 a = fromWorld * 9.0, b = toWorld * 9.0;
    vec3 d = b - a;
    float len = length(d);
    if (len < 1e-6) return false;
    vec3 dir = d / len;

    ivec3 cell = ivec3(floor(a));
    ivec3 last = ivec3(floor(b));

    ivec3 stp;
    vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (dir[i] > 1e-9) {
            stp[i] = 1;  tMax[i] = (float(cell[i] + 1) - a[i]) / dir[i];  tDelta[i] = 1.0 / dir[i];
        } else if (dir[i] < -1e-9) {
            stp[i] = -1; tMax[i] = (a[i] - float(cell[i])) / -dir[i];     tDelta[i] = 1.0 / -dir[i];
        } else {
            stp[i] = 0;  tMax[i] = 3.4e38;                                tDelta[i] = 3.4e38;
        }
    }

    int axis = 1;   // which axis produced the most recent step; seeds the face normal
    for (int n = 0; n < maxCells; ++n) {
        if (phxOccupancySolid(cell, occBox)) {
            hitWorld = (vec3(cell) + 0.5) / 9.0;
            vec3 nrm = vec3(0.0);
            nrm[axis] = (stp[axis] > 0) ? -1.0 : 1.0;   // face points back along the step
            hitNormal = nrm;
            return true;
        }
        if (cell == last) return false;
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) { cell.x += stp.x; tMax.x += tDelta.x; axis = 0; }
            else                 { cell.z += stp.z; tMax.z += tDelta.z; axis = 2; }
        } else {
            if (tMax.y < tMax.z) { cell.y += stp.y; tMax.y += tDelta.y; axis = 1; }
            else                 { cell.z += stp.z; tMax.z += tDelta.z; axis = 2; }
        }
        if (tMax.x > len && tMax.y > len && tMax.z > len) return false;
    }
    return false;
}

/// How far the contiguous SOLID run containing a light extends, measured outward from the light
/// along `dirOut`, in world units. 0.0 when the light sits in air -- which is the common case and
/// costs a single cell lookup.
///
/// This is how the emitter's own body gets excluded from its own shadow test. That body is whatever
/// solid the light is embedded in, so it is found by walking, not assumed to be some fixed size: a
/// glow block reports its own half-extent, a flame in a firebox reports 0, and the masonry around
/// that firebox is therefore NOT excluded and still blocks.
///
/// Bounded to `maxCells` because an emitter is small; a light genuinely buried deep in rock stops
/// at the bound and lights nothing, which is the right answer for a buried light.
float phxEmitterRunLength(vec3 lightWorld, vec3 dirOut, int maxCells, ivec4 occBox) {
    vec3 a = lightWorld * 9.0;                 // micro space
    ivec3 cell = ivec3(floor(a));

    ivec3 stp;
    vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (dirOut[i] > 1e-9) {
            stp[i] = 1;  tMax[i] = (float(cell[i] + 1) - a[i]) / dirOut[i];  tDelta[i] = 1.0 / dirOut[i];
        } else if (dirOut[i] < -1e-9) {
            stp[i] = -1; tMax[i] = (a[i] - float(cell[i])) / -dirOut[i];     tDelta[i] = 1.0 / -dirOut[i];
        } else {
            stp[i] = 0;  tMax[i] = 3.4e38;                                   tDelta[i] = 3.4e38;
        }
    }

    float t = 0.0;   // micro units travelled so far
    for (int n = 0; n < maxCells; ++n) {
        if (!phxOccupancySolid(cell, occBox)) return t / 9.0;   // reached air: the run ends here
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) { t = tMax.x; cell.x += stp.x; tMax.x += tDelta.x; }
            else                 { t = tMax.z; cell.z += stp.z; tMax.z += tDelta.z; }
        } else {
            if (tMax.y < tMax.z) { t = tMax.y; cell.y += stp.y; tMax.y += tDelta.y; }
            else                 { t = tMax.z; cell.z += stp.z; tMax.z += tDelta.z; }
        }
    }
    return t / 9.0;
}

/// Visibility between a surface point and a light, both in ABSOLUTE world units.
/// 1.0 = nothing solid between them, 0.0 = something is.
///
/// ⚠️ `geomNormal` must be the GEOMETRIC face normal, NOT a normal-mapped one: offsetting the ray
/// origin along a tilted normal can slide it along the surface, or back into it, instead of
/// clearing it.
///
/// Two guards: start 2 micro cells along the normal (or the surface shadows itself and every lit
/// face goes black), and stop short of the light by the MEASURED extent of the emitter's own body
/// rather than by a fixed distance (see phxEmitterRunLength).
float phxLightVisibility(vec3 surfaceWorld, vec3 geomNormal, vec3 lightWorld, ivec4 occBox) {
    if ((occBox.w & 2) == 0) return 1.0;   // light tracing off / no occupancy

    vec3 start = surfaceWorld + geomNormal * (2.0 / 9.0);
    vec3 delta = lightWorld - start;
    float dist = length(delta);
    if (dist < 1e-4) return 1.0;
    vec3 dir = delta / dist;

    // STOP SHORT BY THE EMITTER'S OWN SIZE -- measured, not a constant.
    //
    // The problem: U3.2 made emissive voxels real lights, and an emissive voxel is SOLID with its
    // light at the cell CENTRE, so a march running all the way to the light always hits the emitter
    // itself and a glow block lit nothing at all (measured: blades around it were silhouettes).
    //
    // The first fix stopped the march a flat HALF VOXEL short. That worked for a free-standing glow
    // block and opened a hole for everything else: a light within 0.5 u of a wall never had the wall
    // tested and shone straight through it. Not hypothetical -- a generated hearth's flame sits in a
    // firebox cut into a masonry wall, and its firelight was landing on the lawn OUTSIDE the house.
    // The sealed-box gates missed it because those rigs put the light in open interior air, never in
    // a cavity with masonry inside half a voxel of the flame.
    //
    // So measure the emitter instead of guessing it: walk outward from the light while cells are
    // solid, and stop the shadow ray where that run ends. A glow block excludes exactly itself; a
    // flame in air excludes nothing and the masonry around it blocks normally.
    //
    // The measuring walk runs light-to-surface, but the SHADOW ray still runs surface-to-light. That
    // direction matters: reversing it spends the cell budget crossing the empty distance first, so a
    // distant light stopped finding a wall standing right next to the receiver (caught by
    // ADistantLightStillGetsOccludedRatherThanRunningOutOfSteps). The measuring walk is bounded to a
    // few cells, so it cannot run out.
    //
    // Remaining ambiguity, deliberately accepted: an emissive voxel placed flush against a wall
    // shares one solid run with that wall and still lights through it. That is the one case where
    // "the emitter's own body" is genuinely not separable from the occluder by geometry alone.
    float runEnd = phxEmitterRunLength(lightWorld, -dir, 32, occBox);
    vec3 target = start + dir * max(dist - runEnd - (0.1 / 9.0), 0.0);
    return phxDdaHitsSolid(start, target, 512, occBox) ? 0.0 : 1.0;
}

// phxSkyVisibility / PHX_SKY_DIRS (the M3 per-fragment 5-ray sky trace) were DELETED 2026-09-19
// (Ravenmere G-141). Ambient is the probe field (gi_field.glsl); the probe pass bounces off the
// field itself. The CPU mirror (VoxelLightOccupancy::skyVisibility) survives for the per-cell bake
// that CPU debris still reads -- LightingPipeline.md §8.

#endif // PHYXEL_OCCUPANCY_GLSL
