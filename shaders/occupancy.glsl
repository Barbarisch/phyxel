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

/// Visibility between a surface point and a light, both in ABSOLUTE world units.
/// 1.0 = nothing solid between them, 0.0 = something is.
///
/// ⚠️ `geomNormal` must be the GEOMETRIC face normal, NOT a normal-mapped one: offsetting the ray
/// origin along a tilted normal can slide it along the surface, or back into it, instead of
/// clearing it.
///
/// Two guards: start 2 micro cells along the normal (or the surface shadows itself and every lit
/// face goes black), and stop 1 micro cell short of the light (so a fixture embedded in its own
/// sconce does not occlude itself).
float phxLightVisibility(vec3 surfaceWorld, vec3 geomNormal, vec3 lightWorld, ivec4 occBox) {
    if ((occBox.w & 2) == 0) return 1.0;   // light tracing off / no occupancy

    vec3 start = surfaceWorld + geomNormal * (2.0 / 9.0);
    vec3 delta = lightWorld - start;
    float dist = length(delta);
    if (dist < 1e-4) return 1.0;
    vec3 dir = delta / dist;

    // Stop HALF A VOXEL short of the light, not 1 micro.
    //
    // U3.2 made emissive voxels real lights, and an emissive voxel is SOLID with its light at the
    // cell CENTRE -- so at 1/9 the march ended 0.5 units deep inside the emitter and every ray hit
    // it. The emitter occluded its own light, completely: a glow block in a night meadow lit
    // nothing, and the blades around it were silhouettes rather than lit grass (measured).
    //
    // Half a voxel is exactly the emitter's own half-extent, so this skips the emitting cell and
    // nothing else. A light sitting closer than 0.5 u to a wall could shine through it; that case
    // is bounded by the sealed-box gates (LightWallMatrix), which are the control for this constant.
    const float kSelfSkip = 0.5;
    vec3 target = start + dir * max(dist - kSelfSkip, 0.0);
    return phxDdaHitsSolid(start, target, 512, occBox) ? 0.0 : 1.0;
}

// ---- M3: TRACED SKY VISIBILITY -------------------------------------------------------
// Moved here from voxel.frag so grass and foliage share ONE sky term with stone; leaving
// it in voxel.frag is how vegetation ended up with a constant 1.0 and no enclosure at all.
const vec3 PHX_SKY_DIRS[9] = vec3[9](
    vec3( 0.000,  0.000, 1.000),
    vec3( 0.500,  0.000, 0.866), vec3(-0.500,  0.000, 0.866),
    vec3( 0.000,  0.500, 0.866), vec3( 0.000, -0.500, 0.866),
    vec3( 0.612,  0.612, 0.500), vec3(-0.612,  0.612, 0.500),
    vec3( 0.612, -0.612, 0.500), vec3(-0.612, -0.612, 0.500)
);

float phxSkyVisibility(vec3 surfaceWorld, vec3 geomNormal, ivec4 occBox) {
    if ((occBox.w & 4) == 0) return 1.0;   // sky tracing off, or occupancy absent

    vec3 Ng = normalize(geomNormal);
    vec3 up = abs(Ng.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, Ng));
    vec3 B = cross(Ng, T);

    vec3 start = surfaceWorld + Ng * (2.0 / 9.0);

    // Matched to the BAKE's measured settings (docs/UnifiedLightingPlan.md M3-REDESIGN).
    //
    // D1 measured this per-fragment path at 24.6 ms/frame and that number is what justified moving
    // sky visibility into a per-cell bake -- the very storage M0 existed to delete. But D1 ran at
    // FULL quality: 9 rays, reach 24, 512 cells. The bake then established by measurement that
    // 5 rays / reach 16 still seals a room at every wall thickness, with a doorway control alive.
    // The per-fragment path was never re-measured at those settings, so the number that retired it
    // was never its real cost.
    //
    // 5 rays keeps the vertical and the four 30-degree directions -- the ones that decide whether a
    // room is sealed -- and drops the four 60-degree diagonals, which largely duplicate them. That
    // is exactly the subset the bake uses, so PHX_SKY_DIRS[0..4] must stay in that order.
    // ⚠️ Reach still decides the largest room that can read as SEALED (D8).
    const float kReach = 16.0;
    const int   kRays  = 5;
    const int   kCells = int(kReach * 9.0) * 2;   // same cell budget the CPU mirror derives

    // GATE, the same idea that made M2's visibility term measure free: there, `dot(N, ldir) > 0`
    // and the radius test meant almost no marches actually ran. This trace had no gate at all --
    // every fragment paid all five rays every frame.
    //
    // Ray 0 is the surface normal itself, and it is the cheapest possible probe. If it escapes,
    // the surface has open sky directly above it and the remaining rays are being spent to confirm
    // a foregone conclusion: an unoccluded normal ray means the fragment is outdoors, which is the
    // overwhelming majority of fragments in any outdoor scene. Take the full weighted answer only
    // when that first ray is BLOCKED -- i.e. when the fragment might actually be enclosed, which is
    // exactly the case this whole system exists to resolve.
    //
    // This is conservative in the direction that matters: it can only ever return MORE sky for a
    // surface whose normal already sees sky. It cannot brighten an interior, because an interior
    // fragment's normal ray hits something and takes the full path.
    vec3 dir0 = normalize(T * PHX_SKY_DIRS[0].x + B * PHX_SKY_DIRS[0].y + Ng * PHX_SKY_DIRS[0].z);
    if (!phxDdaHitsSolid(start, start + dir0 * kReach, kCells, occBox)) return 1.0;

    float lit = 0.0, total = 0.0;
    for (int r = 0; r < kRays; ++r) {
        vec3 d = PHX_SKY_DIRS[r];
        vec3 dir = normalize(T * d.x + B * d.y + Ng * d.z);
        float w = max(0.0, dot(dir, Ng));
        total += w;

        if (!phxDdaHitsSolid(start, start + dir * kReach, kCells, occBox)) lit += w;
    }
    return total > 0.0 ? lit / total : 1.0;
}

#endif // PHYXEL_OCCUPANCY_GLSL
